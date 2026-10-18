"""바이트 프리미티브 (dev 문서 §11.1). 리틀 엔디언, 표준 라이브러리만, Python 3.9 문법."""

import struct


class ParseError(Exception):
    """파일이 예상한 레이아웃과 다를 때."""


class Refuse(Exception):
    """지원 범위 밖 파일 — CLI 종료 코드 4."""


_S = {
    "i8": struct.Struct("<b"), "u8": struct.Struct("<B"),
    "i16": struct.Struct("<h"), "u16": struct.Struct("<H"),
    "i32": struct.Struct("<i"), "u32": struct.Struct("<I"),
    "i64": struct.Struct("<q"), "u64": struct.Struct("<Q"),
    "f32": struct.Struct("<f"), "f64": struct.Struct("<d"),
}


class Reader:
    """bytes 위의 커서. names 가 있으면 fname() 이 문자열을 돌려준다."""

    def __init__(self, data, pos=0, end=None, names=None):
        self.data = data
        self.pos = pos
        self.end = len(data) if end is None else end
        self.names = names

    def _unpack(self, key):
        s = _S[key]
        p = self.pos
        if p + s.size > self.end:
            raise ParseError("읽기 범위 초과: %s @%d (끝 %d)" % (key, p, self.end))
        self.pos = p + s.size
        return s.unpack_from(self.data, p)[0]

    def i8(self): return self._unpack("i8")
    def u8(self): return self._unpack("u8")
    def i16(self): return self._unpack("i16")
    def u16(self): return self._unpack("u16")
    def i32(self): return self._unpack("i32")
    def u32(self): return self._unpack("u32")
    def i64(self): return self._unpack("i64")
    def u64(self): return self._unpack("u64")
    def f32(self): return self._unpack("f32")
    def f64(self): return self._unpack("f64")

    def bool32(self):
        v = self.u32()
        if v > 1:
            raise ParseError("bool32 값이 0/1 이 아님: %d @%d" % (v, self.pos - 4))
        return v == 1

    def bytes(self, n):
        if n < 0 or self.pos + n > self.end:
            raise ParseError("바이트 범위 초과: %d @%d (끝 %d)" % (n, self.pos, self.end))
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def remaining(self):
        return self.end - self.pos

    def fstring(self):
        """FString: L>0 ANSI(L 바이트, NUL 포함), L<0 UTF-16LE(-L 코드 유닛, NUL 포함), 0 빈 문자열."""
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            raw = self.bytes(n)
            # 엔진은 ANSI 경로에 Latin-1 범위만 쓴다. 저장 시 UTF-8 FString 은 따로 다룬다(utf8string).
            return raw[:-1].decode("latin-1") if raw[-1:] == b"\x00" else raw.decode("latin-1")
        if n < -0x40000000:
            raise ParseError("FString 길이 이상: %d @%d" % (n, self.pos - 4))
        raw = self.bytes(-n * 2)
        s = raw.decode("utf-16-le", errors="surrogatepass")
        return s[:-1] if s.endswith("\x00") else s

    def utf8string(self):
        """FUtf8String: int32 길이 + UTF-8 바이트, NUL 없음 (5.8 Core/Private/Containers/String.cpp.inl:1810 의 비 TCHAR 분기)."""
        n = self.i32()
        if n < 0:
            raise ParseError("UTF-8 문자열 길이 음수: %d" % n)
        return self.bytes(n).decode("utf-8", errors="replace")

    def fname_raw(self):
        return self.i32(), self.i32()

    def fname(self):
        idx, num = self.fname_raw()
        return self.name_str(idx, num)

    def name_str(self, idx, num):
        if self.names is None:
            return "#%d_%d" % (idx, num)
        if idx < 0 or idx >= len(self.names):
            raise ParseError("이름 인덱스 범위 밖: %d (이름 %d개) @%d" % (idx, len(self.names), self.pos - 8))
        base = self.names[idx]
        return base if num == 0 else "%s_%d" % (base, num - 1)

    def fguid(self):
        a, b, c, d = self.u32(), self.u32(), self.u32(), self.u32()
        return "%08X%08X%08X%08X" % (a, b, c, d)

    def sub(self, size):
        """현재 위치에서 size 바이트짜리 하위 리더를 만들고 커서를 그만큼 넘긴다."""
        if size < 0 or self.pos + size > self.end:
            raise ParseError("하위 범위 초과: %d @%d (끝 %d)" % (size, self.pos, self.end))
        r = Reader(self.data, self.pos, self.pos + size, self.names)
        self.pos += size
        return r
