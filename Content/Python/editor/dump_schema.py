"""에디터 Python: 스키마 덤프 입구 (goal 문서 "Schema dump").

에디터 안에서 실행한다:  py "<플러그인>/Content/Python/editor/dump_schema.py" [출력 경로]
기본 출력은 <프로젝트>/Saved/RadarUMGDiff/schema.json.

실제 덤프는 플러그인 C++ 명령 RadarUMGDiff.DumpSchema 가 한다 — unreal 모듈은 BlueprintVisible 속성만 노출하므로
(PythonScriptPlugin PyGenUtil.cpp IsScriptExposedProperty) 저장되는 모든 UPROPERTY 의 기본값을 Python 으로는 읽을 수 없다.
"""

import os
import sys

import unreal


def main(argv):
    out = argv[0] if argv else os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
                                            "RadarUMGDiff", "schema.json")
    unreal.SystemLibrary.execute_console_command(None, 'RadarUMGDiff.DumpSchema "%s"' % out.replace("\\", "/"))
    unreal.log("RadarUMGDiff 스키마: %s" % out)


if __name__ == "__main__":
    main(sys.argv[1:])
