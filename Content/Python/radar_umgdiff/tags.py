"""속성 태그 리더 (dev 문서 §11.6).

A. 완전 타입 이름 형식 (FileVersionUE5 >= 1012) — 5.8 Runtime/CoreUObject/Private/UObject/PropertyTag.cpp 410~530:
   FName Name (None 이면 끝) · FPropertyTypeName (FName + i32 InnerCount 노드 목록) · i32 Size · u8 Flags
   [i32 ArrayIndex if 0x01] [FGuid if 0x02] [확장 if 0x04] · 값 Size 바이트
B. 옛 형식 (< 1012) — M5 (5.1). 지금은 거부한다.

모든 값은 네이티브 생산자의 JSON 형(RadarPropertyCompare ToJson)과 같게 만든다.
"""

from .reader import Reader, ParseError, Refuse
from . import natives
from .text import read_text, UnsupportedText

FLAG_HAS_ARRAY_INDEX = 0x01
FLAG_HAS_PROPERTY_GUID = 0x02
FLAG_HAS_EXTENSIONS = 0x04
FLAG_BINARY_OR_NATIVE = 0x08
FLAG_BOOL_TRUE = 0x10
FLAG_SKIPPED = 0x20

# EPropertyTagExtension (PropertyTag.cpp 44~52)
EXT_OVERRIDABLE_INFO = 0x02
EXT_HAS_EXTERNAL_OBJECTS = 0x04

INT_TYPES = {
    "Int8Property": "i8", "Int16Property": "i16", "IntProperty": "i32", "Int64Property": "i64",
    "UInt16Property": "u16", "UInt32Property": "u32", "UInt64Property": "u64",
    "FloatProperty": "f32", "DoubleProperty": "f64",
}
OBJECT_TYPES = {"ObjectProperty", "ClassProperty", "WeakObjectProperty", "InterfaceProperty"}
SOFT_TYPES = {"SoftObjectProperty", "SoftClassProperty"}


class TypeNode:
    __slots__ = ("name", "params")

    def __init__(self, name, params):
        self.name = name
        self.params = params

    def param(self, i):
        return self.params[i] if i < len(self.params) else None

    def type_string(self):
        """네이티브 TypeString 과 같은 문자열."""
        n = self.name
        p0 = self.param(0)
        if n == "StructProperty" and p0 is not None:
            return n + ":" + p0.name
        if n in ("EnumProperty", "ByteProperty") and p0 is not None:
            return n + ":" + p0.name
        if n in ("ArrayProperty", "SetProperty", "OptionalProperty") and p0 is not None:
            return n + ":" + p0.type_string()
        if n == "MapProperty" and len(self.params) >= 2:
            return n + ":" + self.params[0].type_string() + "," + self.params[1].type_string()
        return n

    def __repr__(self):
        return self.name + ("(" + ",".join(repr(p) for p in self.params) + ")" if self.params else "")


def read_type_name(r):
    """FPropertyTypeName: 노드마다 FName + i32 InnerCount, Remaining=1 에서 시작해 노드마다 += InnerCount-1."""
    flat = []
    remaining = 1
    while remaining > 0:
        name = r.fname()
        inner = r.i32()
        if inner < 0 or inner > 64:
            raise ParseError("타입 이름 InnerCount 이상: %d" % inner)
        flat.append((name, inner))
        remaining += inner - 1
    pos = [0]

    def build():
        name, inner = flat[pos[0]]
        pos[0] += 1
        return TypeNode(name, [build() for _ in range(inner)])

    return build()


class Accounting:
    """커버리지 장부: 이해한 바이트와 해석 못 한 구조체별 바이트."""

    def __init__(self):
        self.understood = 0
        self.unparsed = {}

    def add_unparsed(self, what, size):
        self.unparsed[what] = self.unparsed.get(what, 0) + size


