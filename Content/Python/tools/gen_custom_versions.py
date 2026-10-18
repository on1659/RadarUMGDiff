"""엔진 소스에서 커스텀 버전 GUID → 이름 표를 뽑아 radar_umgdiff/data/custom_versions.json 에 쓴다.

손으로 GUID 를 옮기지 않기 위한 생성기다 (dev 문서 §11.4).
  py -3 tools/gen_custom_versions.py --engine "C:\\Program Files\\Epic Games\\UE_5.8" [--engine ...]
여러 엔진을 주면 합친다(같은 GUID 는 같은 이름이어야 한다).
"""

import argparse
import json
import os
import re
import sys

# const FGuid FEditorObjectVersion::GUID(0xE4B068ED, 0xF49442E9, 0xA231DA0B, 0x2E46BB41);
PAT_MEMBER = re.compile(r"const\s+FGuid\s+(F\w+)::GUID\s*\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)")
# static const FGuid GUID(0x..., ...);  — 구조체 안 선언 (이름은 가장 가까운 struct 이름)
PAT_INLINE = re.compile(r"(?:inline\s+)?static\s+(?:constexpr\s+|const\s+)+FGuid\s+GUID\s*(?:\{|\()\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)")
PAT_STRUCT = re.compile(r"struct\s+(?:\w+_API\s+)?(F\w+)")


def guid_text(parts):
    return "".join("%08X" % int(p, 16) for p in parts)


def scan(engine_root, table, origin):
    src = os.path.join(engine_root, "Engine", "Source")
    plug = os.path.join(engine_root, "Engine", "Plugins")
    for root_dir in (src, plug):
        for dirpath, _, files in os.walk(root_dir):
            for f in files:
                if not f.endswith((".cpp", ".h", ".inl")):
                    continue
                path = os.path.join(dirpath, f)
                try:
                    text = open(path, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                if "FGuid" not in text or "GUID" not in text:
                    continue
                for m in PAT_MEMBER.finditer(text):
                    add(table, guid_text(m.groups()[1:]), m.group(1), path, origin)
                for m in PAT_INLINE.finditer(text):
                    head = text[:m.start()]
                    structs = PAT_STRUCT.findall(head)
                    if structs:
                        add(table, guid_text(m.groups()), structs[-1], path, origin)


def add(table, guid, name, path, origin):
    cur = table.get(guid)
    if cur is None:
        table[guid] = {"name": name, "source": os.path.relpath(path, os.path.dirname(os.path.dirname(os.path.dirname(path)))).replace("\\", "/"), "engines": [origin]}
    else:
        if origin not in cur["engines"]:
            cur["engines"].append(origin)
        if cur["name"] != name:
            cur.setdefault("aliases", [])
            if name not in cur["aliases"]:
                cur["aliases"].append(name)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", action="append", required=True)
    ap.add_argument("-o", default=os.path.join(os.path.dirname(__file__), "..", "radar_umgdiff", "data", "custom_versions.json"))
    a = ap.parse_args(argv)
    table = {}
    for e in a.engine:
        ver = os.path.basename(e.rstrip("\\/")).replace("UE_", "")
        scan(e, table, ver)
    out = {"generatedBy": "tools/gen_custom_versions.py", "engines": [os.path.basename(e.rstrip("\\/")) for e in a.engine],
           "versions": {k: table[k] for k in sorted(table)}}
    os.makedirs(os.path.dirname(os.path.abspath(a.o)), exist_ok=True)
    with open(a.o, "w", encoding="utf-8", newline="\n") as fp:
        json.dump(out, fp, ensure_ascii=False, indent=2)
        fp.write("\n")
    print("커스텀 버전 %d개 → %s" % (len(table), os.path.abspath(a.o)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
