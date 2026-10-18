"""한 리비전의 전체 상태 (dev 문서 §6, §11.10).

익스포트 전체(§6 형) + 위젯 트리(부모·인덱스) + 객체 단위 표(objects: 위젯·슬롯·CDO). 객체 id 는 네이티브 state.json 과 같다:
위젯 = 블루프린트 기준 경로(WidgetTree.HealthBar), 슬롯 = <위젯 id>/Slot.
"""

from . import ANALYZER_NAME, SCHEMA_VERSION
from .tables import Package
from .exports import read_all
from .tags import TaggedStruct, tag_key

RF_CLASS_DEFAULT_OBJECT = 0x10


class Obj:
    """객체 하나: 익스포트 + 태그 표."""

    def __init__(self, oid, export, props):
        self.id = oid
        self.export = export
        self.props = props            # TaggedStruct (속성 이름 → 값, .types, .tags)

    @property
    def class_path(self):
        return self.export["class"]


def _props_of(export):
    tags = export.get("properties", [])
    out = TaggedStruct()
    counts = {}
    for t in tags:
        counts[t["name"]] = counts.get(t["name"], 0) + 1
    for t in tags:
        k = tag_key(t, counts)
        out[k] = t["value"] if "value" in t else {"raw": t.get("raw", ""), "structName": t.get("structName")}
        out.types[k] = t["_node"]
        out.tags[k] = t
    return out


