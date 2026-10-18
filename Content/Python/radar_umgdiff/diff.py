"""상태 둘 → diff.json (dev 문서 §5, §7 과 같은 규칙). 네이티브 RadarNativeDiffProducer / RadarPropertyCompare 를 따라 쓴다.

두 생산자가 같은 줄을 내게 하는 규칙 (dev §19 M1·M3 기록):
- 비교 대상 = 파일에 저장되는 속성. 한쪽에만 태그가 있으면 changed + maybeDefault (없는 쪽은 아키타입 값 = 네이티브 "default").
- 잎 구조체(ini LeafStructs)는 한 줄 + changedComponents, 그 밖의 구조체는 멤버로 내려간다. 배열은 인덱스 단위.
- 수치는 ini 허용 오차(상대), FText 는 source(+table/tableKey) 로만 비교, 키만 다르면 textKeys(저심각도, Python 만).
- 이동 = 부모가 바뀌었거나, 양쪽에 같은 부모로 남은 형제들 사이의 순서가 바뀐 것.
"""

from collections import OrderedDict
import json

from . import ANALYZER_NAME, SCHEMA_VERSION
from .text import text_compare_key
from .tags import TaggedStruct
from . import natives
from .graphdiff import diff_graphs, diff_variables
from .animdiff import diff_animations

STRUCTURAL_WIDGET = {"Slot", "Slots"}
STRUCTURAL_SLOT = {"Parent", "Content"}
F32_NATIVE_STRUCTS = {"LinearColor", "DeprecateSlateVector2D", "Vector2f", "Vector3f", "Vector4f"}


class Ctx:
    def __init__(self, settings):
        self.ignored = settings.ignored
        self.auto = settings.auto
        self.leaf = settings.leaf_structs
        self.ftol = settings.float_tolerance
        self.dtol = settings.double_tolerance
        self.rename_threshold = settings.rename_threshold
        self.structural = set()


def _near(a, b, tol):
    if a == b:
        return True
    try:
        return abs(a - b) <= tol * max(abs(a), abs(b))
    except TypeError:
        return False


def _struct_name(node):
    return node.params[0].name if node is not None and node.name == "StructProperty" and node.params else None


def equal(a, b, node, ctx):
    if a is None or b is None:
        return a is None and b is None
    n = node.name if node is not None else None
    if n == "FloatProperty":
        return _near(a, b, ctx.ftol)
    if n == "DoubleProperty":
        return _near(a, b, ctx.dtol)
    if n == "TextProperty":
        ka, kb = text_compare_key(a), text_compare_key(b)
        if ka[0] is None or kb[0] is None:      # 스트링테이블: source 모름 → 테이블·키만
            return ka[1:] == kb[1:]
        return ka == kb
    if isinstance(a, dict) and isinstance(b, dict):
        if "raw" in a or "raw" in b:
            return a == b
        sname = _struct_name(node)
        tol = ctx.ftol if sname in F32_NATIVE_STRUCTS else ctx.dtol
        keys = list(OrderedDict.fromkeys(list(a.keys()) + list(b.keys())))
        for k in keys:
            if (k in a) != (k in b):
                return False
            sub = _member_node(a, b, k)
            if sub is None:
                if isinstance(a[k], float) or isinstance(b[k], float):
                    if not _near(a[k], b[k], tol):
                        return False
                    continue
                if isinstance(a[k], dict) and isinstance(b[k], dict):
                    if not equal(a[k], b[k], None, ctx):
                        return False
                    continue
            if not equal(a[k], b[k], sub, ctx):
                return False
        return True
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return False
        inner = node.params[0] if node is not None and node.params else None
        return all(equal(x, y, inner, ctx) for x, y in zip(a, b))
    if isinstance(a, float) or isinstance(b, float):
        return _near(a, b, ctx.ftol)
    return a == b


def _member_node(a, b, k):
    for s in (a, b):
        if isinstance(s, TaggedStruct) and k in s.types:
            return s.types[k]
    return None


