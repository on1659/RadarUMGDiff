"""익스포트 몸체 [prefix][tags … None][suffix] (dev 문서 §11.5, §11.9).

- prefix: FileVersionUE5 >= 1011 이면 UObject 루트에 u8 EClassSerializationControlExtension (Class.cpp 5.8 1587~1600)
- tags  : tags.read_tags
- suffix: PossiblySerializeObjectGuid — i32 bool (+ FGuid 16 바이트) (LazyObjectPtr.cpp), 그다음 클래스별 접미부
- 1010 이상은 ScriptSerializationStart/EndOffset 으로 태그 구간을 검증한다 (SerialOffset 기준 상대).
"""

from .reader import ParseError
from .tags import read_tags, Accounting
from . import summary as S

# UBlueprintCore::Serialize 의 GeneratedClass 는 UEVer < VER_UE4_BLUEPRINT_SKEL_CLASS_TRANSIENT_AGAIN 블록 안에서만 쓴다
# (5.8 Runtime/Engine/Private/Blueprint.cpp 303~335) → 5.x 저장본의 블루프린트 익스포트에는 GUID 뒤 추가 바이트가 없다.

# UStruct 파생(클래스·함수·구조체): UObject 부분 뒤에 UStruct::Serialize 데이터가 온다 — 이 분석기는 읽지 않는다
STRUCT_CLASSES = {"Class", "BlueprintGeneratedClass", "WidgetBlueprintGeneratedClass", "Function", "ScriptStruct",
                  "UserDefinedStruct", "DelegateFunction", "SparseDelegateFunction", "AnimBlueprintGeneratedClass"}


def read_export(pkg, e):
    """익스포트 하나 → dict (state.json exports[] 항목, §6)."""
    start = e["serialOffset"]
    end = start + e["serialSize"]
    r = pkg.reader(start, end)
    acct = Accounting()
    cls = pkg.class_name(e)
    out = {"index": e["index"], "objectName": e["objectName"], "class": pkg.object_path(e["class"]) if e["class"] else "/Script/CoreUObject.Class",
           "className": cls, "super": e["super"], "template": e["template"], "outer": e["outer"],
           "objectFlags": e["objectFlags"], "serialOffset": start, "serialSize": e["serialSize"],
           "scriptStart": e["scriptStart"], "scriptEnd": e["scriptEnd"]}
    try:
        if e["scriptStart"] is not None:
            r.pos = start + e["scriptStart"]
            if e["scriptStart"] > 0:
                acct.add_unparsed("(pre-script:%s)" % cls, e["scriptStart"])
        prefix = None
        if pkg.ue5 >= S.UE5_PROPERTY_TAG_EXTENSION_AND_OVERRIDABLE_SERIALIZATION:
            prefix = r.u8()
            acct.understood += 1
            if prefix not in (0,):
                raise ParseError("모르는 SerializationControlExtension 0x%02X" % prefix)
        out["prefix"] = {"serializationControl": prefix}
        tags = read_tags(r, pkg, acct, owner=cls)
        if e["scriptEnd"] is not None and r.pos != start + e["scriptEnd"]:
            raise ParseError("태그 끝 %d != ScriptSerializationEndOffset %d" % (r.pos - start, e["scriptEnd"]))
        out["derivedScriptEnd"] = r.pos - start
        out["properties"] = tags
        # 접미부
        suffix = {}
        if cls in STRUCT_CLASSES:
            # UStruct::Serialize 는 UObject::Serialize(태그·GUID) 뒤에 자기 데이터를 쓴다 — GUID 이후 전부 해석하지 않음
            pos0 = r.pos
            has_guid = r.bool32()
            suffix["hasObjectGuid"] = has_guid
            if has_guid:
                suffix["objectGuid"] = r.fguid()
            acct.understood += r.pos - pos0
            rest = end - r.pos
            if rest:
                acct.add_unparsed("(struct-data:%s)" % cls, rest)
                r.pos = end
        else:
            pos0 = r.pos
            has_guid = r.bool32()
            suffix["hasObjectGuid"] = has_guid
            suffix["objectGuid"] = r.fguid() if has_guid else None
            acct.understood += r.pos - pos0
            if r.pos < end and _is_graph_node(pkg, e, cls):
                _read_node_suffix(pkg, r, end, suffix, acct, cls)
            rest = end - r.pos
            if rest:
                acct.add_unparsed("(suffix:%s)" % cls, rest)
                suffix["unparsedSuffixBytes"] = rest
                r.pos = end
        out["suffix"] = suffix
    except ParseError as ex:
        out["error"] = str(ex)
        acct.understood = 0
        acct.unparsed = {"(error:%s)" % cls: e["serialSize"]}
    out["bytesUnderstood"] = acct.understood
    out["unparsed"] = acct.unparsed
    return out


def _is_graph_node(pkg, e, cls):
    """그래프 노드 익스포트: Outer 가 EdGraph 익스포트이거나 클래스가 K2Node_/EdGraphNode 계열 (파일에 있는 정보만)."""
    if cls.startswith("K2Node") or cls.startswith("EdGraphNode") or cls.startswith("MaterialGraphNode"):
        return True
    outer = e["outer"]
    if outer > 0 and outer - 1 < len(pkg.exports):
        return pkg.class_name(pkg.exports[outer - 1]) == "EdGraph"
    return False


def _read_node_suffix(pkg, r, end, suffix, acct, cls):
    """UEdGraphNode 핀 배열 (+ UK2Node_EditablePinBase 사용자 핀). 끝이 익스포트 끝과 맞을 때만 받아들인다."""
    from .graphs import read_owned_pins, read_user_pins
    from . import versions as V
    if not V.at_least(pkg, "FBlueprintsObjectVersion", "EdGraphPinOptimized"):
        return
    pos0 = r.pos
    try:
        pins = read_owned_pins(r, pkg)
        user = None
        if r.pos < end:
            user = read_user_pins(r, pkg)
        if r.pos != end:
            raise ParseError("노드 접미부 끝 %d != 익스포트 끝 %d" % (r.pos, end))
    except ParseError as ex:
        r.pos = pos0
        suffix["nodeSuffixError"] = str(ex)
        return
    suffix["pins"] = pins
    if user is not None:
        suffix["userDefinedPins"] = user
    acct.understood += r.pos - pos0


def read_all(pkg):
    return [read_export(pkg, e) for e in pkg.exports]
