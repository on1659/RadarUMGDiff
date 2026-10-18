"""리더 프리미티브·태그·FText·소프트 경로를 손으로 만든 바이트로 검사한다 (dev 문서 §14 "Python 단위").

  python -I -m unittest discover -s <plugin>/Content/Python/tests -t <plugin>/Content/Python
"""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from radar_umgdiff.reader import Reader, ParseError          # noqa: E402
from radar_umgdiff import tags as T                           # noqa: E402
from radar_umgdiff.text import read_text                      # noqa: E402
from radar_umgdiff import softpath                            # noqa: E402
from radar_umgdiff import config                              # noqa: E402


def i32(v):
    return struct.pack("<i", v)


def fname(i, n=0):
    return i32(i) + i32(n)


def fstring_ansi(s):
    b = s.encode("latin-1") + b"\x00"
    return i32(len(b)) + b


def fstring_wide(s):
    b = (s + "\x00").encode("utf-16-le")
    return i32(-(len(b) // 2)) + b


class FakePkg:
    """태그 리더가 쓰는 Package 의 최소 표면."""

    def __init__(self, names, ue5=1018, fortnite=300, soft_paths=None):
        self.names = names
        self.ue5 = ue5
        self.summary = {"customVersions": {softpath.FORTNITE_MAIN_GUID: fortnite},
                        "softObjectPathsCount": len(soft_paths or []), "fileVersionUE5": ue5}
        self.soft_paths = soft_paths or []

    def object_path(self, idx):
        return None if idx == 0 else "obj%d" % idx

    def normalize_path(self, p):
        return p


class TestReader(unittest.TestCase):
    def test_fstring(self):
        data = fstring_ansi("abc") + fstring_wide("체력") + i32(0)
        r = Reader(data)
        self.assertEqual(r.fstring(), "abc")
        self.assertEqual(r.fstring(), "체력")
        self.assertEqual(r.fstring(), "")
        self.assertEqual(r.remaining(), 0)

    def test_fname_number(self):
        r = Reader(fname(1, 0) + fname(1, 3), names=["None", "Button"])
        self.assertEqual(r.fname(), "Button")
        self.assertEqual(r.fname(), "Button_2")

    def test_guid(self):
        r = Reader(struct.pack("<IIII", 1, 2, 3, 0xABCDEF01))
        self.assertEqual(r.fguid(), "0000000100000002" "00000003ABCDEF01")

    def test_bounds(self):
        with self.assertRaises(ParseError):
            Reader(b"\x01\x02").i32()

    def test_utf8string(self):
        b = "서브/경로".encode("utf-8")
        self.assertEqual(Reader(i32(len(b)) + b).utf8string(), "서브/경로")


class TestTags(unittest.TestCase):
    NAMES = ["None", "Padding", "StructProperty", "Margin", "/Script/SlateCore", "Left", "FloatProperty",
             "bIsEnabled", "BoolProperty", "Tint", "LinearColor", "/Script/CoreUObject", "Count", "IntProperty"]

    def node(self, name, inner):
        return fname(self.NAMES.index(name)) + i32(inner)

    def test_tagged_struct_and_native_struct_and_bool(self):
        # Padding: StructProperty(Margin(/Script/SlateCore)) 태그 스트림 {Left: 4.0}
        inner = fname(5) + self.node("FloatProperty", 0) + i32(4) + b"\x00" + struct.pack("<f", 4.0) + fname(0)
        tag1 = fname(1) + self.node("StructProperty", 1) + self.node("Margin", 1) + self.node("/Script/SlateCore", 0) + i32(len(inner)) + b"\x00" + inner
        # bIsEnabled: BoolProperty, 값 바이트 없음, 0x10 = true
        tag2 = fname(7) + self.node("BoolProperty", 0) + i32(0) + b"\x10"
        # Tint: LinearColor 네이티브(0x08) f32×4
        col = struct.pack("<ffff", 0.5, 0.25, 1.0, 1.0)
        tag3 = fname(9) + self.node("StructProperty", 1) + self.node("LinearColor", 1) + self.node("/Script/CoreUObject", 0) + i32(16) + b"\x08" + col
        # Count[2] = 7 (ArrayIndex 플래그)
        tag4 = fname(12) + self.node("IntProperty", 0) + i32(4) + b"\x01" + i32(2) + i32(7)
        data = tag1 + tag2 + tag3 + tag4 + fname(0)
        pkg = FakePkg(self.NAMES)
        acct = T.Accounting()
        tags = T.read_tags(Reader(data, names=self.NAMES), pkg, acct)
        d = T.tags_to_dict(tags)
        self.assertEqual(d["Padding"], {"Left": 4.0})
        self.assertIs(d["bIsEnabled"], True)
        self.assertEqual(d["Tint"], {"R": 0.5, "G": 0.25, "B": 1.0, "A": 1.0})
        self.assertEqual(d["Count[2]"], 7)
        self.assertEqual(tags[0]["type"], "StructProperty:Margin")
        self.assertEqual(acct.understood, len(data))
        self.assertEqual(acct.unparsed, {})

    def test_unknown_native_struct_is_raw(self):
        names = ["None", "Thing", "StructProperty", "Mystery", "/Script/X"]
        tag = fname(1) + fname(2) + i32(1) + fname(3) + i32(1) + fname(4) + i32(0) + i32(3) + b"\x08" + b"\xAA\xBB\xCC"
        acct = T.Accounting()
        tags = T.read_tags(Reader(tag + fname(0), names=names), FakePkg(names), acct)
        self.assertFalse(tags[0]["parsed"])
        self.assertEqual(tags[0]["raw"], "aabbcc")
        self.assertEqual(acct.unparsed, {"Mystery": 3})


class TestText(unittest.TestCase):
    def test_culture_invariant(self):
        # flags 2, history -1, bool(4바이트) 1, FString "♥" (UTF-16) — WBP_HUD HeartGlyph 실측 바이트
        data = bytes.fromhex("02000000ff01000000feffffff65260000")
        v = read_text(Reader(data), FakePkg([]))
        self.assertEqual(v["source"], "♥")

    def test_base_with_dev_notes(self):
        data = i32(0) + b"\x00" + fstring_ansi("NS") + fstring_ansi("KEY") + fstring_wide("체력") + fstring_ansi("")
        v = read_text(Reader(data), FakePkg([], fortnite=300))
        self.assertEqual((v["namespace"], v["key"], v["source"]), ("NS", "KEY", "체력"))

    def test_base_without_dev_notes_before_260(self):
        data = i32(0) + b"\x00" + fstring_ansi("") + fstring_ansi("K") + fstring_ansi("x")
        r = Reader(data)
        read_text(r, FakePkg([], fortnite=200))
        self.assertEqual(r.remaining(), 0)

    def test_string_table(self):
        names = ["None", "/Game/Strings/ST_UI.ST_UI"]
        data = i32(0) + struct.pack("<b", 11) + fname(1) + fstring_ansi("HealthTip")
        v = read_text(Reader(data, names=names), FakePkg(names))
        self.assertEqual((v["table"], v["tableKey"], v["source"]), ("/Game/Strings/ST_UI.ST_UI", "HealthTip", None))


class TestSoftPath(unittest.TestCase):
    def test_full_utf8_subpath(self):
        names = ["None", "/Game/UI/WBP_HUD", "WBP_HUD"]
        sub = "WidgetTree.Bar".encode("utf-8")
        data = fname(1) + fname(2) + i32(len(sub)) + sub
        pkg = FakePkg(names, fortnite=300)
        self.assertEqual(softpath.read_soft_object_path_full(Reader(data, names=names), pkg.summary), "/Game/UI/WBP_HUD.WBP_HUD:WidgetTree.Bar")

    def test_list_index(self):
        pkg = FakePkg(["None"], soft_paths=["/Game/A.A", "/Game/B.B"])
        self.assertEqual(softpath.read_soft_object_path_value(Reader(i32(1)), pkg), "/Game/B.B")


class TestConfig(unittest.TestCase):
    def test_plugin_ini(self):
        s = config.load()
        self.assertIn("LinearColor", s.leaf_structs)
        self.assertIn("bIsVariable", s.auto)
        self.assertAlmostEqual(s.rename_threshold, 0.8)


if __name__ == "__main__":
    unittest.main()