def _text(v):
    if v is None:
        return None
    if isinstance(v, str):
        return v
    if isinstance(v, dict) and "source" in v and "namespace" in v:
        if v["source"] is not None:
            return v["source"]
        if "generated" in v:
            fmt = (v["generated"].get("format") or {}).get("source") if isinstance(v["generated"].get("format"), dict) else None
            return "(생성 텍스트%s)" % (": " + fmt if fmt else "")
        return "%s:%s" % (v.get("table"), v.get("tableKey"))
    return json.dumps(v, ensure_ascii=False, separators=(",", ":"))


def _jsonable(v):
    if isinstance(v, dict):
        return OrderedDict((k, _jsonable(x)) for k, x in v.items() if k not in ("historyType", "flags", "devNotes"))
    if isinstance(v, list):
        return [_jsonable(x) for x in v]
    return v


def _value(v, present):
    if not present:
        return OrderedDict([("state", "absent"), ("text", None), ("value", None)])
    out = OrderedDict([("state", "present"), ("text", _text(v)), ("value", _jsonable(v))])
    return out


def _is_leaf(node, ctx):
    s = _struct_name(node)
    return s is not None and s in ctx.leaf


def _row(path, node, o, n, op, np_, kind, ctx, name):
    r = OrderedDict()
    r["path"] = path
    r["category"] = "기타"
    r["type"] = node.type_string() if node is not None else "?"
    r["kind"] = kind
    r["maybeDefault"] = (op != np_)
    if name in ctx.auto:
        r["auto"] = True
    r["old"] = _value(o, op) if kind != "added" else None
    r["new"] = _value(n, np_) if kind != "removed" else None
    if op and np_ and _is_leaf(node, ctx) and isinstance(o, dict) and isinstance(n, dict):
        comps = []
        for k in OrderedDict.fromkeys(list(o.keys()) + list(n.keys())):
            if (k in o) != (k in n) or not equal(o.get(k), n.get(k), _member_node(o, n, k), ctx):
                comps.append(k)
        if comps:
            r["changedComponents"] = comps
    elif _is_leaf(node, ctx) and (op != np_):
        side = o if op else n
        if isinstance(side, dict):
            r["changedComponents"] = list(side.keys())
    return r


def diff_value(o, n, op, np_, node, path, ctx, rows, name):
    if op and np_ and equal(o, n, node, ctx):
        return
    if not op and not np_:
        return
    sname = _struct_name(node)
    is_struct_dict = isinstance(o if op else n, dict) and not ("raw" in (o if op else n))
    if sname is not None and sname not in ctx.leaf and is_struct_dict and node.name == "StructProperty":
        a = o if op else TaggedStruct()
        b = n if np_ else TaggedStruct()
        diff_members(a, b, path + ".", ctx, rows)
        return
    if node is not None and node.name == "ArrayProperty":
        a = o if op else []
        b = n if np_ else []
        inner = node.params[0] if node.params else None
        common = min(len(a), len(b))
        for i in range(common):
            diff_value(a[i], b[i], True, True, inner, "%s[%d]" % (path, i), ctx, rows, name)
        for i in range(common, len(a)):
            rows.append(_row("%s[%d]" % (path, i), inner, a[i], None, True, False, "removed", ctx, name))
        for i in range(common, len(b)):
            rows.append(_row("%s[%d]" % (path, i), inner, None, b[i], False, True, "added", ctx, name))
        return
    if node is None and op and np_ and _plain_struct(o) and _plain_struct(n):
        # 네이티브 리더가 만든 구조체 값(EdGraphPinType 의 PinValueType 등) — 네이티브 DiffStruct 처럼 멤버로 내려간다
        diff_members(o, n, path + ".", ctx, rows)
        return
    rows.append(_row(path, node, o, n, op, np_, "changed", ctx, name))


def _plain_struct(v):
    return isinstance(v, dict) and not isinstance(v, TaggedStruct) and "raw" not in v and not ("source" in v and "namespace" in v)


