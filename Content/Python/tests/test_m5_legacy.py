"""M5: 옛 태그 형식(FileVersionUE5 < 1012, 5.1 저장본) — 손으로 만든 바이트 (dev 문서 §11.6 B, §14)."""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from radar_umgdiff.reader import Reader, Refuse                 # noqa: E402
from radar_umgdiff import tags as T                            # noqa: E402
from radar_umgdiff import structkinds                          # noqa: E402
from tests.test_primitives import FakePkg, fname, i32          # noqa: E402

GUID0 = b"\x00" * 16


class TestLegacyTags(unittest.TestCase):
    NAMES = ["None", "Padding", "StructProperty", "Margin", "Left", "FloatProperty", "bIsEnabled", "BoolProperty",
             "Tint", "LinearColor", "Count", "IntProperty", "Items", "ArrayProperty", "MyUserStruct", "Value",
             "Blob", "Opaque", "Mode", "ByteProperty", "ESlateVisibility", "Visible", "Raw"]
    KINDS = {"fileVersionUE5": 1008, "kinds": {"Margin": "tagged", "LinearColor": "immutable"}}

    def n(self, name):
        return fname(self.NAMES.index(name))

    def pkg(self):
        p = FakePkg(self.NAMES, ue5=1008)
        p._struct_kinds = self.KINDS
        return p

    def head(self, name, ptype, size, array_index=0, extra=b"", guid=None):
        return (self.n(name) + self.n(ptype) + i32(size) + i32(array_index) + extra
                + (b"\x01" + guid if guid else b"\x00"))

    def struct_extra(self, sname):
        return self.n(sname) + GUID0

    def margin_body(self, left):
        inner = self.head("Left", "FloatProperty", 4) + struct.pack("<f", left)
        return inner + self.n("None")

    def read(self, data):
        acct = T.Accounting()
        tags = T.read_tags(Reader(data + self.n("None"), names=self.NAMES), self.pkg(), acct)
        return tags, acct, len(data) + 8

    def test_struct_bool_int_guid(self):
        body = self.margin_body(4.0)
        col = struct.pack("<ffff", 0.5, 0.25, 1.0, 1.0)
        guid = struct.pack("<IIII", 1, 2, 3, 4)
        data = (self.head("Padding", "StructProperty", len(body), extra=self.struct_extra("Margin")) + body
                + self.head("bIsEnabled", "BoolProperty", 0, extra=b"\x01")
                + self.head("Tint", "StructProperty", 16, extra=self.struct_extra("LinearColor")) + col
                + self.head("Count", "IntProperty", 4, array_index=2, guid=guid) + i32(7)
                + self.head("Mode", "ByteProperty", 8, extra=self.n("ESlateVisibility")) + self.n("Visible"))
        tags, acct, total = self.read(data)
        d = T.tags_to_dict(tags)
        self.assertEqual(d["Padding"], {"Left": 4.0})
        self.assertIs(d["bIsEnabled"], True)
        self.assertEqual(d["Tint"], {"R": 0.5, "G": 0.25, "B": 1.0, "A": 1.0})
        self.assertEqual(d["Count[2]"], 7)
        self.assertEqual(d["Mode"], "Visible")
        self.assertEqual(tags[0]["type"], "StructProperty:Margin")
        self.assertEqual(tags[3]["propertyGuid"], "00000001000000020000000300000004")
        self.assertEqual(tags[4]["type"], "ByteProperty:ESlateVisibility")
        self.assertEqual(acct.understood, total)
        self.assertEqual(acct.unparsed, {})

    def test_struct_array_inner_tag(self):
        # ArrayProperty(StructProperty): count 뒤에 내부 태그(Name=배열 이름, Type=StructProperty, StructName, StructGuid, HasPropertyGuid)
        e1, e2 = self.margin_body(1.0), self.margin_body(2.0)
        inner_tag = self.head("Items", "StructProperty", len(e1) + len(e2), extra=self.struct_extra("Margin"))
        value = i32(2) + inner_tag + e1 + e2
        data = self.head("Items", "ArrayProperty", len(value), extra=self.n("StructProperty")) + value
        tags, acct, total = self.read(data)
        self.assertEqual(tags[0]["value"], [{"Left": 1.0}, {"Left": 2.0}])
        self.assertEqual(tags[0]["type"], "ArrayProperty:StructProperty:Margin")
        self.assertEqual(acct.understood, total)

    def test_empty_struct_array_still_has_inner_tag(self):
        inner_tag = self.head("Items", "StructProperty", 0, extra=self.struct_extra("Margin"))
        value = i32(0) + inner_tag
        data = self.head("Items", "ArrayProperty", len(value), extra=self.n("StructProperty")) + value
        tags, acct, total = self.read(data)
        self.assertEqual(tags[0]["value"], [])
        self.assertEqual(acct.understood, total)

    def test_unknown_struct_read_as_tags(self):
        # 표에 없는 구조체(사용자 정의 구조체): 태그 스트림으로 읽혀 크기가 맞으면 받아들인다
        body = self.head("Value", "IntProperty", 4) + i32(5) + self.n("None")
        data = self.head("Raw", "StructProperty", len(body), extra=self.struct_extra("MyUserStruct")) + body
        tags, acct, total = self.read(data)
        self.assertEqual(T.tags_to_dict(tags)["Raw"], {"Value": 5})
        self.assertEqual(acct.understood, total)

    def test_unknown_struct_native_bytes_are_raw(self):
        data = self.head("Blob", "StructProperty", 3, extra=self.struct_extra("Opaque")) + b"\xAA\xBB\xCC"
        tags, acct, _ = self.read(data)
        self.assertFalse(tags[0]["parsed"])
        self.assertEqual(tags[0]["raw"], "aabbcc")
        self.assertEqual(acct.unparsed, {"(struct-kind-unknown:Opaque)": 3})

    def test_refuse_without_table(self):
        p = FakePkg(self.NAMES, ue5=1005)
        with self.assertRaises(Refuse):
            structkinds.table_for(p)


if __name__ == "__main__":
    unittest.main()
