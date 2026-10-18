"""스키마 층 (goal 문서 "Schema dump" — 마지막 분석기 단계, "현재 상태 완성" 층).

schema.json 은 에디터의 RadarUMGDiff.DumpSchema(= editor/dump_schema.py)가 쓴다: 네이티브 클래스·구조체마다 속성 목록과 기본값.
값의 JSON 형은 diff.json 과 같다(RadarProperty::ToJson) — Python 이 파일에서 읽은 값과 그대로 비교할 수 있다.

쓰임 두 가지:
1. diff: maybeDefault 줄(한쪽에만 태그가 있음)의 없는 쪽을 그 객체 아키타입의 기본값으로 채운다.
   두 값이 같으면 줄을 지우고(스키마가 같음을 증명), 다르면 상태를 default 로 적고 maybeDefault 를 끈다.
   양쪽에 태그가 있는 줄(판정이 이미 난 줄)은 건드리지 않는다 → "스키마 유무와 무관한 판정" (test_schema_invariance).
2. dump: 태그에 없는 저장 속성을 기본값으로 채워 state: default 로 보인다.
아키타입이 블루프린트 클래스(중첩 UserWidget, BP 부모)면 스키마에 없다 — 그 줄은 그대로 보류로 남는다.
"""

import io
import json
import re

from .conform import value_eq

_INDEX = re.compile(r"^(.*)\[(\d+)\]$")


class Schema:
    def __init__(self, data):
        self.classes = data.get("classes", {})
        self.structs = data.get("structs", {})
        self.engine = data.get("engine")

    @classmethod
    def load(cls, path):
        with io.open(path, encoding="utf-8") as fp:
            return cls(json.load(fp))

    def defaults_of(self, type_path):
        t = self.classes.get(type_path) or self.structs.get(type_path)
        return t.get("defaults") if t else None

    def properties_of(self, type_path):
        t = self.classes.get(type_path) or self.structs.get(type_path)
        return t.get("properties", []) if t else []


_MISSING = object()          # 스키마에 그 경로가 없다 (블루프린트 클래스·지금 C++ 에 없는 속성)
_NO_ELEMENT = object()       # 기본 배열에 그 원소가 없다 = 그쪽은 원소 없음


def walk(defaults, path):
    """속성 경로("Brush.TintColor.SpecifiedColor", "Slots[0]", "Offsets[1]")를 기본값 JSON 안에서 따라간다."""
    cur = defaults
    for tok in path.split("."):
        if not isinstance(cur, dict):
            return _MISSING
        if tok in cur:                        # 정적 배열 "이름[i]" 키
            cur = cur[tok]
            continue
        m = _INDEX.match(tok)
        if not m or m.group(1) not in cur:
            return _MISSING
        arr = cur[m.group(1)]
        i = int(m.group(2))
        if not isinstance(arr, list):
            return _MISSING
        if i >= len(arr):
            return _NO_ELEMENT
        cur = arr[i]
    return cur


def _text(v):
    if v is None:
        return None
    return v if isinstance(v, str) else json.dumps(v, ensure_ascii=False, separators=(",", ":"))


def resolve_rows(rows, defaults):
    """maybeDefault 줄을 스키마 기본값으로 확정한다. 돌려주는 값 = 남은 줄."""
    if defaults is None:
        return rows
    out = []
    for r in rows:
        if not r.get("maybeDefault"):
            out.append(r)
            continue
        side = "old" if (r.get("old") is None or r["old"].get("state") == "absent") else "new"
        other = "new" if side == "old" else "old"
        dv = walk(defaults, r["path"])
        if dv is _MISSING:
            out.append(r)
            continue
        present = r.get(other)
        pv = present.get("value") if present else None
        if dv is _NO_ELEMENT and present is None:
            continue
        if present is not None and dv is not _NO_ELEMENT and _same(dv, pv):
            continue                         # 스키마가 같음을 증명 — 줄을 지운다 (저장된 쪽에 없는 멤버는 기본값과 같다)
        if dv is _NO_ELEMENT:
            # 기본 배열에 없는 원소: 없는 쪽은 "원소 없음" → added/removed 로 확정
            r["kind"] = "added" if side == "old" else "removed"
            r[side] = None
        else:
            r[side] = {"state": "default", "text": _text(dv), "value": dv}
            if r["kind"] in ("added", "removed"):
                r["kind"] = "changed"
        r["maybeDefault"] = False
        out.append(r)
    return out


def _same(default, present):
    """기본값과 저장된 값이 같은지. null 기본값(빈 오브젝트 참조)은 null 하고만 같다."""
    if default is None or present is None:
        return default is None and present is None
    return value_eq(default, present)


def apply(d, schema, old, new):
    """diff.json(d) 의 maybeDefault 줄을 스키마로 확정한다. 줄이 다 사라진 항목은 그 항목도 지운다."""
    parent = (d.get("asset") or {}).get("parentClass")
    widgets = []
    for w in d.get("widgets", []):
        if w["kind"] in ("changed", "moved", "renamed-probable"):
            w["properties"] = resolve_rows(w["properties"], schema.defaults_of(w["class"]))
            slot = w.get("slot") or {}
            scls = (slot.get("class") or {}).get("new") or (slot.get("class") or {}).get("old")
            if slot.get("class", {}).get("old") == slot.get("class", {}).get("new"):
                slot["properties"] = resolve_rows(slot.get("properties", []), schema.defaults_of(scls))
            moved = w["kind"] == "moved" or w.get("moved")
            slot_changed = bool(slot.get("properties")) or (slot.get("class", {}).get("old") != slot.get("class", {}).get("new"))
            if w["kind"] == "changed" and not w["properties"] and not slot_changed and not moved:
                continue
            if w["kind"] == "changed" and not w["properties"] and (slot_changed or moved):
                w["kind"], w["area"] = ("moved" if moved else "changed"), "slot"
        widgets.append(w)
    d["widgets"] = widgets
    if parent:
        d["cdo"] = resolve_rows(d.get("cdo", []), schema.defaults_of(parent))
    for v in d.get("variables", []):
        v["properties"] = resolve_rows(v.get("properties", []), schema.defaults_of("/Script/Engine.BPVariableDescription"))
    d["variables"] = [v for v in d.get("variables", []) if v["kind"] != "changed" or v["properties"]]
    for g in d.get("graphs", []):
        nodes = []
        for n in g.get("nodes", []):
            if n["kind"] == "changed":
                n["properties"] = resolve_rows(n.get("properties", []), schema.defaults_of(n["class"]))
                if not n["properties"] and not n.get("pins"):
                    g["layout" if n["area"] == "graph-layout" else "logic"]["moved" if n["area"] == "graph-layout" else "changed"] -= 1
                    continue
            nodes.append(n)
        g["nodes"] = nodes
    d["graphs"] = [g for g in d.get("graphs", []) if g["kind"] != "changed" or g["nodes"]]
    caps = d["producer"]["capabilities"]
    if "default-state" not in caps:
        caps.append("default-state")
    return d
