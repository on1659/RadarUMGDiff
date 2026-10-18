"""FText (dev 문서 §11.8). 5.8 Runtime/Core/Private/Internationalization/Text.cpp FText::SerializeText, TextHistory.cpp.

JSON 형은 네이티브(TextToJson)와 같다: {source, namespace, key, table, tableKey}. 비교 키는 source(+table/tableKey).
- StringTableEntry 의 source 는 파일에 없다(테이블을 불러야 안다) → None(모름). 일치 비교에서 None 은 건너뛴다.
- 에디터는 로드할 때 네임스페이스·키를 다시 만들 수 있다(TextHistory.cpp 824~843) → 키만 다른 것은 Python 만 낸다(text-key-only).
- 생성 텍스트(포맷·숫자·날짜·변환 히스토리, M4)는 표시 문자열을 문화권 규칙으로 계산해야 알 수 있다 → source 는 None(모름)으로 두고
  구조(포맷 원문 + 인자)를 "generated" 에 담는다. 두 생성 텍스트는 그 구조로 비교한다(text_compare_key).
"""

from .reader import ParseError
from . import versions as V

HISTORY_NONE = -1
HISTORY_BASE = 0
HISTORY_NAMED_FORMAT = 1
HISTORY_ORDERED_FORMAT = 2
HISTORY_ARGUMENT_FORMAT = 3
HISTORY_AS_NUMBER = 4
HISTORY_AS_PERCENT = 5
HISTORY_AS_CURRENCY = 6
HISTORY_AS_DATE = 7
HISTORY_AS_TIME = 8
HISTORY_AS_DATETIME = 9
HISTORY_TRANSFORM = 10
HISTORY_STRING_TABLE = 11

# EFormatArgumentType (Text.h 143): Int, UInt, Float, Double, Text, Gender
ARG_INT, ARG_UINT, ARG_FLOAT, ARG_DOUBLE, ARG_TEXT, ARG_GENDER = range(6)
DATETIME_STYLE_CUSTOM = 5            # EDateTimeStyle::Custom (Text.h 125~137)


class UnsupportedText(Exception):
    """아직 읽지 않는 히스토리(TextGenerator) — 호출자가 raw 로 남긴다."""


def _arg_value(r, pkg):
    """FFormatArgumentValue (Text.cpp 1498~1540): i8 형 + 값. Gender 는 UInt 로 저장된다."""
    t = r.i8()
    if t == ARG_INT:
        return {"type": "Int", "value": r.i64()}
    if t in (ARG_UINT, ARG_GENDER):
        return {"type": "UInt" if t == ARG_UINT else "Gender", "value": r.u64()}
    if t == ARG_FLOAT:
        return {"type": "Float", "value": r.f32()}
    if t == ARG_DOUBLE:
        return {"type": "Double", "value": r.f64()}
    if t == ARG_TEXT:
        return {"type": "Text", "value": read_text(r, pkg)}
    raise ParseError("FFormatArgumentValue 형 이상: %d" % t)


def _arg_data(r, pkg):
    """FFormatArgumentData (Text.cpp 1766~1840)."""
    name = r.fstring()
    t = r.u8() if V.at_least(pkg, "FEditorObjectVersion", "TextFormatArgumentDataIsVariant") else ARG_TEXT
    if t == ARG_INT:
        v = r.i64() if V.at_least(pkg, "FUE5ReleaseStreamObjectVersion", "TextFormatArgumentData64bitSupport") else r.i32()
    elif t == ARG_FLOAT:
        v = r.f32()
    elif t == ARG_DOUBLE:
        v = r.f64()
    elif t == ARG_TEXT:
        v = read_text(r, pkg)
    elif t == ARG_GENDER:
        v = r.u8()
    else:
        v = None
    return {"name": name, "type": t, "value": v}


def _number_options(r, pkg):
    """FNumberFormattingOptions (Text.cpp 140~162)."""
    o = {}
    if V.at_least(pkg, "FEditorObjectVersion", "AddedAlwaysSignNumberFormattingOption"):
        o["AlwaysSign"] = r.bool32()
    o["UseGrouping"] = r.bool32()
    o["RoundingMode"] = r.i8()
    o["MinimumIntegralDigits"] = r.i32()
    o["MaximumIntegralDigits"] = r.i32()
    o["MinimumFractionalDigits"] = r.i32()
    o["MaximumFractionalDigits"] = r.i32()
    return o