def diff_members(a, b, prefix, ctx, rows, skip_names=frozenset(), class_changed=False):
    keys = list(OrderedDict.fromkeys(list(a.keys()) + list(b.keys())))
    for k in keys:
        base = k.split("[", 1)[0]
        if base in ctx.ignored or base in ctx.structural or base in skip_names:
            continue
        node = _member_node(a, b, k)
        if class_changed and (k in a) != (k in b):
            # 클래스가 바뀐 객체(슬롯 클래스 교체 등): 한쪽에만 있는 속성은 그 클래스에만 있는 속성으로 본다 — 네이티브 DiffObjects 와 같다.
            # (두 클래스 모두에 있는데 한쪽이 기본값이라 저장 안 된 속성은 구별할 수 없다 — dev §19 M3)
            rows.append(_row(prefix + k, node, a.get(k), b.get(k), k in a, k in b, "removed" if k in a else "added", ctx, base))
            continue
        diff_value(a.get(k), b.get(k), k in a, k in b, node, prefix + k, ctx, rows, base)


def diff_props(oprops, nprops, ctx, structural, class_changed=False):
    saved = ctx.structural
    ctx.structural = structural
    rows = []
    diff_members(oprops, nprops, "", ctx, rows, class_changed=class_changed)
    ctx.structural = saved
    for r in rows:
        if class_changed and r["kind"] in ("added", "removed"):
            r["maybeDefault"] = False
    return rows


def count_present(oprops, nprops, ctx, structural):
    def keys(p):
        return [k for k in p.keys() if k.split("[", 1)[0] not in ctx.ignored and k.split("[", 1)[0] not in structural]
    ok, nk = keys(oprops), keys(nprops)
    same = sum(1 for k in ok if k in nprops and equal(oprops[k], nprops[k], _member_node(oprops, nprops, k), ctx))
    return len(ok), len(nk), same


def _label(obj):
    v = obj.props.get("DisplayLabel")
    return v if isinstance(v, str) else ""


def _relative_index(self_state, other_state, wid):
    me_parent = self_state.parent.get(wid)
    sib = []
    for w, p in self_state.parent.items():
        if p == me_parent and other_state.parent.get(w) == me_parent and w in other_state.widgets:
            sib.append((self_state.index[w], w))
    sib.sort()
    ids = [w for _, w in sib]
    return ids.index(wid) if wid in ids else -1


def _slot_diff(old_state, new_state, oid, nid, ctx):
    os_ = old_state.slots.get(oid) if oid else None
    ns_ = new_state.slots.get(nid) if nid else None
    out = OrderedDict()
    out["class"] = OrderedDict([("old", os_.class_path if os_ else None), ("new", ns_.class_path if ns_ else None)])
    out["properties"] = diff_props(os_.props, ns_.props, ctx, STRUCTURAL_SLOT, class_changed=(os_.class_path != ns_.class_path)) if (os_ and ns_) else []
    return out