def read_tags(r, pkg, acct, owner=""):
    """태그 스트림을 None 까지 읽는다. [태그 dict]."""
    tags = []
    while True:
        start = r.pos
        name = r.fname()
        if name == "None":
            acct.understood += r.pos - start
            return tags
        if pkg.ue5 < 1012:
            raise Refuse("FileVersionUE5 < 1012 (옛 태그 형식)는 M5 에서 지원합니다")
        tnode = read_type_name(r)
        size = r.i32()
        flags = r.u8()
        array_index = r.i32() if flags & FLAG_HAS_ARRAY_INDEX else 0
        prop_guid = r.fguid() if flags & FLAG_HAS_PROPERTY_GUID else None
        ext = None
        if flags & FLAG_HAS_EXTENSIONS:
            ext = {"flags": r.u8()}
            if ext["flags"] & EXT_OVERRIDABLE_INFO:
                ext["overrideOperation"] = r.u8()
                ext["experimentalOverridableLogic"] = r.u8() != 0
            if ext["flags"] & EXT_HAS_EXTERNAL_OBJECTS:
                ext["externalObjects"] = r.u8() != 0
        acct.understood += r.pos - start
        if size < 0 or r.pos + size > r.end:
            raise ParseError("태그 %s 크기 이상: %d @%d" % (name, size, r.pos))
        tag = {"name": name, "type": tnode.type_string(), "size": size, "flags": flags, "arrayIndex": array_index}
        if prop_guid:
            tag["propertyGuid"] = prop_guid
        if ext:
            tag["extensions"] = ext
        tag["_node"] = tnode
        vr = r.sub(size)
        if flags & FLAG_SKIPPED:
            tag["parsed"] = False
            tag["skipped"] = True
            acct.add_unparsed("(skipped)", size)
        elif ext and ext["flags"] & EXT_OVERRIDABLE_INFO and tnode.name in ("ArrayProperty", "SetProperty", "MapProperty"):
            # 오버라이더블 직렬화의 컨테이너는 Replaced/Added/Removed 레이아웃이 다르다 (PropertyArray.cpp 239~) — 아직 읽지 않는다
            _raw(tag, vr, acct, "(overridable-container)")
        else:
            try:
                sub_acct = Accounting()
                tag["value"] = read_value(vr, tnode, flags, pkg, sub_acct, size)
                if vr.pos != vr.end:
                    raise ParseError("값 끝이 맞지 않음: %s %d/%d" % (name, vr.pos - (vr.end - size), size))
                tag["parsed"] = not sub_acct.unparsed
                acct.understood += sub_acct.understood
                for k, v in sub_acct.unparsed.items():
                    acct.add_unparsed(k, v)
                if sub_acct.unparsed:
                    tag["unparsedStructs"] = sorted(sub_acct.unparsed)
            except (Unparsed, UnsupportedText) as ex:
                vr.pos = vr.end - size
                _raw(tag, vr, acct, getattr(ex, "what", None) or tnode.type_string())
        tags.append(tag)


class Unparsed(Exception):
    def __init__(self, what):
        Exception.__init__(self, what)
        self.what = what


def _raw(tag, vr, acct, what):
    start = vr.end - tag["size"]
    tag["parsed"] = False
    tag["raw"] = vr.data[start:vr.end].hex()
    tag["structName"] = what
    acct.add_unparsed(what, tag["size"])
    vr.pos = vr.end


def read_value(r, t, flags, pkg, acct, size):
    """값 하나. 이해한 바이트는 acct.understood 에 더한다(해석 못 한 부분은 Unparsed 예외 또는 acct.unparsed)."""
    n = t.name
    start = r.pos
    v = _read_value(r, t, flags, pkg, acct, size)
    return v


def _count(acct, r, start):
    acct.understood += r.pos - start


