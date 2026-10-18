"""Config/DefaultRadarUMGDiff.ini 리더 — C++ URadarUMGDiffSettings 와 같은 파일, 같은 키 (dev 문서 §12).

configparser 는 '+Key=' 반복을 못 받으므로 직접 읽는다. 문법: [섹션], Key=Value, +Key=값(배열 추가), ; 주석.
"""

import os

SECTION = "/Script/RadarUMGDiffEditor.RadarUMGDiffSettings"
DEFAULT_PATH = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "Config", "DefaultRadarUMGDiff.ini"))


class Settings:
    def __init__(self, values, arrays):
        self.values = values
        self.arrays = arrays

    def get(self, key, default=None):
        return self.values.get(key, default)

    def number(self, key, default):
        try:
            return float(self.values[key])
        except (KeyError, ValueError):
            return default

    def flag(self, key, default):
        v = self.values.get(key)
        return default if v is None else v.strip().lower() in ("true", "1", "yes")

    def array(self, key):
        return list(self.arrays.get(key, []))

    @property
    def ignored(self):
        return set(self.array("IgnoredProperties"))

    @property
    def auto(self):
        return set(self.array("AutoProperties"))

    @property
    def leaf_structs(self):
        return set(self.array("LeafStructs"))

    @property
    def float_tolerance(self):
        return self.number("FloatTolerance", 1e-6)

    @property
    def double_tolerance(self):
        return self.number("DoubleTolerance", 1e-12)

    @property
    def rename_threshold(self):
        return self.number("RenameThreshold", 0.8)


def load(path=None):
    path = path or DEFAULT_PATH
    values, arrays = {}, {}
    section = None
    with open(path, encoding="utf-8-sig") as fp:
        for raw in fp:
            line = raw.strip()
            if not line or line.startswith(";"):
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
                continue
            if section != SECTION or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if key.startswith("+"):
                arrays.setdefault(key[1:], []).append(value)
            elif key.startswith("-"):
                lst = arrays.setdefault(key[1:], [])
                if value in lst:
                    lst.remove(value)
            else:
                values[key] = value
    return Settings(values, arrays)