def _format_number(r, pkg):
    """FTextHistory_FormatNumber::Serialize (TextHistory.cpp 1679~): 원본 값, 옵션 유무(+옵션), 문화권 이름."""
    g = {"sourceValue": _arg_value(r, pkg)}
    if r.bool32():
        g["options"] = _number_options(r, pkg)
    g["culture"] = r.fstring()
    return g


def _generated(r, pkg, history):
    if history in (HISTORY_NAMED_FORMAT, HISTORY_ORDERED_FORMAT, HISTORY_ARGUMENT_FORMAT):
        g = {"format": read_text(r, pkg)}
        n = r.i32()
        if n < 0 or n > 100000:
            raise ParseError("포맷 인자 수 이상: %d" % n)
        if history == HISTORY_NAMED_FORMAT:
            g["arguments"] = [{"name": r.fstring(), "value": _arg_value(r, pkg)} for _ in range(n)]
        elif history == HISTORY_ORDERED_FORMAT:
            g["arguments"] = [_arg_value(r, pkg) for _ in range(n)]
        else:
            g["arguments"] = [_arg_data(r, pkg) for _ in range(n)]
        return g
    if history in (HISTORY_AS_NUMBER, HISTORY_AS_PERCENT):
        return _format_number(r, pkg)
    if history == HISTORY_AS_CURRENCY:
        code = r.fstring()                   # VER_UE4_ADDED_CURRENCY_CODE_TO_FTEXT 이후 (5.x 저장본 전부)
        g = _format_number(r, pkg)
        g["currencyCode"] = code
        return g
    if history == HISTORY_AS_DATE:
        return {"ticks": r.i64(), "dateStyle": r.i8(), "timeZone": r.fstring(), "culture": r.fstring()}
    if history == HISTORY_AS_TIME:
        return {"ticks": r.i64(), "timeStyle": r.i8(), "timeZone": r.fstring(), "culture": r.fstring()}
    if history == HISTORY_AS_DATETIME:
        g = {"ticks": r.i64(), "dateStyle": r.i8(), "timeStyle": r.i8()}
        if g["dateStyle"] == DATETIME_STYLE_CUSTOM:
            g["customPattern"] = r.fstring()
        g["timeZone"] = r.fstring()
        g["culture"] = r.fstring()
        return g
    if history == HISTORY_TRANSFORM:
        return {"source": read_text(r, pkg), "transform": r.u8()}
    raise UnsupportedText("FText 히스토리 %d 는 아직 읽지 않습니다" % history)


def read_text(r, pkg):
    flags = r.u32()
    history = r.i8()
    out = {"source": "", "namespace": "", "key": "", "table": None, "tableKey": None, "historyType": history, "flags": flags}
    if history == HISTORY_NONE:
        has_invariant = r.bool32()   # FArchive << bool 은 4바이트
        if has_invariant:
            out["source"] = r.fstring()
        return out
    if history == HISTORY_BASE:
        out["namespace"] = r.fstring()
        out["key"] = r.fstring()
        out["source"] = r.fstring()
        if V.at_least(pkg, "FFortniteMainBranchObjectVersion", "AddDevNotesToFText"):
            out["devNotes"] = r.fstring()
        return out
    if history == HISTORY_STRING_TABLE:
        out["table"] = r.fname()
        out["tableKey"] = r.fstring()
        out["source"] = None
        return out
    out["source"] = None
    out["generated"] = _generated(r, pkg, history)
    return out


def _generated_key(v):
    import json
    return json.dumps(v, sort_keys=True, ensure_ascii=False, default=str)


def text_compare_key(v):
    """비교에 쓰는 값 (source + 테이블 [+ 생성 구조]). None source 는 모름."""
    if not isinstance(v, dict):
        return v
    if "generated" in v:
        return (None, v.get("table"), v.get("tableKey"), v.get("historyType"), _generated_key(v["generated"]))
    return (v.get("source"), v.get("table"), v.get("tableKey"))
