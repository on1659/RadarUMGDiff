"""정확도 보고서 (dev 문서 §14 "커버리지"·"일치"·"스키마 불변"). 마크다운을 쓴다.

  py -3 tools/accuracy_report.py --repo <저장소> --fixtures <플러그인>/Test/Fixtures
        [--native-hist <네이티브 DumpHistory 폴더>] [--native-fixtures <네이티브 픽스처 diff 폴더>] [--schema schema.json] -o report.md

- 커버리지: 파일마다 이해한 바이트 / 익스포트 바이트. "범위 안"은 클래스·함수 익스포트의 UStruct 데이터(이 분석기 범위 밖, §11.9)를 뺀 값.
- 일치: 네이티브 diff.json 폴더가 주어지면 같은 쌍의 Python diff 와 conform (스키마 없이 / 있으면 스키마로).
- 스키마 불변: 스키마가 주어지면 쌍마다 스키마 유무로 판정 집합을 비교한다.
"""

import argparse
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from radar_umgdiff import tables, exports, gitsource, conform, config as config_mod, schema as schema_mod  # noqa: E402
import umgdiff_cli as cli  # noqa: E402


def coverage(data, path):
    pkg = tables.Package(data, path=path)
    total = und = struct_bytes = 0
    unparsed = {}
    for e in exports.read_all(pkg):
        total += e["serialSize"]
        und += e["bytesUnderstood"]
        for k, v in e["unparsed"].items():
            unparsed[k] = unparsed.get(k, 0) + v
            if k.startswith("(struct-data:"):
                struct_bytes += v
    return total, und, struct_bytes, unparsed


