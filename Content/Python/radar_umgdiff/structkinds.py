"""구조체 종류 표 (dev 문서 §11.6, data/struct_kinds_<engine>.json — 커맨드릿 -run=RadarStructKinds 가 만든다).

옛 태그 형식(FileVersionUE5 < 1012)에는 구조체 몸체가 태그 스트림인지 네이티브 바이트인지가 없다. 표에서 StructName 으로 찾는다:
  native, immutable → 바이트 (natives.py 리더)
  tagged            → 태그 스트림
  표에 없음          → 모름 (사용자 정의 구조체는 태그 스트림이라 태그로 읽어 보고, 크기가 맞지 않으면 raw)
표는 그 패키지의 FileVersionUE5 와 같은 값으로 만든 것만 쓴다. 없으면 거부한다(코드 4).
"""

import glob
import io
import json
import os

from .reader import Refuse

NATIVE = "native"
IMMUTABLE = "immutable"
TAGGED = "tagged"

_TABLES = None


def _tables():
    global _TABLES
    if _TABLES is None:
        _TABLES = {}
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
        for path in sorted(glob.glob(os.path.join(here, "struct_kinds_*.json"))):
            with io.open(path, encoding="utf-8") as f:
                t = json.load(f)
            t["_file"] = os.path.basename(path)
            _TABLES[t["fileVersionUE5"]] = t
    return _TABLES


def table_for(pkg):
    """그 패키지의 FileVersionUE5 로 만든 표. 없으면 Refuse."""
    t = _tables().get(pkg.ue5)
    if t is None:
        have = ", ".join("%d(%s)" % (k, v["_file"]) for k, v in sorted(_tables().items())) or "없음"
        raise Refuse("FileVersionUE5 %d 의 옛 태그를 읽을 구조체 종류 표가 없습니다 (있는 표: %s). "
                     "그 엔진에서 -run=RadarStructKinds 를 돌려 data/struct_kinds_<engine>.json 을 만드세요" % (pkg.ue5, have))
    return t


def kind_of(table, struct_name):
    """'native' | 'immutable' | 'tagged' | None(모름)."""
    return table["kinds"].get(struct_name)
