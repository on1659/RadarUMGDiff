"""그래프·블루프린트 변수 diff (dev 문서 §7.5). 네이티브 RadarNativeDiffProducer 의 DiffGraphs·DiffPins·DiffVariables 와 같은 규칙.

- 그래프: UbergraphPages·FunctionGraphs·MacroGraphs·DelegateSignatureGraphs 의 그래프 이름으로 짝짓는다.
- 노드: NodeGuid 로 짝짓는다. 속성 줄은 태그 비교(NodeGuid·DeprecatedPins 제외), 배치 속성만 바뀌면 graph-layout.
- 핀: PinId 로 짝짓는다. DefaultValue · DefaultObject · DefaultTextValue · LinkedTo("<노드 NodeGuid>/<PinId>" 정렬) · PinType.
- 변수: NewVariables 를 VarGuid 로 짝짓는다. 이름이 바뀌면 renamedFrom.
노드 제목(title)은 파일에 없다(GetNodeTitle 은 런타임 계산) — Python 은 클래스·멤버 이름으로 대신 적고, 일치 비교는 제목을 보지 않는다.
"""

from collections import OrderedDict

from .state import _props_of
from .tags import TaggedStruct

LAYOUT = {"NodePosX", "NodePosY", "NodeWidth", "NodeHeight", "NodeComment", "bCommentBubbleVisible",
          "bCommentBubblePinned", "CommentColor", "bCommentBubbleMakeVisible"}
GRAPH_LISTS = ("UbergraphPages", "FunctionGraphs", "MacroGraphs", "DelegateSignatureGraphs")
NODE_STRUCTURAL = {"NodeGuid", "DeprecatedPins"}


class _Graphs:
    """한 리비전의 그래프 → 노드 → 핀 표."""

    def __init__(self, state):
        self.state = state
        path_to_export = {state.path_of[i]: e for i, e in state.by_index.items()}
        bp = state.bp_props()
        self.graphs = OrderedDict()          # 이름 → [노드 dict]
        self.guid_of_export = {}             # 노드 익스포트 인덱스 → NodeGuid
        nodes_of = OrderedDict()
        for key in GRAPH_LISTS:
            for gpath in bp.get(key) or []:
                ge = path_to_export.get(gpath) if isinstance(gpath, str) else None
                if ge is None:
                    continue
                gprops = _props_of(ge)
                nodes = []
                for npath in gprops.get("Nodes") or []:
                    ne = path_to_export.get(npath) if isinstance(npath, str) else None
                    if ne is not None:
                        nodes.append(ne)
                nodes_of[ge["objectName"]] = nodes
        for name, exports in nodes_of.items():
            out = []
            for ne in exports:
                props = _props_of(ne)
                guid = props.get("NodeGuid")
                self.guid_of_export[ne["index"]] = guid
                out.append({"export": ne, "props": props, "guid": guid, "pins": (ne.get("suffix") or {}).get("pins") or []})
            self.graphs[name] = out

    def pin_key(self, ref):
        guid = self.guid_of_export.get(ref["node"]) or "?"
        return "%s/%s" % (guid, ref["pinId"])


def _title(node):
    p = node["props"]
    for k in ("CustomFunctionName",):
        if isinstance(p.get(k), str) and p[k] != "None":
            return p[k]
    for k in ("FunctionReference", "EventReference", "VariableReference"):
        ref = p.get(k)
        if isinstance(ref, dict) and isinstance(ref.get("MemberName"), str):
            return ref["MemberName"]
    return node["export"]["className"]


def _text_source(t):
    if not isinstance(t, dict):
        return ""
    if t.get("source") is not None:
        return t["source"]
    return "%s:%s" % (t.get("table"), t.get("tableKey"))


def _pin_fields(graphs, pin):
    """(이름, 비교 문자열, value) — 네이티브 PinFields 와 같은 순서·같은 값 형."""
    links = sorted(graphs.pin_key(r) for r in pin["linkedTo"] if r)
    obj = pin["defaultObject"]
    return [
        ("DefaultValue", pin["defaultValue"], pin["defaultValue"]),
        ("DefaultObject", obj or "", obj),
        ("DefaultTextValue", _text_source(pin["defaultTextValue"]), _text_source(pin["defaultTextValue"])),
        ("LinkedTo", ", ".join(links), links),
        ("PinType", None, pin["pinType"]),
    ]


def _pin_type_text(t):
    import json
    return json.dumps(t, sort_keys=True, ensure_ascii=False)


