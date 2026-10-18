"""네이티브 diff.json 과 Python diff.json 비교 (dev 문서 §11.10, §14 "일치").

비교 대상: widgets · slots · cdo · bindings 의 (id, kind) 집합과, 같은 항목 안 properties[] 의 (path, kind),
그리고 양쪽 모두 "present" 인 값. M4 부터 variables(varGuid·kind·renamedFrom·속성 줄), animations(애니메이션·트랙·섹션 범위·
섹션 속성·채널·채널 속성·키 값·키 속성), graphs(그래프·노드 NodeGuid·kind·area·노드 속성·핀 PinId·핀 속성)도 같은 식으로 본다.
제외: text, category, maybeDefault, textKeys, 노드 title(파일에 없다), displayLabel/parent/index 의 표기, notes.
상태 대응: 네이티브 default ≡ Python absent.
값 비교: 수치는 허용 오차, 객체는 양쪽에 다 있는 키만(Python 태그 구조체는 저장된 멤버만 가진다), FText 는 source(+table/tableKey),
Python 이 모르는 값(None source)은 건너뛴다.
"""

from collections import OrderedDict

STATE_MAP = {"default": "absent"}     # 네이티브 default ≡ Python absent ≡ Python(스키마) default — 아키타입 값이라는 같은 뜻


def _num_eq(a, b, tol):
    if a == b:
        return True
    try:
        return abs(a - b) <= tol * max(abs(a), abs(b), 1e-30)
    except TypeError:
        return False


def value_eq(native, python, tol=1e-5):
    """네이티브 값과 Python 값이 같은지 (Python 이 모르는 부분은 통과)."""
    if python is None:
        return True
    if isinstance(native, bool) or isinstance(python, bool):
        return native == python
    if isinstance(native, (int, float)) and isinstance(python, (int, float)):
        return _num_eq(float(native), float(python), tol)
    if isinstance(native, dict) and isinstance(python, dict):
        if "source" in native and "namespace" in native:
            if python.get("source") is None:
                return native.get("table") == python.get("table") and native.get("tableKey") == python.get("tableKey")
            return native.get("source") == python.get("source") and native.get("table") == python.get("table") and native.get("tableKey") == python.get("tableKey")
        if "raw" in python:
            return True
        for k in python:
            if k in native and not value_eq(native[k], python[k], tol):
                return False
        return True
    if isinstance(native, list) and isinstance(python, list):
        return len(native) == len(python) and all(value_eq(a, b, tol) for a, b in zip(native, python))
    return native == python


def _state(v):
    if v is None:
        return None
    s = v.get("state")
    return STATE_MAP.get(s, s)


def _props(rows):
    return OrderedDict((r["path"], r) for r in rows)


MAYBE_DEFAULT = "Python: 기본값일 수 있음(판정 보류)"


def _compare_rows(where, nrows, prows, out):
    n, p = _props(nrows), _props(prows)
    for path in n:
        if path not in p:
            out.append((where, path, "네이티브에만 있는 속성 줄", n[path]["kind"], None))
    for path in p:
        if path not in n:
            # Python 이 한쪽 태그만 본 줄(maybeDefault)은 스키마 없이는 판정할 수 없다 — 불일치가 아니라 보류로 따로 센다 (goal: "changed (maybe default)")
            why = MAYBE_DEFAULT if p[path].get("maybeDefault") else "Python 에만 있는 속성 줄"
            out.append((where, path, why, None, p[path]["kind"]))
    for path in n:
        if path not in p:
            continue
        a, b = n[path], p[path]
        if a["kind"] != b["kind"]:
            out.append((where, path, "kind 다름", a["kind"], b["kind"]))
            continue
        for side in ("old", "new"):
            av, bv = a.get(side), b.get(side)
            if (av is None) != (bv is None):
                out.append((where, path, side + " 유무 다름", av is not None, bv is not None))
                continue
            if av is None:
                continue
            sa, sb = _state(av), _state(bv)
            if sa != sb and path != "*":
                out.append((where, path, side + " 상태 다름", av.get("state"), bv.get("state")))
                continue
            if sa == "present" and path != "*" and not value_eq(av.get("value"), bv.get("value")):
                out.append((where, path, side + " 값 다름", av.get("value"), bv.get("value")))


