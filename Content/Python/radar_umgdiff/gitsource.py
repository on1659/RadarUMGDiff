"""git show / log 래퍼 (dev 문서 §8.2 와 같은 명령). 출력은 바이너리 그대로 받는다."""

import os
import subprocess


def _run(args, cwd, binary=False):
    p = subprocess.run(["git"] + args, cwd=cwd, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError("git %s 실패: %s" % (" ".join(args), p.stderr.decode("utf-8", "replace").strip()))
    return p.stdout if binary else p.stdout.decode("utf-8", "replace")


def repo_root(path):
    d = path if os.path.isdir(path) else os.path.dirname(os.path.abspath(path))
    return _run(["rev-parse", "--show-toplevel"], d).strip()


def log(root, relpath, follow=True):
    """[{sha, short, date, author, subject, path}] 최신 → 과거."""
    args = ["-c", "core.quotepath=off", "log", "--name-only", "--format=%x1e%H%x1f%h%x1f%aI%x1f%an%x1f%s"]
    if follow:
        args.append("--follow")
    args += ["--", relpath]
    out = _run(args, root)
    result = []
    for rec in out.split("\x1e"):
        lines = [l for l in rec.splitlines() if l.strip()]
        if not lines:
            continue
        f = lines[0].split("\x1f")
        if len(f) < 5:
            continue
        result.append({"sha": f[0], "short": f[1], "date": f[2], "author": f[3], "subject": f[4],
                       "path": lines[1].strip() if len(lines) > 1 else relpath})
    return result


def show(root, sha, relpath):
    return _run(["-c", "core.quotepath=off", "show", "%s:%s" % (sha, relpath)], root, binary=True)


def all_blobs(root, pathspec_glob):
    """저장소 전체 히스토리에서 경로가 패턴에 맞는 블롭 (sha, path) — 정확도 모드용 (dev 문서 §11.10)."""
    import fnmatch
    out = _run(["rev-list", "--all", "--objects"], root)
    seen = {}
    for line in out.splitlines():
        parts = line.split(" ", 1)
        if len(parts) == 2 and fnmatch.fnmatch(parts[1], pathspec_glob):
            seen[parts[0]] = parts[1]
    return sorted(seen.items(), key=lambda kv: (kv[1], kv[0]))


def cat_blob(root, blob_sha):
    return _run(["cat-file", "blob", blob_sha], root, binary=True)