def diff_widgets(old, new, ctx):
    out = []
    removed, added = [], []
    for wid in new.order:
        nw = new.widgets[wid]
        ow = old.widgets.get(wid)
        if ow is None:
            added.append(wid)
            continue
        props = []
        if ow.class_path != nw.class_path:
            props.append(OrderedDict([("path", "(class)"), ("category", "기타"), ("type", "Class"), ("kind", "changed"), ("maybeDefault", False),
                                      ("old", OrderedDict([("state", "present"), ("text", ow.class_path), ("value", ow.class_path)])),
                                      ("new", OrderedDict([("state", "present"), ("text", nw.class_path), ("value", nw.class_path)]))]))
        props += diff_props(ow.props, nw.props, ctx, STRUCTURAL_WIDGET, class_changed=(ow.class_path != nw.class_path))
        slot = _slot_diff(old, new, wid, wid, ctx)
        moved = old.parent.get(wid) != new.parent.get(wid) or _relative_index(old, new, wid) != _relative_index(new, old, wid)
        slot_changed = bool(slot["properties"]) or slot["class"]["old"] != slot["class"]["new"]
        if not props and not slot_changed and not moved:
            continue
        if props:
            kind, area = "changed", "designer"
        else:
            kind, area = ("moved" if moved else "changed"), "slot"
        severity = "low" if (props and not slot_changed and not moved and len(props) == 1 and props[0]["path"] == "DisplayLabel") else "normal"
        out.append(_entry(wid, kind, area, severity, nw.class_path, ow, nw, old, new, wid, wid, props, slot, moved_too=(bool(props) and moved)))
    for wid in old.order:
        if wid not in new.widgets:
            removed.append(wid)

    # 이름 변경 추정 (§7.3)
    cands = []
    for r in removed:
        ro = old.widgets[r]
        for a in added:
            ao = new.widgets[a]
            if ro.class_path != ao.class_path:
                continue
            if old.parent.get(r) == new.parent.get(a) and old.index.get(r, -1) == new.index.get(a, -1):
                cands.append((r, a, 1.0, "same-parent-index"))
                continue
            on, nn, same = count_present(ro.props, ao.props, ctx, STRUCTURAL_WIDGET)
            mx = max(on, nn)
            score = float(same) / mx if mx else 0.0
            if score >= ctx.rename_threshold:
                cands.append((r, a, score, "property-set"))
    cands.sort(key=lambda c: -c[2])   # 안정 정렬 (네이티브 StableSort 와 같다)
    used_r, used_a = set(), set()
    for r, a, score, ev in cands:
        if r in used_r or a in used_a:
            continue
        used_r.add(r)
        used_a.add(a)
        ro, ao = old.widgets[r], new.widgets[a]
        props = diff_props(ro.props, ao.props, ctx, STRUCTURAL_WIDGET)
        slot = _slot_diff(old, new, r, a, ctx)
        e = _entry(a, "renamed-probable", "designer", "normal", ao.class_path, ro, ao, old, new, r, a, props, slot,
                   rename=OrderedDict([("from", r), ("confidence", score), ("evidence", ev), ("graphConfirmed", False)]))
        out.append(e)
    for a in added:
        if a not in used_a:
            out.append(_one_sided(new, a, True, ctx))
    for r in removed:
        if r not in used_r:
            out.append(_one_sided(old, r, False, ctx))
    return out


def _entry(wid, kind, area, severity, cls, ow, nw, old, new, oid, nid, props, slot, moved_too=False, rename=None):
    d = OrderedDict()
    d["id"] = wid
    d["kind"] = kind
    if moved_too:
        d["moved"] = True
    d["area"] = area
    d["severity"] = severity
    d["class"] = cls
    if rename:
        d["rename"] = rename
    d["displayLabel"] = OrderedDict([("old", _label(ow) if ow else None), ("new", _label(nw) if nw else None)])
    d["parent"] = OrderedDict([("old", old.parent.get(oid) if ow else None), ("new", new.parent.get(nid) if nw else None)])
    d["index"] = OrderedDict([("old", old.index.get(oid) if ow else None), ("new", new.index.get(nid) if nw else None)])
    d["badges"] = []
    d["properties"] = props
    d["slot"] = slot
    return d


def _one_sided(state, wid, added, ctx):
    w = state.widgets[wid]
    d = OrderedDict()
    d["id"] = wid
    d["kind"] = "added" if added else "removed"
    d["area"] = "designer"
    d["severity"] = "normal"
    d["class"] = w.class_path
    d["displayLabel"] = OrderedDict([("old", None if added else _label(w)), ("new", _label(w) if added else None)])
    d["parent"] = OrderedDict([("old", None if added else state.parent.get(wid)), ("new", state.parent.get(wid) if added else None)])
    d["index"] = OrderedDict([("old", None if added else state.index.get(wid)), ("new", state.index.get(wid) if added else None)])
    d["badges"] = []
    d["stateRef"] = wid
    star = OrderedDict([("path", "*"), ("category", "기타"), ("type", ""), ("kind", d["kind"]), ("maybeDefault", False)])
    v = OrderedDict([("state", "present"), ("text", "…전체 상태 덤프 참조…"), ("value", None)])
    star["old"] = None if added else v
    star["new"] = v if added else None
    d["properties"] = [star]
    s = state.slots.get(wid)
    d["slot"] = OrderedDict([("class", OrderedDict([("old", None if added else (s.class_path if s else None)),
                                                    ("new", (s.class_path if s else None) if added else None)])),
                             ("properties", [])])
    return d


