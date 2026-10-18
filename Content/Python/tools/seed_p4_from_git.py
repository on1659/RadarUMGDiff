"""git 히스토리를 로컬 Perforce 서버에 제출로 재생한다 — RadarUMGDiff 프로바이더 백엔드 검증 전용.

  py -3 seed_p4_from_git.py --p4 <p4.exe> --port <host:port> --user <user> --client <client> \\
      --repo <git 저장소> Content/UI/WBP_HUD.uasset Content/UI/WBP_Main.uasset ...

동작
- 파일마다 git 커밋을 오래된 것부터 하나씩: 그 커밋의 블롭을 작업 파일에 쓰고 `p4 add -t binary+l`(첫 번) 또는 `p4 edit`,
  `p4 submit -d "<제목> (git <sha7>, <날짜>, <작성자>)"`. 결과 P4 리비전 #n ↔ git 커밋 1:1.
- 기본은 `--write-root` 에 지정한 **별도 폴더**(임시 워크스페이스 Root)에 블롭을 쓰고 거기서 제출한다. git 작업 트리는 건드리지 않는다.
  끝난 뒤 실제 워크스페이스(프로젝트 위 좁은 뷰)는 `p4 -c <client> sync -k //depot/<project>/Content/UI/...` 로 보유 목록만 맞춘다(파일을 쓰지 않는다).
  2026-10-18: 에디터가 떠 있으면 Content/UI/*.uasset 쓰기 열기가 거부되므로(공유 모드) 작업 트리에 쓰는 방식은 쓸 수 없었다.
- `--write-root` 를 생략하면 git 작업 트리에 직접 쓰고(백업 → 복원), 에디터가 닫혀 있을 때만 쓴다.
- 커밋이 0개인 파일(미추적)은 건너뛴다. 클라이언트 뷰 밖 파일이면 p4 가 거부하므로 그대로 실패를 보고한다.
- 표준 라이브러리만 쓴다. Python 3.9 이상.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

# 콘솔이 cp949 여도 커밋 제목(—, … 등)을 그대로 찍는다
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def run(cmd, cwd=None, input_bytes=None, check=True):
    # p4 는 PWD 환경 변수를 현재 디렉터리로 믿는다(bash 가 넘긴 PWD 가 다른 폴더를 가리킬 수 있다).
    # 그래서 p4 에는 절대 경로를 넘기고, PWD 도 cwd 로 맞춘다.
    env = dict(os.environ)
    if cwd:
        env["PWD"] = cwd
    r = subprocess.run(cmd, cwd=cwd, input=input_bytes, capture_output=True, env=env)
    if check and r.returncode != 0:
        sys.stderr.write("명령 실패: %s\n%s\n" % (" ".join(cmd), r.stderr.decode("utf-8", "replace")))
        sys.exit(1)
    return r


def git_commits(repo, relpath):
    """오래된 것부터 (sha, 날짜ISO, 작성자, 제목)."""
    r = run(["git", "-c", "core.quotepath=off", "log", "--reverse", "--format=%H%x1f%aI%x1f%an%x1f%s", "--", relpath], cwd=repo)
    out = []
    for line in r.stdout.decode("utf-8", "replace").splitlines():
        if not line.strip():
            continue
        sha, date, author, subject = line.split("\x1f", 3)
        out.append((sha, date, author, subject))
    return out


def git_blob(repo, sha, relpath):
    return run(["git", "show", "%s:%s" % (sha, relpath)], cwd=repo).stdout


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--p4", required=True, help="p4.exe 경로")
    ap.add_argument("--port", required=True)
    ap.add_argument("--user", required=True)
    ap.add_argument("--client", required=True)
    ap.add_argument("--repo", required=True, help="git 저장소 루트 (히스토리 출처)")
    ap.add_argument("--write-root", default=None, help="블롭을 쓰고 제출할 폴더 = --client 의 Root. 생략하면 --repo 에 직접 쓴다")
    ap.add_argument("files", nargs="+", help="저장소 상대 경로")
    a = ap.parse_args()

    p4 = [a.p4, "-p", a.port, "-u", a.user, "-c", a.client]
    repo = os.path.abspath(a.repo)
    wroot = os.path.abspath(a.write_root) if a.write_root else repo

    # 1. 작업 사본 백업 (바이트 그대로) — 작업 트리에 직접 쓸 때만
    backup = {}
    for rel in a.files:
        full = os.path.join(wroot, rel.replace("/", os.sep))
        if wroot == repo and os.path.exists(full):
            with open(full, "rb") as f:
                backup[rel] = f.read()

    total = 0
    try:
        for rel in a.files:
            full = os.path.join(wroot, rel.replace("/", os.sep))
            os.makedirs(os.path.dirname(full), exist_ok=True)
            commits = git_commits(repo, rel)
            if not commits:
                print("건너뜀 (git 커밋 없음): %s" % rel)
                continue
            have = run(p4 + ["files", "-e", full], cwd=wroot, check=False)
            in_depot = have.returncode == 0 and b"no such file" not in have.stderr and have.stdout.strip() != b""
            # 이어서 돌리기: 이미 디포에 #N 까지 있으면 처음 N 커밋은 건너뛴다 (앞선 실행이 같은 순서로 올렸다고 본다)
            done = 0
            if in_depot:
                m = re.search(rb"#(\d+) - ", have.stdout)
                done = int(m.group(1)) if m else 0
                if done:
                    print("  %s: 디포에 #%d 까지 있음, 그만큼 건너뜀" % (rel, done))
            for i, (sha, date, author, subject) in enumerate(commits):
                if i < done:
                    continue
                blob = git_blob(repo, sha, rel)
                if in_depot or i > 0:
                    run(p4 + ["edit", full], cwd=wroot)
                    with open(full, "wb") as f:
                        f.write(blob)
                else:
                    with open(full, "wb") as f:
                        f.write(blob)
                    run(p4 + ["add", "-t", "binary+l", full], cwd=wroot)
                desc = "%s (git %s, %s, %s)" % (subject, sha[:7], date, author)
                r = run(p4 + ["submit", "-d", desc, full], cwd=wroot, check=False)
                out = r.stdout.decode("utf-8", "replace") + r.stderr.decode("utf-8", "replace")
                if r.returncode != 0:
                    if "No files to submit" in out or "no files to submit" in out:
                        run(p4 + ["revert", full], cwd=wroot, check=False)
                        print("  같은 내용이라 건너뜀: %s %s" % (rel, sha[:7]))
                        continue
                    sys.stderr.write("submit 실패: %s %s\n%s\n" % (rel, sha[:7], out))
                    sys.exit(1)
                total += 1
                print("  %s #%d  %s" % (rel, i + 1, desc))
    finally:
        # 2. 작업 사본 복원 + 검증
        bad = []
        for rel, data in backup.items():
            full = os.path.join(wroot, rel.replace("/", os.sep))
            with open(full, "wb") as f:
                f.write(data)
            with open(full, "rb") as f:
                if sha256(f.read()) != sha256(data):
                    bad.append(rel)
        if bad:
            sys.stderr.write("복원 실패: %s\n" % ", ".join(bad))
            sys.exit(2)
        if backup:
            print("작업 사본 %d개 바이트 그대로 복원" % len(backup))

    print("제출 %d건" % total)


if __name__ == "__main__":
    main()
