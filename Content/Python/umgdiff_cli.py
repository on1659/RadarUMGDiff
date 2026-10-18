"""RadarUMGDiff Python 분석기 CLI (dev 문서 §11.11). 표준 라이브러리만, Python 3.9 문법.

  umgdiff_cli.py dump    <uasset> [-o out.json] [--package /Game/...] [--schema schema.json]
  umgdiff_cli.py diff    <old.uasset> <new.uasset> -o <dir> [--package /Game/...] [--hide-auto] [--schema schema.json]
  umgdiff_cli.py conform <native-diff.json 또는 폴더> <python-diff.json 또는 폴더>
  umgdiff_cli.py history [<repo-relpath> ...] -o <dir> [--repo <저장소>] [--schema schema.json]   (경로가 없으면 Content 아래 모든 WBP_*.uasset)
  umgdiff_cli.py schema-check --schema schema.json [--repo <저장소>] [--fixtures <폴더>]   스키마 유무로 판정이 같은지 (goal Acceptance)
  umgdiff_cli.py summary <diff.json>

종료 코드 0 성공, 1 불일치(conform), 3 인자, 4 거부(버전·커스텀 버전·표 없음).
"""

import argparse
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from radar_umgdiff import state as state_mod, diff as diff_mod, conform as conform_mod, config as config_mod, gitsource  # noqa: E402
from radar_umgdiff import schema as schema_mod  # noqa: E402
from radar_umgdiff.reader import Refuse  # noqa: E402


def _utf8_stdout():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except AttributeError:
            pass


def _write_json(obj, path):
    d = os.path.dirname(os.path.abspath(path))
    if not os.path.isdir(d):
        os.makedirs(d)
    with io.open(path, "w", encoding="utf-8", newline="\n") as fp:
        json.dump(obj, fp, ensure_ascii=False, indent=2)
        fp.write("\n")


def _read(path):
    with open(path, "rb") as fp:
        return fp.read()


def _hide_auto(d):
    def strip(rows):
        return [r for r in rows if not r.get("auto")]
    for w in d["widgets"]:
        w["properties"] = strip(w["properties"])
        w["slot"]["properties"] = strip(w["slot"]["properties"])
    d["cdo"] = strip(d["cdo"])
    d["summary"] = diff_mod.count_summary(d)
    return d


def _schema(a):
    return schema_mod.Schema.load(a.schema) if getattr(a, "schema", None) else None


def cmd_dump(a):
    st = state_mod.State(_read(a.uasset), a.uasset, a.package)
    out = st.to_json(_schema(a))
    if a.o:
        _write_json(out, a.o)
        cov = out["coverage"]
        print("%s: 익스포트 %d개, 커버리지 %.4f (디자이너 %.4f)" % (a.uasset, len(out["exports"]), cov["ratio"], cov["designerRatio"]))
    else:
        json.dump(out, sys.stdout, ensure_ascii=False, indent=2)
        print()
    return 0


def _diff(old_bytes, new_bytes, old_path, new_path, package, settings, old_rev=None, new_rev=None, schema=None):
    o = state_mod.State(old_bytes, old_path, package)
    n = state_mod.State(new_bytes, new_path, package)
    d = diff_mod.make_diff(o, n, settings, old_rev, new_rev)
    if schema is not None:
        schema_mod.apply(d, schema, o, n)
        d["summary"] = diff_mod.count_summary(d)
    return o, n, d


def cmd_diff(a):
    settings = config_mod.load(a.config)
    sch = _schema(a)
    o, n, d = _diff(_read(a.old), _read(a.new), a.old, a.new, a.package, settings,
                    {"kind": "file", "path": os.path.abspath(a.old)}, {"kind": "file", "path": os.path.abspath(a.new)}, sch)
    if a.hide_auto:
        d = _hide_auto(d)
    _write_json(d, os.path.join(a.o, "diff.json"))
    _write_json(o.to_json(sch), os.path.join(a.o, "old.state.json"))
    _write_json(n.to_json(sch), os.path.join(a.o, "new.state.json"))
    print(_summary_line(d))
    return 0


