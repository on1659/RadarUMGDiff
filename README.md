# Radar UMG Diff

위젯 블루프린트(WBP) 두 리비전을 **실제로 렌더해서** 나란히·겹쳐 보여 주고, 무엇이 바뀌었는지
위젯·슬롯·바인딩·클래스 기본값·애니메이션(키 단위)·그래프 단위로 알려 주는 언리얼 에디터 플러그인입니다.
같은 결과(`diff.json`)를 에디터 없이 만드는 표준 라이브러리 전용 Python 분석기가 함께 들어 있습니다.

- 엔진: UE 5.8 (`5.8` 브랜치), UE 5.1 (`5.1` 브랜치). 두 브랜치의 차이는 `RadarEngineCompat.h` 의 `#if` 결과, 픽스처 저장본, `data/struct_kinds_ue5.1.json` 뿐입니다
- 형상관리: 에디터의 소스 컨트롤 프로바이더(Perforce·Git 등) · git 명령줄 · 파일 두 개 — 어느 쪽이든 같은 화면
- 게임 모듈에 의존하지 않습니다 (에디터 모듈 하나)

## 설치

프로젝트의 `Plugins/RadarUMGDiff` 에 두고(서브모듈 권장) 에디터를 빌드합니다. 런타임 모듈은 없습니다.

```
git submodule add -b 5.8 https://github.com/on1659/RadarUMGDiff.git Plugins/RadarUMGDiff
```

## 쓰는 법 (에디터)

1. 콘텐츠 브라우저에서 위젯 블루프린트를 우클릭 → **Radar UMG Diff...**
2. 리비전 피커에서 백엔드 탭(소스 컨트롤 / git / 파일 두 개)을 고르고
   **이전과 비교**(한 번 클릭) · **작업 사본과 비교** · 두 항목을 골라 **선택한 둘 비교**.
   피커의 **diff.json 열기...** 는 CLI 나 다른 PC 가 만든 결과를 미리보기 없이(data-only) 엽니다.
3. 뷰어
   - 모드: 나란히 / 겹치기(불투명도) / 뒤집기(SpaceBar). 두 패널은 이동·확대를 같이 합니다.
   - 오버레이: 바뀐 위젯(노랑) · 추가(초록) · 이동(파랑 유령 상자와 화살표) · 지워진 위젯은 새 쪽의 원래 자리에 빗금 빨간 상자,
     마우스를 올리면 "<이름> 가 여기 있었음". 그려지지 않는 위젯(비활성 스위처 페이지 등)은 그려진 조상 가장자리에 칩.
   - 계층: 지워진 위젯은 옛 자리에 취소선, 이동은 옛 자리에 유령 행, 이름 변경 추정은 "이름 변경?" 표시.
   - 상세: 바뀐 속성만 속성 | 이전 | 이후 (색 칩, 텍스트 단어 diff, 애셋 링크). **전체 상세 열기** = 엔진 디테일 두 개.
   - 요약 칩·검색·**변경만**·**변경 없음 흐리게**·**자동 값 숨기기**(디자이너가 스스로 바꾸는 값, ini `AutoProperties`).
   - 다음/이전: F8 / F7.
   - 애니메이션 탭: 애니메이션 → 트랙 → 섹션(범위) → 채널 → 키(무비신 틱 단위) 의 값·보간·탄젠트 변화.
   - 그래프 탭: 그래프별 로직/배치 변경 수와 노드 목록, **Blueprint Diff에서 열기**(엔진 비교 창).
   - **Python 분석기로 다시 만들기**: 같은 두 파일을 번들 Python 분석기로 분석해 그 결과를 data-only 창으로 엽니다.

### 콘솔 명령

| 명령 | 하는 일 |
|---|---|
| `RadarUMGDiff.Picker <패키지>` | 리비전 피커 |
| `RadarUMGDiff.Open <패키지> <이전> <이후>` | 바로 비교. 리비전: `working`·`head`·`git:<sha>`·`scm:<리비전>`·`file:<경로>` |
| `RadarUMGDiff.Dump <패키지> <이전> <이후> <폴더>` | diff.json + 양쪽 state.json |
| `RadarUMGDiff.DumpHistory <폴더> [패키지...]` | git 인접 커밋 쌍마다 diff.json (정확도 모드) |
| `RadarUMGDiff.OpenJson <diff.json>` | data-only 뷰어 |
| `RadarUMGDiff.DumpSchema [출력]` | Python 분석기용 스키마(클래스·구조체 속성과 기본값) |
| `RadarUMGDiff.BuildFixtures [Anim\|Graph\|Text\|Bind\|Layout\|All] [-force]` | 테스트 픽스처를 버전별로 만들어 `Test/Fixtures/` 에 저장 (그 엔진의 저장본) |