def _read_value(r, t, flags, pkg, acct, size):
    n = t.name
    start = r.pos
    if n == "BoolProperty":
        return bool(flags & FLAG_BOOL_TRUE)
    if n in INT_TYPES:
        v = getattr(r, INT_TYPES[n])()
        _count(acct, r, start)
        return v
    if n == "ByteProperty":
        if size == 8:
            v = r.fname()
        else:
            v = r.u8()
        _count(acct, r, start)
        return v
    if n == "EnumProperty":
        if size == 8:
            v = r.fname()
        else:
            v = {1: r.u8, 2: r.u16, 4: r.i32}.get(size, lambda: None)()
            if v is None:
                raise Unparsed("EnumProperty(size %d)" % size)
        _count(acct, r, start)
        return v
    if n == "NameProperty":
        v = r.fname()
        _count(acct, r, start)
        return v
    if n == "StrProperty":
        v = r.fstring()
        _count(acct, r, start)
        return v
    if n == "TextProperty":
        v = read_text(r, pkg)
        _count(acct, r, start)
        return v
    if n in OBJECT_TYPES:
        idx = r.i32()
        _count(acct, r, start)
        return pkg.object_path(idx)
    if n == "LazyObjectProperty":
        v = r.fguid()
        _count(acct, r, start)
        return v
    if n in SOFT_TYPES:
        from .softpath import read_soft_object_path_value
        v = read_soft_object_path_value(r, pkg)
        _count(acct, r, start)
        return pkg.normalize_path(v) or None
    if n == "StructProperty":
        sname = t.param(0).name if t.param(0) is not None else "?"
        return read_struct(r, sname, bool(flags & FLAG_BINARY_OR_NATIVE), pkg, acct, size)
    if n in ("ArrayProperty", "SetProperty"):
        inner = t.param(0)
        out = []
        if n == "SetProperty":
            removed = r.i32()
            _count(acct, r, start)
            for _ in range(removed):
                _read_element(r, inner, flags, pkg, acct)
            start = r.pos
        count = r.i32()
        _count(acct, r, start)
        if count < 0:
            raise ParseError("배열 길이 음수")
        elem_size = (size - 4) // count if count and n == "ArrayProperty" else None
        for _ in range(count):
            out.append(_read_element(r, inner, flags, pkg, acct, elem_size))
        return out
    if n == "MapProperty":
        kt, vt = t.param(0), t.param(1)
        removed = r.i32()
        _count(acct, r, start)
        if removed:
            # 키 형만 알면 읽을 수 있으나 저장본에서 관측 전까지 보류
            raise Unparsed("MapProperty(removed keys)")
        start = r.pos
        count = r.i32()
        _count(acct, r, start)
        out = []
        for _ in range(count):
            k = _read_element(r, kt, flags, pkg, acct, None, map_part=True)
            v = _read_element(r, vt, flags, pkg, acct, None, map_part=True)
            out.append({"key": k, "value": v})
        return out
    if n == "FieldPathProperty":
        # FFieldPath 저장 (5.8 CoreUObject/Private/UObject/FieldPath.cpp 325~357): TArray<FName> Path(안쪽 필드가 0번) + 소유 UStruct.
        # JSON 은 네이티브 ToText(ExportText = FFieldPath::ToString) 와 같다: "<소유자 경로>:<바깥>.….<필드>", 빈 경로는 "".
        count = r.i32()
        if count < 0 or count > 64:
            raise ParseError("FieldPath 길이 이상: %d" % count)
        names = [r.fname() for _ in range(count)]
        owner = pkg.object_path(r.i32())
        _count(acct, r, start)
        if not names:
            return ""
        inner = ".".join(reversed(names))
        return pkg.normalize_path(owner + ":" + inner) if owner else inner
    if n == "DelegateProperty":
        obj = r.i32()
        fn = r.fname()
        _count(acct, r, start)
        return {"object": pkg.object_path(obj), "function": fn}
    if n in ("MulticastInlineDelegateProperty", "MulticastSparseDelegateProperty", "MulticastDelegateProperty"):
        count = r.i32()
        out = []
        for _ in range(count):
            obj = r.i32()
            fn = r.fname()
            out.append({"object": pkg.object_path(obj), "function": fn})
        _count(acct, r, start)
        return out
    raise Unparsed(t.type_string())


def _read_element(r, t, tag_flags, pkg, acct, elem_size=None, map_part=False):
    """배열·집합·맵의 원소 하나 (내부 태그 없음 — A 형식)."""
    n = t.name
    start = r.pos
    if n == "BoolProperty":
        v = r.u8() != 0
        _count(acct, r, start)
        return v
    if n == "StructProperty":
        sname = t.param(0).name if t.param(0) is not None else "?"
        native = bool(tag_flags & FLAG_BINARY_OR_NATIVE)
        if map_part and native:
            # 맵은 키·값 중 하나만 네이티브여도 플래그가 선다 (PropertyMap.cpp 1834) → 어느 쪽인지 파일만으로는 모른다
            reader = natives.reader_for(sname)
            if reader is None:
                raise Unparsed("MapProperty(native %s)" % sname)
        return read_struct(r, sname, native, pkg, acct, None)
    if n in ("ByteProperty", "EnumProperty"):
        if elem_size == 1:
            v = r.u8()
        else:
            v = r.fname()
        _count(acct, r, start)
        return v
    return _read_value(r, t, tag_flags & ~FLAG_BOOL_TRUE, pkg, acct, elem_size or 0)


def read_struct(r, sname, native, pkg, acct, size):
    start = r.pos
    if native:
        reader = natives.reader_for(sname)
        if reader is None:
            raise Unparsed(sname)
        v = reader(r, pkg)
        _count(acct, r, start)
        return v
    # 태그 스트림 구조체 몸체: [tags … None], 접두·접미 없음
    sub = read_tags(r, pkg, acct, owner=sname)
    return tags_to_dict(sub)


class TaggedStruct(dict):
    """태그 스트림 구조체 값. dict 는 {멤버: 값}(있는 멤버만), .types 는 {멤버: TypeNode}, .tags 는 {멤버: 태그}."""

    def __init__(self):
        dict.__init__(self)
        self.types = {}
        self.tags = {}


def tag_key(t, counts):
    if t["arrayIndex"] or counts.get(t["name"], 0) > 1:
        return "%s[%d]" % (t["name"], t["arrayIndex"])
    return t["name"]


def tags_to_dict(tags):
    """태그 목록 → TaggedStruct {이름 또는 이름[i]: 값}. 해석 못 한 태그는 {"raw": …}."""
    out = TaggedStruct()
    counts = {}
    for t in tags:
        counts[t["name"]] = counts.get(t["name"], 0) + 1
    for t in tags:
        key = tag_key(t, counts)
        out[key] = t["value"] if "value" in t else {"raw": t.get("raw", ""), "structName": t.get("structName")}
        out.types[key] = t["_node"]
        out.tags[key] = t
    return out