def _summary_line(d):
    k = d["summary"]["byKind"]
    a = d["summary"]["byArea"]
    return "추가 %d · 삭제 %d · 변경 %d · 이동 %d · 이름변경(추정) %d · 키만 %d | 디자이너 %d · 슬롯 %d · 바인딩 %d · CDO %d · 애니 %d · 그래프 %d/%d" % (
        k["added"], k["removed"], k["changed"], k["moved"], k["renamed-probable"], k["text-key-only"],
        a["designer"], a["slot"], a["binding"], a["cdo"], a["anim"], a["graph-logic"], a["graph-layout"])


def cmd_summary(a):
    with io.open(a.diff, encoding="utf-8") as fp:
        d = json.load(fp)
    print(_summary_line(d))
    return 0


def _pairs(root, native_dir, python_dir):
    """두 폴더에서 상대 경로가 같은 diff.json 쌍."""
    out = []
    for dirpath, _, files in os.walk(native_dir):
        if "diff.json" in files:
            rel = os.path.relpath(dirpath, native_dir)
            p = os.path.join(python_dir, rel, "diff.json")
            out.append((rel, os.path.join(dirpath, "diff.json"), p if os.path.isfile(p) else None))
    return sorted(out)


def cmd_conform(a):
    def load(p):
        with io.open(p, encoding="utf-8") as fp:
            return json.load(fp)
    if os.path.isdir(a.native):
        pairs = _pairs(None, a.native, a.python)
    else:
        pairs = [("", a.native, a.python)]
    total = 0
    deferred = 0
    missing = 0
    for rel, n, p in pairs:
        if p is None:
            print("[%s] Python diff.json 없음" % rel)
            missing += 1
            continue
        rows = conform_mod.compare(load(n), load(p))
        bad = [r for r in rows if r[2] != conform_mod.MAYBE_DEFAULT]
        maybe = [r for r in rows if r[2] == conform_mod.MAYBE_DEFAULT]
        total += len(bad)
        deferred += len(maybe)
        if bad:
            print("[%s] 판정 불일치 %d건" % (rel, len(bad)))
            print(conform_mod.format_table(bad, a.limit))
        if maybe:
            print("[%s] 판정 보류(Python 이 한쪽 태그만 봄 — 스키마로 확정) %d건" % (rel, len(maybe)))
            print(conform_mod.format_table(maybe, a.limit))
    print("쌍 %d개, 판정 불일치 %d건, 판정 보류 %d건, Python 없음 %d개" % (len(pairs), total, deferred, missing))
    return 1 if (total or missing) else 0


def _history_pairs(root, paths):
    """git 히스토리의 (자산, 옛 id, 새 id, 옛 바이트, 새 바이트, 옛 경로, 새 경로, 옛 rev, 새 rev) — 인접 커밋 쌍 + 수정된 작업 사본."""
    if not paths:
        paths = sorted(set(p for _, p in gitsource.all_blobs(root, "Content/*/WBP_*.uasset")) |
                       set(p for _, p in gitsource.all_blobs(root, "Content/WBP_*.uasset")))
        # 현재 트리에 있는 것만 (지워진 파일의 옛 블롭은 dump 로만)
        paths = [p for p in paths if os.path.isfile(os.path.join(root, p))]
    for rel in paths:
        asset = os.path.splitext(os.path.basename(rel))[0]
        hist = gitsource.log(root, rel)          # 최신 → 과거
        pairs = [(hist[i], hist[i - 1]) for i in range(len(hist) - 1, 0, -1)]
        dirty = gitsource._run(["status", "--porcelain", "--", rel], root).strip() != ""
        if hist and dirty:
            pairs.append((hist[0], None))
        for old_c, new_c in pairs:
            new_id = new_c["short"] if new_c else "working"
            old_bytes = gitsource.show(root, old_c["sha"], old_c["path"])
            new_bytes = gitsource.show(root, new_c["sha"], new_c["path"]) if new_c else _read(os.path.join(root, rel))
            old_rev = {"kind": "commit", "sha": old_c["sha"], "date": old_c["date"], "author": old_c["author"], "subject": old_c["subject"]}
            new_rev = ({"kind": "commit", "sha": new_c["sha"], "date": new_c["date"], "author": new_c["author"], "subject": new_c["subject"]}
                       if new_c else {"kind": "working", "dirty": True})
            yield asset, old_c["short"], new_id, old_bytes, new_bytes, old_c["path"], rel, old_rev, new_rev, None


