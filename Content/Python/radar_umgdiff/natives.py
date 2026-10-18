"""네이티브(바이너리) 구조체 리더 (dev 문서 §11.7). 레이아웃은 NoExportTypes.h 와 각 구조체 Serialize.

리더는 (Reader, Package) → JSON 값. 표에 없는 구조체는 tags.py 가 raw + unparsed 로 남긴다.
JSON 키 이름·순서는 네이티브 생산자(RadarPropertyCompare ToJson)가 리플렉션 멤버 순서로 내는 것과 같게 한다.
"""

import datetime


def _xy(r):
    return {"X": r.f64(), "Y": r.f64()}


def _xyz(r):
    return {"X": r.f64(), "Y": r.f64(), "Z": r.f64()}


def _xyzw(r):
    return {"X": r.f64(), "Y": r.f64(), "Z": r.f64(), "W": r.f64()}


def _linear_color(r, pkg):
    return {"R": r.f32(), "G": r.f32(), "B": r.f32(), "A": r.f32()}


def _color(r, pkg):
    # FColor 메모리·저장 순서 B, G, R, A (NoExportTypes.h)
    b, g, rr, a = r.u8(), r.u8(), r.u8(), r.u8()
    return {"B": b, "G": g, "R": rr, "A": a}


def _guid(r, pkg):
    return r.fguid()


def _int_point(r, pkg):
    return {"X": r.i32(), "Y": r.i32()}


def _int_vector(r, pkg):
    return {"X": r.i32(), "Y": r.i32(), "Z": r.i32()}


def _rotator(r, pkg):
    return {"Pitch": r.f64(), "Yaw": r.f64(), "Roll": r.f64()}


def _box2d(r, pkg):
    mn = _xy(r)
    mx = _xy(r)
    return {"Min": mn, "Max": mx, "bIsValid": r.u8() != 0}


def _datetime(r, pkg):
    return {"Ticks": r.i64()}


def _frame_number(r, pkg):
    return {"Value": r.i32()}


def _frame_rate(r, pkg):
    return {"Numerator": r.i32(), "Denominator": r.i32()}


def _soft_object_path(r, pkg):
    from .softpath import read_soft_object_path_value
    return pkg.normalize_path(read_soft_object_path_value(r, pkg))


def _text(r, pkg):
    from .text import read_text
    return read_text(r, pkg)


# ── 무비신 채널 (dev 문서 §11.7 M4 행) ──
# TMovieSceneCurveChannelImpl::Serialize (5.8 Runtime/MovieScene/Private/Channels/MovieSceneCurveChannelImpl.cpp 1121~1223):
#   u8 PreInfinityExtrap, u8 PostInfinityExtrap,
#   i32 sizeof(FFrameNumber)=4, i32 N, N×i32 Times,
#   i32 sizeof(값), i32 M, M×값 (메모리 그대로 — FMovieSceneFloatValue 28바이트, FMovieSceneDoubleValue 32바이트),
#   DefaultValue (float/double), bool bHasDefaultValue(4), FFrameRate TickResolution (i32 둘),
#   FFortniteMainBranchObjectVersion >= SerializeFloatChannelShowCurve 면 bool bShowCurve(4)
# 값 메모리 배치 (MovieSceneFloatChannel.h 60~95, MovieSceneDoubleChannel.h 73~100, MovieSceneCurveChannelCommon.h 45~75):
#   Value, Tangent{ArriveTangent f32, LeaveTangent f32, ArriveTangentWeight f32, LeaveTangentWeight f32, u8 TangentWeightMode, 패딩 3},
#   u8 InterpMode, u8 TangentMode, u8 PaddingByte, 패딩 1  (static_assert 로 패딩이 고정돼 있다)
# 열거형 이름 (Engine/Classes/Curves/RealCurve.h, RichCurve.h) — 네이티브 ToJson 은 TEnumAsByte 를 이름으로 낸다
RICH_EXTRAP = ["RCCE_Cycle", "RCCE_CycleWithOffset", "RCCE_Oscillate", "RCCE_Linear", "RCCE_Constant", "RCCE_None"]
RICH_INTERP = ["RCIM_Linear", "RCIM_Constant", "RCIM_Cubic", "RCIM_None"]
RICH_TANGENT = ["RCTM_Auto", "RCTM_User", "RCTM_Break", "RCTM_None", "RCTM_SmartAuto"]
RICH_WEIGHT = ["RCTWM_WeightedNone", "RCTWM_WeightedArrive", "RCTWM_WeightedLeave", "RCTWM_WeightedBoth"]


def _enum(names, v):
    return names[v] if 0 <= v < len(names) else v


def _tangent(r):
    t = {"ArriveTangent": r.f32(), "LeaveTangent": r.f32(), "ArriveTangentWeight": r.f32(), "LeaveTangentWeight": r.f32(),
         "TangentWeightMode": _enum(RICH_WEIGHT, r.u8())}
    r.bytes(3)
    return t