def _binding_key(b):
    return "%s:%s" % (b.get("ObjectName", ""), b.get("PropertyName", "None"))


def _binding_fields(b):
    kind = b.get("Kind", "EBindingKind::Property")
    kind = kind.split("::", 1)[1] if isinstance(kind, str) and "::" in kind else ("Function" if kind == 0 else "Property")
    return OrderedDict([("function", b.get("FunctionName", "None")), ("kind", kind), ("sourceProperty", b.get("SourceProperty", "None"))])


def diff_bindings(old, new):
    ob = {_binding_key(b): b for b in (old.bp_props().get("Bindings") or []) if isinstance(b, dict)}
    nb = {_binding_key(b): b for b in (new.bp_props().get("Bindings") or []) if isinstance(b, dict)}
    out = []
    for k, b in nb.items():
        nf = _binding_fields(b)
        if k in ob:
            of = _binding_fields(ob[k])
            if of == nf:
                continue
            out.append(OrderedDict([("id", k), ("kind", "changed"), ("area", "binding"), ("old", of), ("new", nf)]))
        else:
            out.append(OrderedDict([("id", k), ("kind", "added"), ("area", "binding"), ("old", None), ("new", nf)]))
    for k, b in ob.items():
        if k not in nb:
            out.append(OrderedDict([("id", k), ("kind", "removed"), ("area", "binding"), ("old", _binding_fields(b)), ("new", None)]))
    return out


def _bp_var_names(state):
    out = {}
    for v in state.bp_props().get("NewVariables") or []:
        if isinstance(v, dict) and isinstance(v.get("VarName"), str):
            out[v["VarName"]] = v.get("VarGuid")
    return out


def diff_cdo(old, new, ctx):
    oc, nc = old.cdo_obj(), new.cdo_obj()
    if not oc or not nc:
        return []
    rows = diff_props(oc.props, nc.props, ctx, set())
    ov, nv = _bp_var_names(old), _bp_var_names(new)
    guids = dict(ov)
    guids.update(nv)
    for r in rows:
        root = r["path"].split(".", 1)[0].split("[", 1)[0]
        # 한쪽 클래스에만 있는 블루프린트 변수 → added/removed (네이티브 DiffObjects 와 같다)
        if root in nv and root not in ov and r["old"] is not None and r["old"]["state"] == "absent":
            r["kind"], r["old"], r["maybeDefault"] = "added", None, False
        elif root in ov and root not in nv and r["new"] is not None and r["new"]["state"] == "absent":
            r["kind"], r["new"], r["maybeDefault"] = "removed", None, False
        if root in guids and guids[root]:
            r["varGuid"] = guids[root]
    return rows


def text_keys(old, new, ctx):
    out = []
    for wid, nw in new.widgets.items():
        ow = old.widgets.get(wid)
        if not ow:
            continue
        for k, v in nw.props.items():
            ov = ow.props.get(k)
            if isinstance(v, dict) and isinstance(ov, dict) and "namespace" in v and "namespace" in ov:
                if text_compare_key(v) == text_compare_key(ov) and (v["namespace"], v["key"]) != (ov["namespace"], ov["key"]):
                    out.append(OrderedDict([("widget", wid), ("path", k), ("kind", "text-key-only"), ("severity", "low"),
                                            ("old", OrderedDict([("namespace", ov["namespace"]), ("key", ov["key"])])),
                                            ("new", OrderedDict([("namespace", v["namespace"]), ("key", v["key"])]))]))
    return out


