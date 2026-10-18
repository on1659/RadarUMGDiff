"""엔진 소스의 커스텀 버전 enum 에서 분석기가 쓰는 게이트 값을 뽑아 data/version_gates.json 을 만든다.

손으로 숫자를 적지 않는다 (dev 문서 §0 "추측 금지"). enum 은 뒤에만 붙으므로 한 멤버의 값은 엔진 버전과 무관하다 —
두 엔진에 다 있으면 같은 값인지 확인하고, 한쪽에만 있으면 그 사실을 적는다.

사용: py -3 tools/gen_version_gates.py [엔진 루트 ...]   (기본: C:/Program Files/Epic Games/UE_5.8, UE_5.1)
"""

import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "radar_umgdiff", "data", "version_gates.json")
CUSTOM = os.path.join(HERE, "..", "radar_umgdiff", "data", "custom_versions.json")

# 분석기가 읽는 게이트 (구조체 이름 → 멤버). 근거 위치는 각 리더의 주석에 있다.
WANTED = {
    "FFrameworkObjectVersion": ["PinsStoreFName", "EdGraphPinContainerType"],
    "FReleaseObjectVersion": ["PinTypeIncludesUObjectWrapperFlag"],
    "FUE5ReleaseStreamObjectVersion": ["SerializeFloatPinDefaultValuesAsSinglePrecision", "TextFormatArgumentData64bitSupport"],
    "FUE5MainStreamObjectVersion": ["EdGraphPinSourceIndex"],
    "FBlueprintsObjectVersion": ["EdGraphPinOptimized"],
    "FSequencerObjectVersion": ["SerializeFloatChannel", "SerializeFloatChannelCompletely"],
    "FFortniteMainBranchObjectVersion": ["SerializeFloatChannelShowCurve", "AddDevNotesToFText", "SoftObjectPathUtf8SubPaths"],
    "FEditorObjectVersion": ["AddedAlwaysSignNumberFormattingOption", "TextFormatArgumentDataIsVariant", "RootMetaDataSupport"],
}

HEADER_DIR = os.path.join("Engine", "Source", "Runtime", "Core", "Public", "UObject")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def enum_members(engine_root, struct):
    """struct F...Version { enum Type { … } } 의 멤버 → 값."""
    folder = os.path.join(engine_root, HEADER_DIR)
    for fn in os.listdir(folder):
        if not fn.endswith(".h"):
            continue
        text = io.open(os.path.join(folder, fn), encoding="utf-8", errors="replace").read()
        if not re.search(r"struct\s+(?:\w+_API\s+)?%s\b" % struct, text):
            continue
        # .inl X-매크로 형식 (Fortnite 계열)
        inc = re.search(r'#include\s+"UObject/(\w+\.inl)"', text)
        if inc:
            inl = io.open(os.path.join(folder, inc.group(1)), encoding="utf-8", errors="replace").read()
            return {m.group(1): int(m.group(2)) for m in re.finditer(r"^\s*\w+_VERSION\((\w+),\s*(\d+)\)", strip_comments(inl), re.M)}
        body = re.search(r"enum\s+Type\s*\{(.*?)\};", strip_comments(text), re.S)
        if not body:
            continue
        out = {}
        value = -1
        for item in body.group(1).split(","):
            item = item.strip()
            if not item:
                continue
            m = re.match(r"(\w+)\s*(?:=\s*(.+))?$", item, re.S)
            if not m:
                continue
            name, expr = m.group(1), m.group(2)
            if expr is not None:
                expr = expr.strip()
                if re.match(r"^-?\d+$", expr):
                    value = int(expr)
                elif expr in out:
                    value = out[expr]
                else:
                    # LatestVersion = VersionPlusOne - 1 같은 식 — 게이트 멤버가 아니므로 건너뛴다
                    continue
            else:
                value += 1
            out[name] = value
        return out
    raise SystemExit("헤더를 찾지 못함: %s (%s)" % (struct, folder))


def main(argv):
    roots = argv or [r"C:\Program Files\Epic Games\UE_5.8", r"C:\Program Files\Epic Games\UE_5.1"]
    custom = json.load(io.open(CUSTOM, encoding="utf-8"))["versions"]
    guid_of = {v["name"]: k for k, v in custom.items()}
    result = {"generatedBy": "tools/gen_version_gates.py", "engines": [os.path.basename(r) for r in roots], "gates": {}}
    for struct, members in WANTED.items():
        entry = {"guid": guid_of.get(struct), "members": {}}
        for root in roots:
            vals = enum_members(root, struct)
            eng = os.path.basename(root).replace("UE_", "")
            for m in members:
                if m not in vals:
                    entry["members"].setdefault(m, {}).setdefault("missingIn", []).append(eng)
                    continue
                cur = entry["members"].setdefault(m, {})
                if "value" in cur and cur["value"] != vals[m]:
                    raise SystemExit("엔진마다 값이 다름: %s.%s %s vs %s" % (struct, m, cur["value"], vals[m]))
                cur["value"] = vals[m]
        if entry["guid"] is None:
            raise SystemExit("custom_versions.json 에 GUID 가 없음: %s" % struct)
        result["gates"][struct] = entry
    io.open(OUT, "w", encoding="utf-8", newline="\n").write(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print("기록:", os.path.normpath(OUT))
    for s, e in result["gates"].items():
        print(" ", s, e["guid"], {k: v.get("value", "—") for k, v in e["members"].items()})


if __name__ == "__main__":
    main(sys.argv[1:])