def _curve_value_memory(r, double):
    v = r.f64() if double else r.f32()
    out = {"Value": v, "Tangent": _tangent(r), "InterpMode": _enum(RICH_INTERP, r.u8()), "TangentMode": _enum(RICH_TANGENT, r.u8()),
           "PaddingByte": r.u8()}
    r.bytes(1)
    return out


def _curve_channel(double):
    elem = 32 if double else 28

    def read(r, pkg):
        from .reader import ParseError
        from . import versions as V
        if not (V.at_least(pkg, "FSequencerObjectVersion", "SerializeFloatChannelCompletely")
                or V.at_least(pkg, "FFortniteMainBranchObjectVersion", "SerializeFloatChannelShowCurve")):
            raise ParseError("이 버전의 채널은 태그로 저장된다")
        out = {"PreInfinityExtrap": _enum(RICH_EXTRAP, r.u8()), "PostInfinityExtrap": _enum(RICH_EXTRAP, r.u8())}
        if r.i32() != 4:
            raise ParseError("FFrameNumber 크기가 4 가 아님")
        times = [r.i32() for _ in range(r.i32())]
        size = r.i32()
        if size != elem:
            raise ParseError("채널 값 크기 %d (기대 %d)" % (size, elem))
        values = [_curve_value_memory(r, double) for _ in range(r.i32())]
        out["Times"] = times
        out["Values"] = values
        out["DefaultValue"] = r.f64() if double else r.f32()
        out["bHasDefaultValue"] = r.bool32()
        out["TickResolution"] = {"Numerator": r.i32(), "Denominator": r.i32()}
        if V.at_least(pkg, "FFortniteMainBranchObjectVersion", "SerializeFloatChannelShowCurve"):
            out["bShowCurve"] = r.bool32()
        return out
    return read


def _curve_value_serialized(double):
    """FMovieSceneFloatValue / DoubleValue 단독 (SerializeChannelValue, MovieSceneCurveChannelImpl.cpp 1274~1318, Completely 형식)."""
    def read(r, pkg):
        from .reader import ParseError
        from . import versions as V
        if not V.at_least(pkg, "FSequencerObjectVersion", "SerializeFloatChannelCompletely"):
            raise ParseError("옛 채널 값 형식")
        v = r.f64() if double else r.f32()
        t = {"ArriveTangent": r.f32(), "LeaveTangent": r.f32(), "ArriveTangentWeight": r.f32(), "LeaveTangentWeight": r.f32(),
             "TangentWeightMode": _enum(RICH_WEIGHT, r.u8())}
        return {"Value": v, "Tangent": t, "InterpMode": _enum(RICH_INTERP, r.u8()), "TangentMode": _enum(RICH_TANGENT, r.u8()),
                "PaddingByte": r.u8()}
    return read


def _frame_range(r, pkg):
    """FMovieSceneFrameRange::Serialize → TRange<FFrameNumber>: 경계마다 u8 종류(0 제외, 1 포함, 2 열림) + i32 값 (Math/RangeBound.h 174)."""
    lo = {"Type": r.u8(), "Value": r.i32()}
    hi = {"Type": r.u8(), "Value": r.i32()}
    return {"LowerBound": lo, "UpperBound": hi}


def _pin_type(r, pkg):
    from .graphs import read_pin_type
    return read_pin_type(r, pkg)


# StructName → (리더, 확정 여부 설명)
READERS = {
    "Vector2D": lambda r, p: _xy(r),
    # FDeprecateSlateVector2D::Serialize 는 FVector2f 로 쓴다 (SlateCore/Private/Types/SlateVector2.cpp:34)
    "DeprecateSlateVector2D": lambda r, p: {"X": r.f32(), "Y": r.f32()},
    "Vector2f": lambda r, p: {"X": r.f32(), "Y": r.f32()},
    "Vector3f": lambda r, p: {"X": r.f32(), "Y": r.f32(), "Z": r.f32()},
    "Vector4f": lambda r, p: {"X": r.f32(), "Y": r.f32(), "Z": r.f32(), "W": r.f32()},
    "Vector": lambda r, p: _xyz(r),
    "Vector4": lambda r, p: _xyzw(r),
    "Quat": lambda r, p: _xyzw(r),
    "Plane": lambda r, p: _xyzw(r),
    "Rotator": _rotator,
    "IntPoint": _int_point,
    "IntVector": _int_vector,
    "LinearColor": _linear_color,
    "Color": _color,
    "Guid": _guid,
    "Box2D": _box2d,
    "DateTime": _datetime,
    "Timespan": _datetime,
    "FrameNumber": _frame_number,
    "FrameRate": _frame_rate,
    "SoftObjectPath": _soft_object_path,
    "SoftClassPath": _soft_object_path,
    "EdGraphPinType": _pin_type,
    "MovieSceneFloatChannel": _curve_channel(False),
    "MovieSceneDoubleChannel": _curve_channel(True),
    "MovieSceneFloatValue": _curve_value_serialized(False),
    "MovieSceneDoubleValue": _curve_value_serialized(True),
    "MovieSceneFrameRange": _frame_range,
}


def reader_for(struct_name):
    return READERS.get(struct_name)