def count_summary(d):
    by_kind = OrderedDict((k, 0) for k in ("added", "removed", "changed", "moved", "renamed-probable", "text-key-only"))
    by_area = OrderedDict((k, 0) for k in ("designer", "slot", "binding", "cdo", "anim", "graph-logic", "graph-layout"))
    for w in d["widgets"]:
        if w["kind"] in by_kind:
            by_kind[w["kind"]] += 1
        if w.get("moved"):
            by_kind["moved"] += 1
        slot = bool(w["slot"]["properties"]) or w["slot"]["class"]["old"] != w["slot"]["class"]["new"] or w["kind"] == "moved" or w.get("moved")
        designer = w["kind"] in ("added", "removed", "renamed-probable") or bool([p for p in w["properties"] if p["path"] != "*"]) or w["kind"] in ("added", "removed")
        if designer:
            by_area["designer"] += 1
        if slot and w["kind"] not in ("added", "removed"):
            by_area["slot"] += 1
    for b in d["bindings"]:
        by_kind[b["kind"]] = by_kind.get(b["kind"], 0) + 1
        by_area["binding"] += 1
    for c in d["cdo"]:
        by_kind[c["kind"]] = by_kind.get(c["kind"], 0) + 1
        by_area["cdo"] += 1
    for v in d.get("variables", []):
        by_kind[v["kind"]] = by_kind.get(v["kind"], 0) + 1
        by_area["cdo"] += 1
    for a in d["animations"]:
        by_kind[a["kind"]] = by_kind.get(a["kind"], 0) + 1
        by_area["anim"] += 1
    for g in d["graphs"]:
        for n in g["nodes"]:
            by_kind[n["kind"]] = by_kind.get(n["kind"], 0) + 1
            by_area[n["area"]] += 1
    by_kind["text-key-only"] = len(d.get("textKeys", []))
    return OrderedDict([("byKind", by_kind), ("byArea", by_area)])


def _side(rev, state):
    out = OrderedDict()
    rev = rev or {"kind": "file", "path": state.pkg.path}
    for k, v in rev.items():
        out[k] = v
    cov = state.coverage()
    out["coverage"] = cov["ratio"]
    out["designerCoverage"] = cov["designerRatio"]
    out["unparsedStructs"] = sorted(cov["unparsedStructs"])
    return out


def _design_size(state):
    c = state.cdo_obj()
    if not c:
        return OrderedDict([("mode", None), ("size", None)])
    mode = c.props.get("DesignSizeMode")
    size = c.props.get("DesignTimeSize")
    return OrderedDict([("mode", mode.split("::", 1)[1] if isinstance(mode, str) and "::" in mode else mode),
                        ("size", [size["X"], size["Y"]] if isinstance(size, dict) else None)])


def make_diff(old, new, settings, old_rev=None, new_rev=None):
    ctx = Ctx(settings)
    d = OrderedDict()
    d["schemaVersion"] = SCHEMA_VERSION
    d["producer"] = OrderedDict([
        ("name", ANALYZER_NAME), ("kind", "python"), ("engine", None), ("pluginBranch", None),
        ("fileVersionUE5", OrderedDict([("old", old.pkg.ue5), ("new", new.pkg.ue5)])),
        ("customVersions", OrderedDict([("old", old.pkg.summary["customVersions"]), ("new", new.pkg.summary["customVersions"])])),
        ("capabilities", ["text-key-only", "coverage", "graph", "anim-keys"]),
    ])
    asset = new.pkg.package_name.rsplit("/", 1)[-1]
    d["asset"] = OrderedDict([("packageName", new.pkg.package_name), ("relPath", new.pkg.path), ("assetName", asset),
                              ("parentClass", (new.bp_props().get("ParentClass") if new.bp else None))])
    d["old"] = _side(old_rev, old)
    d["new"] = _side(new_rev, new)
    d["designSize"] = OrderedDict([("old", _design_size(old)), ("new", _design_size(new))])
    d["summary"] = None
    d["widgets"] = diff_widgets(old, new, ctx)
    d["bindings"] = diff_bindings(old, new)
    d["cdo"] = diff_cdo(old, new, ctx)
    d["variables"] = diff_variables(old, new, ctx)
    d["animations"] = diff_animations(old, new, ctx)
    d["graphs"] = diff_graphs(old, new, ctx)
    d["textKeys"] = text_keys(old, new, ctx)
    notes = []
    seen = set()
    for st in (new, old):
        for wid, w in st.widgets.items():
            if not w.class_path.startswith("/Script/") and wid not in seen:
                seen.add(wid)
                notes.append(OrderedDict([("code", "nested-userwidget-current-version"), ("widget", wid), ("severity", "info")]))
    d["notes"] = notes
    d["summary"] = count_summary(d)
    return d
