"""M4 리더(무비신 채널·핀 타입·생성 텍스트·프레임 범위)를 손으로 만든 바이트로 검사한다 (dev 문서 §14 "Python 단위").

레이아웃 근거는 각 리더의 주석 (natives.py, graphs.py, text.py).
"""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from radar_umgdiff.reader import Reader                       # noqa: E402
from radar_umgdiff import natives, graphs, versions           # noqa: E402
from radar_umgdiff.text import read_text, text_compare_key    # noqa: E402


def i32(v):
    return struct.pack("<i", v)


def u8(v):
    return struct.pack("<B", v)


def f32(v):
    return struct.pack("<f", v)


def fname(i, n=0):
    return i32(i) + i32(n)


def fstring(s):
    if not s:
        return i32(0)
    b = s.encode("latin-1") + b"\x00"
    return i32(len(b)) + b


def gate(struct_name, member):
    g = versions._gates()[struct_name]
    return g["guid"], g["members"][member]["value"]


class FakePkg:
    """M4 리더가 쓰는 Package 표면: 이름표 + 커스텀 버전(5.8 저장본처럼 모든 게이트 이상)."""

    def __init__(self, names):
        self.names = names
        cv = {}
        for s, e in versions._gates().items():
            cv[e["guid"]] = max(m["value"] for m in e["members"].values() if "value" in m) + 1
        self.summary = {"customVersions": cv}

    def object_path(self, idx):
        return None if idx == 0 else "obj%d" % idx


class TestChannel(unittest.TestCase):
    def test_float_channel(self):
        def value(v, interp, tangent_mode):
            # Value, Tangent{4×f32, u8, 패딩3}, Interp, TangentMode, PaddingByte, 패딩1 = 28바이트
            return f32(v) + f32(0.5) + f32(-0.5) + f32(0) + f32(0) + u8(0) + b"\0\0\0" + u8(interp) + u8(tangent_mode) + u8(0) + b"\0"
        data = (u8(4) + u8(4) + i32(4) + i32(2) + i32(0) + i32(60000) + i32(28) + i32(2)
                + value(1.0, 2, 0) + value(2.5, 1, 1) + f32(0.0) + i32(0) + i32(60000) + i32(1) + i32(0))
        r = Reader(data)
        ch = natives.reader_for("MovieSceneFloatChannel")(r, FakePkg([]))
        self.assertEqual(r.pos, len(data))
        self.assertEqual(ch["Times"], [0, 60000])
        self.assertEqual(ch["PreInfinityExtrap"], "RCCE_Constant")
        self.assertEqual(ch["Values"][1]["Value"], 2.5)
        self.assertEqual(ch["Values"][0]["InterpMode"], "RCIM_Cubic")
        self.assertEqual(ch["Values"][1]["InterpMode"], "RCIM_Constant")
        self.assertEqual(ch["Values"][1]["TangentMode"], "RCTM_User")
        self.assertEqual(ch["Values"][0]["Tangent"]["ArriveTangent"], 0.5)
        self.assertEqual(ch["TickResolution"], {"Numerator": 60000, "Denominator": 1})
        self.assertFalse(ch["bHasDefaultValue"])

    def test_frame_range(self):
        data = u8(1) + i32(0) + u8(0) + i32(90001)
        rr = natives.reader_for("MovieSceneFrameRange")(Reader(data), FakePkg([]))
        self.assertEqual(rr["LowerBound"], {"Type": 1, "Value": 0})
        self.assertEqual(rr["UpperBound"], {"Type": 0, "Value": 90001})


class TestPinType(unittest.TestCase):
    def test_array_struct_pin(self):
        names = ["None", "struct"]
        data = (fname(1) + fname(0) + i32(-3)               # PinCategory, PinSubCategory, PinSubCategoryObject
                + u8(1)                                     # ContainerType Array
                + i32(0) + i32(0)                           # bIsReference, bIsWeakPointer
                + i32(0) + fname(0) + b"\0" * 16            # MemberReference
                + i32(1) + i32(0) + i32(0))                 # bIsConst, bIsUObjectWrapper, bSerializeAsSinglePrecisionFloat
        r = Reader(data, names=names)
        t = graphs.read_pin_type(r, FakePkg(names))
        self.assertEqual(r.pos, len(data))
        self.assertEqual(t["PinCategory"], "struct")
        self.assertEqual(t["PinSubCategoryObject"], "obj-3")
        self.assertEqual(t["ContainerType"], "EPinContainerType::Array")
        self.assertTrue(t["bIsConst"])
        self.assertEqual(t["PinValueType"]["TerminalCategory"], "None")
        self.assertEqual(t["PinSubCategoryMemberReference"]["MemberGuid"], "0" * 32)

    def test_map_pin_reads_value_type(self):
        names = ["None", "name", "int"]
        data = (fname(1) + fname(0) + i32(0) + u8(3)
                + fname(2) + fname(0) + i32(0) + i32(0) + i32(0) + i32(0)   # FEdGraphTerminalType
                + i32(0) + i32(0) + i32(0) + fname(0) + b"\0" * 16 + i32(0) + i32(0) + i32(0))
        r = Reader(data, names=names)
        t = graphs.read_pin_type(r, FakePkg(names))
        self.assertEqual(r.pos, len(data))
        self.assertEqual(t["ContainerType"], "EPinContainerType::Map")
        self.assertEqual(t["PinValueType"]["TerminalCategory"], "int")


class TestGeneratedText(unittest.TestCase):
    def base(self, key, source):
        return i32(0) + struct.pack("<b", 0) + fstring("") + fstring(key) + fstring(source) + fstring("")

    def test_named_format(self):
        data = (i32(0) + struct.pack("<b", 1) + self.base("FMT", "{A}x")
                + i32(1) + fstring("A") + struct.pack("<b", 0) + struct.pack("<q", 10))
        r = Reader(data)
        t = read_text(r, FakePkg([]))
        self.assertEqual(r.pos, len(data))
        self.assertIsNone(t["source"])
        self.assertEqual(t["generated"]["format"]["source"], "{A}x")
        self.assertEqual(t["generated"]["arguments"], [{"name": "A", "value": {"type": "Int", "value": 10}}])

    def test_as_number_and_transform(self):
        number = (i32(0) + struct.pack("<b", 4) + struct.pack("<b", 3) + struct.pack("<d", 0.25)   # Double 0.25
                  + i32(1) + i32(0) + i32(1) + struct.pack("<b", 0) + i32(1) + i32(9) + i32(0) + i32(2)
                  + fstring("ko"))
        r = Reader(number)
        t = read_text(r, FakePkg([]))
        self.assertEqual(r.pos, len(number))
        self.assertEqual(t["generated"]["sourceValue"], {"type": "Double", "value": 0.25})
        self.assertEqual(t["generated"]["options"]["MaximumFractionalDigits"], 2)
        upper = i32(0) + struct.pack("<b", 10) + self.base("U", "hello") + u8(1)
        r = Reader(upper)
        t2 = read_text(r, FakePkg([]))
        self.assertEqual(r.pos, len(upper))
        self.assertEqual(t2["generated"]["source"]["source"], "hello")

    def test_generated_compare_key_differs_by_argument(self):
        def text(v):
            return (i32(0) + struct.pack("<b", 1) + self.base("FMT", "{A}x")
                    + i32(1) + fstring("A") + struct.pack("<b", 0) + struct.pack("<q", v))
        a = read_text(Reader(text(10)), FakePkg([]))
        b = read_text(Reader(text(20)), FakePkg([]))
        self.assertNotEqual(text_compare_key(a), text_compare_key(b))


if __name__ == "__main__":
    unittest.main()