커맨드릿(에디터를 닫은 창에서):

```
UnrealEditor-Cmd.exe <Project>.uproject -run=RadarUMGDiffDump -asset=/Game/UI/WBP_HUD -old=git:<sha> -new=working -out=<폴더> [-state]
UnrealEditor-Cmd.exe <Project>.uproject -run=RadarUMGDiffDump -history -out=<폴더>
UnrealEditor-Cmd.exe <Project>.uproject -run=RadarStructKinds [-out=<경로>]     # 옛 태그(FileVersionUE5 < 1012)용 구조체 종류 표
```

## Python 분석기 (에디터 없이)

표준 라이브러리만 씁니다. 엔진 번들 Python(5.1 = 3.9, 5.8 = 3.11)과 시스템 Python 3.14 에서 검사했습니다.

```
python -I Content/Python/umgdiff_cli.py dump    <uasset> [-o out.json] [--schema schema.json]
python -I Content/Python/umgdiff_cli.py diff    <old.uasset> <new.uasset> -o <폴더> [--package /Game/...] [--schema schema.json] [--hide-auto]
python -I Content/Python/umgdiff_cli.py conform <네이티브 diff.json|폴더> <Python diff.json|폴더>
python -I Content/Python/umgdiff_cli.py history [<저장소 상대 경로> ...] -o <폴더> [--repo <저장소>] [--schema schema.json]
python -I Content/Python/umgdiff_cli.py schema-check --schema schema.json [--repo <저장소>] [--fixtures Test/Fixtures]
python -I Content/Python/umgdiff_cli.py summary <diff.json>
```

종료 코드: 0 성공, 1 불일치(conform·schema-check), 3 인자, 4 거부(지원하지 않는 파일 버전·커스텀 버전).

- 지원 파일 버전: FileVersionUE5 1004~1018. 1012 이상은 완전 타입 이름 태그라 그대로 읽습니다. 1012 미만(5.1 저장본 = 1008)의
  옛 태그에는 구조체 몸체가 태그인지 바이트인지와 집합·맵 원소 구조체 이름이 없어서, 그 파일 버전으로 만든
  `Content/Python/radar_umgdiff/data/struct_kinds_<engine>.json`(그 엔진에서 `-run=RadarStructKinds`)이 있어야 읽습니다. 없으면 거부(4).
- **스키마는 선택**입니다. 없어도 해석·판정은 같고(`schema-check` 로 확인), 있으면 한쪽에만 태그가 있는 줄
  ("기본값일 수 있음")을 아키타입 기본값으로 확정하고 dump 에 `state: default` 줄을 채웁니다.
  만들기: 에디터에서 `py "<플러그인>/Content/Python/editor/dump_schema.py"` (→ `Saved/RadarUMGDiff/schema.json`).
- 키만 바뀐 텍스트(원문 같음)는 Python 만 `textKeys[]` 에 낮은 심각도로 냅니다(에디터는 로드할 때 키를 다시 만들 수 있다).

## diff.json

`schemaVersion: 1`. 두 생산자(에디터 네이티브, Python)가 같은 형을 씁니다. 머리에 `producer`(이름·종류·엔진·
파일 버전·커스텀 버전·`capabilities`), `asset`, `old`/`new`(리비전 출처와 커버리지), `designSize`, `summary`.
본문:

| 배열 | 항목 식별 | 내용 |
|---|---|---|
| `widgets[]` | `WidgetTree.<이름>` | kind(added·removed·changed·moved·renamed-probable), 부모·인덱스, `properties[]`, `slot{class, properties[]}` |
| `bindings[]` | `<위젯>:<속성>` | 함수·종류·원본 속성 |
| `cdo[]` | 속성 경로 | 클래스 기본값 (블루프린트 변수면 `varGuid`) |
| `variables[]` | `varGuid` | 블루프린트 변수 정의(형·이름 변경 `renamedFrom`) |
| `animations[]` | 애니메이션 이름 | `tracks[]`(`<위젯>/<트랙 클래스>`) → `sections[]`(범위·속성) → `channels[]` → `keys[]`(프레임·값·보간·탄젠트) |
| `graphs[]` | 그래프 이름 | 로직/배치 수, `nodes[]`(NodeGuid·클래스·area) → `pins[]`(PinId: 기본값·연결·핀 타입) |
| `textKeys[]` | 위젯·속성 | Python 만 — 키만 바뀐 텍스트 |

속성 한 줄: `path`(점 구분, 배열 `[i]`), `category`, `type`, `kind`, `maybeDefault`, `old`/`new` =
`{state: present|absent|default, text, value}`. 잎 구조체(ini `LeafStructs`)는 한 줄 + `changedComponents`.

## 설정 `Config/DefaultRadarUMGDiff.ini`

색 표, 외곽선 두께, 흐리게 불투명도, 기본 모드, 이름 변경 추정 문턱(`RenameThreshold`), 수치 허용 오차,
`IgnoredProperties`·`AutoProperties`·`LeafStructs`, 임시 폴더, 키(F8/F7/SpaceBar), Python·git 실행 파일.
빌드 없이 바꿀 수 있습니다(에디터를 다시 열면 적용, Python 분석기는 바로 적용).

## 정확도

`Content/Python/tools/accuracy_report.py` 가 [Test/accuracy_report.md](Test/accuracy_report.md) 를 만듭니다(그 브랜치 엔진의 픽스처). 2026-10-18 (UE 5.8.3):

| 대상 | 쌍·파일 | 결과 |
|---|---|---|
| 커버리지 (픽스처 21개 + 개발 프로젝트 git 블롭 37개) | 1,111,953 익스포트 바이트 | 범위 안 **1.0000**, 전체 0.9797 |
| 네이티브 vs Python — git 히스토리 | 22쌍 | 판정 불일치 **0** (스키마 없이 보류 5, 스키마로 2) |
| 네이티브 vs Python — 픽스처 (레이아웃·애니메이션·그래프·텍스트·바인딩) | 15쌍 | 판정 불일치 **0**, 보류 0 |
| 스키마 유무 | 37쌍 | 바뀐 판정 **0**, 스키마가 확정한 보류 줄 123 |
| 커맨드릿 vs 에디터 안 덤프 | 22쌍 | 같은 diff.json |

"범위 안" 은 클래스·함수 익스포트의 UStruct 데이터(컴파일된 블루프린트 클래스 몸체 — 비교 대상이 아닌 생성물)를 뺀 값입니다.
남은 보류 2건은 그 사이 C++ 에서 이름이 바뀐 속성의 옛 태그입니다 — 에디터는 지금 클래스로 로드하므로 그 값을 볼 수 없고,
Python 은 파일에 있는 그대로 알려 줍니다.

2026-10-18 (UE 5.1.1, `5.1` 브랜치, 5.1 에디터로 저장한 픽스처):

| 대상 | 쌍·파일 | 결과 |
|---|---|---|
| 커버리지 (픽스처 21개, FileVersionUE5 1008) | 334,794 익스포트 바이트 | 범위 안 **1.0000**, 전체 0.9464 |
| 5.1 커맨드릿 vs Python — 픽스처 | 15쌍 | 판정 불일치 **0**, 보류 0 |

## 알려진 한계

- 그래프 노드 제목은 파일에 없어서(에디터가 계산) Python 결과는 클래스·멤버 이름으로 대신 적습니다.
- 생성 텍스트(포맷·숫자·날짜)의 표시 문자열은 문화권 규칙이 있어야 계산됩니다 — Python 은 포맷 원문과 인자 구조로 비교합니다.
  `TextGenerator` 히스토리는 읽지 않습니다(raw).
- 중첩 UserWidget·블루프린트 부모 클래스의 기본값은 스키마에 없어 "기본값일 수 있음" 으로 남을 수 있습니다.
- 애니메이션 키 위치는 무비신 틱(tick resolution) 단위입니다.