def _fixture_pairs(folder):
    """픽스처 폴더 <이름>/v<N>/<이름>.uasset 의 인접 버전 쌍. 패키지 이름은 /RadarUMGDiff/Test/<이름>."""
    for name in sorted(os.listdir(folder)):
        vdirs = sorted((int(v[1:]), v) for v in os.listdir(os.path.join(folder, name)) if v.startswith("v") and v[1:].isdigit())
        for (va, a_), (vb, b_) in zip(vdirs, vdirs[1:]):
            pa = os.path.join(folder, name, a_, name + ".uasset")
            pb = os.path.join(folder, name, b_, name + ".uasset")
            yield name, a_, b_, _read(pa), _read(pb), pa, pb, {"kind": "file", "path": pa}, {"kind": "file", "path": pb}, "/RadarUMGDiff/Test/" + name


def cmd_history(a):
    settings = config_mod.load(a.config)
    root = gitsource.repo_root(a.repo)
    sch = _schema(a)
    count = 0
    rows = []
    for asset, old_id, new_id, ob, nb, op, np_, orev, nrev, _ in _history_pairs(root, a.paths):
        out_dir = os.path.join(a.o, asset, "%s_%s" % (old_id, new_id))
        o, n, d = _diff(ob, nb, op, np_, None, settings, orev, nrev, sch)
        _write_json(d, os.path.join(out_dir, "diff.json"))
        count += 1
        rows.append((asset, old_id, new_id, d["new"]["coverage"], d["new"]["designerCoverage"], _summary_line(d)))
    for r in rows:
        print("%-18s %s→%-8s 커버리지 %.4f 디자이너 %.4f  %s" % r)
    print("diff.json %d개 → %s" % (count, a.o))
    return 0


def _decided(d):
    """판정이 난 것들: 항목 (위치, kind) 과 maybeDefault 가 아닌 줄 (위치, 경로, kind, old 값, new 값).
    maybeDefault 줄은 스키마 없이는 미결이므로(goal: "changed (maybe default) unless the schema proves equality") 넣지 않는다."""
    out = set()

    def rows(where, rs):
        decided = False
        for r in rs:
            if r.get("maybeDefault"):
                continue
            decided = True
            ov = json.dumps((r.get("old") or {}).get("value"), sort_keys=True, ensure_ascii=False)
            nv = json.dumps((r.get("new") or {}).get("value"), sort_keys=True, ensure_ascii=False)
            out.add(("row", where, r["path"], r["kind"], ov, nv))
        return decided

    for w in d["widgets"]:
        has = rows("widget " + w["id"], w["properties"]) | rows("slot " + w["id"], w["slot"]["properties"])
        if has or w["kind"] in ("added", "removed", "moved", "renamed-probable") or w.get("moved"):
            out.add(("entity", "widget", w["id"], w["kind"], bool(w.get("moved"))))
    rows("cdo", d["cdo"])
    for v in d.get("variables", []):
        if rows("variable " + v["varGuid"], v.get("properties", [])) or v["kind"] != "changed":
            out.add(("entity", "variable", v["varGuid"], v["kind"], v.get("renamedFrom")))
    for g in d.get("graphs", []):
        for n in g["nodes"]:
            if rows("node " + n["guid"], n.get("properties", [])) or n.get("pins") or n["kind"] != "changed":
                out.add(("entity", "node", n["guid"], n["kind"], n["area"] if n.get("pins") or n["kind"] != "changed" else None))
    for b in d.get("bindings", []):
        out.add(("entity", "binding", b["id"], b["kind"], None))
    for an in d.get("animations", []):
        out.add(("entity", "animation", an["id"], an["kind"], None))
    return out