def compare(native, python):
    """불일치 목록 [(위치, 경로, 이유, 네이티브, Python)]."""
    out = []
    nw = OrderedDict((w["id"], w) for w in native.get("widgets", []))
    pw = OrderedDict((w["id"], w) for w in python.get("widgets", []))
    for wid in nw:
        if wid not in pw:
            out.append(("widget " + wid, "", "네이티브에만 있는 위젯 항목", nw[wid]["kind"], None))
    for wid in pw:
        if wid not in nw:
            out.append(("widget " + wid, "", "Python 에만 있는 위젯 항목", None, pw[wid]["kind"]))
    for wid in nw:
        if wid not in pw:
            continue
        a, b = nw[wid], pw[wid]
        if a["kind"] != b["kind"] or bool(a.get("moved")) != bool(b.get("moved")):
            out.append(("widget " + wid, "", "kind 다름", (a["kind"], a.get("moved", False)), (b["kind"], b.get("moved", False))))
        ra, rb = a.get("rename"), b.get("rename")
        if (ra or {}).get("from") != (rb or {}).get("from"):
            out.append(("widget " + wid, "", "rename.from 다름", (ra or {}).get("from"), (rb or {}).get("from")))
        _compare_rows("widget " + wid, a.get("properties", []), b.get("properties", []), out)
        sa, sb = a.get("slot", {}), b.get("slot", {})
        if sa.get("class") != sb.get("class"):
            out.append(("slot " + wid, "", "슬롯 클래스 다름", sa.get("class"), sb.get("class")))
        _compare_rows("slot " + wid, sa.get("properties", []), sb.get("properties", []), out)
    _compare_rows("cdo", native.get("cdo", []), python.get("cdo", []), out)
    nb = {b["id"]: b for b in native.get("bindings", [])}
    pb = {b["id"]: b for b in python.get("bindings", [])}
    for k in set(nb) | set(pb):
        a, b = nb.get(k), pb.get(k)
        if a is None or b is None:
            out.append(("binding " + k, "", "한쪽에만 있는 바인딩", a and a["kind"], b and b["kind"]))
        elif a["kind"] != b["kind"] or a.get("old") != b.get("old") or a.get("new") != b.get("new"):
            out.append(("binding " + k, "", "바인딩 다름", (a["kind"], a.get("old"), a.get("new")), (b["kind"], b.get("old"), b.get("new"))))
    compare_variables(native, python, out)
    compare_animations(native, python, out)
    compare_graphs(native, python, out)
    return out


def _by(items, key):
    return OrderedDict((key(x), x) for x in items)


def _keyed(where, na, pa, key, out, label):
    """두 목록을 key 로 짝짓고 한쪽에만 있는 것을 적는다. 짝지은 (n, p) 목록을 돌려준다."""
    n, p = _by(na, key), _by(pa, key)
    pairs = []
    for k in n:
        if k not in p:
            out.append((where, str(k), "네이티브에만 있는 " + label, n[k].get("kind"), None))
        else:
            pairs.append((k, n[k], p[k]))
    for k in p:
        if k not in n:
            out.append((where, str(k), "Python 에만 있는 " + label, None, p[k].get("kind")))
    return pairs


def _compare_values(where, path, a, b, out):
    for side in ("old", "new"):
        av, bv = a.get(side), b.get(side)
        if (av is None) != (bv is None):
            out.append((where, path, side + " 유무 다름", av is not None, bv is not None))
        elif av is not None and not value_eq(av.get("value"), bv.get("value")):
            out.append((where, path, side + " 값 다름", av.get("value"), bv.get("value")))


def compare_variables(native, python, out):
    for k, a, b in _keyed("variables", native.get("variables", []), python.get("variables", []), lambda v: v["varGuid"], out, "변수"):
        where = "variable " + str(a.get("name"))
        if a["kind"] != b["kind"] or (a.get("renamedFrom") or None) != (b.get("renamedFrom") or None):
            out.append((where, "", "kind/renamedFrom 다름", (a["kind"], a.get("renamedFrom")), (b["kind"], b.get("renamedFrom"))))
        _compare_rows(where, a.get("properties", []), b.get("properties", []), out)