def _pin_rows(graphs_old, graphs_new, op, np_):
    rows = []
    of = _pin_fields(graphs_old, op) if op else None
    nf = _pin_fields(graphs_new, np_) if np_ else None
    count = len(of or nf)
    for i in range(count):
        key = (of or nf)[i][0]
        ot = (_pin_type_text(of[i][2]) if key == "PinType" else of[i][1]) if of else None
        nt = (_pin_type_text(nf[i][2]) if key == "PinType" else nf[i][1]) if nf else None
        if of and nf and ot == nt:
            continue
        r = OrderedDict()
        r["path"] = key
        r["category"] = "핀"
        r["type"] = "ArrayProperty:StrProperty" if key == "LinkedTo" else ("StructProperty:EdGraphPinType" if key == "PinType" else "StrProperty")
        r["kind"] = "changed" if (of and nf) else ("added" if nf else "removed")
        r["maybeDefault"] = False
        r["old"] = OrderedDict([("state", "present"), ("text", ot), ("value", of[i][2])]) if of else None
        r["new"] = OrderedDict([("state", "present"), ("text", nt), ("value", nf[i][2])]) if nf else None
        rows.append(r)
    return rows


def _diff_pins(go, gn, onode, nnode):
    out = []
    old_pins = OrderedDict((p["pinId"], p) for p in (onode["pins"] if onode else []) if p)
    seen = set()
    for p in (nnode["pins"] if nnode else []):
        if not p:
            continue
        seen.add(p["pinId"])
        o = old_pins.get(p["pinId"])
        d = OrderedDict([("pinId", p["pinId"]), ("name", p["name"]), ("kind", "changed" if o else "added")])
        if o:
            d["properties"] = _pin_rows(go, gn, o, p)
            if not d["properties"]:
                continue
        else:
            d["properties"] = []
        out.append(d)
    for pid, p in old_pins.items():
        if pid not in seen:
            out.append(OrderedDict([("pinId", pid), ("name", p["name"]), ("kind", "removed"), ("properties", [])]))
    return out


def _node_entry(node, kind, area):
    return OrderedDict([("guid", node["guid"]), ("class", node["export"]["class"]), ("title", _title(node)),
                        ("kind", kind), ("area", area), ("properties", []), ("pins", [])])


def diff_graphs(old, new, ctx):
    from .diff import diff_props
    go, gn = _Graphs(old), _Graphs(new)
    out = []

    def one(name, onodes, nnodes):
        g = OrderedDict([("id", name), ("kind", "added" if onodes is None else ("removed" if nnodes is None else "changed")),
                         ("logic", OrderedDict([("added", 0), ("removed", 0), ("changed", 0)])), ("layout", OrderedDict([("moved", 0)])),
                         ("nodes", [])])
        by_guid = OrderedDict((n["guid"], n) for n in (onodes or []))
        seen = set()
        for n in nnodes or []:
            seen.add(n["guid"])
            o = by_guid.get(n["guid"])
            if o is None:
                g["nodes"].append(_node_entry(n, "added", "graph-logic"))
                g["logic"]["added"] += 1
                continue
            e = _node_entry(n, "changed", "graph-logic")
            e["properties"] = diff_props(o["props"], n["props"], ctx, NODE_STRUCTURAL,
                                         class_changed=(o["export"]["class"] != n["export"]["class"]))
            e["pins"] = _diff_pins(go, gn, o, n)
            if not e["properties"] and not e["pins"]:
                continue
            layout_only = not e["pins"] and all(r["path"].split(".", 1)[0].split("[", 1)[0] in LAYOUT for r in e["properties"])
            e["area"] = "graph-layout" if layout_only else "graph-logic"
            g["layout" if layout_only else "logic"]["moved" if layout_only else "changed"] += 1
            g["nodes"].append(e)
        for guid, o in by_guid.items():
            if guid not in seen:
                g["nodes"].append(_node_entry(o, "removed", "graph-logic"))
                g["logic"]["removed"] += 1
        if g["kind"] == "changed" and not g["nodes"]:
            return
        out.append(g)

    for name, nodes in gn.graphs.items():
        one(name, go.graphs.get(name), nodes)
    for name, nodes in go.graphs.items():
        if name not in gn.graphs:
            one(name, nodes, None)
    return out


def diff_variables(old, new, ctx):
    from .diff import diff_props

    def vars_of(state):
        out = OrderedDict()
        for v in state.bp_props().get("NewVariables") or []:
            if isinstance(v, dict) and v.get("VarGuid"):
                out[v["VarGuid"]] = v
        return out

    ov, nv = vars_of(old), vars_of(new)
    out = []
    for guid, v in nv.items():
        d = OrderedDict([("name", v.get("VarName")), ("varGuid", guid), ("kind", "added"), ("area", "cdo")])
        o = ov.get(guid)
        if o is not None:
            rows = diff_props(o if isinstance(o, TaggedStruct) else TaggedStruct(), v if isinstance(v, TaggedStruct) else TaggedStruct(), ctx, set())
            if not rows:
                continue
            d["kind"] = "changed"
            if o.get("VarName") != v.get("VarName"):
                d["renamedFrom"] = o.get("VarName")
            d["properties"] = rows
        else:
            d["properties"] = []
        out.append(d)
    for guid, o in ov.items():
        if guid not in nv:
            out.append(OrderedDict([("name", o.get("VarName")), ("varGuid", guid), ("kind", "removed"), ("area", "cdo"), ("properties", [])]))
    return out
