"""커스텀 버전 게이트 (data/version_gates.json — tools/gen_version_gates.py 가 엔진 소스에서 만든다).

리더는 "이 패키지가 그 버전 이상으로 저장됐나"만 묻는다. 엔진 FArchive::CustomVer 는 패키지에 없는 GUID 에 -1 을 돌려준다.
"""

import io
import json
import os

_GATES = None


def _gates():
    global _GATES
    if _GATES is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "version_gates.json")
        _GATES = json.load(io.open(path, encoding="utf-8"))["gates"]
    return _GATES


def custom_version(pkg, struct):
    entry = _gates()[struct]
    return pkg.summary["customVersions"].get(entry["guid"], -1)


def at_least(pkg, struct, member):
    entry = _gates()[struct]
    return custom_version(pkg, struct) >= entry["members"][member]["value"]
