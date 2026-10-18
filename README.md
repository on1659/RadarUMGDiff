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
git submodule add -b 5.8 https://github.com/on1659/RadarUMGDiff.git Plugins/RadarUMGDiff   # UE 5.8 프로젝트
git submodule add -b 5.1 https://github.com/on1659/RadarUMGDiff.git Plugins/RadarUMGDiff   # UE 5.1 프로젝트
```
서브모듈을 못 쓰는 곳(Perforce 만 쓰는 프로젝트)은 `git clone -b <브랜치> … Plugins/RadarUMGDiff` 로 받아 폴더째 넣습니다.
폴더 이름은 `RadarUMGDiff` 그대로여야 하고, .uproject 에 따로 등록할 것은 없습니다(프로젝트 Plugins 폴더의 플러그인은 자동으로 켜집니다).

## 새 컴퓨터에서 시작하기 — Claude 세션 프롬프트

설계 문서는 이 저장소의 `docs/` 에 있습니다. `docs/radar-umg-diff.md` 가 결정의 정본, `docs/radar-umg-diff-dev.md` 가 구현 지시
(폴더 구조·diff.json 스키마·바이트 레이아웃·마일스톤·검증)입니다. 두 문서 안의 `C:\Work\CL9`, `C:\Work\P4` 경로는 처음 개발한 PC 의
것이니 새 PC 에서는 그 자리에 자기 프로젝트 경로를 넣어 읽습니다. 문서를 고칠 때는 이 저장소의 `docs/` 를 먼저 고치고,
CL9 저장소의 `docs/goal/` 사본은 그에 맞춥니다.

### A. 회사 프로젝트에 붙여서 쓰기 (UE 5.1 + Perforce 같은 환경)

새 세션 첫 메시지로 아래를 붙입니다. `<프로젝트>` 자리만 바꿉니다.

```
이 PC 의 언리얼 프로젝트 <프로젝트 폴더 절대경로> 에 RadarUMGDiff 플러그인을 붙여서 쓸 수 있게 해 줘. 위젯 블루프린트 두 리비전을
에디터 안에서 렌더해 비교하는 에디터 전용 플러그인이다. 먼저 <프로젝트>/Plugins/RadarUMGDiff/README.md 를 전부 읽어라.

순서:
1. 프로젝트의 엔진 버전(.uproject 의 EngineAssociation, 엔진 폴더의 Engine/Build/Build.version)을 확인하고, 플러그인 폴더의
   git 브랜치가 그 버전과 맞는지 본다(5.1 프로젝트는 5.1 브랜치, 5.8 프로젝트는 5.8 브랜치). 다르면 맞는 브랜치로 바꾼다.
2. 에디터 타깃을 빌드한다. C++ 프로젝트면 평소 방법으로, 아니면
   <엔진>\Engine\Build\BatchFiles\Build.bat <프로젝트이름>Editor Win64 Development -Project=<uproject 절대경로> 로.
   빌드 오류가 플러그인 안이면 고친다. 엔진이 회사에서 고친 소스 빌드라 API 가 다르면 분기는
   Source/RadarUMGDiffEditor/Private/RadarEngineCompat.h 한 곳에만 둔다.
3. 에디터를 띄우고 위젯 블루프린트 하나를 우클릭해 "Radar UMG Diff..." 가 뜨는지, 리비전 피커의 소스 컨트롤 탭(에디터가
   Perforce 에 연결돼 있을 때)에 그 파일의 히스토리가 나오고 "이전과 비교" 로 뷰어가 열리는지 확인한다. 결과는 스크린샷으로 보여 준다.
   히스토리가 안 나오면 "파일 두 개" 탭으로 P4V 에서 받은 리비전 파일 둘을 비교해 뷰어 자체는 도는지 본다.
4. 엔진이 소스 빌드(회사 수정 엔진)면 UnrealEditor-Cmd.exe <uproject> -run=RadarStructKinds 로 구조체 종류 표를 이 엔진에서
   다시 뽑아 Content/Python/radar_umgdiff/data/struct_kinds_<버전>.json 을 바꾼다. 런처 엔진이면 건너뛴다.
5. 실제 위젯 블루프린트 서너 개로 Content/Python/umgdiff_cli.py dump 를 돌려(번들 Python:
   <엔진>\Engine\Binaries\ThirdParty\Python3\Win64\python.exe -I) 커버리지와 읽지 못한 구조체 이름을 보고한다.
   읽지 못한 구조체가 있으면 README 의 "정확도" 절대로 리더를 추가할지 적어 둔다.

규칙: 프로젝트의 에셋·게임 코드는 건드리지 않고 Plugins/RadarUMGDiff 만 만진다. p4 sync -f, p4 clean 을 돌리지 않는다. 추측으로
고치지 않고 엔진 소스를 열어 확인한다. 조절 수치는 Config/DefaultRadarUMGDiff.ini 에 둔다. 주석·UI 문자열은 한국어다. 눈으로 볼
결과는 스크린샷으로 올린다. 끝났다고 말하기 전에 3~5 를 실제로 돌리고 결과를 그대로 보고한다.
```

### B. 개발 이어가기 (새 기능·버그·다른 엔진 버전)

```
RadarUMGDiff 플러그인 개발을 이어간다. 저장소 https://github.com/on1659/RadarUMGDiff, 로컬 위치 <플러그인 폴더 절대경로>.
브랜치는 엔진 버전별이다: 5.8 = UE 5.8, 5.1 = UE 5.1. 공통 변경은 5.8 에 먼저 넣고 5.1 에 체리픽한다. 두 브랜치의 차이는
Source/RadarUMGDiffEditor/Private/RadarEngineCompat.h 의 분기와 Content/Python/radar_umgdiff/data/struct_kinds_ue5.1.json 뿐이어야 한다.

먼저 읽을 것(읽기만, 순서대로): README.md, docs/radar-umg-diff.md(결정의 정본, Design Decisions 와 Acceptance Criteria),
docs/radar-umg-diff-dev.md(구현 지시 — §3.3 엔진 호환 표, §5 diff.json, §8 리비전 소스, §9 뷰어, §11 Python 바이트 레이아웃,
§14 테스트, §15 마일스톤). 문서 안의 C:\Work\CL9, C:\Work\P4 는 처음 개발한 PC 의 경로다.

할 일: <여기에 적는다>

규칙: 결정을 바꾸면 docs/radar-umg-diff.md 에 날짜와 함께 적고 기각한 대안도 남긴다. 구현 세부와 엔진 소스 근거는 dev 문서에 적는다.
추측으로 구현하지 않고 엔진 소스를 열어 확인한다. 조절 수치는 Config/DefaultRadarUMGDiff.ini. 주석·UI 문자열은 한국어, Copyright
헤더만 예외. 빌드가 도는 동안 Python 쪽을 진행한다. 검증은 dev 문서 §14(일치 테스트·픽스처·커버리지) 대로 실제로 돌리고 결과를
그대로 보고한다. 눈으로 볼 결과는 에디터 스크린샷으로 올린다. 커밋은 재촉 없이 나중에 한 번에, 두 브랜치 모두.
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