def row_cov(name, total, und, sb):
    scope = total - sb
    return "| %s | %d | %.4f | %.4f |" % (name, total, und / total if total else 1, und / scope if scope else 1)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo")
    ap.add_argument("--fixtures")
    ap.add_argument("--native-hist")
    ap.add_argument("--native-fixtures")
    ap.add_argument("--schema")
    ap.add_argument("--config")
    ap.add_argument("-o", required=True)
    a = ap.parse_args(argv)
    settings = config_mod.load(a.config)
    sch = schema_mod.Schema.load(a.schema) if a.schema else None
    out = ["# RadarUMGDiff 정확도 보고서", "", "`tools/accuracy_report.py` 가 만든다. 손으로 고치지 않는다.", ""]

    # ── 커버리지 ──
    out += ["## 커버리지", "", "| 파일 | 익스포트 바이트 | 전체 | 범위 안 (UStruct 데이터 제외) |", "|---|---:|---:|---:|"]
    unparsed_all = {}
    sums = [0, 0, 0]
    if a.fixtures:
        for name in sorted(os.listdir(a.fixtures)):
            for v in sorted(os.listdir(os.path.join(a.fixtures, name))):
                p = os.path.join(a.fixtures, name, v, name + ".uasset")
                if not os.path.isfile(p):
                    continue
                t, u, sb, un = coverage(open(p, "rb").read(), p)
                out.append(row_cov("%s %s" % (name, v), t, u, sb))
                sums = [sums[0] + t, sums[1] + u, sums[2] + sb]
                for k, x in un.items():
                    unparsed_all[k] = unparsed_all.get(k, 0) + x
    blob_count = 0
    if a.repo:
        root = gitsource.repo_root(a.repo)
        seen = set()
        for sha, path in sorted(set(gitsource.all_blobs(root, "Content/*/WBP_*.uasset")) | set(gitsource.all_blobs(root, "Content/WBP_*.uasset"))):
            if sha in seen:
                continue
            seen.add(sha)
            data = gitsource.cat_blob(root, sha)
            t, u, sb, un = coverage(data, path)
            blob_count += 1
            sums = [sums[0] + t, sums[1] + u, sums[2] + sb]
            for k, x in un.items():
                unparsed_all[k] = unparsed_all.get(k, 0) + x
        out.append("| CL9 git 블롭 %d개 (합) | | | |" % blob_count)
    out.append(row_cov("**합계**", sums[0], sums[1], sums[2]))
    out += ["", "해석하지 않은 바이트 (구조체·접미부별):", ""]
    for k, v in sorted(unparsed_all.items(), key=lambda kv: -kv[1]):
        out.append("- `%s` %d 바이트" % (k, v))
    if not unparsed_all:
        out.append("- 없음")

    # ── 일치 ──
    def conform_dir(native_dir, python_pairs, label):
        lines = ["", "### " + label, ""]
        total = deferred = pairs = 0
        notes = []
        for rel, nd, pd in python_pairs:
            npath = os.path.join(native_dir, rel, "diff.json")
            if not os.path.isfile(npath):
                continue
            import json
            rows = conform.compare(json.load(io.open(npath, encoding="utf-8")), pd)
            bad = [r for r in rows if r[2] != conform.MAYBE_DEFAULT]
            maybe = [r for r in rows if r[2] == conform.MAYBE_DEFAULT]
            pairs += 1
            total += len(bad)
            deferred += len(maybe)
            for r in bad + maybe:
                notes.append("- `%s` %s %s — %s" % (rel, r[0], r[1], r[2]))
        lines.append("쌍 %d개 · 판정 불일치 **%d** · 판정 보류 %d" % (pairs, total, deferred))
        lines += [""] + notes if notes else []
        return lines

    if a.native_hist and a.repo:
        root = gitsource.repo_root(a.repo)
        for use_schema in ([False, True] if sch else [False]):
            pairs = []
            for asset, oid, nid, ob, nb, op, np_, orev, nrev, pkgname in cli._history_pairs(root, []):
                _, _, d = cli._diff(ob, nb, op, np_, pkgname, settings, orev, nrev, sch if use_schema else None)
                pairs.append((os.path.join(asset, "%s_%s" % (oid, nid)), None, d))
            out += conform_dir(a.native_hist, pairs, "CL9 git 히스토리 — 네이티브 vs Python" + (" (스키마)" if use_schema else ""))
    if a.native_fixtures and a.fixtures:
        for use_schema in ([False, True] if sch else [False]):
            pairs = []
            for name, va, vb, ob, nb, op, np_, orev, nrev, pkgname in cli._fixture_pairs(a.fixtures):
                _, _, d = cli._diff(ob, nb, op, np_, pkgname, settings, orev, nrev, sch if use_schema else None)
                pairs.append((os.path.join(name, "%s_%s" % (va, vb)), None, d))
            out += conform_dir(a.native_fixtures, pairs, "픽스처 — 네이티브 vs Python" + (" (스키마)" if use_schema else ""))

    # ── 스키마 불변 ──
    if sch:
        out += ["", "## 스키마 유무와 판정", ""]
        srcs = []
        if a.repo:
            srcs.append(cli._history_pairs(gitsource.repo_root(a.repo), []))
        if a.fixtures:
            srcs.append(cli._fixture_pairs(a.fixtures))
        pairs = bad = resolved = 0
        for src in srcs:
            for asset, oid, nid, ob, nb, op, np_, orev, nrev, pkgname in src:
                _, _, plain = cli._diff(ob, nb, op, np_, pkgname, settings, orev, nrev, None)
                _, _, with_s = cli._diff(ob, nb, op, np_, pkgname, settings, orev, nrev, sch)
                bad += len(cli._decided(plain) - cli._decided(with_s))
                resolved += sum(1 for r in cli._all_rows(plain) if r.get("maybeDefault")) - sum(1 for r in cli._all_rows(with_s) if r.get("maybeDefault"))
                pairs += 1
        out.append("쌍 %d개 · 스키마를 넣어 사라지거나 바뀐 판정 **%d** · 스키마가 확정한 보류 줄 %d (엔진 %s)" % (pairs, bad, resolved, sch.engine))
    io.open(a.o, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
    print("기록:", a.o)


if __name__ == "__main__":
    main()