def cmd_schema_check(a):
    settings = config_mod.load(a.config)
    sch = _schema(a)
    sources = []
    if a.repo:
        sources.append(_history_pairs(gitsource.repo_root(a.repo), []))
    if a.fixtures:
        sources.append(_fixture_pairs(a.fixtures))
    pairs = bad = resolved = 0
    for src in sources:
        for asset, old_id, new_id, ob, nb, op, np_, orev, nrev, package in src:
            _, _, plain = _diff(ob, nb, op, np_, package, settings, orev, nrev, None)
            _, _, with_s = _diff(ob, nb, op, np_, package, settings, orev, nrev, sch)
            p, w = _decided(plain), _decided(with_s)
            missing = sorted(p - w)
            pairs += 1
            maybe_before = sum(1 for r in _all_rows(plain) if r.get("maybeDefault"))
            maybe_after = sum(1 for r in _all_rows(with_s) if r.get("maybeDefault"))
            resolved += maybe_before - maybe_after
            if missing:
                bad += len(missing)
                print("[%s %s→%s] 스키마를 넣자 사라지거나 바뀐 판정 %d건" % (asset, old_id, new_id, len(missing)))
                for m in missing[:20]:
                    print("   ", m)
    print("쌍 %d개, 스키마 유무로 달라진 판정 %d건, 스키마가 확정한 보류 줄 %d건" % (pairs, bad, resolved))
    return 1 if bad else 0


def _all_rows(d):
    for w in d["widgets"]:
        for r in w["properties"] + w["slot"]["properties"]:
            yield r
    for r in d["cdo"]:
        yield r
    for v in d.get("variables", []):
        for r in v.get("properties", []):
            yield r
    for g in d.get("graphs", []):
        for n in g["nodes"]:
            for r in n.get("properties", []):
                yield r


def main(argv=None):
    _utf8_stdout()
    ap = argparse.ArgumentParser(prog="umgdiff_cli.py")
    ap.add_argument("--config", default=None, help="DefaultRadarUMGDiff.ini 경로 (기본: 플러그인 Config/)")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("dump")
    p.add_argument("uasset")
    p.add_argument("-o")
    p.add_argument("--package")
    p.add_argument("--schema", help="schema.json (에디터 RadarUMGDiff.DumpSchema) — 태그에 없는 속성을 기본값으로 채워 state: default 로")
    p = sub.add_parser("diff")
    p.add_argument("old")
    p.add_argument("new")
    p.add_argument("-o", required=True)
    p.add_argument("--package")
    p.add_argument("--hide-auto", action="store_true", help="ini AutoProperties 줄을 빼고 쓴다")
    p.add_argument("--schema", help="schema.json — maybeDefault 줄을 기본값으로 확정")
    p = sub.add_parser("conform")
    p.add_argument("native")
    p.add_argument("python")
    p.add_argument("--limit", type=int, default=60)
    p = sub.add_parser("history")
    p.add_argument("paths", nargs="*")
    p.add_argument("-o", required=True)
    p.add_argument("--repo", default=os.getcwd())
    p.add_argument("--schema")
    p = sub.add_parser("schema-check")
    p.add_argument("--schema", required=True)
    p.add_argument("--repo")
    p.add_argument("--fixtures")
    p = sub.add_parser("summary")
    p.add_argument("diff")
    a = ap.parse_args(argv)
    if not a.cmd:
        ap.print_help()
        return 3
    try:
        return {"dump": cmd_dump, "diff": cmd_diff, "conform": cmd_conform, "history": cmd_history, "schema-check": cmd_schema_check, "summary": cmd_summary}[a.cmd](a)
    except Refuse as ex:
        print("거부: %s" % ex, file=sys.stderr)
        return 4


if __name__ == "__main__":
    sys.exit(main())