class State:
    def __init__(self, data, path="", package_name=None):
        self.pkg = Package(data, path, package_name)
        self.exports = read_all(self.pkg)
        self.by_index = {e["index"]: e for e in self.exports}
        self.path_of = {e["index"]: self.pkg.object_path(e["index"]) for e in self.exports}
        self._find_roots()
        self._build_widgets()

    # ── 찾기 ──
    def _find_roots(self):
        self.bp = next((e for e in self.exports if e["className"] in ("WidgetBlueprint",) and self.pkg.exports[e["index"] - 1]["isAsset"]), None)
        bp_index = self.bp["index"] if self.bp else None
        self.widget_tree = next((e for e in self.exports if e["className"] == "WidgetTree" and e["outer"] == bp_index), None)
        self.cdo = next((e for e in self.exports if e["objectFlags"] & RF_CLASS_DEFAULT_OBJECT and e["outer"] == 0
                         and e["objectName"].startswith("Default__")), None)

    def _build_widgets(self):
        self.widgets = {}          # id → Obj
        self.slots = {}            # 위젯 id → Obj
        self.parent = {}           # 위젯 id → 부모 id
        self.index = {}            # 위젯 id → 부모 안 인덱스
        self.order = []
        self.root = None
        if not self.widget_tree or not self.bp:
            return
        bp_index = self.bp["index"]
        wt_index = self.widget_tree["index"]
        path_to_export = {self.path_of[i]: e for i, e in self.by_index.items()}

        def wid(export):
            return self.pkg.relative_path(export["index"], bp_index)

        wt_props = _props_of(self.widget_tree)
        candidates = {e["index"]: e for e in self.exports if e["outer"] == wt_index}

        # 루트 + 패널 Slots → Content 를 따라 도달 가능한 위젯만 (네이티브 ForEachWidget 과 같은 집합)
        def visit(path):
            e = path_to_export.get(path)
            if e is None or e["index"] not in candidates:
                return
            w_id = wid(e)
            if w_id in self.widgets:
                return
            obj = Obj(w_id, e, _props_of(e))
            self.widgets[w_id] = obj
            self.order.append(w_id)
            for i, slot_path in enumerate(obj.props.get("Slots") or []):
                se = path_to_export.get(slot_path) if isinstance(slot_path, str) else None
                if se is None:
                    continue
                sprops = _props_of(se)
                content = sprops.get("Content")
                ce = path_to_export.get(content) if isinstance(content, str) else None
                if ce is None:
                    continue
                c_id = wid(ce)
                self.slots[c_id] = Obj(c_id + "/Slot", se, sprops)
                self.parent[c_id] = w_id
                self.index[c_id] = i
                visit(content)
            # 중첩 UserWidget 의 이름 있는 슬롯 내용 (FNamedSlotBinding{Name, Content})
            for binding in obj.props.get("NamedSlotBindings") or []:
                if isinstance(binding, dict) and isinstance(binding.get("Content"), str):
                    visit(binding["Content"])

        root = wt_props.get("RootWidget")
        if isinstance(root, str):
            e = path_to_export.get(root)
            if e is not None:
                self.root = wid(e)
            visit(root)
        # 위젯 트리 자신이 부모 위젯의 이름 있는 슬롯을 채우는 내용 (UWidgetTree::NamedSlotBindings, TMap<FName, UWidget*>)
        for pair in wt_props.get("NamedSlotBindings") or []:
            if isinstance(pair, dict) and isinstance(pair.get("value"), str):
                visit(pair["value"])

    def cdo_obj(self):
        return Obj("(CDO)", self.cdo, _props_of(self.cdo)) if self.cdo else None

    def bp_props(self):
        return _props_of(self.bp) if self.bp else TaggedStruct()

    # ── 덤프 ──
    def coverage(self):
        total = sum(e["serialSize"] for e in self.exports)
        und = sum(e["bytesUnderstood"] for e in self.exports)
        unparsed = {}
        for e in self.exports:
            for k, v in e["unparsed"].items():
                unparsed[k] = unparsed.get(k, 0) + v
        # 디자이너 범위(위젯·슬롯·CDO·블루프린트·위젯 트리) 커버리지 — M3 목표 ≥ 0.98 (dev §14)
        designer_idx = {o.export["index"] for o in self.widgets.values()} | {o.export["index"] for o in self.slots.values()}
        for e in (self.cdo, self.bp, self.widget_tree):
            if e:
                designer_idx.add(e["index"])
        d_total = sum(self.by_index[i]["serialSize"] for i in designer_idx)
        d_und = sum(self.by_index[i]["bytesUnderstood"] for i in designer_idx)
        return {"exportBytes": total, "understood": und, "ratio": round(und / total, 6) if total else 1.0,
                "designerBytes": d_total, "designerUnderstood": d_und,
                "designerRatio": round(d_und / d_total, 6) if d_total else 1.0,
                "unparsedStructs": dict(sorted(unparsed.items(), key=lambda kv: -kv[1]))}

    def objects_json(self, schema):
        """위젯·슬롯·CDO 의 저장 속성 전부: 태그에 있으면 present, 없으면 스키마 기본값으로 default (goal Acceptance "dump shows
        default-valued properties labelled default"). 스키마에 없는 클래스(블루프린트 클래스)는 태그만."""
        from .diff import _jsonable
        out = []
        objs = list(self.widgets.values()) + list(self.slots.values())
        cdo = self.cdo_obj()
        parent = self.bp_props().get("ParentClass") if self.bp else None
        for o in objs + ([cdo] if cdo else []):
            cls = parent if o is cdo else o.class_path
            rows = [{"path": k, "state": "present", "value": _jsonable(v)} for k, v in o.props.items()]
            for prop in (schema.properties_of(cls) if schema and cls else []):
                if not prop.get("saved"):
                    continue
                dims = prop.get("arrayDim", 1)
                names = [prop["name"]] if dims == 1 else ["%s[%d]" % (prop["name"], i) for i in range(dims)]
                defaults = schema.defaults_of(cls) or {}
                for i, name in enumerate(names):
                    # 태그 키는 0번 원소 하나만 있으면 이름만, 여럿이면 이름[i] (tags.tag_key)
                    if name in o.props or (i == 0 and prop["name"] in o.props):
                        continue
                    if name in defaults:
                        rows.append({"path": name, "state": "default", "value": defaults[name]})
            out.append({"id": o.id, "class": o.class_path, "archetypeClass": cls, "properties": rows})
        return out

    def to_json(self, schema=None):
        s = self.pkg.summary
        out = {
            "schemaVersion": SCHEMA_VERSION,
            "producer": {"name": ANALYZER_NAME, "kind": "python", "capabilities": ["coverage", "text-key-only"]},
            "file": {"path": self.pkg.path, "packageName": self.pkg.package_name, "size": len(self.pkg.data),
                     "fileVersionUE4": s["fileVersionUE4"], "fileVersionUE5": s["fileVersionUE5"],
                     "licenseeVersion": s["licenseeVersion"], "savedBy": s["savedByEngineVersion"]["text"],
                     "customVersions": s["customVersions"], "totalHeaderSize": s["totalHeaderSize"]},
            "names": self.pkg.names,
            "imports": self.pkg.imports,
            "exports": [_strip(e) for e in self.exports],
            "widgetTree": {"root": self.root, "children": self._children()},
            "coverage": self.coverage(),
        }
        if schema is not None:
            out["producer"]["capabilities"].append("default-state")
            out["objects"] = self.objects_json(schema)
        return out

    def _children(self):
        out = {}
        for c, p in self.parent.items():
            out.setdefault(p, []).append((self.index[c], c))
        return {p: [c for _, c in sorted(v)] for p, v in out.items()}


def _strip(e):
    """JSON 으로 쓸 수 있게 내부 필드(_node)를 뺀다."""
    out = dict(e)
    if "properties" in out:
        out["properties"] = [{k: v for k, v in t.items() if k != "_node"} for t in out["properties"]]
    return out