def compare_animations(native, python, out):
    for aid, a, b in _keyed("animations", native.get("animations", []), python.get("animations", []), lambda x: x["id"], out, "애니메이션"):
        where = "anim " + aid
        if a["kind"] != b["kind"]:
            out.append((where, "", "kind 다름", a["kind"], b["kind"]))
        for tid, ta, tb in _keyed(where, a.get("tracks", []), b.get("tracks", []), lambda x: x["id"], out, "트랙"):
            tw = where + "/" + tid
            if ta["kind"] != tb["kind"] or ta.get("sectionCount") != tb.get("sectionCount"):
                out.append((tw, "", "트랙 kind/섹션 수 다름", (ta["kind"], ta.get("sectionCount")), (tb["kind"], tb.get("sectionCount"))))
            for si, sa, sb in _keyed(tw, ta.get("sections", []), tb.get("sections", []), lambda x: x["index"], out, "섹션"):
                sw = "%s[%d]" % (tw, si)
                if sa["kind"] != sb["kind"] or sa.get("range") != sb.get("range"):
                    out.append((sw, "", "섹션 kind/범위 다름", (sa["kind"], sa.get("range")), (sb["kind"], sb.get("range"))))
                _compare_rows(sw, sa.get("properties", []), sb.get("properties", []), out)
                for cn, ca, cb in _keyed(sw, sa.get("channels", []), sb.get("channels", []), lambda x: x["name"], out, "채널"):
                    cw = sw + "." + cn
                    _compare_rows(cw, ca.get("properties", []), cb.get("properties", []), out)
                    for fr, ka, kb in _keyed(cw, ca.get("keys", []), cb.get("keys", []), lambda x: (x["frame"], x["kind"]), out, "키"):
                        kw = "%s@%d" % (cw, fr[0])
                        _compare_values(kw, "value", ka, kb, out)
                        _compare_rows(kw, ka.get("properties", []), kb.get("properties", []), out)


def compare_graphs(native, python, out):
    for gid, a, b in _keyed("graphs", native.get("graphs", []), python.get("graphs", []), lambda x: x["id"], out, "그래프"):
        where = "graph " + gid
        if a["kind"] != b["kind"] or a.get("logic") != b.get("logic") or a.get("layout") != b.get("layout"):
            out.append((where, "", "그래프 kind/개수 다름", (a["kind"], a.get("logic"), a.get("layout")), (b["kind"], b.get("logic"), b.get("layout"))))
        for guid, na, nb in _keyed(where, a.get("nodes", []), b.get("nodes", []), lambda x: x["guid"], out, "노드"):
            nw = "%s/%s" % (where, guid[:8])
            if na["kind"] != nb["kind"] or na["area"] != nb["area"] or na["class"] != nb["class"]:
                out.append((nw, "", "노드 kind/area/class 다름", (na["kind"], na["area"], na["class"]), (nb["kind"], nb["area"], nb["class"])))
            _compare_rows(nw, na.get("properties", []), nb.get("properties", []), out)
            for pid, pa, pb in _keyed(nw, na.get("pins", []), nb.get("pins", []), lambda x: x["pinId"], out, "핀"):
                pw = "%s.%s" % (nw, pa.get("name"))
                if pa["kind"] != pb["kind"]:
                    out.append((pw, "", "핀 kind 다름", pa["kind"], pb["kind"]))
                _compare_rows(pw, pa.get("properties", []), pb.get("properties", []), out)


def format_table(rows, limit=200):
    lines = []
    for where, path, why, nv, pv in rows[:limit]:
        lines.append("%-40s %-40s %-22s 네이티브=%s  Python=%s" % (where, path, why, _short(nv), _short(pv)))
    if len(rows) > limit:
        lines.append("… %d건 더" % (len(rows) - limit))
    return "\n".join(lines)


def _short(v):
    s = repr(v)
    return s if len(s) <= 80 else s[:77] + "..."
