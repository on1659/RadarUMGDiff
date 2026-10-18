<!-- 이 파일은 CL9 저장소 docs/goal/ 의 같은 이름 문서를 2026-10-18 에 복사한 것이다. 이후 고칠 때는 이 저장소(docs/)를 먼저 고치고
     CL9 사본을 맞춘다. 본문의 C:\Work\CL9, C:\Work\P4 경로는 처음 개발한 PC 의 것이다. -->

# RadarUMGDiff 개발 문서

작성 2026-10-17. 결정의 정본은 `C:\Work\CL9\docs\goal\radar-umg-diff.md`(이하 goal 문서)이고, 이 문서는 그 결정을
**실제로 어떻게 구현하는가**를 적는다. 두 문서가 어긋나면 goal 문서가 이기고, 이 문서를 고친다.
이 문서의 모든 바이트 레이아웃·API 시그니처는 이 PC에 설치된 두 엔진의 소스에서 직접 확인한 것이다.
확인하지 못한 것은 본문에 **(확인 필요)** 로 표시했다. 그 표시가 없는 문장은 추측이 아니다.

- UE 5.8.3: `C:\Program Files\Epic Games\UE_5.8` (CL9 프로젝트 엔진, 레지스트리에는 5.4/5.6만 등록돼 있어 경로로 직접 지정)
- UE 5.1.1: `C:\Program Files\Epic Games\UE_5.1` (회사 엔진과 같은 버전, 2026-10-17 설치, Build.version 5.1.1 CL 23901901)

읽는 순서: 0 → 1 → 2 → 3 → 5(계약) → 그다음은 맡은 마일스톤의 절.

---

## 0. 구현 세션이 지켜야 할 규칙

이 저장소(CL9)의 규칙이 그대로 적용된다. 요약만 두고, 전문은 각 정본을 읽는다.

| 규칙 | 정본 |
|---|---|
| 빌드는 직접 UBT를 부르지 않고 브로커에 요청한다. `py -3 C:\Work\CL9\Tools\Build\build_request.py --kind build|compile --reason "..." --wait 540`. `build`는 에디터를 끄고 다시 켠다(요청 전에 내 에셋 저장). 기다리는 동안 다른 일을 한다. | `C:\Work\CL9\docs\goal\build-broker.md` |
| 에디터 Python·PIE·맵 로드는 다른 세션의 촬영/PIE 잠금을 먼저 확인한다. | `C:\Work\CL9\docs\workflow\sessions.md`, `/cl9 status` |
| 주석·UI 문자열은 한국어. Copyright 헤더만 예외. 문자열은 ko만 만들고 번역은 하지 않는다. | `C:\Work\CL9\docs\decisions.md`, `docs/conventions.md` |
| 조절 수치(색·문턱값·불투명도·임시 폴더 등)는 코드에 박지 않고 `Config/DefaultRadarUMGDiff.ini`에 둔다. | 전역 CLAUDE.md, goal 문서 In-scope |
| 추측으로 구현하지 않는다. 이 문서에 (확인 필요)가 붙은 항목은 해당 엔진 소스를 열어 확인한 뒤 구현한다. | 전역 CLAUDE.md |
| 커밋은 플러그인 폴더와 이 문서들만. CL9 게임 코드·에셋은 건드리지 않는다. 커밋을 보고마다 재촉하지 않는다. | 메모리 `commit-later-no-nagging` |
| 눈으로 볼 변경은 사용자에게 "확인해 보라" 하지 않고 스크린샷(에디터 창 캡처)으로 올린다. | 메모리 `film-instead-of-asking` |
| 마일스톤 하나가 끝나면 goal 문서의 Acceptance Criteria 체크박스를 채우고, 미결은 연쇄 시작 때 한 번에 묻는다. | 메모리 `autonomous-step-chain` |

모델 권고(goal 문서 Execution Notes): 파서·오버레이 지오메트리·diff 모델은 Fable 5.1 또는 Opus, 커맨드릿 배선·ini·README·CLI 인자는 Sonnet도 된다.

---

## 1. 한눈에 보는 구조

```
                ┌──────────────────────────── 생산자(엔진 버전 × 환경) ────────────────────────────┐
                │  네이티브 C++ (에디터 안 · 커맨드릿)   ue5.8 빌드 → 5.8 생산자                        │
                │                                        ue5.1 빌드 → 5.1 생산자                        │
                │  Python 분석기 (에디터 밖, 코드 한 벌)  FileVersionUE5 로 분기: 1008(5.1) … 1018(5.8)  │
                └───────────────────────────────┬─────────────────────────────────────────────────┘
                                                │  diff.json (계약 하나, §5)
                                                ▼
   ┌─────────────────────────────── 뷰어 (코드 한 벌, 엔진마다 빌드) ────────────────────────────────┐
   │  입력 (a) diff.json ──────► 요약 띠 · 계층 · 3열 상세 · 애니메이션 탭 · 그래프 요약 · 검색 · 다음/이전   │
   │  입력 (b) 로드된 UWidgetBlueprint 둘 ──► 나란히 미리보기 · 오버레이(변경/추가/이동 외곽선, 삭제 빨간 상자) │
   │  (b)가 없으면 data-only 모드: 미리보기 자리에 안내, 나머지는 그대로                                  │
   └───────────────────────────────────────────────────────────────────────────────────────────────┘
```

- 옛 리비전 파일은 형상관리 종류와 무관하게 세 갈래로 얻는다(§8.0): 연결된 소스 컨트롤 프로바이더 → git CLI → 파일 두 개.
- (b)는 우리 코드가 뽑는 것이 아니라 **그 에디터의 로더**가 뽑는다. 5.1 에디터는 5.1 에셋을, 5.8 에디터는 5.8 에셋과 그 이전 에셋(업그레이드 로드)을 읽는다.
- 그래서 "보이는 것은 같고 뽑아내는 것만 다르다"가 성립한다. 뷰어 코드는 한 벌이고, 엔진 API가 다른 세 곳(옛 리비전 로드, 미리보기 생성·파기, SZoomPan)만 `#if`로 갈린다(§3.3).

---

## 2. 저장소 · 브랜치 · 폴더

### 2.1 저장소와 브랜치

- 플러그인은 **GitHub 저장소 하나**, 엔진 버전별 **브랜치 하나씩**. **2026-10-18 사용자 결정: 공개 저장소 `RadarUMGDiff`, CL9 안에는 git 서브모듈, 브랜치 이름 `5.8`·`5.1`** (이 문서의 `ue5.8`/`ue5.1` 은 `5.8`/`5.1` 로 읽는다). 저장소 생성·push 는 외부 공개 동작이라 그때 사용자에게 확인한다. 그 전까지 코드는 CL9 작업 트리의 `Plugins/RadarUMGDiff/` 에서 만든다(CL9 git 에는 아직 추적되지 않음 — 서브모듈로 바꿀 때 충돌 없음).
- 두 브랜치의 차이는 C++ 세 곳의 `#if` 분기 결과와 `Content/Python/radar_umgdiff/data/struct_kinds_ue5.1.json` 한 파일뿐이어야 한다. Python 패키지·diff.json 스키마·README는 두 브랜치에서 동일하게 유지한다(한쪽을 고치면 다른 쪽에 체리픽).
- CL9는 `ue5.8` 브랜치를 `C:\Work\CL9\Plugins\RadarUMGDiff\`에 둔다. `.gitignore`에 `Plugins/` 예외는 없다(2026-10-17 확인, `Plugins` 폴더 자체가 아직 없음).

### 2.2 플러그인 폴더

```
Plugins/RadarUMGDiff/
  RadarUMGDiff.uplugin
  README.md                                  사용법 · diff.json 스키마 요약 · 커버리지/정확도 설명
  Config/
    DefaultRadarUMGDiff.ini                  §12 의 모든 조절값
  Source/RadarUMGDiffEditor/
    RadarUMGDiffEditor.Build.cs
    Public/
      RadarUMGDiffEditorModule.h
      RadarDiffModel.h                       §4 식별자·종류·영역, diff.json 과 1:1 인 C++ 구조체
      RadarDiffJson.h                        모델 ↔ JSON (JsonUtilities 가 아니라 손으로 쓴 직렬화, 필드 순서 고정)
      RadarRevisionSource.h                  IRadarRevisionSource + 백엔드 셋(프로바이더·git CLI·파일 둘) · 임시 패키지 · 로드 세션(FGCObject)
      RadarNativeDiffProducer.h              로드된 두 UWidgetBlueprint → FRadarDiff
      RadarUMGDiffSettings.h                 UDeveloperSettings, ini 매핑
    Private/
      RadarUMGDiffEditorModule.cpp           ToolMenus 등록 · 명령 · 창 열기
      RadarEngineCompat.h                    §3.3 의 #if 분기를 한 파일에 모은다 (RadarCompat:: 네임스페이스)
      RadarDiffJson.cpp
      RadarRevisionSource.cpp                공통(임시 폴더·로드·세션)
      RadarRevisionSource_Provider.cpp       ISourceControlProvider 백엔드 (§8.1)
      RadarRevisionSource_Git.cpp            git CLI 백엔드 (§8.2)
      RadarRevisionSource_Files.cpp          파일 두 개 백엔드 (§8.3)
      RadarNativeDiffProducer.cpp
      RadarPropertyCompare.cpp               FProperty 순회·Identical·텍스트화·FText 비교
      RadarRenameHeuristic.cpp               이동·이름 변경 추정
      Viewer/
        SRadarDiffWindow.cpp/.h              창 루트, 세션 소유, 탭·모드 상태
        SRadarSummaryStrip.cpp/.h
        SRadarHierarchy.cpp/.h               병합 트리(STreeView)
        SRadarDetailsGrid.cpp/.h             속성|이전|이후 3열 (SListView + 커스텀 행)
        SRadarPreviewPane.cpp/.h             SDesignSurface 파생, SZoomPan/SDPIScaler 안에 UUserWidget::TakeWidget()
        SRadarOverlay.cpp/.h                 OnPaint 에서 매 프레임 사각형 계산
        SRadarAnimationTab.cpp/.h
        SRadarGraphTab.cpp/.h
        RadarValueRenderers.cpp/.h           색 칩·FMargin 성분 강조·단어 diff·에셋 짧은 이름
      Commandlet/
        RadarUMGDiffDumpCommandlet.cpp/.h    -run=RadarUMGDiffDump
        RadarStructKindsCommandlet.cpp/.h    -run=RadarStructKinds  (ue5.1 브랜치에 필수, ue5.8 에도 둔다)
  Content/
    Python/
      umgdiff_cli.py                         dump | diff | conform | history | summary
      tools/seed_p4_from_git.py              git 히스토리를 로컬 P4 에 재생 (§8.4-4, 검증 전용)
      radar_umgdiff/
        __init__.py
        reader.py                            바이트 프리미티브 (int32/uint8/int64/FString/FName/FGuid)
        summary.py                           FPackageFileSummary (§11.3)
        tables.py                            이름표 · 임포트 · 익스포트 · 커스텀 버전 · 소프트 경로 목록
        tags.py                              태그 리더 두 형식 (§11.5)
        structkinds.py                       구조체 종류 결정 (§11.6) + data/ 로딩
        natives.py                           네이티브 구조체 리더 (§11.7)
        text.py                              FText (§11.8)
        softpath.py                          FSoftObjectPath (§11.8)
        exports.py                           익스포트 몸체 모델 [prefix][tags…None][suffix] (§11.4)
        classes.py                           클래스별 접미부: UEdGraphNode 핀, FUserPinInfo, UBlueprintCore 등 (§11.9)
        state.py                             전체 상태 덤프 (§6)
        diff.py                              상태 둘 → diff.json (§7 과 같은 규칙)
        conform.py                           네이티브 diff.json 과 비교
        gitsource.py                         git show / log 래퍼
        data/
          struct_kinds_ue5.1.json            §11.6 (ue5.1 브랜치)
      editor/
        dump_schema.py                       에디터 Python: UCLASS/USTRUCT 속성·기본값 → Saved/RadarUMGDiff/schema.json (M4)
    Test/
      WBP_Fix_Anim.uasset  …                 §13 픽스처 (버전별로 저장한 사본은 Test/<engine>/ 아래)
```

- Python 패키지는 **표준 라이브러리만** 쓰고, **Python 3.9 문법**으로 쓴다. 5.1 에디터 번들 Python은 3.9.7(`python39.dll`), 5.8은 3.11(`python311.dll`), 이 PC의 시스템 Python은 3.14.7이다. `match` 문, `X | Y` 타입 표기, `list[int]`를 런타임 평가하는 코드를 쓰지 않는다. CI 격으로 `conform`을 세 인터프리터에서 돈다.

---

## 3. 빌드 설정

### 3.1 `RadarUMGDiff.uplugin`

```json
{
  "FileVersion": 3, "Version": 1, "VersionName": "0.1",
  "FriendlyName": "Radar UMG Diff",
  "Description": "위젯 블루프린트 두 리비전을 렌더해서 비교한다",
  "Category": "Editor", "CanContainContent": true,
  "Modules": [ { "Name": "RadarUMGDiffEditor", "Type": "Editor", "LoadingPhase": "Default" } ]
}
```
`CanContainContent: true`는 `Content/Test` 픽스처 때문이다. 런타임 모듈은 없다.
`LoadingPhase`는 `Default`다 (2026-10-18 M4 정정): 커맨드릿 클래스는 `FEngineLoop::PreInitPostStartupScreen`에서 찾는데
(5.8 `LaunchEngineLoop.cpp` 3953), `PostEngineInit` 모듈은 그 뒤(4068)에 로드되어 `-run=RadarUMGDiffDump`가
"could not find the class"로 실패했다. 모듈 시작은 ToolMenus 시작 콜백 등록뿐이라 `Default`에서 문제가 없다.

### 3.2 `RadarUMGDiffEditor.Build.cs`

공통: `Core, CoreUObject, Engine, Slate, SlateCore, UnrealEd, Kismet, UMG, UMGEditor, GraphEditor, ToolMenus, ContentBrowser, Projects, PropertyEditor, EditorStyle, InputCore, Json, JsonUtilities, MovieScene, BlueprintGraph, SourceControl`. `SourceControl`은 엔진 공통 모듈(`Developer/SourceControl`)이고 특정 프로바이더 플러그인(Perforce, Git…)에는 의존하지 않는다.
**PythonScriptPlugin 의존은 없다.**

엔진별 추가:
- `ue5.8`: `AssetDefinition` (FRevisionInfo가 `Editor/AssetDefinition/Public/AssetDefinition.h:227`에 있다).
- `ue5.1`: `AssetTools` (FRevisionInfo가 `Developer/AssetTools/Public/IAssetTypeActions.h:28`에 있다).

`Target.Version.MajorVersion/MinorVersion`으로 분기한다.

### 3.3 `RadarEngineCompat.h` — 엔진 차이를 한 파일에

아래 표는 M5 전에 소스로 확인한 핵심 넷이다. **M5 실측(스파이크 8)으로 갈리는 곳은 더 많다 — 전체 목록은 §19 M5 표.** 다른 곳에 `#if ENGINE_*`를 쓰지 않는다(판정 매크로 `RADAR_UE51`).

| 항목 | ue5.8 | ue5.1 |
|---|---|---|
| FRevisionInfo 헤더 | `AssetDefinition.h` (필드 `FString Revision; int32 Changelist; FDateTime Date;`) | `IAssetTypeActions.h` (같은 필드, 2026-10-17 확인) |
| 옛 리비전 로드 | `DiffUtils::LoadPackageForDiff(FPackagePath temp, FPackagePath original)` — `Editor/UnrealEd/Public/DiffUtils.h:365`. 내부는 `LoadPackage(nullptr, TempPkgName, LOAD_ForDiff \| LOAD_DisableCompileOnLoad \| LOAD_DisableEngineVersionChecks, nullptr, &Context)`이고 Context에 원본→임시 패키지 매핑을 넣는다(`UnrealEd/Private/DiffUtils.cpp`) | 그 함수가 없다. `DiffUtils.h`는 `Editor/Kismet/Public/`에 있고 로드 함수는 없다. 5.1 자체 Blueprint diff는 `LoadPackage(NULL, *TempPkgName, LOAD_ForDiff \| LOAD_DisableCompileOnLoad)`를 직접 부른다(`Editor/Kismet/Private/SBlueprintEditorToolbar.cpp:578`). 5.1에도 `LOAD_DisableEngineVersionChecks`(`ObjectMacros.h:82`)와 `FLinkerInstancingContext::AddPackageMapping`(`LinkerInstancingContext.h:55`)이 있으므로 **5.8 `LoadPackageForDiff` 본문을 `RadarCompat::LoadPackageForDiff`로 옮겨 두 브랜치가 같은 코드를 쓴다**(임시 패키지 판정 `FPackageName::IsTempPackage`, 매핑 컨텍스트, 세 플래그). 분기는 사라진다 |
| 미리보기 생성 | `FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint(UObject* Outer, UWidgetBlueprint* BP, const FCreateWidgetFromBlueprintParams& {EWidgetDesignFlags FlagsToApply; ULocalPlayer* LocalPlayer})` — `UMGEditor/Public/WidgetBlueprintEditorUtils.h:91`. 본문: `FMakeClassSpawnableOnScope` 안에서 `NewObject<UUserWidget>(Outer, BP->GeneratedClass)` → `ClearFlags(RF_Transactional)` → `SetDesignerFlags` → 부모 체인의 NamedSlot 내용 수집 → `DuplicateAndInitializeFromWidgetTree(LatestWidgetTree, NamedSlotMap)` → 다시 `SetDesignerFlags` | 함수가 없다. 5.1 디자이너는 `FWidgetBlueprintEditor::UpdatePreview`(`UMGEditor/Private/WidgetBlueprintEditor.cpp` 1583~1620)에서 같은 순서를 직접 수행한다: `FMakeClassSpawnableOnScope` + `NewObject<UUserWidget>(PreviewScene.GetWorld(), GeneratedClass)` → `ClearFlags(RF_Transactional)` → `SetDesignerFlags(GetCurrentDesignerFlags())` → `SetPlayerContext` → `WidgetTree` 교체. **그 순서를 RadarCompat::CreatePreviewWidget 에 옮겨 쓴다.** NamedSlot 병합에 쓰는 `UUserWidget::DuplicateAndInitializeFromWidgetTree(UWidgetTree*, const TMap<FName, UWidget*>&)`는 5.1에도 있다(`Runtime/UMG/Public/Blueprint/UserWidget.h:224`). 결과적으로 5.8 함수 본문을 그대로 옮기면 두 브랜치 공통 코드가 된다 |
| 미리보기 파기 | `FWidgetBlueprintEditorUtils::DestroyUserWidget(UUserWidget*)`: `MarkAsGarbage()` + `ReleaseSlateResources(true)` + 약참조 소멸 ensure | 함수 없음. 5.1 `DestroyPreview()`가 같은 두 호출을 한다. RadarCompat::DestroyPreviewWidget 으로 동일 구현 |
| SZoomPan | `UMGEditor/Public/Designer/SZoomPan.h`, UMGEDITOR_API. `SLATE_ATTRIBUTE(FVector2D, ViewOffset)`, `SLATE_ATTRIBUTE(float, ZoomAmount)`, `SetContent` | `UMGEditor/Private/Designer/SZoomPan.h` — **Private**이라 플러그인에서 못 쓴다. 같은 API(ViewOffset·ZoomAmount 속성, OnArrangeChildren에서 `GetRelativeLayoutScale`)의 `SRadarZoomPan`을 플러그인 안에 둔다. 두 브랜치 모두 `SRadarZoomPan`을 쓰면 분기가 사라지므로 **그렇게 한다** |

공통으로 쓸 수 있다고 확인된 것: `SDesignSurface`(두 엔진 모두 Public, UMGEDITOR_API; `GetZoomAmount`, `ZoomToFit`, `OnPaintBackground`, `PaintBackgroundAsLines`), `SDPIScaler`(Slate 공용), `EWidgetDesignFlags`(5.8 `Runtime/UMG/Public/Components/Widget.h:175`, 5.1 같은 파일 `:166`, 값 `Designing=1, ShowOutline=2, ExecutePreConstruct=4, Previewing=8`), `SBlueprintDiff::CreateDiffWindow(FText, const UBlueprint*, const UBlueprint*, const FRevisionInfo&, const FRevisionInfo&)`(5.1은 non-const `UBlueprint*`), `FDetailsDiff` + `DetailsWidget()` + `HighlightProperty` + `GetDisplayedProperties`(`Kismet/Public/DetailsDiff.h`; 생성자는 5.8 `(const UObject*, bool bScrollbarOnLeft)`, 5.1 `(const UObject*, FOnDisplayedPropertiesChanged)` — M5 정정), `UWidgetBlueprint::FindDiffs`, `FPackagePath::FromLocalPath`, Content Browser 메뉴 이름 `ContentBrowser.AssetContextMenu.<클래스이름>`(`ContentBrowser/Private/ContentBrowserMenuContexts.cpp:115`이 `"ContentBrowser.AssetContextMenu." + 클래스 이름`으로 만든다. 우리는 `ContentBrowser.AssetContextMenu.WidgetBlueprint`를 `UToolMenus::Get()->ExtendMenu`로 확장).

---

## 4. 공통 모델 (C++ `RadarDiffModel.h` = Python `diff.py` = diff.json)

### 4.1 식별자

| 대상 | id | 근거 |
|---|---|---|
| 위젯 | 블루프린트 기준 오브젝트 경로. 예 `WidgetTree.HealthBar`. 중첩 UserWidget 내부는 펼치지 않는다(그 위젯은 하나의 잎) | 엔진 `UWidgetBlueprint::FindDiffs`가 `Widget->GetPathName(this)`를 키로 쓴다(`UMGEditor/Private/WidgetBlueprint.cpp`). Python은 익스포트 표의 Outer 체인으로 같은 문자열을 만든다 |
| 슬롯 | 자식 위젯 id + `/Slot`. 예 `WidgetTree.HealthBar/Slot`. 슬롯 오브젝트 이름(`CanvasPanelSlot_3`)은 쓰지 않는다 | 슬롯 이름은 재생성될 때 바뀐다. 슬롯 클래스가 바뀌면 같은 행에 `class: {old,new}`로 |
| 바인딩 | `ObjectName:PropertyName` (`FDelegateEditorBinding`의 두 필드) | `WidgetBlueprint.h` struct FDelegateEditorBinding: `FString ObjectName; FName PropertyName; FName FunctionName; FName SourceProperty; FGuid MemberGuid; EBindingKind Kind` |
| CDO 변수 | 변수 이름 + VarGuid | 이름이 바뀌면 VarGuid로 이어 붙여 `renamed` |
| 애니메이션 | `UWidgetAnimation` 이름 | `UWidgetBlueprint::Animations` |
| 트랙 | 바인딩(위젯 이름 또는 Root) + 트랙 클래스 | |
| 섹션 | 트랙 안 인덱스 | |
| 키 | 프레임 번호(FFrameNumber) | |
| 그래프 | 그래프 이름 (`EventGraph`, 함수명, 매크로명) | |
| 노드 | NodeGuid | |
| 핀 | PinId | |
| 변수 | 이름 + VarGuid | |

### 4.2 변경 종류 `kind`

`added` · `removed` · `changed` · `moved` · `renamed-probable`. 속성 한 줄의 상태는 `present` · `absent` · `default`.
`absent ↔ present`는 `changed`에 `maybeDefault: true`를 붙인다. 스키마(schema.json, M4)가 두 값이 같음을 증명하면 그 줄은 지운다.
`text-key-only`는 Python 생산자만 내는 저심각도 종류(§7.6).

### 4.3 영역 `area`

`designer`(위젯 속성) · `slot` · `binding` · `cdo` · `anim` · `graph-logic` · `graph-layout`. 그래프 노드의 `NodePosX/Y`, 주석 박스 크기·색, `bCommentBubbleVisible` 같은 배치 속성만 `graph-layout`이고 나머지는 `graph-logic`.

### 4.4 심각도 `severity`

`normal`(기본) · `low`(text-key-only, graph-layout, DisplayLabel만 바뀜) · `info`(중첩 UserWidget·텍스처가 현재 버전으로 대체 표시됨 등 배지). 요약 띠 기본 필터는 `low` 포함, `info` 제외.

---

## 5. diff.json 계약

버전 필드 `schemaVersion: 1`. 필드 순서는 아래 순서로 고정하고(Python `json.dumps(..., sort_keys=False)`, C++ 손 직렬화), 들여쓰기 2, UTF-8, 줄 끝 `\n`. 수치는 `FProperty::ExportTextItem`과 같은 정밀도로 문자열(`text`)과 JSON 수(`value`) 둘 다 담는다. 일치 테스트는 `value`와 `kind`만 비교하고 `text`는 비교하지 않는다(5.1/5.8 부동소수 출력 자릿수가 다를 수 있음).

```json
{
  "schemaVersion": 1,
  "producer": {
    "name": "RadarUMGDiff.Native", "kind": "native",
    "engine": "5.8.3", "pluginBranch": "ue5.8",
    "fileVersionUE5": { "old": 1017, "new": 1018 },
    "customVersions": { "old": { "<guid>": 12 }, "new": { "<guid>": 12 } },
    "capabilities": ["default-state", "live-objects"]
  },
  "asset": {
    "packageName": "/Game/UI/WBP_HUD", "relPath": "Content/UI/WBP_HUD.uasset",
    "assetName": "WBP_HUD", "parentClass": "/Script/CL9.CL9HUDWidget"
  },
  "old": { "kind": "commit", "sha": "65c99c5…", "date": "2026-10-02T11:20:00+09:00", "author": "…", "subject": "…",
           "coverage": 1.0, "unparsedStructs": [] },
  "new": { "kind": "working", "sha": null, "head": "347f315…", "dirty": true, "coverage": 1.0, "unparsedStructs": [] },
  "designSize": { "old": { "mode": "FillScreen", "size": [1920, 1080] }, "new": { "mode": "FillScreen", "size": [1920, 1080] } },
  "summary": {
    "byKind": { "added": 2, "removed": 1, "changed": 7, "moved": 1, "renamed-probable": 1, "text-key-only": 0 },
    "byArea": { "designer": 6, "slot": 3, "binding": 0, "cdo": 1, "anim": 0, "graph-logic": 1, "graph-layout": 1 }
  },
  "widgets": [
    {
      "id": "WidgetTree.HealthBar", "kind": "changed", "area": "designer", "severity": "normal",
      "class": "/Script/UMG.ProgressBar",
      "displayLabel": { "old": "체력", "new": "체력" },
      "parent": { "old": "WidgetTree.HudRoot", "new": "WidgetTree.HudRoot" },
      "index": { "old": 1, "new": 1 },
      "badges": [],
      "properties": [
        {
          "path": "FillColorAndOpacity", "category": "Appearance",
          "type": "StructProperty:LinearColor",
          "kind": "changed", "maybeDefault": false,
          "old": { "state": "present", "text": "(R=0.886,G=0.294,B=0.290,A=1.0)", "value": { "R": 0.886, "G": 0.294, "B": 0.290, "A": 1.0 } },
          "new": { "state": "present", "text": "(R=0.388,G=0.600,B=0.133,A=1.0)", "value": { "R": 0.388, "G": 0.600, "B": 0.133, "A": 1.0 } }
        },
        {
          "path": "ToolTipText", "category": "Behavior", "type": "TextProperty", "kind": "changed",
          "old": { "state": "present", "text": "체력 상태", "value": { "source": "체력 상태", "namespace": "", "key": "A1B2…", "table": null, "tableKey": null } },
          "new": { "state": "present", "text": "체력 게이지", "value": { "source": "체력 게이지", "namespace": "", "key": "C3D4…", "table": "/Game/UI/ST_HUD", "tableKey": "HealthTip" } }
        }
      ],
      "slot": {
        "class": { "old": "/Script/UMG.CanvasPanelSlot", "new": "/Script/UMG.CanvasPanelSlot" },
        "properties": [
          { "path": "LayoutData.Offsets", "category": "Layout", "type": "StructProperty:Margin", "kind": "changed",
            "old": { "state": "present", "text": "(Left=24,Top=24,Right=320,Bottom=28)", "value": { "Left": 24, "Top": 24, "Right": 320, "Bottom": 28 } },
            "new": { "state": "present", "text": "(Left=24,Top=24,Right=400,Bottom=28)", "value": { "Left": 24, "Top": 24, "Right": 400, "Bottom": 28 } },
            "changedComponents": ["Right"] }
        ]
      }
    },
    { "id": "WidgetTree.StaminaBar_Old", "kind": "removed", "area": "designer", "class": "/Script/UMG.ProgressBar",
      "displayLabel": { "old": "StaminaBar_Old", "new": null }, "parent": { "old": "WidgetTree.HudRoot", "new": null }, "index": { "old": 2, "new": null },
      "properties": [ { "path": "*", "kind": "removed", "old": { "state": "present", "text": "…전체 상태 덤프 참조…" } } ],
      "slot": { "class": { "old": "/Script/UMG.CanvasPanelSlot", "new": null }, "properties": [ … ] } },
    { "id": "WidgetTree.RoundTimer", "kind": "renamed-probable", "area": "designer", "class": "/Script/UMG.TextBlock",
      "rename": { "from": "WidgetTree.Timer", "confidence": 0.9, "evidence": "same-parent-index" },
      "properties": [ … ] },
    { "id": "WidgetTree.OrderList", "kind": "moved", "area": "slot",
      "parent": { "old": "WidgetTree.HudRoot", "new": "WidgetTree.HudRoot" }, "index": { "old": 3, "new": 1 } }
  ],
  "bindings": [ { "id": "HealthBar:Percent", "kind": "changed", "area": "binding", "old": { "function": "GetHealth", "kind": "Function" }, "new": { "function": "GetHealthPct", "kind": "Function" } } ],
  "cdo": [ { "id": "MaxOrders", "varGuid": "…", "kind": "changed", "area": "cdo", "type": "IntProperty",
             "old": { "state": "present", "text": "3", "value": 3 }, "new": { "state": "default", "text": "0", "value": 0 } } ],
  "animations": [
    { "id": "FadeIn", "kind": "changed", "area": "anim",
      "tracks": [ { "id": "HealthBar/MovieScene2DTransformTrack", "kind": "changed",
                    "sections": [ { "index": 0, "kind": "changed",
                                    "range": { "old": [0, 30], "new": [0, 45] },
                                    "channels": [ { "name": "Translation.X", "kind": "changed",
                                                    "keys": [ { "frame": 0, "kind": "changed", "old": 0.0, "new": 12.0 },
                                                              { "frame": 30, "kind": "removed", "old": 100.0, "new": null } ] } ] } ] } ] }
  ],
  "graphs": [
    { "id": "EventGraph", "kind": "changed",
      "logic": { "added": 1, "removed": 0, "changed": 0 }, "layout": { "moved": 1 },
      "nodes": [ { "guid": "…", "class": "/Script/BlueprintGraph.K2Node_CallFunction", "title": "Set Percent", "kind": "added", "area": "graph-logic",
                   "pins": [ { "pinId": "…", "name": "InPercent", "kind": "added", "defaultValue": { "old": null, "new": "1.0" } } ] },
                 { "guid": "…", "class": "/Script/BlueprintGraph.K2Node_Event", "title": "Event Construct", "kind": "changed", "area": "graph-layout",
                   "properties": [ { "path": "NodePosX", "kind": "changed", "old": { "value": 100 }, "new": { "value": 340 } } ] } ],
      "variables": [ { "name": "MaxOrders", "varGuid": "…", "kind": "changed", "properties": [ … ] } ] }
  ],
  "textKeys": [ { "widget": "WidgetTree.Title", "path": "Text", "kind": "text-key-only", "severity": "low",
                  "old": { "namespace": "", "key": "AAAA" }, "new": { "namespace": "", "key": "BBBB" } } ],
  "notes": [ { "code": "nested-userwidget-current-version", "widget": "WidgetTree.QuickSlots", "severity": "info" } ]
}
```

필드 규칙:
- `producer.capabilities`에 쓸 수 있는 값: `live-objects`(에디터 안 네이티브 생산자, 뷰어가 (b)를 가진다), `default-state`(스키마가 있어 `default` 상태를 낼 수 있다), `text-key-only`(Python), `graph`, `anim-keys`, `coverage`(Python). 뷰어는 선언되지 않은 종류의 필터 칩을 회색으로 그린다.
- `old`/`new`의 `kind`: `scm`(프로바이더 리비전; `provider`, `revision`, `changelist`, `user`, `description`, `date` 필드) · `commit`(git CLI; `sha`, `date`, `author`, `subject`) · `head` · `working` · `file`(`path`만). 어느 백엔드가 만들었는지 뷰어 제목 띠에 그대로 보인다.
- 모든 `properties[]` 항목은 `path`(점 구분, 배열은 `[i]`, 맵은 `[key]`), `category`(UPROPERTY 메타 Category, 없으면 `"기타"`), `type`(`<PropertyClass>[:<StructName|EnumName|InnerType>]`), `kind`, `old`, `new`를 가진다. 값 객체는 `state`, `text`, `value`, 선택 `asset`(오브젝트·소프트 참조는 `/Game/...` 경로), 선택 `textKey`.
- `removed`/`added` 위젯은 속성을 전부 적지 않고 `"*"` 한 줄로 표시하되, 뷰어가 "이 위젯의 전체 속성"을 보일 수 있게 **전체 상태 덤프(§6)의 export 인덱스를 `stateRef`로 가리킨다**(네이티브 생산자는 상태 덤프도 같이 쓴다).
- 수치 비교 허용 오차: float `1e-6` 상대, double `1e-12`. 그 안이면 `changed`로 내지 않는다(ini `FloatTolerance`).

---

## 6. 전체 상태 덤프 (state.json)

한 리비전의 "완전한 현재 상태". `umgdiff_cli.py dump <uasset>`과 네이티브 커맨드릿 `-dump-state` 둘 다 쓴다. 뷰어의 "전체 상세 열기"가 아니라, removed/added 위젯의 속성 표와 일치 테스트의 입력이다.

```json
{
  "schemaVersion": 1, "producer": { … },
  "file": { "path": "…", "size": 85203, "fileVersionUE4": 522, "fileVersionUE5": 1018, "licenseeVersion": 0,
            "savedBy": "5.8.3-58210709+++UE5+Release-5.8", "customVersions": { "<guid>": 12 }, "totalHeaderSize": 37583 },
  "names": ["/Game/UI/WBP_HUD", "…"],
  "imports": [ { "index": -1, "classPackage": "/Script/CL9", "className": "CL9HUDWidget", "outer": -29, "objectName": "Default__CL9HUDWidget", "packageName": "None", "optional": false } ],
  "exports": [
    { "index": 1, "objectName": "ItemLabelBox", "class": "/Script/UMG.Border", "super": 0, "template": 0, "outer": 138,
      "objectFlags": 0, "serialOffset": 37583, "serialSize": 436, "scriptStart": 0, "scriptEnd": 432,
      "prefix": { "serializationControl": 0 },
      "properties": [ { "name": "Padding", "type": "StructProperty:Margin", "size": 124, "flags": 0, "arrayIndex": 0, "parsed": true,
                        "value": { "Left": 4, "Top": 2, "Right": 4, "Bottom": 2 } } ],
      "suffix": { "hasObjectGuid": false, "objectGuid": null, "classSpecific": null },
      "bytesUnderstood": 436, "unparsed": [] }
  ],
  "widgetTree": { "root": "WidgetTree.HudRoot", "children": { "WidgetTree.HudRoot": ["WidgetTree.HealthBar", "…"] } },
  "coverage": { "exportBytes": 47620, "understood": 47620, "ratio": 1.0, "unparsedStructs": {} }
}
```

- `scriptStart/scriptEnd`는 1010 이상에서만 파일에 있다. 1008(5.1)에서는 `null`을 쓰고, 파서가 계산한 값을 `derivedScriptStart/End`에 적는다.
- `parsed: false`인 속성은 `raw`(hex)와 `structName`을 가지며 `coverage.unparsedStructs[structName] += size`.
- Python 덤프의 `value`는 네이티브 덤프의 `value`와 **같은 JSON 형**이어야 한다. 그 매핑은 §11.7 표의 "JSON" 열이다.

---

## 7. 네이티브 생산자 (C++, `RadarNativeDiffProducer`)

입력: 로드된 두 `UWidgetBlueprint*`(old, new)와 두 `FRevisionInfo`. 출력: `FRadarDiff`(→ diff.json) + 두 `FRadarState`(→ state.json).

### 7.1 수집

1. 위젯: `BP->WidgetTree->ForEachWidget` 로 모든 `UWidget*`를 모아 `id = Widget->GetPathName(BP)`로 맵을 만든다. 각 위젯에 대해 부모(`Widget->GetParent()`)와 부모 안 인덱스(`Parent->GetChildIndex(Widget)`), `Widget->Slot`, `Widget->GetDisplayLabel()`(`Runtime/UMG/Public/Components/Widget.h:975`, 내부 `FString DisplayLabel`), 클래스.
2. NamedSlot 내용: `WidgetTree->GetSlotNames` + `GetContentForSlot`은 트리 순회에 이미 포함된다. 중첩 UserWidget의 내부 트리는 순회하지 않는다(그 위젯은 잎, `notes`에 `nested-userwidget-current-version` 배지).
3. 바인딩: `BP->Bindings`(`TArray<FDelegateEditorBinding>`).
4. CDO: `BP->GeneratedClass->GetDefaultObject()`의 속성 중 `FBlueprintEditorUtils::GetBlueprintVariables`가 돌려주는 블루프린트 변수들만(`NewVariables`의 `VarName`/`VarGuid`). 네이티브 부모 클래스 속성은 디자이너 "Class Defaults" 패널에 보이는 것만 대상이며 그 판정은 `EditDefaultsOnly|EditAnywhere|BlueprintReadWrite` 플래그. (확인함 — M1·M4: WBP_HUD 히스토리에서 CDO 줄 CookGaugeClass·CookGaugeLift·QuickSlotClass 등이 네이티브·Python 양쪽에 잡힌다. 구현은 블루프린트 변수만이 아니라 CDO 전체를 이름으로 짝지어 비교하고, 블루프린트 변수 줄에 varGuid 를 붙인다 — §19 M1 ⑤)
5. 애니메이션: `BP->Animations`. 각 `UWidgetAnimation`의 `MovieScene`에서 바인딩(`GetBindings()` → `FMovieSceneBinding`의 `GetObjectGuid`, 이름은 `AnimationBindings[].WidgetName`), 트랙(`GetTracks()`), 섹션(`UMovieSceneTrack::GetAllSections()`), 채널(`FMovieSceneChannelProxy`에서 `GetChannels<FMovieSceneFloatChannel>()`/`FMovieSceneDoubleChannel`), 키(`GetTimes()`, `GetValues()`). `AnimationBindings`는 5.1에서는 `UWidgetBlueprint`(`WidgetBlueprint.h:182`)에, 5.8에서는 `UWidgetAnimation`(`Runtime/UMG/Public/Animation/WidgetAnimation.h:147`)에 있다. RadarCompat에 `GetAnimationBindings(BP, Anim)` 하나로 감싼다(다섯 번째 분기, 작다).
6. 그래프: `BP->UbergraphPages`, `FunctionGraphs`, `MacroGraphs`, `DelegateSignatureGraphs`. 노드 `UEdGraphNode::NodeGuid`, 핀 `UEdGraphPin::PinId`, 노드 제목 `GetNodeTitle(ENodeTitleType::ListView)`.

### 7.2 속성 비교 (`RadarPropertyCompare.cpp`)

- 같은 id의 두 오브젝트 A(old), B(new)에 대해 `TFieldIterator<FProperty>(Class)`로 순회한다. 제외: `CPF_Transient`, `CPF_Deprecated`, `CPF_DuplicateTransient`, 이름이 `Slot`(별도 처리), `WidgetGeneratedBy`, `bCreatedByConstructionScript`, `Navigation`의 내부 포인터 등 **ini `IgnoredProperties` 목록**(기본값은 §12).
- 비교: `Property->Identical_InContainer(A, B, 0, PPF_DeepComparison)`. 다르면 구조체는 재귀(`FStructProperty` → 멤버 순회, `changedComponents`를 채움), 배열은 인덱스 정렬 diff, 맵은 키 정렬 diff, 오브젝트 참조는 `GetPathName` 비교(같은 패키지 안 참조는 id로 치환).
- 텍스트화: `Property->ExportTextItem_InContainer(Str, A, nullptr, nullptr, PPF_None)`. JSON 값은 §11.7 표의 형으로 손으로 만든다(`FJsonObjectConverter`는 FText·소프트 경로를 우리가 원하는 형으로 내지 않는다).
- FText: `FTextInspector::GetSourceString`, `GetNamespace`, `GetKey`, `FTextInspector::GetTextId`, 스트링테이블은 `FTextInspector::GetTableIdAndKey`. **비교는 source string(+table id/key)로만** 한다. 네임스페이스·키만 바뀐 것은 네이티브 생산자는 내지 않는다(로드 시 키가 재생성돼 신뢰할 수 없음, goal 문서 결정).
- `default` 상태: 네이티브 생산자는 CDO/아키타입(`Widget->GetArchetype()`)과 `Identical`이면 `state: "default"`를 낼 수 있다(capability `default-state`). Python은 schema.json이 있을 때만.

### 7.3 이동·이름 변경 추정 (`RadarRenameHeuristic.cpp`)

```
1. same-id 매칭: id가 양쪽에 있으면 짝. (parent 또는 index 다르면 kind=moved, 속성도 다르면 moved+changed 두 플래그, 행은 하나)
2. 남은 removed 집합 R, added 집합 A 에 대해:
   for r in R: for a in A with a.class == r.class:
      score = 1.0  if (a.parent == r.parent && a.index == r.index)
            = (동일 속성 수 / max(속성 수)) otherwise, 단 RenameThreshold(ini, 기본 0.8) 미만은 후보 아님
   점수 내림차순으로 1:1 탐욕 매칭 → kind=renamed-probable, rename.confidence=score, evidence ∈ {same-parent-index, property-set}
3. renamed-probable 은 그래프 diff를 접지 않는다. 그래프에서 같은 이름 변경이 실제로 관측될 때만
   (K2Node_VariableGet/Set 의 VariableReference.MemberName 이 old→new 로 바뀐 노드가 있을 때) rename.graphConfirmed=true 를 붙인다.
```

### 7.4 애니메이션 diff

- 애니메이션: 이름으로 매칭. 트랙: 바인딩 이름 + 트랙 클래스. 섹션: 트랙 안 인덱스. 채널: 채널 이름(`FMovieSceneChannelMetaData::Name`). 키: 프레임 번호.
- 키 값 비교는 §5의 허용 오차. 탄젠트(`FMovieSceneTangentData`)와 보간 모드도 속성으로 낸다.
- 리스트 레벨(M1): 애니메이션 추가·삭제·트랙 수 변화까지. 키 레벨(M4): 위 전부.

### 7.5 그래프 diff

- 노드: NodeGuid 매칭. 없는 쪽이 added/removed. 둘 다 있으면 `FProperty` 비교(§7.2)로 바뀐 속성을 모으고, 그 속성이 `NodePosX/NodePosY/NodeWidth/NodeHeight/NodeComment/bCommentBubbleVisible/bCommentBubblePinned/CommentColor`면 `graph-layout`, 아니면 `graph-logic`. 핀: PinId 매칭, `DefaultValue/DefaultObject/DefaultTextValue/LinkedTo(상대 PinId 집합)/PinType`.
- 상세는 우리 화면에 그리지 않는다. 그래프 탭은 그래프별 logic/layout 개수와 "Blueprint Diff에서 열기"(`SBlueprintDiff::CreateDiffWindow`)만.

### 7.6 text-key-only (Python 전용)

Python `diff.py`는 같은 위젯·같은 속성에서 source string은 같고 namespace/key만 다른 FText를 `textKeys[]`에 `severity: low`로 낸다. 네이티브는 내지 않는다. 일치 테스트는 이 배열을 비교에서 제외한다.

---

## 8. 리비전 로딩 (`RadarRevisionSource`)

### 8.0 백엔드 셋 — 형상관리 종류와 무관하게 (사용자 결정 2026-10-18)

```
class IRadarRevisionSource {
  virtual FText GetDisplayName() = 0;                               // "소스 컨트롤 (Perforce)", "git", "파일 두 개"
  virtual bool  IsAvailableFor(const FString& AssetFile) = 0;
  virtual TArray<FRadarRevision> GetHistory(const FString& AssetFile) = 0;      // 최신 → 과거
  virtual bool  Fetch(const FRadarRevision&, const FString& AssetFile, FString& OutTempFile) = 0;  // Saved/Diff/ 아래 파일
};
struct FRadarRevision { EKind Kind; FString Id /*sha 또는 리비전 문자열*/; int32 Changelist; FDateTime Date; FString User; FString Description; FString LocalFile /*Kind==File*/; };
```
순서: (A) 프로바이더 → (B) git CLI → (C) 파일 두 개. 피커는 `IsAvailableFor`가 참인 백엔드만 보이고, "이전과 비교"는 첫 번째 가능한 백엔드의 히스토리 두 번째 항목(없으면 HEAD/최신 체크인)과 작업 사본을 비교한다. 리비전 하나를 받은 뒤의 로드·세션은 백엔드와 무관하게 §8.5·8.6.

### 8.1 (A) 엔진 소스 컨트롤 프로바이더 백엔드

프로젝트가 무엇을 연결했든(Perforce, Git 플러그인, Plastic/Unity VCS, SVN, Diversion…) 엔진 인터페이스는 하나다. 5.1과 5.8의 헤더가 아래 호출 모두에서 같다(2026-10-18 확인).
```
ISourceControlModule& M = ISourceControlModule::Get();
IsAvailableFor = M.IsEnabled() && M.GetProvider().IsAvailable();                 // ISourceControlProvider.h 5.8:191,199 / 5.1:114,122
GetHistory:
  TSharedRef<FUpdateStatus> Op = ISourceControlOperation::Create<FUpdateStatus>();
  Op->SetUpdateHistory(true);                                                    // SourceControlOperations.h 5.8:503 / 5.1:376
  Provider.Execute(Op, { AbsFile }, EConcurrency::Synchronous);                  // 동기. 피커는 호출 중 "히스토리 받는 중" 표시
  FSourceControlStatePtr St = Provider.GetState(AbsFile, EStateCacheUsage::Use);
  for i in 0..St->GetHistorySize(): Rev = St->GetHistoryItem(i)                 // ISourceControlState.h 5.8:59,66 / 5.1:33,40
     FRadarRevision{ Kind=Scm, Id=Rev->GetRevision(), Changelist=Rev->GetCheckInIdentifier(), Date=Rev->GetDate(),
                     User=Rev->GetUserName(), Description=Rev->GetDescription() }        // ISourceControlRevision.h 5.8·5.1 64~88 동일
Fetch:
  Rev = St->FindHistoryRevision(Revision.Id);                                    // ISourceControlState.h 5.8:80 / 5.1:54
  FString Temp; Rev->Get(Temp);                                                  // ISourceControlRevision.h:44 (두 버전 동일). 프로바이더가 FPaths::DiffDir() = <Project>/Saved/Diff/ 에 쓴다
```
- 이것이 엔진 자체의 "Diff against previous" 경로다. 5.1 `SBlueprintEditorToolbar.cpp:578` 근처(`OnDiffRevisionPicked`: `GetHistoryItem` → `Revision->Get(TempPkgName)` → `LoadPackage(LOAD_ForDiff|LOAD_DisableCompileOnLoad)`), 5.8은 `DiffUtils::LoadPackageForDiff(TSharedPtr<ISourceControlRevision>)` 오버로드(`DiffUtils.h:366`)가 같은 일을 한다. 우리는 `Get`으로 받은 파일 경로를 §8.5로 넘긴다.
- 엔진의 기성 드롭다운 `SBlueprintRevisionMenu`(`Kismet/Public/SBlueprintRevisionMenu.h`, 두 버전 같은 인자: `Construct(InArgs, const UBlueprint*)`, `bIncludeLocalRevision`, `OnRevisionSelected(FRevisionInfo const&)`)를 "이전과 비교" 드롭다운에 재사용할 수 있다. 다만 두 리비전을 고르는 피커 본체는 우리 목록(§9.9)이다.
- 프로바이더가 켜져 있어도 그 파일의 히스토리가 0개면(그 VCS 관리 밖) `IsAvailableFor`를 거짓으로 보고 (B)로 넘어간다.
- Perforce 프로바이더가 `Get`으로 받은 파일 이름은 `FPaths::CreateTempFilename(FPaths::DiffDir(), File, Extension)`이다(5.8 `Plugins/Developer/PerforceSourceControl/Source/PerforceSourceControl/Private/PerforceSourceControlRevision.cpp:78`). 즉 `Saved/Diff/` 아래 임의 이름 → §8.5가 우리 규칙 이름으로 바꾼다.
- 검증은 **이 PC에 실제 Perforce 서버를 세워서** 한다(사용자 결정 2026-10-18, 절차 §8.4). 번들 Git 프로바이더(`Engine/Plugins/Developer/GitSourceControl`, 두 엔진 모두)도 두 번째 프로바이더로 같은 검증을 거친다.

### 8.2 (B) git CLI 백엔드

프로바이더가 없거나 그 파일에 히스토리가 없을 때. 플러그인·설정 없이 `git`만 PATH에 있으면 된다. 모두 `FPlatformProcess::ExecProcess`로, 작업 디렉터리는 에셋 파일이 있는 폴더.
```
git rev-parse --show-toplevel
git -c core.quotepath=off log --format=%H%x1f%h%x1f%aI%x1f%an%x1f%s --follow -- <relpath>
git status --porcelain -- <relpath>                 (더럽으면 HEAD 도 후보로)
git show <sha>:<relpath>  → stdout 바이트 그대로 파일에 쓴다 (텍스트 변환 금지, 바이너리 모드)
```
Python `gitsource.py`도 같은 명령을 `subprocess.run(..., capture_output=True)`로 부른다(`text=False`).

### 8.3 (C) 파일 두 개 백엔드

항상 보인다. 사용자가 어떤 방법으로든 얻은 `.uasset` 두 개(P4V "Get Revision → to file", SVN export, 백업 사본, 다른 브랜치 체크아웃 폴더)를 파일 대화상자 또는 드래그로 지정한다. `Fetch`는 그 파일을 `Saved/Diff/temp-file-<해시8>-<AssetName>.uasset`으로 **복사**한다(원본 자리에서 로드하면 `/Temp/` 규칙이 깨지고 이름이 충돌한다). 한쪽을 "작업 사본"으로 둘 수도 있다. CLI `umgdiff_cli.py diff <a> <b>`는 처음부터 이 모드다.

### 8.4 로컬 Perforce 서버로 프로바이더 백엔드 검증하기

p4·p4d는 원래 이 PC에 없었고 2026-10-18에 받았다(아래 1). 두 엔진에 `Engine/Plugins/Developer/PerforceSourceControl`이 번들돼 있으므로 서버만 있으면 된다.

**2026-10-18 실제로 수행했고 아래가 그 결과다. 서버가 떠 있고 히스토리가 올라가 있다. 기동법·계정은 `C:\Work\P4\README.txt`, 비밀번호는 `C:\Work\P4\secret.txt`(저장소 밖).**

1. **받기 (완료).** `ftp.perforce.com/perforce/r26.1/bin.ntx64/`의 `p4d.exe`(24.4MB)·`p4.exe`(11.5MB), 빌드 `2026.1/3089746 (2026/10/07)`, 같은 폴더 `SHA256SUMS`로 검증. r26.2~26.6 폴더에는 서버 실행 파일이 없다(P4 One 클라이언트만). 둔 곳 `C:\Work\P4\bin\`, 설치 프로그램·서비스 등록 없음.
2. **서버 (떠 있음).** `p4d.exe -r C:\Work\P4\root -p 127.0.0.1:1666 -L C:\Work\P4\p4d.log -J C:\Work\P4\root\journal`(PowerShell `Start-Process … -WindowStyle Hidden`으로 독립 프로세스). **함정**: 탐색기에서 `p4d.exe`를 더블클릭하면 인자 없이 떠서 `bin`에 db.*를 만들고 `0.0.0.0:1666`을 잡는다 — 한 번 그렇게 떴고 끈 뒤 정식으로 다시 띄웠다. `bin`의 빈 db.*는 남아 있다.
3. **계정·워크스페이스 (완료).** 2026.1 서버 기본 보안 수준은 **security=4**(비밀번호 필수, `dm.user.noautocreate=2`): `p4 passwd -P`는 거부되고 stdin 파이프(bash에서 `printf '%s\n%s\n' "$PW" "$PW" | p4 passwd`; PowerShell 파이프는 "Passwords don't match"로 실패)만 통한다. 첫 사용자 `Radar`가 super. `p4 login` 티켓은 `%USERPROFILE%\p4tickets.txt`. `p4 set`으로 P4PORT/P4USER/P4CLIENT를 사용자 레지스트리에 넣었다. 클라이언트 `CL9P4`: Root `C:\Work\CL9`, `allwrite noclobber`, View `//depot/CL9/Content/UI/... //CL9P4/Content/UI/...` 한 줄. **`p4 sync -f`·`p4 clean` 금지.**
4. **git 히스토리 재생 (완료).** `seed_p4_from_git.py`. **발견 둘.** (a) 에디터가 떠 있으면 `Content/**/*.uasset` 전부(미추적 파일까지) 쓰기 열기가 거부된다(공유 모드). 그래서 작업 트리에 쓰지 않고 **별도 폴더 `C:\Work\P4\seed`를 Root로 둔 임시 클라이언트 `CL9P4Seed`**(같은 뷰)에서 제출하고, `CL9P4`는 `p4 sync -k`(파일을 쓰지 않고 보유 목록만)로 맞춘다(`--write-root`). (b) p4는 환경 변수 `PWD`를 현재 폴더로 믿는다 — bash가 넘긴 PWD가 다른 폴더면 상대 경로가 거기서 풀린다. p4에는 절대 경로를 넘기고 서브프로세스 env의 PWD도 cwd로 맞춘다. 커밋 제목의 `—`가 cp949 콘솔에서 예외를 내서 stdout을 UTF-8로 재설정했고, 중단 뒤 이어 돌릴 수 있게 디포 #N만큼 건너뛴다. 결과는 아래 §8.4 검증값.
5. **에디터 연결 (남음).** Revision Control → Connect to Revision Control → Perforce, Server `127.0.0.1:1666`, User `Radar`, Workspace `CL9P4`, 비밀번호 `secret.txt`. 설정은 `Saved/Config/WindowsEditor/SourceControlSettings.ini`. 2026-10-18에는 다른 세션이 에디터를 쓰고 있어(editor.lock) 손대지 않았다.
6. **검증 (남음, M0 이후).** WBP_HUD 우클릭 → Radar UMG Diff → "소스 컨트롤 (Perforce)" 탭에 #1~#11(설명 "… (git sha7, 날짜, 작성자)")이 뜨고 "이전과 비교"가 열린다. 같은 두 리비전을 (B) git 탭으로 열어 `conform` 0 차이. 커맨드릿 `-old=#10 -new=#11`. 끝나면 `p4 admin stop`, 에디터 연결 해제.
7. **Git 프로바이더로 반복 (남음).** Revision Control → Git, 같은 WBP로 목록·diff 확인. 끄고 (B)만으로도 같은 목록.

검증값 (2026-10-18 13:40): `p4 changes //depot/CL9/Content/UI/...` 33건. 파일별 P4 헤드 리비전 == git 커밋 수: CookGauge 1, Dawn 2, HUD 11, Main 3, OrderSelect 2, Pause 3, QuickSlot 1, Scan 2, ScanLabel 2, Settings 1, Shop 2, Spectate 3. DebugPanel·GameOver·Title은 git 커밋이 없어 디포에 없다. 파일 타입 `binary+l`. 재생 전후 `WBP_HUD.uasset` 작업 사본 SHA256 동일(더러운 상태 유지). `CL9P4`는 `sync -k`로 12 파일 보유 목록만 갱신, 디스크 쓰기 0.

### 8.5 임시 패키지와 로드

- 경로: `<Project>/Saved/Diff/temp-<id>-<AssetName>.uasset` (ini `TempDir`, 기본 `Saved/Diff` = `FPaths::DiffDir()`). `<id>`는 sha7 / 프로바이더 리비전 문자열(파일명에 못 쓰는 글자는 `_`) / 파일 해시8. 프로바이더 백엔드가 `Get`으로 직접 쓴 파일은 그 자리에서 쓰되 이름이 다르면 우리 규칙으로 바꾼다. 같은 id를 다시 열면 재사용.
- 패키지 이름은 `FPackagePath::FromLocalPath(temp)`가 `/Temp/...`로 매핑한다(`Saved/`가 `/Temp/` 마운트). **이 마운트가 `IsTempPackage`를 참으로 만들어** 5.8 `LoadPackageForDiff`가 `LOAD_ForDiff` 경로로 간다(`UnrealEd/Private/DiffUtils.cpp`: 임시 패키지가 아니면 `LOAD_None`으로 보통 로드). 시스템 temp 폴더를 쓰면 이 조건이 깨지므로 금지(goal 문서 결정).
- 5.8: `DiffUtils::LoadPackageForDiff(FPackagePath::FromLocalPath(temp), FPackagePath::FromLocalPath(original))`.
- 5.1: 같은 본문을 `RadarCompat::LoadPackageForDiff`로 가진다(§3.3). 즉 `LoadPackage(nullptr, *TempPkgName, LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks, nullptr, &Context)`에 `Context.AddPackageMapping(OriginalPkgFName, TempPkgFName)`.
- 로드 뒤 `UWidgetBlueprint*`는 `FindObject<UWidgetBlueprint>(Package, *AssetName)` 또는 `GetObjectsWithOuter`로 찾는다(5.1 Blueprint diff는 `GetObjectsWithOuter(PreviousTempPkg, ...)`를 쓴다).
- 작업 사본 = 현재 에디터에 로드된 그 에셋(`FAssetData::GetAsset()`). HEAD(git) 또는 "최신 체크인"(프로바이더 히스토리 0번)은 작업 사본이 더러울 때만 후보.
- `old`/`new` 양쪽이 임시 패키지일 수 있다(두 커밋 비교). 같은 에셋 이름의 두 패키지가 동시에 살아야 하므로 패키지 이름이 sha로 달라야 한다(위 경로 규칙이 보장).

### 8.6 세션 수명 (`FRadarDiffSession : FGCObject`)

보유: `UPackage* OldPkg, NewPkg`(작업 사본이면 null), `UWidgetBlueprint* OldBP, NewBP`, `UUserWidget* OldPreview, NewPreview`, 열려 있는 Blueprint Diff 창의 `TWeakPtr<SWindow>`. `AddReferencedObjects`에 전부 추가. 창이 닫히면 `RadarCompat::DestroyPreviewWidget` 둘 → 임시 패키지는 `ResetLoaders` 후 `MarkAsGarbage`(ini `KeepTempPackages=false`면 파일도 삭제). Blueprint Diff 창이 아직 열려 있으면 세션을 그 창의 `OnWindowClosed`까지 살려 둔다.

---

## 9. 뷰어 (Slate)

### 9.1 창 구성과 상태

`SRadarDiffWindow`(SWindow 안 루트)가 `FRadarDiff`, `FRadarDiffSession`(없으면 data-only), `FRadarViewState`를 가진다.

```
FRadarViewState
  EPreviewMode Mode = SideBySide | Overlay | Flip      (Space 가 Flip 토글, ini DefaultPreviewMode)
  float OverlayOpacity (ini, 기본 0.5)
  bool bChangedOnly (기본 true), bDimUnchanged (기본 true)
  TSet<EKind> KindFilter, TSet<EArea> AreaFilter      (요약 칩)
  FString SearchText
  FString SelectedId                                   (계층·오버레이·상세가 공유)
  int32 CursorIndex                                    (Next/Prev 가 걷는 평탄 리스트의 위치)
  float Zoom; FVector2D ViewOffset                     (두 미리보기 공유)
  ETab Tab = Designer | Animation | Graph
```

평탄 리스트 = `widgets ∪ bindings ∪ cdo ∪ animations(키까지) ∪ graphs(노드까지)`를 탭 순서·계층 순서로 늘어놓고 필터를 적용한 것. Next/Prev(ini 키, 기본 F8/F7)는 이 리스트를 걷고, 선택 항목이 다른 탭에 있으면 탭을 바꾸며, 위젯이면 두 미리보기를 그 위젯이 보이도록 스크롤·줌(`ZoomToFit` 변형)한다.

### 9.2 요약 띠 `SRadarSummaryStrip`

`summary.byKind`·`byArea` 칩. 클릭 토글 → `KindFilter/AreaFilter`. `producer.capabilities`에 없는 종류의 칩은 비활성 회색 + 툴팁 "이 생산자는 이 종류를 내지 않습니다". 오른쬭에 검색 입력, "변경만", "변경 없음 흐리게" 체크.

### 9.3 계층 `SRadarHierarchy` (STreeView)

병합 알고리즘:
```
노드 = (id, class, label(old/new), kind, oldParent, oldIndex, newParent, newIndex, descendantChangeCount)
1. new 트리를 기준으로 트리를 만든다 (added 는 newIndex 에 그대로 있다).
2. removed 위젯은 oldParent 가 살아 있으면 그 부모의 자식 목록에 oldIndex 위치로 끼운다 (취소선 + 빗금 배경).
   oldParent 도 removed 면 그 removed 조상 아래에 재귀로 매달린다.
3. moved 위젯은 새 위치에 실제 행, 옛 위치에 "유령 행"(반투명, → 화살표와 "newIndex 로 이동") 을 둔다.
4. renamed-probable 은 새 이름 행 하나에 "Timer → RoundTimer (추정 0.9)".
5. descendantChangeCount = 자손 중 kind != unchanged 인 수. 배지로 그린다.
6. 필터: bChangedOnly 면 kind==unchanged 이고 descendantChangeCount==0 인 행을 숨긴다. 검색은 id/displayLabel/속성 path/old·new text 에 부분 일치, 일치한 행은 펼치고 일치 글자를 강조색으로.
```
행 클릭 → `SelectedId`. 더블클릭 → 두 미리보기를 그 위젯에 맞춰 줌.

### 9.4 상세 `SRadarDetailsGrid`

선택 id의 `properties[]`(+ `slot.properties[]`)를 `category`별로 묶어 `속성 | 이전 | 이후` 3열. 행 렌더러(`RadarValueRenderers`):

| 타입 | 렌더 |
|---|---|
| `StructProperty:LinearColor`/`Color`/`SlateColor` | 색 칩 + 16진 텍스트 |
| `StructProperty:Margin`/`Anchors`/`Vector2D`/`Vector4` | 성분별 텍스트, `changedComponents`만 강조 배경 |
| `TextProperty` | 단어 단위 diff(공백 분할 LCS), 삭제 단어 빨간 배경, 추가 단어 녹색, 아래 작은 글씨로 `table/tableKey` 또는 `namespace/key` |
| 오브젝트·소프트 참조 | 짧은 이름, 툴팁에 전체 경로, 클릭 시 Content Browser 동기화 |
| `StructProperty:SlateBrush` 등 중첩 구조체 | 접을 수 있는 하위 행 |
| `state: default` | 값 앞에 `(기본)` 회색 |
| `maybeDefault: true` | 행 끝에 `?` 아이콘 + 툴팁 "한쪽은 저장되지 않은 값입니다. 스키마가 있으면 확정됩니다" |

하단 링크: "전체 상세 열기" → `FDetailsDiff(OldObj)`와 `FDetailsDiff(NewObj)`의 `DetailsWidget()` 둘을 좌우로 둔 새 창(data-only 모드에서는 비활성, 대신 state.json의 전체 속성 표를 3열 그리드로). removed/added 위젯은 `stateRef`로 state.json 전체 속성을 한쪽 열에만 채운다.

### 9.5 미리보기 `SRadarPreviewPane`

```
SRadarPreviewPane : SDesignSurface
  └ SRadarZoomPan (ViewOffset/ZoomAmount 는 FRadarViewState 공유 속성)
      └ SDPIScaler (DPIScale = FWidgetBlueprintEditorUtils::GetWidgetPreviewDPIScale(위젯, 미리보기 해상도) — 확인함: 5.8 SDesignerView::GetPreviewDPIScale(SDesignerView.cpp:1163)이 이것을 부른다. 미리보기 해상도는 UWidgetDesignerSettings::DefaultPreviewResolution, 영역·크기는 GetWidgetPreviewAreaAndSize(WidgetBlueprintEditorUtils.cpp:2227))
          └ SBox (WidthOverride/HeightOverride = designSize)
              └ UserWidget->TakeWidget()
  └ SRadarOverlay (SOverlay 상단 레이어, 입력 통과)
```
- 생성: `RadarCompat::CreatePreviewWidget(World, BP, Flags)`; `Flags = Designing` 기본, ini `ExecutePreConstruct=true`면 `| ExecutePreConstruct`. `World`는 `FPreviewScene` 하나를 세션이 소유(`FPreviewScene::ConstructionValues()`), 두 미리보기가 같은 월드.
- 두 패널은 **같은 디자인 크기**(new 쪽 `DesignSizeMode/DesignTimeSize`)로 강제한다. old의 디자인 크기가 다르면 상단에 경고 띠.
- 모드: `SideBySide`(SSplitter 좌우, 패널 하나당 하나), `Overlay`(두 TakeWidget을 같은 SOverlay에, 위쪽 패널 `ColorAndOpacity` A=OverlayOpacity), `Flip`(한 패널만 보이고 Space로 교체).
- `TakeWidget()`은 WidgetTree 템플릿에 하지 않는다(goal 문서: `check(!RF_ArchetypeObject)`에 걸린다). 반드시 생성한 `UUserWidget` 인스턴스에.

### 9.6 오버레이 `SRadarOverlay::OnPaint`

매 프레임, 두 미리보기의 **살아 있는** SWidget 지오메트리에서 다시 계산한다. 캡처해 두지 않는다.

```
RootGeom(side) = UserWidget(side)->GetCachedWidget()->GetCachedGeometry()        // 그려진 뒤에만 유효
WidgetGeom(side, id) = FindWidget(side, id)->GetCachedWidget()->GetCachedGeometry()
LocalRect(side, id) = RootGeom.AbsoluteToLocal(WidgetGeom.GetAbsolutePosition()) … GetAbsoluteSize()/RootGeom.Scale
                       → root-local, pre-DPI 좌표. 저장은 이 좌표로만.
Paint: OverlayGeom 의 LocalToPaintGeometry(LocalRect * Zoom + ViewOffset) 로 상자를 그린다.

for entry in widgets (필터 통과):
  changed/renamed : NEW 측 LocalRect 외곽선 (SourceControl.Diff.Modified 색), old 측도 같은 상자(옵션)
  added           : NEW 측 외곽선 (Added 색)
  moved           : NEW 측 외곽선 (Moved 색) + OLD 측 옛 상자 → NEW 측에 점선 유령 상자, 화살표
  removed         : OLD 측에서 LocalRect(old, id) 와 가장 가까운 살아 있는 조상 A 의 LocalRect(old, A) 를 구해
                    offset = Rect(id) - Rect(A); size = Rect(id).size
                    NEW 측 Rect(new, A) + offset, size 로 빗금 점선 빨간 상자, 모서리 글리프, hover 라벨 "<DisplayLabel> 가 여기 있었음"
                    old 지오메트리가 없으면(Collapsed, 비활성 스위처 페이지, 스크롤 밖) → 조상 Rect 의 가장자리에 마커 칩
  dim unchanged   : 바뀌지 않은 위젯은 그리지 않고, bDimUnchanged 면 전체에 반투명 덮개 뒤 바뀐 상자 영역만 뚫는다 (FSlateDrawElement::MakeBox 네 조각)
선 스타일: 2px 외곽선 + 1px 어두운 테두리(halo). 색은 ini 색 표. 선택된 id 는 두껍게.
```
**스파이크 3 결과 (2026-10-18, WBP_Fix_Layout v1→v2):**
- **Collapsed 위젯은 미리보기에서 지오메트리가 있다.** 디자인 타임 위젯은 `Visibility` 를 무시하고 `GetVisibilityInDesigner()`(= `bHiddenInDesigner` 면 Collapsed, 아니면 Visible)를 쓴다(5.8 `UMG/Private/Components/Widget.cpp` 1309·1469~1473, `IsDesignTime()` 분기). 우리 미리보기는 `Designing` 플래그로 만들므로 디자이너와 똑같이 Collapsed 위젯(HiddenNote)이 그려지고, 지워지면 일반 빗금 상자가 나온다. 마커 칩으로 가는 경우는 `bHiddenInDesigner` 위젯뿐이다.
- **비활성 스위처 페이지의 위젯은 지오메트리가 없다**(그려지지 않아 `Debug_GetLastPaintFrame` 이 오래됨). 그 부모 페이지도 새 쪽에서 비활성이면 "가장 가까운 살아 있는 조상"도 그려지지 않는다 → 구현은 **그려진 조상이 나올 때까지 계속 올라가서**(PageBText → PageB(비활성) → Pages 스위처) 그 가장자리에 마커 칩 "✕ PageBText" 를 단다. 처음 구현은 첫 살아 있는 조상에서 멈춰 아무것도 그리지 않았다.

지오메트리가 "유효"한지는 `GetCachedGeometry().GetLocalSize()`가 0이 아니고 `SWidget::Debug_GetLastPaintFrame()`(5.8 `SWidget.h:830`, 5.1 `:809`, 두 버전 모두 있음)이 `GFrameNumber - 1` 이상인지로 판정한다.

### 9.7 탭

- 디자이너: 위 전부.
- 애니메이션: `animations[]`를 트리(애니메이션 → 트랙 → 섹션 → 채널 → 키)로. 키 행은 `frame | old | new`.
- 그래프: 그래프별 행(`logic` 수 / `layout` 수) + "Blueprint Diff에서 열기" → `SBlueprintDiff::CreateDiffWindow(Title, OldBP, NewBP, OldRev, NewRev)`. data-only 모드에서는 비활성.

### 9.8 data-only 모드

세션의 (b)가 없으면(CLI가 만든 diff.json을 "파일에서 열기" 메뉴로 열었거나, 임시 패키지 로드가 실패했을 때) 미리보기 자리에 안내 "이 에디터는 리비전 <sha>를 로드할 수 없어 미리보기가 없습니다. 생산자: <producer.name>"을 두고, 오버레이·전체 상세·Blueprint Diff 버튼을 비활성화한다. 그 외 모든 패널은 같다.

### 9.9 진입점

- Content Browser 우클릭(`ContentBrowser.AssetContextMenu.WidgetBlueprint` 확장) → "Radar UMG Diff..." → 리비전 피커 `SRadarRevisionPicker`. 위에 백엔드 탭(§8.0 순서로, 가능한 것만): **소스 컨트롤(<프로바이더 이름>)** 은 리비전·CL·날짜·사용자·설명 열, **git** 은 sha7·날짜·작성자·제목 열, **파일 두 개** 는 경로 입력 둘. 목록 맨 위에 항상 "작업 사본"과 HEAD/최신 체크인. 버튼 "이전과 비교"(한 번 클릭), "작업 사본과 비교", 두 항목 선택 뒤 "비교". 피커 아래 줄에 "이 리비전은 <백엔드>에서 받습니다"를 보여 어디서 오는지 숨기지 않는다.
- 엔진 기성 메뉴 "Source Control → Diff Against Depot / Previous"는 그대로 엔진 Blueprint Diff를 연다. 그 항목을 우리 뷰어로 바꿔 끼우는 것(5.8 `UAssetDefinition::PerformAssetDiff`, 5.1 `FAssetTypeActions_Blueprint::PerformAssetDiff` 대체)은 v1 범위 밖이고, 가능 여부 — 확인함: 같은 클래스의 에셋 정의는 먼저 등록된 것이 이기고 중복은 무시된다(5.8 `AssetDefinitionRegistry.cpp` 85~103, 정적 등록 정의만 예외). 플러그인이 바꿔 끼우려면 엔진 `UAssetDefinition_WidgetBlueprint` 를 `UnregisterAssetDefinition` 해야 해서 엔진 diff 경로 전체를 대체하게 된다 — v1 은 하지 않는다. 우리 입구는 우리 메뉴 항목이다.
- 메뉴 "diff.json 열기..." → data-only 모드.
- 커맨드릿(§10)은 UI 없이 같은 생산자를 돈다.

---

## 10. 커맨드릿과 CLI 액션

### 10.1 `-run=RadarUMGDiffDump`

```
UnrealEditor-Cmd.exe <Project.uproject> -run=RadarUMGDiffDump -asset=/Game/UI/WBP_HUD -old=<sha|HEAD> -new=<sha|working> -out=<dir> [-state]
```
`<dir>/diff.json`(+`-state`면 `old.state.json`, `new.state.json`). 종료 코드 0 성공, 2 로드 실패, 3 인자 오류. 클래스 이름은 `URadarUMGDiffDumpCommandlet`.

### 10.2 `-run=RadarStructKinds`

`UScriptStruct` 전체를 돌며 `{ "<StructName>": "tagged|native|immutable", ... }`와 `"engine": "5.1.1"`, `"fileVersionUE5": 1008`을 `Saved/RadarUMGDiff/struct_kinds.json`에 쓴다(`-out=` 로 바꿀 수 있음). M5 에서 `containers`(네이티브 클래스·구조체의 집합·맵 속성 → 원소 구조체 이름, 상속 포함)와 `conflicts` 를 더했다 — 옛 형식 집합·맵 태그에는 원소 구조체 이름이 없기 때문(§19 M5). 판정은 §11.6. 결과를 `Content/Python/radar_umgdiff/data/struct_kinds_<engine>.json`에 복사해 커밋한다. 5.1에서 PythonScriptPlugin이 `StructFlags`를 노출하지 않음을 확인했으므로(`Plugins/Experimental/PythonScriptPlugin/Source`에 `StructFlags`/`HasSerializer`/`UseNativeSerialization` 참조 없음) **커맨드릿이 유일한 길**이다.

### 10.3 에디터 안 CLI 액션

뷰어 메뉴 "Python 분석기로 다시 만들기"는 `Engine/Binaries/ThirdParty/Python3/Win64/python.exe -I <plugin>/Content/Python/umgdiff_cli.py diff <old.uasset> <new.uasset> -o <dir>`를 `FPlatformProcess::ExecProcess`로 띄우고 결과 diff.json을 data-only 세션으로 연다. `IPythonScriptPlugin::ExecPythonCommandEx`는 쓰지 않는다(goal 문서 결정).

---

## 11. Python 분석기

### 11.1 원칙

- 리틀 엔디언. 모든 읽기는 `reader.py`의 `Reader` 클래스(`pos`, `i8/u8/i16/u16/i32/u32/i64/u64/f32/f64`, `fstring()`, `fname()`, `fguid()`, `bytes(n)`).
- `FString`: int32 길이 L. L>0 → L바이트 ANSI(끝 NUL 포함), L<0 → `-L`개의 UTF-16LE 코드 유닛(끝 NUL 포함), L==0 → 빈 문자열. 디코드 뒤 끝 NUL을 뗀다.
- `FName`(디스크): int32 index + int32 number. number 0 = 번호 없음, n>0 → 표시 이름 `Name_{n-1}`.
- `FGuid`: u32 A,B,C,D → 엔진 문자열 표기 `%08X%08X%08X%08X`.
- **거부 규칙**: `FileVersionUE5`가 지원 범위(브랜치별 선언) 밖이거나, 커스텀 버전 GUID가 우리가 아는 표에 없고 그 GUID로 분기하는 리더가 필요해지면 명확한 메시지로 종료(코드 4). 모르는 GUID라도 쓰이지 않으면 경고만.
- 커버리지: `understood / sum(SerialSize of all exports)`. 태그 헤더·None·접두·접미 바이트는 understood. 구조체 몸체는 리더가 있을 때만 understood, 없으면 `raw` + unparsed.

### 11.2 지원 버전 선언

| 브랜치 | FileVersionUE5 | 근거 |
|---|---|---|
| ue5.8 | 1018 (CL9 저장본). 1012~1018을 받는다 | `ObjectVersion.h` 5.8: 1000 INITIAL … 1017 OS_SUB_OBJECT_SHADOW_SERIALIZATION, 1018 IMPORT_TYPE_HIERARCHIES(요약 필드 분기에서 확인). WBP_HUD 헤더가 1018 |
| ue5.1 | **1008** (`ADD_SOFTOBJECTPATH_LIST`가 5.1 `ObjectVersion.h`의 마지막 값). 1004~1011을 받는다 | `C:\Program Files\Epic Games\UE_5.1\Engine\Source\Runtime\Core\Public\UObject\ObjectVersion.h` |

Python 코드는 한 벌이고 두 범위를 다 받는다. 브랜치별 "선언"은 `data/struct_kinds_<engine>.json`의 존재로 결정된다: 1012 미만 파일을 읽는데 해당 표가 없으면 거부(코드 4) — 옛 태그는 표 없이 구조체 몸체를 해석할 수 없기 때문(§11.6).

### 11.3 패키지 요약 `summary.py`

`Runtime/CoreUObject/Private/UObject/PackageFileSummary.cpp`(5.8) 순서. 5.1은 같은 순서에서 게이트 밖 필드가 빠진다. 앞 세션이 WBP_HUD(1018)로 이 순서를 끝까지 파싱해 `TotalHeaderSize 37583 == summary 끝 514 + 이름표…`가 맞음을 확인했다(아래 "검증값").

| # | 필드 | 형 | 게이트 | 5.1(1008) | 5.8(1018) |
|---|---|---|---|---|---|
| 1 | Tag | u32 | `0x9E2A83C1` | ○ | ○ |
| 2 | LegacyFileVersion | i32 | `<= -8`이면 FileVersionUE5 존재(두 버전 같은 조건) | **-8** (`PackageFileSummary.cpp:103`) | -9 (`:63`) |
| 3 | LegacyUE3Version | i32 | `!= -4`일 때 | ○ | ○ (864) |
| 4 | FileVersionUE4 | i32 | | 522 | 522 |
| 5 | FileVersionUE5 | i32 | Legacy `<= -8` | 1008 | 1018 |
| 6 | FileVersionLicenseeUE | i32 | | ○ | ○ |
| 7 | SavedHash | 20바이트 FIoHash | `>= 1016 PACKAGE_SAVED_HASH` | × | ○ |
| 8 | TotalHeaderSize | i32 | `>= 1016`이면 여기 | × | ○ |
| 9 | CustomVersions | §11.4 컨테이너 | Legacy `<= -2`, 형식은 Legacy로 결정(-9 → Optimized) | ○ | ○ |
| 10 | TotalHeaderSize | i32 | `< 1016`이면 여기 | ○ | × |
| 11 | PackageName(옛 FolderName) | FString | | ○ | ○ |
| 12 | PackageFlags | u32 | | ○ | ○ |
| 13 | NameCount, NameOffset | i32×2 | | ○ | ○ |
| 14 | SoftObjectPathsCount, Offset | i32×2 | `>= 1008 ADD_SOFTOBJECTPATH_LIST` | ○ | ○ |
| 15 | LocalizationId | FString | `>= VER_UE4_ADDED_PACKAGE_SUMMARY_LOCALIZATION_ID` 그리고 FilterEditorOnly 아님 | ○ | ○ |
| 16 | GatherableTextDataCount, Offset | i32×2 | `>= VER_UE4_SERIALIZE_TEXT_IN_PACKAGES` | ○ | ○ |
| 17 | ExportCount, ExportOffset, ImportCount, ImportOffset | i32×4 | | ○ | ○ |
| 18 | CellExportCount/Offset, CellImportCount/Offset | i32×4 | `>= 1015 VERSE_CELLS` | × | ○ |
| 19 | MetaDataOffset | i32 | `>= 1014 METADATA_SERIALIZATION_OFFSET` | × | ○ |
| 20 | DependsOffset | i32 | | ○ | ○ |
| 21 | SoftPackageReferencesCount, Offset | i32×2 | `>= VER_UE4_ADD_STRING_ASSET_REFERENCES_MAP` | ○ | ○ |
| 22 | SearchableNamesOffset | i32 | `>= VER_UE4_ADDED_SEARCHABLE_NAMES` | ○ | ○ |
| 23 | ThumbnailTableOffset | i32 | | ○ | ○ |
| 24 | ImportTypeHierarchiesCount, Offset | i32×2 | `>= 1018 IMPORT_TYPE_HIERARCHIES` | × | ○ |
| 25 | Guid | FGuid | `< 1016`이면 여기 | ○ | × |
| 26 | PersistentGuid | FGuid | `>= VER_UE4_ADDED_PACKAGE_OWNER`, FilterEditorOnly 아님 | ○ | ○ |
| 27 | OwnerPersistentGuid | FGuid | `>= ADDED_PACKAGE_OWNER && < NON_OUTER_PACKAGE_IMPORT` | × | × |
| 28 | GenerationCount + (ExportCount, NameCount)×N | i32 | | ○ | ○ |
| 29 | SavedByEngineVersion | u16 major, u16 minor, u16 patch, u32 changelist, FString branch | | ○ | ○ |
| 30 | CompatibleWithEngineVersion | 같은 형 | | ○ | ○ |
| 31 | CompressionFlags | u32 | | ○ | ○ |
| 32 | CompressedChunks | i32 (0이어야 함, 아니면 거부) | | ○ | ○ |
| 33 | PackageSource | u32 | | ○ | ○ |
| 34 | AdditionalPackagesToCook | i32 수 + FString×N | | ○ | ○ |
| 34b | NumTextureAllocations | i32 | `LegacyFileVersion > -7`일 때만 | × | × |
| 35 | AssetRegistryDataOffset | i32 | | ○ | ○ |
| 36 | BulkDataStartOffset | i64 | | ○ | ○ |
| 37 | WorldTileInfoDataOffset | i32 | | ○ | ○ |
| 38 | ChunkIDs | i32 수 + i32×N | | ○ | ○ |
| 39 | PreloadDependencyCount, PreloadDependencyOffset | i32×2 | | ○ | ○ |
| 40 | NamesReferencedFromExportDataCount | i32 | `>= 1001` | ○ | ○ |
| 41 | PayloadTocOffset | i64 | `>= 1002 PAYLOAD_TOC` | ○ | ○ |
| 42 | DataResourceOffset | i32 | `>= 1009 DATA_RESOURCES` | × | ○ |

29~42의 게이트 중 UE4 상수(`VER_UE4_*`)는 모두 522보다 작으므로 두 버전에서 항상 참이다. 5.1 `PackageFileSummary.cpp`의 28~41 순서가 5.8과 같음을 확인했다(GenerationCount → SavedByEngineVersion → CompatibleWithEngineVersion → CompressionFlags → CompressedChunks → PackageSource → AdditionalPackagesToCook → [NumTextureAllocations] → AssetRegistryDataOffset → BulkDataStartOffset → WorldTileInfoDataOffset → ChunkIDs → PreloadDependencyCount/Offset → NamesReferencedFromExportDataCount → PayloadTocOffset; 5.1에는 42 DataResourceOffset이 없다). 검증값(WBP_HUD, 1018): 요약 끝 514 = NameOffset, 이름 218개 끝 5204 = SoftObjectPathsOffset, 임포트 41개 8298~9938, 익스포트 139개 9938~25506 = DependsOffset, TotalHeaderSize 37583 = 첫 익스포트 SerialOffset.

### 11.4 표들 `tables.py`

- 이름표(`FNameEntrySerialized`, `Runtime/Core/Private/UObject/UnrealNames.cpp`): FString(§11.1 규칙 그대로, 음수면 UTF-16) + `u16 NonCasePreservingHash` + `u16 CasePreservingHash`(`>= VER_UE4_NAME_HASHES_SERIALIZED`, 두 버전 모두 있음). 해시는 읽고 버린다.
- 커스텀 버전 컨테이너(Optimized 형식): i32 수 + `{FGuid Key, i32 Version}`×N (`Runtime/Core/Private/Serialization/CustomVersion.cpp`). GUID→이름 표를 `data/custom_versions.json`에 둔다(FEditorObjectVersion, FFortniteMainBranchObjectVersion, FSequencerObjectVersion, FBlueprintsObjectVersion, FFrameworkObjectVersion, FUE5MainStreamObjectVersion, FUE5ReleaseStreamObjectVersion 등. GUID 값은 각 `*ObjectVersion.cpp`에서 복사).
- 소프트 경로 목록(`>= 1008`): `SoftObjectPathsOffset`에서 `FSoftObjectPath`×Count, 각각 `FTopLevelAssetPath{FName PackageName, FName AssetName}` + `FString SubPath`(§11.8).
- 임포트(`ObjectResource.cpp` operator<< FObjectImport): `FName ClassPackage, FName ClassName, i32 OuterIndex, FName ObjectName`, `FName PackageName`(`>= VER_UE4_NON_OUTER_PACKAGE_IMPORT`, 두 버전 ○), `i32 bImportOptional`(`>= 1003 OPTIONAL_RESOURCES`, 두 버전 ○). 1개 = 7×4+… = 앞 세션 검증 40바이트/개(41개가 1640바이트).
- 익스포트(operator<< FObjectExport):

| 필드 | 형 | 게이트 | 5.1 | 5.8 |
|---|---|---|---|---|
| ClassIndex, SuperIndex | i32×2 | | ○ | ○ |
| TemplateIndex | i32 | `>= VER_UE4_TemplateIndex_IN_COOKED_EXPORTS` | ○ | ○ |
| OuterIndex | i32 | | ○ | ○ |
| ObjectName | FName | | ○ | ○ |
| ObjectFlags | u32 | | ○ | ○ |
| SerialSize, SerialOffset | i64×2 | `>= VER_UE4_64BIT_EXPORTMAP_SERIALSIZES` | ○ | ○ |
| bForcedExport, bNotForClient, bNotForServer | i32×3 (bool을 i32로) | | ○ | ○ |
| PackageGuid | FGuid | `< 1005 REMOVE_OBJECT_EXPORT_PACKAGE_GUID` | × | × |
| bIsInheritedInstance | i32 | `>= 1006 TRACK_OBJECT_EXPORT_IS_INHERITED` | ○ | ○ |
| PackageFlags | u32 | | ○ | ○ |
| bNotAlwaysLoadedForEditorGame | i32 | `>= VER_UE4_LOAD_FOR_EDITOR_GAME` | ○ | ○ |
| bIsAsset | i32 | `>= VER_UE4_COOKED_ASSETS_IN_EDITOR_SUPPORT` | ○ | ○ |
| bGeneratePublicHash | i32 | `>= 1003 OPTIONAL_RESOURCES` | ○ | ○ |
| FirstExportDependency, SerializationBeforeSerializationDependencies, CreateBeforeSerializationDependencies, SerializationBeforeCreateDependencies, CreateBeforeCreateDependencies | i32×5 | `>= VER_UE4_PRELOAD_DEPENDENCIES_IN_COOKED_EXPORTS` | ○ | ○ |
| ScriptSerializationStartOffset, ScriptSerializationEndOffset | i64×2 | `>= 1010 SCRIPT_SERIALIZATION_OFFSET` 그리고 unversioned 아님 | **×** | ○ |

패키지 인덱스 규약: `0` = null, `>0` = 익스포트 `[i-1]`, `<0` = 임포트 `[-i-1]`.
ScriptSerialization 오프셋의 의미(5.8 `LinkerSave.cpp:258,267`): 익스포트 `SerialOffset` 기준 상대 오프셋으로, `SerializeScriptProperties` 직전과 직후의 `Tell()`. WBP_HUD 검증값: ItemLabelBox size 436, start 0, end 432 → 태그 스트림이 몸체 처음부터 432바이트, 뒤 4바이트는 §11.4 접미(bool false).

### 11.5 익스포트 몸체 모델 `exports.py`

```
UObject 루트 익스포트 몸체 = [prefix] [tags … None] [suffix]
  prefix : >= 1011 : u8 SerializationControlExtensions (UStruct::SerializeVersionedTaggedProperties, bIsUClass 일 때만 — Class.cpp 5.8 1587~1600)
                      0x00 NoExtension, 0x02 OverridableSerializationInformation (이면 뒤에 u8 OverridableOperation — 5.8 `Class.cpp` 1521~1529 로 확인. CL9 37 블롭·픽스처 21개에서 0x00 만 관측, 다른 값이면 분석기가 거부한다)
           <  1011 : 없음
  tags   : §11.6 형식, FName == "None" 에서 끝
  suffix : PossiblySerializeObjectGuid (LazyObjectPtr.cpp): TryEnterField 가 바이너리에서 i32 bool 을 쓰고, true 면 FGuid 16 바이트.
           → 항상 4 바이트 + (16 바이트 선택). CDO 포함 모든 UObject (ObjClass != UClass).
           그다음 클래스별 접미부 §11.9
구조체 몸체(태그 안의 StructProperty 값) = [tags … None]  (prefix·suffix 없음)
```
5.1에서는 UObject::Serialize(5.1 `Obj.cpp` 1400~1480)의 링커 로드 경로가 이름·아우터·클래스를 쓰지 않으므로(`!IsLoading && !IsSaving` 분기만 쓴다) 몸체는 태그로 바로 시작한다. 1010 미만이라 Script 오프셋이 없어도 **시작 = 0, 끝 = None 직후**로 파서가 직접 정한다(`derivedScriptStart/End`).

WBP_HUD(1018) 실측 두 건(앞 세션 스파이크, 이 문서 §11.6 설명의 근거):
```
ItemLabelBox (Border, size 436):
  00                                  prefix NoExtension
  7e000000 00000000                   FName(126, 0)  = 태그 이름
  9e000000 00000000 01000000          type node { FName(158,0), InnerCount 1 }   ← StructProperty
  68000000 00000000 01000000          type node { FName(104,0), InnerCount 1 }   ← 구조체 이름
  04000000 00000000 00000000          type node { FName(4,0)="/Script/SlateCore", InnerCount 0 } ← 패키지
  7c000000                            Size 124
  00                                  Flags 0 (태그 스트림 구조체, 네이티브 아님)
  65000000 00000000 …                 값: 구조체 안 첫 태그 FName(101,0) …
  … 마지막 8 바이트 00000000 00000000 의 앞 4 = None 의 number, 뒤 4 = suffix bool false
BorderSlot (size 71, scriptEnd 67):
  00 | 7f000000 00000000 | 79000000 00000000 00000000 | 04000000 | 00 | 01000000      ← 태그1: 4바이트 값(오브젝트 인덱스 1)
     | 28000000 00000000 | 79000000 00000000 00000000 | 04000000 | 00 | 7d000000      ← 태그2
     | 78000000 00000000                                                               ← None (names[120])
     | 00000000                                                                        ← suffix bool false
  1 + 2×(8+12+4+1+4) + 8 = 67 ✓
```

### 11.6 태그 리더 `tags.py` — 두 형식

**A. 완전 타입 이름 형식 (`FileVersionUE5 >= 1012`)** — `PropertyTag.cpp` 5.8 420~530줄
```
FName Name                       (None 이면 끝)
FPropertyTypeName TypeName       노드 목록: 각 노드 = FName(8) + i32 InnerCount. 첫 노드부터 Remaining=1 로 시작, 노드마다 Remaining += InnerCount-1, 0 이 되면 끝
i32  Size
u8   Flags   0x01 HasArrayIndex  0x02 HasPropertyGuid  0x04 HasPropertyExtensions  0x08 HasBinaryOrNativeSerialize  0x10 BoolTrue  0x20 SkippedSerialize
[i32 ArrayIndex]        if 0x01
[FGuid PropertyGuid]    if 0x02
[u8 PropertyExtensions  if 0x04 ;  0x02 OverridableInformation 이면 u8 OverriddenPropertyOperation + u8(bool) ExperimentalOverridableLogic ; 0x04 HasExternalsObjects 면 u8(bool)]  (확인함: 5.8 `PropertyTag.cpp` 44~56 `EPropertyTagExtension` — 0x01 은 ReserveForFutureUse. CL9 37 블롭에서 태그 플래그 0x04(HasPropertyExtensions) 0개 = 스파이크 4)
값 Size 바이트
```
타입 트리 해석: 첫 노드 = 속성 클래스 이름(`StructProperty`, `ArrayProperty`, `BoolProperty`, `ObjectProperty`, `TextProperty`, `EnumProperty`, `MapProperty`…). `StructProperty`의 자식 1개 = 구조체 이름(그 자식 = 패키지 이름, 선택적으로 다음 노드가 GUID). `ArrayProperty/SetProperty/OptionalProperty`의 자식 1개 = 내부 타입(재귀). `MapProperty` 자식 2개 = 키·값. `ByteProperty/EnumProperty` 자식 = enum 이름(+패키지). **구조체 몸체가 태그 스트림인지 네이티브인지는 Flags 0x08로 안다.** 0x08이면 §11.7 네이티브 리더, 없으면 §11.5의 구조체 몸체(재귀 태그). `BoolProperty`는 값 바이트가 없고 0x10이 값이다(Size 0). `SkippedSerialize`(0x20)는 값이 없다.

**B. 옛 형식 (`< 1012`)** — 5.8 `PropertyTag.cpp` 205~365줄 `LoadPropertyTagNoFullType`, 5.1 `PropertyTag.cpp` 94~125줄 (같은 레이아웃)
```
FName Name                       (None 이면 끝)
FName Type                       속성 클래스 이름
i32  Size
i32  ArrayIndex
타입별:
  StructProperty : FName StructName ; FGuid StructGuid (>= VER_UE4_STRUCT_GUID_IN_PROPERTY_TAG, 두 버전 ○)
  BoolProperty   : u8 BoolVal            (값 바이트 없음, Size 0)
  ByteProperty   : FName EnumName        (None 이면 u8 값, 아니면 FName 값)
  EnumProperty   : FName EnumName
  ArrayProperty  : FName InnerType       (>= VAR_UE4_ARRAY_PROPERTY_INNER_TAGS)
  SetProperty    : FName InnerType       (>= VER_UE4_PROPERTY_TAG_SET_MAP_SUPPORT)
  MapProperty    : FName InnerType, FName ValueType
u8   HasPropertyGuid ; [FGuid PropertyGuid] if 1     (>= VER_UE4_PROPERTY_GUID_IN_PROPERTY_TAG)
값 Size 바이트
```
없는 것: Flags 바이트, 네이티브 여부. 배열 값의 레이아웃(`FArrayProperty::SerializeItem`, 5.1 `PropertyArray.cpp` 68·121~126, 5.8 `:638~645`의 `< 1012` 분기): `i32 count`, 그다음 원소가 StructProperty이고 `>= VER_UE4_INNER_ARRAY_TAG_INFO`(두 버전 ○)이면 **B 형식 FPropertyTag 하나(InnerTag: Name=배열 속성 이름, Type=StructProperty, Size=0 또는 전체, ArrayIndex 0, StructName, StructGuid, HasPropertyGuid)**, 그다음 원소 몸체 count개. 원소가 구조체가 아니면 count 뒤에 바로 원소들. (A 형식에서는 내부 태그가 없고 타입 트리가 원소 타입을 담는다.) 옛 형식에는 네이티브 여부가 없으므로 **구조체 몸체 해석에 §11.6 표가 필요**하다.

### 11.6 구조체 종류 `structkinds.py` + `data/struct_kinds_<engine>.json`

옛 형식에서 StructName을 보고 몸체가 태그 스트림인지 네이티브 바이트인지 정한다. 엔진 판정(5.1 `Class.cpp` 2735~2761, 5.8도 같은 함수):
```
UseBinarySerialization(Ar) = Ar.WantBinaryPropertySerialization() || (StructFlags & STRUCT_Immutable)     // 패키지 저장은 WantBinary 가 false
UseNativeSerialization()   = (StructFlags & STRUCT_SerializeNative)                                          // CppStructOps 가 Serialize 를 가짐
둘 중 하나면 네이티브(바이트), 아니면 태그 스트림
```
표 형식:
```json
{ "engine": "5.1.1", "fileVersionUE5": 1008, "generatedBy": "RadarStructKinds commandlet",
  "kinds": { "Vector2D": "immutable", "Margin": "tagged", "SlateBrush": "tagged", "Text": "native", "SoftObjectPath": "native", "FrameNumber": "immutable", "...": "..." } }
```
`immutable`과 `native`는 파서에게 같은 뜻(바이트)이고 리더는 §11.7 표에서 StructName으로 찾는다. 리더가 없으면 Size만큼 건너뛰고 `raw`. 새 형식(≥1012)에서도 이 표를 **검증용**으로 쓴다: Flags 0x08과 표가 어긋나면 경고(엔진이 바뀐 것).
이 표는 goal 문서의 "스키마 덤프"가 아니다(기본값을 담지 않는다). 그래서 "스키마 유무와 판정이 같다"는 규칙은 그대로다.

### 11.7 네이티브 구조체 리더 `natives.py`

StructName → 리더. 레이아웃은 `NoExportTypes.h`(두 엔진 모두 `Runtime/CoreUObject/Public/UObject/NoExportTypes.h`)와 각 구조체의 `Serialize`/`operator<<`. LWC(≥1004)는 두 버전 모두 적용되므로 벡터류는 double.

| StructName | 바이트 | JSON | 비고 |
|---|---|---|---|
| Vector2D | f64×2 | `{X,Y}` | |
| Vector | f64×3 | `{X,Y,Z}` | |
| Vector4, Quat, Plane | f64×4 | `{X,Y,Z,W}` | |
| Rotator | f64×3 | `{Pitch,Yaw,Roll}` | |
| Transform | **태그 스트림** (Rotation·Translation·Scale3D 멤버, 각 멤버는 Quat/Vector 네이티브) | `{Rotation,Translation,Scale3D}` | 확인함: `TTransformStructOpsTypeTraits` 의 `WithSerializer` 가 주석 처리돼 있다(5.8 `CoreUObject/Private/UObject/Property.cpp` 485~495) → 리더 불필요 |
| IntPoint, IntVector | i32×2 / ×3 | `{X,Y}` | |
| Color | u8 B,G,R,A | `{R,G,B,A}` + `hex` | 저장 순서가 BGRA |
| LinearColor | f32×4 | `{R,G,B,A}` | |
| Guid | u32×4 | 문자열 | |
| Box2D | Vector2D Min, Max, u8 bIsValid | | |
| DateTime, Timespan | i64 | ISO 문자열 / 초 | |
| FrameNumber | i32 | 수 | |
| FrameRate | i32 Numerator, Denominator | `{n,d}` | |
| FrameTime | FrameNumber + f32 SubFrame | | |
| Text | §11.8 | `{source,namespace,key,table,tableKey,historyType}` | |
| SoftObjectPath, SoftClassPath | §11.8 | 문자열 경로 | |
| FieldPath | `TArray<FName>` Path(안쪽 필드가 0번) + 소유 UStruct FPackageIndex — 확인함: 5.8 `FieldPath.cpp` 325~357 | `"<소유자>:<바깥>.….<필드>"`, 빈 경로 `""` | M4 구현 |
| ScriptDelegate | i32 PackageIndex Object + FName FunctionName | | |
| MulticastScriptDelegate | i32 수 + ScriptDelegate×N | | |
| EdGraphPinType | `FEdGraphPinType::Serialize`(5.8 `EdGraphPin.cpp:163`, 5.1 `:156`) | UPROPERTY 멤버 이름 그대로의 구조 | M4 구현 (`graphs.py`) |
| UserPinInfo | `operator<<(FArchive&, FUserPinInfo&)`(5.8 `K2Node_EditablePinBase.cpp:80`, 5.1 `:78`) | `{PinName, PinType, DesiredPinDirection, PinDefaultValue}` | M4 구현 |
| MovieSceneFloatChannel / DoubleChannel / TangentData / FloatValue / DoubleValue | `MovieSceneFloatChannel.cpp` 5.8 `:456`(impl 호출), 5.1 `:197`; `MovieSceneDoubleChannel.cpp` 5.8 `:760`, 5.1 `:217`. FSequencerObjectVersion 게이트 | 키 배열 `{frame,value,interp,tangents}` | M4. 이 구조체들은 `SerializeFromMismatchedTag`도 가지므로 태그의 구조체 이름이 Float↔Double로 다를 수 있다 |

위 표에 없는 네이티브 구조체 → `raw`. 디자이너 속성에서 자주 나오는 `Margin, Anchors, SlateBrush, SlateColor, SlateFontInfo, ButtonStyle, TextBlockStyle, WidgetTransform, SlateChildSize, AnchorData`는 **태그 스트림**이라 리더가 필요 없다(5.1 표로 확정, 5.8은 Flags로 확정).

### 11.8 FText와 소프트 경로 `text.py`, `softpath.py`

FText(`Runtime/Core/Private/Internationalization/Text.cpp` `FText::SerializeText`, 두 버전 같은 구조):
```
u32 Flags
i8  HistoryType          (>= VER_UE4_FTEXT_HISTORY, 두 버전 ○)   -1 None, 0 Base, 1 NamedFormat, 2 OrderedFormat, 3 ArgumentFormat, 4 AsNumber, 5 AsPercent, 6 AsCurrency, 7 AsDate, 8 AsTime, 9 AsDateTime, 10 Transform, 11 StringTableEntry, 12 TextGenerator  (TextHistory.h)
None (-1)         : u8(bool) bHasCultureInvariantString ; [FString CultureInvariantString]
Base (0)          : FString Namespace, FString Key, FString SourceString                       (FTextHistory_Base::Serialize, TextHistory.cpp)
                    ; FFortniteMainBranchObjectVersion >= AddDevNotesToFText 이고 에디터 데이터 포함이면 FString DevNotes.  5.1 엔진에는 AddDevNotesToFText 값 자체가 없으므로 5.1 저장본에는 절대 없다. 5.8 저장본은 헤더의 FFortniteMainBranch 커스텀 버전 값으로 판정(CL9 WBP_HUD 에서 M3 때 실측해 적는다)
StringTableEntry  : FName TableId, FString Key                                                   (FTextHistory_StringTableEntry::Serialize)
Transform         : FText SourceText(재귀), u8 TransformType
NamedFormat/OrderedFormat/ArgumentFormat/AsNumber…: 포맷 소스 + 인자 배열 — M4 구현(`text.py`, §19 M4). TextGenerator 만 raw
```
`FTextKey::SerializeAsString`은 FString 하나다. 비교 키는 SourceString(+TableId/Key).

FSoftObjectPath(`SoftObjectPath.cpp` `SerializePathWithoutFixup`):
```
>= 1007 FSOFTOBJECTPATH_REMOVE_ASSET_PATH_FNAMES (두 버전 ○):
    FTopLevelAssetPath { FName PackageName, FName AssetName }   (TopLevelAssetPath.h:130)
    SubPathString: FFortniteMainBranchObjectVersion < SoftObjectPathUtf8SubPaths 면 FString(와이드 규칙), 그 이상이면 UTF-8 FString (확인함: 5.1 엔진에는 `SoftObjectPathUtf8SubPaths` 멤버가 없다 — `data/version_gates.json` 의 missingIn 5.1 → 5.1 저장본은 FString 규칙)
단, 요약의 SoftObjectPathsCount > 0 이면 속성 값 안의 FSoftObjectPath 는 **i32 목록 인덱스 하나**다 (`FLinkerLoad::operator<<(FSoftObjectPath&)`: `SoftObjectPathList.Num() > 0` 이면 인덱스를 읽는다. 5.1·5.8 같은 코드). 목록 자체(§11.4)는 위 전체 레이아웃으로 읽는다. WBP_HUD 요약은 SoftObjectPathsCount 9
```

### 11.9 클래스별 접미부 `classes.py`

익스포트의 클래스(임포트 체인으로 네이티브 클래스 이름을 얻는다)에 따라 suffix bool(+guid) 뒤에 더 읽는다.

| 클래스 | 접미부 | 근거 |
|---|---|---|
| (공통) 모든 UObject | i32 bool bHasGuid [+ FGuid] — §11.5 suffix | `LazyObjectPtr.cpp` |
| UWidget 파생, UPanelSlot, UWidgetAnimation, UMovieScene | 없음 | 5.1·5.8 모두 `Widget.cpp`, `PanelSlot.cpp`, `Animation/WidgetAnimation.cpp`, `MovieScene.cpp`에 `::Serialize(` 재정의 없음. `Runtime/UMG/Private/UserWidget.cpp`, `WidgetTree.cpp`도 두 버전 모두 0건 |
| UBlueprintCore(= UWidgetBlueprint 조상) | **i32 FPackageIndex GeneratedClass** 하나(저장 시 `bSerializeGeneratedClass`가 항상 참이라 늘 쓴다). 그 앞의 옛 분기(`FFrameworkObjectVersion < BlueprintGeneratedClassIsAlwaysAuthoritative`면 u8 bool, `UEVer < VER_UE4_BLUEPRINT_SKEL_CLASS_TRANSIENT_AGAIN`이면 SkeletonGeneratedClass 인덱스)는 5.1·5.8 저장본에 해당 없음 | 5.1 `Blueprint.cpp:276`(34~47줄), 5.8 같은 함수 — 두 버전 같은 코드 |
| UWidgetBlueprint | 커스텀 버전 등록만, 바이트 없음 | 5.1 `WidgetBlueprint.cpp` |
| UEdGraphNode | `FBlueprintsObjectVersion >= EdGraphPinOptimized`면 `UEdGraphPin::SerializeAsOwningNode` 핀 배열 | 5.1 `EdGraphNode.cpp`, `EdGraphPin.cpp:853`; 5.8 `:993` |
| UK2Node_EditablePinBase | `TArray<FUserPinInfo>` | `K2Node_EditablePinBase.cpp` 5.1 `:259` 근처 `Ar << SerializedItems` |
| UMovieSceneSection | 커스텀 버전 등록 + `FSequencerObjectVersion < FloatToIntConversion` 옛 업그레이드만. 현재 저장본에 추가 바이트 없음 | 5.1·5.8 `MovieSceneSection.cpp` UMovieSceneSection::Serialize |
| UMetaData (FileVersionUE5 < 1014 에서만 익스포트) | `TMap<FWeakObjectPtr(i32 패키지 인덱스), TMap<FName, FString>>` ObjectMetaDataMap + `FEditorObjectVersion >= RootMetaDataSupport` 이면 `TMap<FName, FString>` RootMetaDataMap — M5 구현 | 5.1 `CoreUObject/Private/UObject/MetaData.cpp` UMetaData::Serialize |

### 11.10 상태 덤프·diff·일치 `state.py`, `diff.py`, `conform.py`

- `state.py`: 익스포트 전체를 §6 형으로. 위젯 트리는 익스포트 중 클래스가 `WidgetTree`인 것의 자식(Outer가 그 익스포트인 UWidget들)과 각 패널의 `Slots` 배열(태그 값에서 오브젝트 인덱스)로 부모·인덱스를 복원한다.
- `diff.py`: 두 상태 → §5. §7과 **같은 규칙**(식별자, 종류, 추정, 허용 오차). 규칙을 두 번 쓰지 않도록 `diff_rules.md`를 §7에서 링크하고 C++와 Python이 같은 상수를 ini/JSON에서 읽는다(`RenameThreshold`, `FloatTolerance`, `IgnoredProperties`는 `Config/DefaultRadarUMGDiff.ini`를 Python도 파싱).
- `conform.py`: 네이티브 diff.json과 Python diff.json을 받아 `widgets/slots/cdo/bindings`의 `(id, kind)` 집합과 `properties[].(path, kind, old.value, new.value)`를 비교. `textKeys`와 `text`는 제외. 불일치는 표로 출력, 종료 코드 1.
- `history`: `git log`로 sha 목록을 받아 모든 쌍(인접) 또는 전체 블롭 덤프를 돈다(정확도 모드). CL9 현재 블롭 수: `WBP_*.uasset` 15개, 전 리비전 블롭 36개(2026-10-17 `git rev-list --all --objects`로 셈. goal 문서의 34는 그날 수치).

### 11.11 CLI `umgdiff_cli.py`

```
umgdiff_cli.py dump    <uasset> [-o out.json] [--struct-kinds data/struct_kinds_ue5.1.json] [--schema schema.json]
umgdiff_cli.py diff    <old.uasset> <new.uasset> -o <dir>            → diff.json, old.state.json, new.state.json
umgdiff_cli.py conform <native-diff.json> <python-diff.json>
umgdiff_cli.py history <repo-relpath> --all -o <dir>                 → 블롭마다 dump, 인접 쌍마다 diff
umgdiff_cli.py summary <diff.json>                                   → 요약 띠 텍스트
```
종료 코드 0 성공, 1 불일치(conform), 3 인자, 4 거부(버전/커스텀 버전/표 없음).

---

## 12. 설정 `Config/DefaultRadarUMGDiff.ini`

`URadarUMGDiffSettings : UDeveloperSettings`(섹션 `[/Script/RadarUMGDiffEditor.RadarUMGDiffSettings]`). Python도 같은 파일을 `configparser`로 읽는다(C++ ini 문법과 호환되는 키만: `Key=Value`, 배열은 `+Key=` 반복).

| 키 | 기본 | 설명 |
|---|---|---|
| `ColorAdded` | `SourceControl.Diff.AddedStyle` 색 | 추가 외곽선 |
| `ColorRemoved` | 빨강 | 삭제 상자 |
| `ColorChanged` | 노랑 | |
| `ColorMoved` | 파랑 | |
| `ColorSelected` | 흰색 | 선택 강조 |
| `OutlineThickness` | 2.0 | px, pre-DPI |
| `HaloThickness` | 1.0 | |
| `RemovedBoxDashLength` | 6.0 | |
| `DimUnchangedOpacity` | 0.55 | |
| `OverlayOpacity` | 0.5 | 겹치기 모드 |
| `DefaultPreviewMode` | SideBySide | |
| `bExecutePreConstruct` | false | 미리보기 플래그 (ini 키는 UPROPERTY 이름 그대로라 `b` 접두가 붙는다. 다른 bool 도 같다: `bKeepTempPackages`, `bChangedOnlyDefault`, `bDimUnchangedDefault`) |
| `RenameThreshold` | 0.8 | §7.3 |
| `FloatTolerance` | 1e-6 | §5 |
| `DoubleTolerance` | 1e-12 | |
| `+IgnoredProperties` | `WidgetGeneratedBy`, `bCreatedByConstructionScript` | §7.2. 비교에서 아예 뺀다 |
| `+AutoProperties` | `DesignerFlags`, `bIsVariable` | 사용자 결정 2026-10-18 "둘 다 기본으로 지원, 요청에 따라 적용": 생산자는 빼지 않고 속성 항목에 `"auto": true` 를 단다. 뷰어 "자동 값 숨기기" 토글, CLI `--hide-auto` 로 가린다 |
| `bHideAutoDefault` | true | 뷰어 토글의 처음 값 |
| `GitExe` | `git` | §8.2. PATH 의 git 또는 절대 경로 |
| `TempDir` | `Saved/Diff` | |
| `KeepTempPackages` | false | |
| `NextKey`, `PrevKey` | F8, F7 | |
| `FlipKey` | SpaceBar | |
| `ChangedOnlyDefault`, `DimUnchangedDefault` | true, true | |
| `PythonExe` | `Binaries/ThirdParty/Python3/Win64/python.exe` | §10.3. 엔진 폴더(`Engine/`) 기준 상대 경로 또는 절대 경로 |

---

## 13. 픽스처 `Content/Test/`

CL9 WBP 15개에는 애니메이션이 없고 그래프도 거의 비어 있으므로(앞 세션 확인) 아래를 직접 만든다. 각각 3~6개의 "의도적으로 다른" 버전을 남긴다 — M4 정정: git 커밋이 아니라 생성기 `RadarUMGDiff.BuildFixtures` 가 버전마다 앞 버전을 고쳐 저장하고 `Test/Fixtures/<이름>/v<N>/` 에 복사한다(§19 M4). `5.1` 브랜치에서는 같은 명령을 5.1 에디터로 돌려 그 엔진 저장본을 만든다. 회사 콘텐츠는 필요 없다. M5 에서 Layout(+Child) 도 생성기 항목이 됐다(`RadarUMGDiff.BuildFixtures Layout`) — 5.8 의 두 저장본은 M2 손 픽스처 그대로다.

| 픽스처 | 담는 것 | 버전 간 변화 |
|---|---|---|
| WBP_Fix_Anim | 애니메이션 2개(Transform·Color·Opacity 트랙), 키 4개씩 | 키 값·프레임 이동·트랙 삭제·애니메이션 추가 |
| WBP_Fix_Graph | 함수(입·출력 핀), 커스텀 이벤트(인자), 변수 3개(TMap·TArray<struct> 포함) | 노드 추가·핀 기본값·노드 이동만·변수 이름 변경(VarGuid 유지) |
| WBP_Fix_Layout | Canvas 안 10개 위젯, 중첩 UserWidget 1개, WidgetSwitcher(비활성 페이지에 위젯), Collapsed 위젯 1개 | 위젯 이동·삭제·이름 변경(같은 자리)·슬롯 클래스 변경(Canvas→Overlay) |
| WBP_Fix_Text | 한국어 DisplayLabel, 스트링테이블 참조 텍스트, 일반 텍스트, 소프트 텍스처 브러시 | 텍스트 변경·키만 변경·테이블 키 변경·텍스처 교체 |
| WBP_Fix_Bind | 속성 바인딩 2개(함수·속성) | 바인딩 함수 변경·삭제 |

스파이크 3(지오메트리 없는 위젯의 `GetCachedGeometry`)과 §9.6의 마커 칩은 WBP_Fix_Layout으로 검증한다.

---

## 14. 테스트

| 종류 | 도구 | 통과 기준 |
|---|---|---|
| 프로바이더 (A) | §8.4 로컬 p4d + 번들 Perforce 플러그인, 그다음 번들 Git 플러그인 | 피커에 P4 리비전 목록, "이전과 비교" 열림, 같은 두 리비전의 (A)/(B) diff.json `conform` 0 차이 |
| Python 단위 | `python -I -m unittest discover Content/Python/tests` (3.9·3.11·3.14 세 번) | 리더 프리미티브·태그 두 형식·FText·소프트 경로를 손으로 만든 바이트로 |
| 헤더 라운드트립 | `dump` 후 `TotalHeaderSize`, 각 표 끝 오프셋이 요약의 다음 오프셋과 같다 | 모든 CL9 블롭 + 픽스처 |
| 익스포트 경계 | 각 익스포트: 파서 끝 위치 == SerialOffset+SerialSize, 1010 이상이면 None 끝 == ScriptSerializationEndOffset | 전부 |
| 커버리지 | `dump`의 `coverage.ratio` 보고. 목표 M3 ≥ 0.98(디자이너 속성), M4 1.0(픽스처 포함) | README에 실측 기록 |
| 일치 | `conform`: 네이티브 vs Python, 블롭 36개 + 픽스처 전 쌍 | 불일치 0 |
| 스키마 불변 | `dump --schema` 유무로 `diff`가 같은 `kind` 집합 | 0 차이 |
| 뷰어 | 에디터에서 WBP_HUD `65c99c5` vs `347f315`, 픽스처 각 쌍을 열어 스크린샷 → 사용자에게 올림 | goal 문서 Acceptance 항목 |
| 5.1 | ue5.1 브랜치 빌드 + 5.1 저장 픽스처로 위 전부 | M5 |

---

## 15. 마일스톤별 작업 목록

각 마일스톤은 "끝났다"를 말하기 전에 그 절의 검증을 돌리고 goal 문서 체크박스를 채운다. 순서는 바꾸지 않는다.

### M0 — 로드·수명 증명 (ue5.8)
1. 플러그인 뼈대(§2.2 중 `.uplugin`, Build.cs, 모듈 cpp, `RadarEngineCompat.h`, 설정 클래스). 브로커 `--kind compile`로 문법 확인 → `--kind build`.
2. Content Browser 메뉴 "Radar UMG Diff..." → `SRadarRevisionPicker`. 백엔드 셋(§8.0) 중 (B) git과 (C) 파일 두 개를 먼저, (A) 프로바이더는 §8.4 로컬 Perforce 서버(먼저)와 번들 Git 프로바이더(다음)로 검증.
3. 받은 임시 파일 → `Saved/Diff/temp-<id>-<name>.uasset` → `LoadPackageForDiff` ×2(같은 에셋 두 리비전, 동시 생존).
4. `SBlueprintDiff::CreateDiffWindow`로 엔진 diff 창을 연다(= 스파이크 2).
완료 기준: WBP_HUD 65c99c5 vs 347f315 창이 뜨고, 닫으면 두 임시 패키지가 GC된다(`obj list class=WidgetBlueprint`로 확인). 스크린샷 1장.

### M1 — 네이티브 diff + diff.json + 커맨드릿 (ue5.8)
§7.1~7.3, 7.4 리스트 레벨, 7.5 노드/핀 수준, §5 직렬화, §6 상태 덤프, §10.1 커맨드릿.
완료 기준: 커맨드릿이 WBP_HUD 두 커밋의 diff.json을 쓰고, 손으로 바꾼 픽스처 변경이 올바른 `kind`로 나온다. `summary`가 goal 문서 §Acceptance의 개수 정의와 같다.

### M2 — 뷰어 (ue5.8)
§9 전부. 먼저 data-only 모드(M1의 diff.json으로)를 만들고, 그다음 미리보기·오버레이.
완료 기준: Acceptance의 뷰어 항목 7개. 스크린샷(나란히·겹치기·삭제 상자·검색) 올림. 스파이크 3 결과를 §9.6에 반영.

### M3 — Python 분석기 1차 (헤더·태그·위젯·슬롯·CDO)
§11.1~11.6(A 형식), 11.7의 디자이너 구조체, 11.8, 11.10, 11.11. `data/custom_versions.json`.
완료 기준: 36 블롭 전부 헤더 라운드트립·익스포트 경계 통과, 커버리지 보고, `conform` 불일치 0(위젯·슬롯·CDO).

### M4 — Python 2차 (그래프·애니메이션·스키마·픽스처)
§11.7 M4 행, §11.9 그래프, FText 포맷 히스토리, `editor/dump_schema.py`, §13 픽스처 저장, 정확도 모드 보고서.
완료 기준: 픽스처 전 쌍 `conform` 0, 스키마 유무 불변 테스트 0 차이, README에 커버리지 표.

### M5 — 회사 엔진 5.1 (ue5.1 브랜치; 5.1.1이 이 PC에 설치돼 있어 여기서 빌드·검증 가능)
1. `ue5.8`을 5.1에서 그대로 빌드해 오류 목록을 받는다(= 스파이크 8). 그 목록이 `#if` 예산.
2. `RadarEngineCompat.h`의 5.1 분기(§3.3 네 항목) + `SRadarZoomPan`.
3. `-run=RadarStructKinds` → `data/struct_kinds_ue5.1.json`.
4. §11.6 B 형식(배열 내부 태그 포함), §11.5의 1010 미만 처리, §11.8 5.1 분기.
5. 5.1 에디터로 §13 픽스처 재작성·저장.
6. `conform`(5.1 네이티브 커맨드릿 vs Python), 뷰어 스크린샷, data-only 모드(5.8 CLI diff.json을 5.1 뷰어로).
완료 기준: Acceptance (M5) 항목 2개.

### 스파이크 상태 (goal 문서의 번호)

| # | 내용 | 상태 |
|---|---|---|
| 1 | WBP_HUD 헤더~익스포트 파싱, 태그 끝 == ScriptEnd | **완료** 2026-10-17 (§11.3 검증값, §11.5 실측) |
| 2 | 두 리비전 `LoadPackageForDiff` 동시 생존 + `CreateUserWidgetFromBlueprint` | **완료** 2026-10-18 (M0, §19) |
| 3 | Collapsed/비활성 스위처 위젯의 `GetCachedGeometry` | **완료** 2026-10-18: Collapsed 는 디자인 타임에 그려져 지오메트리 있음, 비활성 페이지는 없음 → 그려진 조상에 칩 (§9.6) |
| 4 | CL9 태그에 0x04 HasPropertyExtensions 없음 | **완료** 2026-10-18: 37 블롭 전체에서 0개 (M3, §19) |
| 5 | 5.1 FileVersionUE5 | **완료**: 1008 |
| 6 | 5.1 UObject::Serialize 접두·접미, 클래스별 접미부 | **완료**: 접두 없음, 접미 bool(+guid), §11.9 표 |
| 7 | 구조체 종류 덤프 경로 | **완료**: Python 노출 없음 → 커맨드릿 §10.2 |
| 8 | 5.8 코드를 5.1에서 빌드한 오류 목록 | **완료** 2026-10-18 (M5, §19 표 — 70줄·열 가지 + 링크 둘) |

---

## 16. 새 세션 시작 프롬프트 (복사해서 쓴다)

```
CL9 저장소 C:\Work\CL9 의 RadarUMGDiff 플러그인을 구현한다. 위젯 블루프린트 두 리비전을 렌더해서 비교하는
UE 에디터 플러그인(C++)과 .uasset 을 직접 읽는 Python 분석기다. 아직 코드는 없고 문서와 검증용 Perforce 서버만 있다.

먼저 읽을 것(읽기만, 순서대로):
1. C:\Work\CL9\docs\goal\radar-umg-diff.md — Design Decisions 와 Acceptance Criteria. 결정은 여기가 정본이다.
2. C:\Work\CL9\docs\goal\radar-umg-diff-dev.md — 전부. §0 규칙, §3.3 엔진 호환 표, §5 diff.json, §8 리비전 소스(백엔드 셋),
   §9 뷰어, §11 Python 바이트 레이아웃, §15 마일스톤. 두 문서가 어긋나면 goal 문서가 이긴다.
3. C:\Work\CL9\docs\goal\build-broker.md — 빌드는 직접 하지 않고 브로커에 요청한다.
4. C:\Work\P4\README.txt — 이미 떠 있는 로컬 Perforce 서버(127.0.0.1:1666, 사용자 Radar, 워크스페이스 CL9P4)와
   거기 올라간 WBP 히스토리 33건. 프로바이더 백엔드 검증에 쓴다. p4 sync -f 금지.

지금 상태(2026-10-18): 엔진은 C:\Program Files\Epic Games\UE_5.8 로 ue5.8 브랜치부터 만든다. 5.1 도 설치돼 있지만 M5 전에는
쓰지 않는다. 플러그인 폴더 Plugins/RadarUMGDiff 에는 Content/Python/tools/seed_p4_from_git.py 하나만 있다. 에디터는 다른
세션이 쓰고 있을 수 있으니 /cl9 status 로 잠금을 먼저 보고, 에디터의 Perforce 연결(README 절차)은 에디터가 비었을 때 한다.

진행 방식: 시작할 때 dev 문서 §17 미결(저장소 이름·공개 여부, 서브모듈 vs 복사, 브랜치 이름, IgnoredProperties 기본값)을
한 번에 묻고, 답을 받은 뒤 M0 → M1 → M2 → M3 → M4 순서로 간다. 마일스톤마다 그 절의 검증을 돌리고 goal 문서 체크박스를
채운다. (확인 필요) 표시는 해당 엔진 소스를 열어 확인한 뒤 구현하고, 확인 결과를 dev 문서에 적는다. 추측으로 구현하지
않는다. 조절 수치는 Config/DefaultRadarUMGDiff.ini 로 뺀다. 주석·UI 문자열은 한국어. 빌드가 도는 동안은 Python 쪽을
진행한다. 눈으로 볼 결과는 에디터 스크린샷으로 올린다. 커밋은 플러그인 폴더와 두 goal 문서만, 재촉하지 말고 나중에
한 번에.
```

---

## 17. 미결 (구현 시작 때 한 번에 묻는다)

1~4는 2026-10-18 구현 시작 때 물었고 답을 받았다.
1. ~~저장소 이름·공개 여부~~ → `RadarUMGDiff`, 공개.
2. ~~서브모듈 vs 복사~~ → 서브모듈.
3. ~~브랜치 이름~~ → `5.8`, `5.1`.
4. ~~`bIsVariable`·`DesignerFlags`~~ → "둘 다 기본으로 지원하고 요청에 따라 적용": 빼지 않고 `auto` 표시 + 뷰어/CLI 토글(§12 `AutoProperties`).
5. 회사 프로젝트의 형상관리는 Perforce 로 들었다(2026-10-18, "p4 설치해서 봐야 한다"). 회사 서버 버전·스트림 사용 여부·워크스페이스 형태(클래식/스트림)는 M5 때 확인.

---

## 18. 근거 부록 — 읽은 소스 위치

5.8 = `C:\Program Files\Epic Games\UE_5.8\Engine\Source\`, 5.1 = `C:\Program Files\Epic Games\UE_5.1\Engine\Source\`.

| 사실 | 5.8 | 5.1 |
|---|---|---|
| UE5 패키지 버전 번호 | `Runtime/Core/Public/UObject/ObjectVersion.h` 47~108 (1000~1018) | 같은 파일, 마지막 값 ADD_SOFTOBJECTPATH_LIST = 1008 |
| 패키지 요약 필드 순서 | `Runtime/CoreUObject/Private/UObject/PackageFileSummary.cpp` operator<< (Legacy -9 `:63`, NumTextureAllocations 게이트 `:468`) | 같은 파일 (Legacy -8 `:103`, FileVersionUE5 게이트 `:128`, NumTextureAllocations `:372`), 꼬리 순서 동일 |
| 이름표 엔트리 | `Runtime/Core/Private/UObject/UnrealNames.cpp` operator<<(FNameEntrySerialized&) | 같음 |
| 임포트·익스포트 | `Runtime/CoreUObject/Private/UObject/ObjectResource.cpp` | 같은 파일; ScriptSerialization 오프셋 미직렬화(헤더 `ObjectResource.h:272`에 필드만, `LinkerLoad.cpp:5874`에서 메모리상 채움) |
| 커스텀 버전 | `Runtime/Core/Private/Serialization/CustomVersion.cpp` 292~350 | 같음 |
| 태그 A 형식·플래그 | `Runtime/CoreUObject/Private/UObject/PropertyTag.cpp` 420~530, `PropertyTag.h` 플래그 | 없음 |
| 태그 B 형식 | 같은 파일 205~365 (`LoadPropertyTagNoFullType`); 배열 내부 태그 `PropertyArray.cpp` 638~645 | `PropertyTag.cpp` 94~125; `PropertyArray.cpp` 68, 121~126 |
| 타입 이름 트리 | `Runtime/CoreUObject/Private/UObject/PropertyTypeName.cpp` 41~50, operator<<(FPropertyTypeName&) | 없음 |
| 익스포트 접두 바이트 | `Runtime/CoreUObject/Private/UObject/Class.cpp` 1521~1534(enum), 1577~1605(`SerializeVersionedTaggedProperties`, bIsUClass && ≥1011) | 없음 |
| UObject::Serialize 순서 | `Runtime/CoreUObject/Private/UObject/Obj.cpp` (SerializeScriptProperties → PossiblySerializeObjectGuid) | `Obj.cpp` 1400~1480 |
| 오브젝트 GUID 접미 | `Runtime/CoreUObject/Private/UObject/LazyObjectPtr.cpp` PossiblySerializeObjectGuid (TryEnterField) | 같음 (`:102`) |
| 구조체 네이티브 판정 | `Runtime/CoreUObject/Private/UObject/Class.cpp` UseBinarySerialization/UseNativeSerialization | `Class.cpp` 2735~2761, `Class.h` 1588~1601, 플래그 `STRUCT_Immutable 0x20`, `STRUCT_SerializeNative 0x800` |
| FText | `Runtime/Core/Private/Internationalization/Text.cpp` 893~1030, `TextHistory.cpp`, `TextHistory.h` 24~42 | 같은 파일: `Text.cpp` 818·823~ (`SerializeText`), `TextHistory.h` 22 (`ETextHistoryType`) |
| 소프트 경로 | `Runtime/CoreUObject/Private/UObject/SoftObjectPath.cpp` SerializePathWithoutFixup, `TopLevelAssetPath.h:130`, `LinkerLoad.cpp` operator<<(FSoftObjectPath&) 인덱스 분기 | 같음 (`LinkerLoad.cpp:1667` SerializeSoftObjectPathList) |
| Script 오프셋 기록 | `Runtime/CoreUObject/Private/UObject/LinkerSave.cpp` 258, 267 | 없음 |
| FindDiffs 키 | `Editor/UMGEditor/Private/WidgetBlueprint.cpp` UWidgetBlueprint::FindDiffs | `WidgetBlueprint.cpp:823` |
| WidgetBlueprint 멤버 | `Editor/UMGEditor/Public/WidgetBlueprint.h` Bindings 226, Animations 229, FDelegateEditorBinding 70~101; AnimationBindings는 `Runtime/UMG/Public/Animation/WidgetAnimation.h:147` | Bindings 248, Animations 254, AnimationBindings 182 |
| 미리보기 생성·파기 | `Editor/UMGEditor/Private/WidgetBlueprintEditorUtils.cpp` CreateUserWidgetFromBlueprint, DestroyUserWidget; `Public/WidgetBlueprintEditorUtils.h` 39~43, 91, 94 | `Editor/UMGEditor/Private/WidgetBlueprintEditor.cpp` 1557~1620 (DestroyPreview, UpdatePreview); `Runtime/UMG/Public/Blueprint/UserWidget.h:224` DuplicateAndInitializeFromWidgetTree |
| 소스 컨트롤 프로바이더 | `Developer/SourceControl/Public/ISourceControlProvider.h` 191, 199, 254; `ISourceControlState.h` 59, 66, 80; `ISourceControlRevision.h` 44, 64~88; `SourceControlOperations.h` 503; `Editor/Kismet/Public/SBlueprintRevisionMenu.h` 27~39; `Runtime/Core/Public/Misc/Paths.h` 425 DiffDir | 같은 헤더들: Provider 114, 122, 162; State 33, 40, 54; Revision 44, 64~88(동일); Operations 376; RevisionMenu 25~37; Paths 347 |
| 옛 리비전 로드 | `Editor/UnrealEd/Public/DiffUtils.h` 365~366, `Private/DiffUtils.cpp` LoadPackageForDiff | `Editor/Kismet/Private/SBlueprintEditorToolbar.cpp` 578; 재료는 다 있음: `ObjectMacros.h:82` LOAD_DisableEngineVersionChecks, `LinkerInstancingContext.h:55` AddPackageMapping |
| FRevisionInfo | `Editor/AssetDefinition/Public/AssetDefinition.h` 227 | `Developer/AssetTools/Public/IAssetTypeActions.h` 28 |
| Blueprint Diff 창 | `Editor/Kismet/Public/SBlueprintDiff.h` 176, 179 | `SBlueprintDiff.h` 165 |
| FDetailsDiff | `Editor/Kismet/Public/DetailsDiff.h` 20~43 | 같은 파일 |
| SDesignSurface / SZoomPan | `Editor/UMGEditor/Public/Designer/SDesignSurface.h`, `Public/Designer/SZoomPan.h` | `Public/Designer/SDesignSurface.h`, **`Private/Designer/SZoomPan.h`** |
| EWidgetDesignFlags | `Runtime/UMG/Public/Components/Widget.h` 175~184 | `Runtime/UMG/Public/Components/Widget.h` 166 |
| Content Browser 메뉴 이름 | `Editor/ContentBrowser/Private/ContentBrowserMenuContexts.cpp` 115, `AssetContextMenu.cpp` 195 | `AssetContextMenu.cpp` 192 |
| 그래프 핀·사용자 핀 | `Runtime/Engine/Private/EdGraph/EdGraphPin.cpp` 163, 993; `Editor/BlueprintGraph/Private/K2Node_EditablePinBase.cpp` 80 | `EdGraphPin.cpp` 156, 853; `K2Node_EditablePinBase.cpp` 78 |
| 시퀀서 채널 | `Runtime/MovieScene/Private/Channels/MovieSceneFloatChannel.cpp` 456, 495; `MovieSceneDoubleChannel.cpp` 760 | `MovieSceneFloatChannel.cpp` 197; `MovieSceneDoubleChannel.cpp` 217 |
| Python 노출 | — | `Plugins/Experimental/PythonScriptPlugin/Source`에 StructFlags 참조 없음; 번들 Python 3.9.7 |
| CL9 자료 | `Content/UI/WBP_*.uasset` 15개, 전 리비전 블롭 36개 (2026-10-17) | |

---

## 19. 구현 기록

마일스톤마다 실제로 확인한 것, 문서와 달라진 것, 다음 사람이 알아야 할 함정을 적는다. 위 절의 계획과 어긋나면 이 절이 실측이다.

### M0 (2026-10-18, 5.8.3) — 완료

구현: `RadarUMGDiff.uplugin`, `RadarUMGDiffEditor.Build.cs`(§3.2 목록 + `DeveloperSettings`·`DesktopPlatform`), 모듈(콘텐츠 브라우저 메뉴), `RadarEngineCompat.h`, `URadarUMGDiffSettings` + `Config/DefaultRadarUMGDiff.ini`, 백엔드 셋(`RadarRevisionSource_Provider/Git/Files.cpp`), 로더·세션(`RadarRevisionSource.cpp`), 피커(`Viewer/SRadarRevisionPicker`), 검증용 콘솔 명령(`RadarConsoleCommands.cpp`). 브로커 빌드 3회 모두 성공, 경고 0.

검증(전부 5.8 에디터에서 실제로 실행):
- 콘텐츠 브라우저에서 WBP_HUD 우클릭 → "위젯 블루프린트 액션" 아래 "Radar UMG Diff..." → 피커가 열린다(`shots/m0_context_menu.png`).
- git 탭: 작업 사본(수정됨) + HEAD 0328cc8 + 커밋 11개(`shots/m0_picker_git.png`).
- `RadarUMGDiff.Open /Game/UI/WBP_HUD git:347f315 git:65c99c5` → `/Temp/Diff/temp-347f315-WBP_HUD` 와 `/Temp/Diff/temp-65c99c5-WBP_HUD` 가 동시에 로드되고, 두 미리보기 UUserWidget 이 같은 월드에 만들어진다(스파이크 2 통과). 엔진 Blueprint Diff 창이 뜬다(`shots/m0_bpdiff.png`).
- 창을 닫으면 세션이 풀리고, `obj gc` 뒤 `obj list class=WidgetBlueprint` 에 `/Temp/Diff/...` 가 0개, `Saved/Diff/` 파일도 지워진다.
- "이전과 비교": git = 65c99c5 ↔ 작업 사본, Perforce = #11 ↔ 작업 사본(`shots/m0_prev_vs_working.png`, `m0_p4_prev_vs_working.png`).
- 로컬 p4d 로 프로바이더 백엔드: Revision Control → Connect → Perforce, 서버·사용자는 P4 환경에서 미리 채워지고 **비밀번호 칸 없이 기존 `p4 login` 티켓으로 연결**된다. 워크스페이스 `CL9P4` 만 넣으면 된다. 피커에 #1~#11(CL 4~14). P4 가 받은 `temp-scm11-WBP_HUD.uasset` 은 `git show 65c99c5:Content/UI/WBP_HUD.uasset` 과 SHA256 이 같다. 검증 뒤 "리비전 컨트롤 비활성화"로 끊었다(`Saved/Config/WindowsEditor/SourceControlSettings.ini` 의 `Provider=None`, 서버 설정은 남김).

문서와 달라진 것·새로 안 것:
- **§8.6 세션 해제 — `MarkAsGarbage` 대신 `RF_Standalone` 해제.** 링커만 끊고 참조를 놓았더니 GC 뒤에도 두 임시 블루프린트가 남았다. `obj refs` 결과: "not currently reachable but it does have some of GARBAGE_COLLECTION_KEEPFLAGS set" — 에디터 GC 는 에셋의 `RF_Standalone` 을 지킨다. 그래서 `RadarCompat::ReleaseTempPackage` 가 패키지 안 오브젝트의 `RF_Standalone` 을 떼고 `ResetLoaders` 한다. `MarkAsGarbage` 는 아직 남은 참조를 강제로 끊어 위험하므로 쓰지 않는다. 같은 리비전을 연 다른 세션이 살아 있으면 그 패키지·임시 파일은 남긴다.
- **§8.1 `Get` 은 파일 이름을 받는다.** Perforce(`PerforceSourceControlRevision.cpp`, `print -o <이름>`)와 Git 프로바이더(`GitSourceControlRevision.cpp`) 모두 `InOutFilename` 이 비어 있지 않으면 그 경로에 쓴다. 그래서 받은 뒤 이름을 바꾸지 않고 처음부터 `temp-scm<리비전>-<에셋>.uasset` 으로 받는다.
- **§8.5 임시 이름의 `<id>`:** git = sha7, 프로바이더 = `scm` + 리비전 문자열(git sha7 과 겹치지 않게 접두), 파일 = `file-` + MD5 앞 8자리.
- **§8.2 바이너리는 `ExecProcess` 로 받으면 안 된다.** `FWindowsPlatformProcess::ExecProcess` 는 출력을 `FUTF8ToTCHAR` 로 바꾼다(`WindowsPlatformProcess.cpp` 1208). `git show` 는 `CreatePipe` 두 개(stdout·stderr 분리) + `CreateProc` + `ReadPipeToArray` 로 받는다(`RadarGit::RunBinary`). 텍스트(`git log`)는 `ExecProcess` 의 UTF-8 디코드로 한글이 맞게 나온다. CL9 의 WBP 는 LFS 가 아니다(`.gitattributes` 없음, 블롭 첫 4바이트 = 패키지 태그).
- **§8.0 "이전과 비교"의 뜻을 정했다:** 작업 사본이 깨끗하면 = 최신 체크인이므로 히스토리 두 번째와, 수정됐으면 최신 체크인과 작업 사본을 비교한다. "수정됨" 판정: git = `git status --porcelain`, 프로바이더 = `IsCheckedOut/IsAdded/IsModified` 이거나, 그 셋이 거짓이면 최신 리비전을 받아 작업 파일과 MD5 비교(체크아웃 없이 쓰는 `allwrite` 워크스페이스는 상태에 수정이 드러나지 않는다 — CL9P4 가 그렇다).
- **비유니코드 P4 서버의 한글 설명이 깨진다.** UTF-8 바이트가 프로바이더를 거치며 부호 확장돼 U+FF80~U+FFFF 로 들어온다(`￭ﾔﾄ` = ED 94 84 = '프'). 백엔드가 "ASCII 와 그 범위만으로 된 문자열이고 하위 바이트가 올바른 UTF-8 일 때만" 되돌린다(`RepairSignExtendedUtf8`). 유니코드 모드 서버에서는 이 경로를 타지 않는다.
- **날짜는 UTC 로 온다.** git(`FDateTime::ParseIso8601` 이 UTC 로 바꿈)·Perforce 모두. 피커는 로컬 시각으로 보인다.
- 5.8 에서 `ForEachObjectWithPackage(…, bool)` 은 폐기 예정 경고 → `EGetObjectsFlags` 판을 `RadarCompat` 안에서만 쓴다(5.1 분기 후보).
- 검증용 콘솔 명령: `RadarUMGDiff.Picker <패키지>`, `RadarUMGDiff.Open <패키지> <이전> <이후>`(리비전: `working | head | git:<sha> | scm:<리비전> | file:<경로>`), `RadarUMGDiff.Sessions`(열린 세션 + 메모리의 임시 위젯 블루프린트 수). MCP 에는 콘솔 실행 도구가 없어 SlateInspector 로 하단 Cmd 칸을 Click → Type(submit) 한다. 콘텐츠 브라우저 에셋 타일은 SlateInspector 트리에 드러나지 않는다.
- 브로커 `compile` 은 게임 타깃(WITH_EDITOR=0)이라 에디터 모듈을 못 잡는다 → 이 플러그인은 항상 `build` 로 요청한다(에디터 재기동). 재기동 때 자동 저장 "패키지 복원" 창이 뜨면 MCP 가 멈춘다(2026-10-18 한 번, 다른 세션의 ABP 자동 저장본).

### M1 (2026-10-18, 5.8.3) — 네이티브 생산자·diff.json·state.json·커맨드릿

구현: `RadarDiffModel.h`(diff.json 과 1:1 모델), `RadarPropertyCompare`(비교·텍스트화·JSON 값), `RadarNativeDiffProducer`(위젯·슬롯·바인딩·CDO·BP 변수·애니메이션 리스트 레벨·그래프 노드/핀·이동·이름 변경 추정), `RadarDiffJson`(손 직렬화·요약), 커맨드릿 `-run=RadarUMGDiffDump`(단일 쌍 + `-history` 정확도 모드), 콘솔 `RadarUMGDiff.Dump` / `RadarUMGDiff.DumpHistory`.

검증:
- WBP_HUD 347f315 → 65c99c5: 네이티브 diff.json = `HurtVignette`·`HurtDirection` 추가 2건(엔진 Blueprint Diff 창이 보인 것과 같음), state.json 객체 59개(위젯 30 + 슬롯 29).
- 정확도 모드(에디터 안 `RadarUMGDiff.DumpHistory`): WBP 12개의 git 인접 커밋 쌍 전부 + 수정된 작업 사본 = 22쌍, 실패 0.
- **커맨드릿 실행 자체는 아직 안 돌렸다**: 세션 규약상 `commandlet` 잠금은 에디터가 하나라도 떠 있으면 거부된다(sessions.md). 에디터 안 명령이 같은 함수(`FRadarNativeDiffProducer::DumpHistory`/`WriteAll`)를 부른다. 에디터를 닫아도 되는 때 한 번 돌린다.
- 손으로 바꾼 픽스처(§15 M1 완료 기준)는 §13 픽스처를 만드는 M4 로 미룬다 — 지금은 CL9 실제 히스토리 22쌍이 그 역할.

문서와 달라진 것·새로 안 것:
- **두 생산자가 같은 줄을 내도록 정한 규칙** (§7.2 보강): ① 비교 대상은 패키지에 저장되는 속성(`CPF_Transient|CPF_Deprecated|CPF_SkipSerialization` 제외 — 문서의 `CPF_DuplicateTransient` 는 저장되므로 빼지 않는다). ② 구조 정보 속성은 비교에서 뺀다: 위젯 `Slot`·`Slots`, 슬롯 `Parent`·`Content`, 노드 `NodeGuid`. ③ 줄 경로: ini `LeafStructs` 에 있는 구조체는 한 줄 + `changedComponents`, 나머지 구조체는 멤버로 내려간다, 배열은 `이름[i]`(남는 원소는 added/removed). ④ 같은 패키지 안 오브젝트 참조는 임시 패키지 이름(`/Temp/Diff/temp-…`)을 원래 패키지 이름으로 바꿔 비교한다 — 안 바꾸면 모든 참조가 바뀐 것으로 나온다. ⑤ 클래스가 다른 두 오브젝트(리비전마다 따로 로드된 BPGC 의 CDO, 슬롯 클래스 교체)는 속성 이름으로 짝짓고, 한쪽 클래스에만 있는 속성은 "저장되지 않는 값"(아키타입에 있으면 아키타입 값, 없으면 0값)일 때 줄을 내지 않는다. ⑥ 이동 = 부모가 바뀌었거나, 양쪽에 같은 부모로 남은 형제들 사이 순서가 바뀐 것(앞 형제가 지워져 인덱스만 밀린 것은 이동이 아니다). ⑦ `stateRef` 는 export 인덱스가 아니라 객체 id(`WidgetTree.X`, `WidgetTree.X/Slot`) — 생산자와 무관하게 같다.
- **크래시 한 번(2026-10-18 15:03)**: CDO 이름 비교에서 새 클래스에만 있는 속성의 기본값을 부모 CDO(`Default__CL9HUDWidget`)에서 읽다가 `ContainerPtrToValuePtr` assertion 으로 에디터가 죽었다. 아키타입이 그 속성의 소유 클래스를 상속할 때만 기본값을 쓴다(`RadarProperty::DefaultPtr`).
- `UBlueprint`/`UWidgetBlueprint` 의 CDO 에는 디자인 크기(`DesignTimeSize`, `DesignSizeMode`)가 있다 → diff.json `designSize` 는 양쪽 BPGC CDO 에서 읽는다.
- FText 의 namespace/key 는 에디터가 로드할 때 다시 만들 수 있다(5.8 `TextHistory.cpp` 824~843, `GIsEditor` 이고 패키지 네임스페이스가 다르면 새 GUID 키) → 네이티브는 source(+table/key)만 비교한다는 goal 결정의 근거가 소스로 확인됐다.
- `UMovieScene::GetBindings()` 비 const 판은 5.7 부터 폐기 경고 → const 판을 쓴다.

### M3 (2026-10-18) — Python 분석기 1차: 헤더·태그·위젯·슬롯·CDO

구현: `reader`·`summary`·`tables`·`softpath`·`text`·`natives`·`tags`(A 형식 전체, B 형식은 거부 — M5)·`exports`·`state`·`diff`·`conform`·`config`(ini 공유)·`gitsource`, CLI `umgdiff_cli.py dump|diff|conform|history|summary`, `tools/gen_custom_versions.py`(엔진 소스에서 커스텀 버전 GUID 표 생성 → `data/custom_versions.json`, 109개), 단위 테스트 14개.

검증:
- 단위 테스트: 3.9.7(5.1 번들)·3.11.8(5.8 번들)·3.14.7(시스템) 모두 OK.
- 헤더 왕복·익스포트 경계: git 히스토리의 `Content/UI/WBP_*.uasset` 블롭 **37개 전부** — 요약 끝 ≤ NameOffset, 이름표 끝 = SoftObjectPathsOffset, 임포트 끝 = ExportOffset, 익스포트 끝 = DependsOffset, TotalHeaderSize = 첫 SerialOffset, 모든 익스포트의 태그 끝 = ScriptSerializationEndOffset. 오류 0.
- 스파이크 4 확정: 37 블롭 전체에서 0x04(HasPropertyExtensions) 태그 0개.
- 커버리지: 전체 0.883(37 블롭 합), 디자이너 범위(위젯·슬롯·CDO·블루프린트·위젯 트리) 대부분 1.0, 최저 0.977 — 남은 미해석은 BP `NewVariables`/`GeneratedVariables` 안의 `EdGraphPinType`(M4)뿐. 전체에서 빠지는 바이트는 그래프 노드 핀(`(suffix:K2Node_*)`, M4)과 클래스·함수 익스포트의 UStruct 데이터(이 분석기 범위 밖).
- **일치(conform)**: 네이티브 22쌍 vs Python 22쌍 → **판정 불일치 0건**(위젯·슬롯·CDO·바인딩의 (id, kind)·속성 줄 (path, kind)·양쪽 present 값). 판정 보류 5건 = Python 이 한쪽 태그만 본 `maybeDefault` 줄: C++ 기본값이 바뀐 `CookGaugeLift`(a4662be 에서 생김, 2쌍), C++ 에서 이름이 바뀌어 현재 코드로는 로드되지 않는 `HotbarSlotClass`(→ `QuickSlotClass`, 2쌍), C++ 기본 배열과 같은 원소 `ScanKinds[0]`(1쌍). goal 문서의 "absent ↔ present 는 스키마가 증명하기 전까지 changed (maybe default)" 그대로이며, M4 스키마 덤프로 확정한다. conform 은 이들을 숨기지 않고 "판정 보류"로 따로 나열한다.

문서와 달라진 것·새로 안 것:
- **§11.9 정정: `UBlueprintCore` 접미부에 GeneratedClass 인덱스는 없다.** `Ar << GeneratedClass` 는 `UEVer < VER_UE4_BLUEPRINT_SKEL_CLASS_TRANSIENT_AGAIN` 블록 안에 있다(5.8 `Runtime/Engine/Private/Blueprint.cpp` 303~335). 5.x 저장본의 WidgetBlueprint 익스포트는 GUID bool 뒤에 바이트가 없다(37 블롭 실측). `UBlueprint::Serialize`·`UWidgetBlueprint::Serialize` 도 추가 바이트 없음.
- **§11.8 정정: FText None 히스토리의 `bHasCultureInvariantString` 은 1바이트가 아니라 4바이트**(`FArchive << bool`). WBP_HUD HeartGlyph 실측 `02000000 ff 01000000 feffffff 6526 0000` = "♥".
- FText Base 뒤 `DevNotes` FString: FortniteMain ≥ 260(`AddDevNotesToFText`, `TextHistory.cpp` 917) — CL9 5.8 저장본에 있다.
- FSoftObjectPath 서브 경로: FortniteMain ≥ 192 면 FUtf8String(int32 길이 + NUL 없는 UTF-8, `String.cpp.inl` 1810 비 TCHAR 분기), 미만이면 FString 길이 규칙(`SoftObjectPathLoadSubPathWorkaround`).
- `FDeprecateSlateVector2D` 는 FVector2f(float 2개)로 직렬화된다(`SlateCore/Private/Types/SlateVector2.cpp:34`) — UMG 의 많은 Vector2D 속성이 이 형이다.
- 맵 태그의 0x08 은 키·값 중 하나만 네이티브여도 선다(`PropertyMap.cpp:1834`) → 어느 쪽인지 파일만으로 모를 때는 해석하지 않고 raw.
- 위젯 집합은 WidgetTree 를 Outer 로 가진 익스포트 전부가 아니라, `RootWidget` 에서 패널 `Slots` → 슬롯 `Content` 와 `NamedSlotBindings` 를 따라 도달하는 것만(네이티브 `ForEachWidget` 과 같은 집합). 패키지에는 BPGC 쪽 WidgetTree 도 있어서, 블루프린트 익스포트를 Outer 로 가진 WidgetTree 를 고른다.
- 패키지 경로를 파일만으로 정한다: `Content/` 아래 상대 경로 → 소프트 경로 목록의 자기 애셋 항목 → `/Unknown/<애셋>`(CLI `--package` 로 덮어쓴다).
- 오브젝트 경로 구분자: 패키지 바로 아래 오브젝트의 하위 오브젝트 앞만 `:`, 나머지는 `.`(`/Game/UI/WBP_HUD.WBP_HUD:WidgetTree.HealthBar`) — `UObject::GetPathName` 과 같다.
- 클래스가 바뀐 객체(슬롯 클래스 교체)는 한쪽에만 있는 태그를 그 클래스에만 있는 속성으로 본다(removed/added). 두 클래스 모두에 있는데 한쪽이 기본값이라 저장 안 된 속성은 스키마 없이 구별할 수 없다 — 알려진 한계.

### M2 (2026-10-18, 5.8.3) — 뷰어

구현: `Viewer/RadarViewModel`(병합 계층·필터·평탄 리스트), `SRadarDiffWindow`(제목 띠·도구 띠·계층 | 미리보기/애니/그래프 | 상세), `SRadarSummaryStrip`(칩·검색·체크 셋), `SRadarHierarchy`, `SRadarDetailsGrid`(+ "전체 상세 열기" = `FDetailsDiff` 두 개), `SRadarPreviewPane`(`SDesignSurface` + `SRadarZoomPan`), `SRadarOverlay`, `SRadarTabs`(애니메이션 리스트 레벨·그래프 + "Blueprint Diff에서 열기"), `RadarValueRenderers`(색 칩·성분 강조·텍스트 단어 diff·애셋 링크), `RadarDiffJsonLoad`(data-only 용 diff.json + state.json 읽기). 피커에 "diff.json 열기...".

검증(에디터에서 실제로 열고 스크린샷, `Saved/RadarUMGDiff/shots/`):
- WBP_HUD 347f315 ↔ 65c99c5: 나란히(`m2_side_by_side`)·겹치기(`m2_overlay`)·뒤집기(Space, `m2_flip_old`/`m2_flip_new`). 두 패널이 보기(이동·줌)를 공유한다.
- 검색(`m2_search`): 이름·DisplayLabel·속성·값에 맞는 행을 강조. 칩 필터(`m2_chip_filter`): 계층·오버레이·다음/이전이 같이 따른다.
- 상세: 바뀐 속성만 속성 | 이전 | 이후, 색 칩·(기본) 라벨. "전체 상세 열기" = 엔진 디테일 두 개(`m2_full_details`).
- data-only(`m2_data_only`): 피커의 "diff.json 열기..." → 미리보기 자리에 안내, 오버레이·전체 상세·Blueprint Diff 비활성, 생산자가 못 내는 종류의 칩은 회색.
- **픽스처 WBP_Fix_Layout v1(파일) ↔ v2(작업 사본)** (`m2_fixture`, `m2_removed_hover`, `m2_next_f8`, `m2_graph_tab`, `m2_fixture_bpdiff`):
  - 요약: 삭제 3 · 변경 1 · 이동 1 · 이름 변경(추정) 1 · 슬롯 1.
  - Title → Heading 은 `renamed-probable`(점수 1.00), 상세에 색 FFFFFFFF → FFCB7CFF 와 DisplayLabel.
  - Icon 은 Canvas → Group(Overlay)[0] 이동 + 슬롯 클래스 변경. 계층에 옛 자리 유령 행 "이동 전 Icon → Group[0] 로 이동", 새 쪽에 옛 상자 → 새 상자 화살표.
  - HpBar(보임)·HiddenNote(Collapsed)는 새 쪽 옛 자리에 빗금 점선 빨간 상자, 위에 마우스를 두면 "HiddenNote 가 여기 있었음". 옛 쪽은 빨간 외곽선.
  - PageBText(비활성 스위처 페이지)는 지오메트리가 없어 Pages 스위처 가장자리에 마커 칩 "✕ PageBText"(§9.6 스파이크 3).
  - F8 → 다음 항목(1/6 → 3/6), 선택·상세·미리보기 초점이 같이 움직인다. 그래프 탭 "Blueprint Diff에서 열기" → 엔진 diff 창(왼쪽 목록에 [OkLabel] "확인하기", Icon 슬롯, 변수 HpBar 삭제가 보인다). 두 창을 닫으면 세션 해제.
- 같은 픽스처 쌍 conform: 네이티브(`RadarUMGDiff.Dump … file:v1 file:v2`) vs Python(`umgdiff_cli.py diff v1 v2 --package /RadarUMGDiff/Test/WBP_Fix_Layout`) → 판정 불일치 0, 보류 0.

문서와 달라진 것·새로 안 것:
- **키는 `OnPreviewKeyDown` 에서 받는다.** 창의 `OnKeyDown` 으로 받았을 때는 미리보기를 클릭한 뒤 Space 가 처리되지 않았다(포커스가 자식 위젯에 있을 때). 터널링 단계인 `OnPreviewKeyDown` 으로 옮긴 뒤 Space·F8 모두 동작.
- **오버레이 좌표는 매 프레임 paint 공간으로 다시 계산한다.** hover 라벨은 처음 구현에서 마우스와 상자의 좌표 공간이 어긋나 뜨지 않았다 — 패널(`SRadarPreviewPane::OnMouseMove`)이 자기 로컬 마우스 좌표를 들고 있고, 오버레이는 같은 패널 로컬 공간의 상자와 그것을 비교한다.
- 취소선은 색 브러시로는 안 그려진다(리소스 없는 브러시) → `FCoreStyle` 의 `GenericWhiteBox` 로 그린다.
- 한쪽 패널이 그려지지 않는 뒤집기 모드를 위해 마지막으로 유효했던 위젯 사각형을 뷰 모델에 남긴다(`LastOldRects`/`LastNewRects`).
- 알려진 작은 것: 그래프·애니메이션 탭에서도 상세 패널이 디자이너 선택(머리글만)을 유지한다.

### M4 (2026-10-18, 5.8.3) — Python 2차: 그래프·애니메이션 키·생성 텍스트·스키마·픽스처

구현:
- Python: `graphs.py`(FEdGraphPinType·FEdGraphTerminalType·UEdGraphPin 본문·소유 핀 배열·FUserPinInfo), `graphdiff.py`(그래프·노드·핀·블루프린트 변수), `animdiff.py`(트랙·섹션·채널·키), `natives.py` 추가 리더(MovieSceneFloat/DoubleChannel·Float/DoubleValue·MovieSceneFrameRange·EdGraphPinType), `text.py` 생성 히스토리(Named/Ordered/ArgumentFormat·AsNumber/Percent/Currency·AsDate/Time/DateTime·Transform), `tags.py` FieldPathProperty, `versions.py` + `tools/gen_version_gates.py` → `data/version_gates.json`(커스텀 버전 게이트 값을 엔진 소스에서 생성), `schema.py`(스키마 층), CLI `--schema`·`schema-check`, `tools/accuracy_report.py` → `Test/accuracy_report.md`, 단위 테스트 7개 추가(총 21).
- C++: 애니메이션 키 단위 diff(`DiffAnimations`·`DiffSection`·`DiffChannel`, 모델 `FRadarSectionDiff`/`FRadarChannelDiff`/`FRadarKeyDiff`, JSON 쓰기·읽기, 애니메이션 탭 트리), 핀 타입 값을 구조 JSON 으로, `FInterfaceProperty` 비교·JSON, 스키마 명령 `RadarUMGDiff.DumpSchema`(`RadarSchemaDump.cpp`) + 입구 `Content/Python/editor/dump_schema.py`, 픽스처 생성기 `RadarUMGDiff.BuildFixtures`(`Private/Fixtures/RadarFixtureBuilder.cpp`), 뷰어 "Python 분석기로 다시 만들기"(§10.3), 모듈 `LoadingPhase` `Default`(§3.1).
- 픽스처 (§13): `Test/Fixtures/<이름>/v<N>/<이름>.uasset` — WBP_Fix_Anim v1~4, WBP_Fix_Graph v1~5, WBP_Fix_Text v1~6, WBP_Fix_Bind v1~3, WBP_Fix_Layout v1~2 (+ WBP_Fix_Child). 에셋 자체는 `Content/Test/`.

검증:
- 일치(네이티브 vs Python, `Test/accuracy_report.md`): 픽스처 15쌍 판정 불일치 **0**, 보류 0 (스키마 유무 모두). CL9 git 히스토리 22쌍 불일치 **0**, 보류 5 → 스키마로 2.
- 커버리지: 픽스처 21개 + CL9 git 블롭 37개, 익스포트 1,111,953 바이트 — 범위 안 **1.0000**(전체 0.9797). 남은 바이트는 `(struct-data:WidgetBlueprintGeneratedClass)` 17,475 · `(struct-data:Function)` 5,140 — 컴파일된 클래스·함수의 UStruct 몸체(§11.9, 이 분석기 범위 밖). M3 의 CL9 현재 WBP 15개 전체 커버리지 0.883 → 0.991.
- 스키마 불변 (`schema-check`, 37쌍): 스키마를 넣어 사라지거나 바뀐 판정 **0**, 스키마가 확정한 보류 줄 123. 스키마 = 클래스 851 · 구조체 515 (5.7 MB). `dump --schema` 는 저장되지 않은 속성을 `state: default` 로 채운다(WBP_Fix_Anim Label: present 3 · default 55).
- 커맨드릿 `-run=RadarUMGDiffDump`: 단일 쌍(WBP_Fix_Layout)과 `-history`(22쌍) — 에디터 안 `DumpHistory` 와 diff.json 이 칸마다 같다(다른 칸 0), Python 과 불일치 0.
- 프로바이더 (A) vs git (B): WBP_HUD 인접 10쌍을 Perforce(`scm:1..11`)·번들 Git 프로바이더(`scm:<sha8>`)·git CLI(`git:<sha>`)로 각각 덤프 → 위젯·바인딩·CDO·변수·애니메이션·그래프·요약 칸이 셋 다 같다(다른 칸 0). 피커의 "소스 컨트롤(Git)" 탭에 11개 리비전(`m4_picker_gitprovider.png`). 검증 뒤 `Provider=None` 으로 되돌렸다.
- 뷰어: 애니메이션 탭 키 단위(`m4_anim_tab.png`), 그래프 탭(`m4_graph_tab.png`), "Python 분석기로 다시 만들기" → data-only 창(`m4_rerun_python.png`).
- ini 조절값은 빌드 없이: `ColorRemoved` 를 바꾸고 에디터만 다시 열자 지워진 상자·칩 색이 바뀌었다(`m4_ini_color_crop.png`), 되돌림. Python 은 ini 를 실행마다 읽는다.
- 단위 테스트 21개: 3.9.7(5.1 번들)·3.11.8(5.8 번들)·3.14.7(시스템) 모두 OK. CLI `diff`·`dump --schema` 를 5.8 번들 3.11 로 실행.
- 플러그인 소스·Build.cs·uplugin 에 "CL9" 0건.

문서와 달라진 것·새로 안 것:
- **채널 이름 규칙**: 채널 = 섹션의 `FMovieSceneChannel` 파생 구조체 속성. 이름은 속성 이름, 정적 배열은 **0번 원소는 이름만, 1번부터 `이름[i]`**(`Translation`, `Translation[1]`). 파일에는 태그의 ArrayIndex 만 있어 정적 배열 크기를 모르기 때문이다(§7.4 의 `FMovieSceneChannelMetaData::Name` 은 파일에 없다).
- **채널 자체 값(DefaultValue·외삽·TickResolution)은 두 쪽 다 저장된 채널일 때만** 비교한다. 아키타입과 같은 채널은 파일에 없고, 없는 쪽 값은 스키마 없이는 모른다 — 네이티브도 아키타입과 같은 채널이면 그 줄을 내지 않는다(같은 집합). 키는 없는 채널 = 키 없음으로 본다.
- **키 위치는 무비신 틱 단위**(위젯 애니메이션 기본 틱 해상도 60000/초, 표시 20fps → 표시 프레임 20 = 틱 60000). diff.json `frame` 과 뷰어 "틱 N".
- **핀 타입 값은 구조 JSON**(FEdGraphPinType 의 UPROPERTY 멤버 이름 그대로, 저장 안 되는 `bIsArray_DEPRECATED` 제외). 비교·`text` 는 네이티브 ExportText, `value` 는 구조 — Python 은 바이너리에서 같은 구조를 만든다. 변수 `VarType` 도 같은 규칙으로 멤버 단위 줄(`VarType.PinSubCategoryObject`).
- **인터페이스 참조(TScriptInterface)** 는 정규화 경로로 비교한다. 처음 구현은 엔진 `Identical`(포인터 비교)로 떨어져 섹션 `Easing.EaseIn/EaseOut` 이 리비전마다 바뀐 것으로 나왔다.
- **스키마 덤프는 C++ 명령**: 에디터 Python 은 BlueprintVisible 속성만 노출한다(5.8 `PythonScriptPlugin/.../PyGenUtil.cpp` 1609 `IsScriptExposedProperty`). `editor/dump_schema.py` 는 `RadarUMGDiff.DumpSchema` 를 부르는 입구다. 스키마 = 위젯·슬롯·위젯 트리·위젯 애니메이션·무비신·트랙·섹션·그래프·노드·블루프린트 계열 네이티브 클래스와 그 속성이 쓰는 구조체의 속성 목록 + 기본값(diff.json 과 같은 JSON 형).
- **"스키마 유무와 무관한 판정"의 뜻**: 판정 = 항목 kind 와 양쪽에 태그가 있는 줄. maybeDefault 줄은 스키마 없이 미결(goal: "changed (maybe default) unless the schema proves equality")이므로 판정에 넣지 않고, 스키마는 그 줄만 확정한다 — 같으면 지우고, 다르면 없는 쪽을 `state: default` 로. 기본값이 null(빈 오브젝트 참조)인 것과 "기본 배열에 그 원소가 없음"은 다르다(처음 구현이 둘을 섞어 CookGaugeClass 를 added 로 냈다).
- **스키마로도 남는 보류 2건**: `HotbarSlotClass` — C++ 에서 `QuickSlotClass` 로 이름이 바뀐 속성의 옛 태그. 지금 클래스에 없어 네이티브는 로드할 때 버리고, Python 은 파일에 있는 그대로 알린다.
- **커스텀 버전 게이트 값은 엔진 소스에서 생성**(`tools/gen_version_gates.py`: enum 멤버 순서 또는 .inl 의 `*_VERSION(Name, ID)`) — 5.8·5.1 둘 다 있는 멤버는 값이 같은지 확인한다. 예: PinsStoreFName 31, EdGraphPinContainerType 26, EdGraphPinSourceIndex 50, SerializeFloatChannelCompletely 12, SerializeFloatChannelShowCurve 53.
- **그래프 노드 판별**: 익스포트의 Outer 가 `EdGraph` 익스포트이거나 클래스가 `K2Node`/`EdGraphNode` 계열. 핀 배열(+ 남으면 사용자 핀 배열)을 읽은 끝이 익스포트 끝과 정확히 맞을 때만 받아들인다. 1018 의 ImportTypeHierarchies 는 플레이스홀더를 만들 수 있는 형에만 기록되어(5.8 `PackageHarvester.cpp` 1404~) 클래스 계보로 쓸 수 없다.
- **FieldPath** (§11.7 의 확인 필요 해소): `TArray<FName>` Path(안쪽 필드가 0번) + 소유 UStruct 인덱스(5.8 `FieldPath.cpp` 325~357). JSON 은 네이티브 ExportText(`FFieldPath::ToString`)와 같은 `"<소유자>:<바깥>.….<필드>"`, 빈 경로는 `""`. CL9 에서는 `FWidgetAnimationBinding.DynamicBinding` 에만 나온다.
- **생성 텍스트**: 표시 문자열은 문화권 규칙으로 계산해야 해서 Python 은 `source` 를 None(모름)으로 두고 구조(`generated`: 포맷 원문 FText + 인자 / 원본 값·옵션·문화권 / 날짜 틱·스타일 / 변환)를 담는다. 두 생성 텍스트는 그 구조로 비교한다. `TextGenerator`(12)만 raw. Text 픽스처 v5·v6(이름·순서 포맷, AsNumber·AsPercent·AsDate·AsCurrency·ToUpper, 인자 값 변경)으로 바이트 경계와 판정을 확인했다.
- **픽스처는 git 커밋이 아니라 생성기**(§13 정정): `RadarUMGDiff.BuildFixtures` 가 버전마다 앞 버전을 고쳐 저장하고(이동·이름 변경을 보려면 GUID 가 이어져야 한다) `Test/Fixtures/<이름>/v<N>/` 에 복사한다. 5.1 브랜치에서 같은 명령으로 그 엔진 저장본을 만든다. 함정 둘: 위젯·애니메이션을 더할 때 `UWidgetBlueprint::OnVariableAdded` 를 불러야 한다(안 부르면 컴파일러 ensure "Animation was added but did not get a GUID", `WidgetBlueprintCompiler.cpp` 792), 5.x 에서 `PC_Float` 단독 핀 카테고리는 int 로 해석된다(`PC_Real` + 서브카테고리 `PC_Float`).
- **커맨드릿 실행 기록**: `UnrealEditor-Cmd` 종료 코드는 1 이었으나 커맨드릿 결과는 0 이다 — 프로젝트의 GameFeatureData 애셋 매니저 설정 오류(LoadErrors)가 종료 코드를 올린다(이 플러그인과 무관). 잠금 도구는 `acquire commandlet --pid <UnrealEditor-Cmd>` 를 "에디터가 떠 있다"로 거부해서 `compile` 잠금을 잡은 채 실행했다.
- 브로커가 다시 뜬 직후 한 번, 빌드 전 에디터 종료가 되지 않아 UBT 가 "Live Coding is active"로 거부했다 — `compile` 잠금 + 수동 종료 후 다시 요청해 통과.
- 피커의 Git 프로바이더 "CL" 칸에는 프로바이더가 돌려주는 숫자 체크인 id 가 나온다(git 에는 체인지리스트 개념이 없다).

### M5 (2026-10-18, 5.1.1) — 회사 엔진 5.1

환경: 플러그인 저장소에 `5.1` 브랜치(5.8 에서 갈라짐)를 만들고 `git worktree` 로 `C:\Work\RadarUMGDiff51\Plugins\RadarUMGDiff` 에 체크아웃했다(CL9 의 `Plugins/RadarUMGDiff` 는 `5.8` 그대로). 5.1 빌드·실행은 CL9 가 아니라 빈 C++ 프로젝트 `C:\Work\RadarUMGDiff51\RadarHost51.uproject`(게임 모듈 하나 + 이 플러그인)에서 한다. 런처 설치 5.1 은 `Build.bat` 이 `GetDotnetPath.bat` 을 건너뛰어(InstalledBuild) 시스템 .NET 8/10 을 잡고 "framework 6.0.0 없음"으로 실패한다 — 번들 `Engine\Binaries\ThirdParty\DotNet\6.0.302\windows` 를 `PATH`·`DOTNET_ROOT` 로 직접 잡아야 한다(`DOTNET_MULTILEVEL_LOOKUP=0`). 컴파일러는 VS 18 MSVC 14.51 로 문제없었다. 커맨드릿 인자는 `cmd.exe` 를 거치면 `-run=X` 의 `=` 가 잘려 커맨드릿이 아니라 에디터가 뜬다 — 프로세스 인자 목록으로 직접 띄운다.

**스파이크 8 (5.8 코드를 5.1 에서 빌드한 오류 목록) — 완료.** 1차 빌드 오류 70줄, 묶으면 열 가지, 그다음 링크 오류 둘:

| 오류 | 5.1 사실 (엔진 소스) | 대응 (`RadarCompat::`) |
|---|---|---|
| `FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint`·`FCreateWidgetFromBlueprintParams`·`DestroyUserWidget` 없음 | §3.3 확인대로 | `CreatePreviewWidget` 5.1 = 5.8 본문 이식(`FMakeClassSpawnableOnScope` — 5.1 `UnrealEd/Public/Kismet2/BlueprintEditorUtils.h:1938`, 최신 트리는 5.1 `UpdatePreview` 규칙 `GetWidgetTreeOwningClass`), `DestroyPreviewWidget` = `MarkAsGarbage`+`ReleaseSlateResources(true)` |
| `EGetObjectsFlags` 없음 | `ForEachObjectWithPackage(…, bool bIncludeNestedObjects)` (`UObjectHash.h:168`) | `ForEachObjectInPackage` |
| `UMovieScene::GetTracks` 없음 | `GetMasterTracks` (`MovieScene.h:686`) | `GetRootTracks` |
| `FProperty::GetElementSize` 없음 | 공개 멤버 `ElementSize` (`UnrealType.h:226`) | `ElementSizeOf` |
| `FScriptSetHelper/MapHelper::GetElementPtr/GetKeyPtr(FIterator)` 없음 | 반복자 `operator*` 가 내부 인덱스, `GetKeyPtr(int32)` (`UnrealType.h` 5060~) | `ContainerIndex(It)` |
| `FDetailsDiff::CreateDetailsView` 없음 | 5.1 `FDetailsDiff` 는 생성자에서 디테일을 만든다(`DetailsDiff.cpp` 14~40; 생성자 인자도 `(const UObject*, FOnDisplayedPropertiesChanged)` 로 §3.3 "공통" 표기와 다름) | `CreateReadOnlyDetailsView` = 같은 설정(이름 칸 숨김·편집 불가·고급 속성 표시) |
| `UE::ContentBrowser::ExtendToolMenu_AssetContextMenu` 없음 | 메뉴 이름은 같다 — `AssetContextMenu.cpp` 340~ `RegisterMenuHierarchy` 가 `JoinMenuPaths("ContentBrowser.AssetContextMenu", 클래스 이름)`, 섹션 `GetAssetActions`(:378), 컨텍스트의 `SelectedAssets` 있음 | `ExtendWidgetBlueprintAssetMenu` = `UToolMenus::ExtendMenu` |
| `FSlateDrawElement::MakeDashedLines` 없음, `FGeometry::ToPaintGeometry(FVector2f, …)` 없음 | `DrawElements.h` 에 점선 없음, `Geometry.h:320` 은 `FVector2D` 인자 | `MakeDashedLines` 5.1 = 변마다 실선 조각, `PaintGeometry(Geo, 크기, 위치)` |
| `UWidgetDesignerSettings::DefaultPreviewResolution` 없음 | 디자이너는 에디터 프로젝트별 ini `[UMGEditor.Designer] PreviewWidth/PreviewHeight`, 없으면 1280×720 (`SDesignerView.cpp` 217~219, 873~891) | `DefaultPreviewResolution` |
| (픽스처 생성기) `UWidgetBlueprint::OnVariableAdded`, `FText::AsLocalizable_Advanced`, `FStringTable::SetSourceString` 3인자 없음 | 5.1 UMGEditor 에는 `WidgetVariableNameToGuidMap` 자체가 없다. `FText::ChangeKey`(`Text.h:715`), `SetSourceString(키, 원문)`(`StringTableCore.h:117`) | `NotifyWidgetVariableAdded/Renamed/Removed`(5.1 은 할 일 없음), `MakeLocalizedText`, `SetStringTableSource` |
| C4458 `WeakThis` 가 멤버를 가림 | 5.1 경고 수준 | 지역 변수 이름 `WeakSelf` (분기 없음) |
| `TCondensedJsonPrintPolicy` 미정의 | 헤더가 간접으로 안 들어옴 | `Policies/CondensedJsonPrintPolicy.h` 포함 (분기 없음) |
| 링크: `GetWidgetPreviewAreaAndSize`·`GetWidgetPreviewDPIScale` | 5.1 에도 있지만 `class FWidgetBlueprintEditorUtils` 가 `UMGEDITOR_API` 가 아니다(`WidgetBlueprintEditorUtils.h:21`) | `WidgetPreviewAreaAndSize`·`WidgetPreviewDPIScale` 5.1 = 본문 이식(`.cpp` 2068~2129) |

`FRevisionInfo`(5.1 `AssetTools`), `LoadPackageForDiff`(§3.3 대로 공통 본문), `SRadarZoomPan`, `SDesignSurface`, `SBlueprintDiff::CreateDiffWindow`, `UWidgetAnimation::GetBindings`(5.1 `WidgetAnimation.h:112`)는 오류 없이 그대로 됐다. 판정 매크로는 `RADAR_UE51` 하나, `#if` 는 여전히 `RadarEngineCompat.h` 에만 있다. 5.8 쪽 분기는 원래 호출을 그대로 둔다.

구현:
- 커맨드릿 `-run=RadarStructKinds`(`Commandlet/RadarStructKindsCommandlet`): `/Script/` 구조체마다 `STRUCT_SerializeNative` → native, 아니면 `STRUCT_Immutable` → immutable, 나머지 tagged(5.1 `Class.cpp` 2735~ `UseBinarySerialization`/`SerializeItem`). 같은 짧은 이름이 다른 종류면 `conflicts`. **추가로 `containers`**: 네이티브 클래스·구조체(상속 포함)의 집합·맵 속성마다 원소 구조체 이름 — 옛 형식의 집합·맵 태그에는 원소 구조체 이름이 없고 내부 태그는 배열에만 있다(5.1 `PropertyArray.cpp` 210 "TODO: Should work for maps + sets too"). 결과 `data/struct_kinds_ue5.1.json`: 5.1.1 · 1008 · 구조체 3,667(tagged 3,531 · native 128 · immutable 8) · 충돌 0 · 집합·맵 속성 344. 참고로 5.1 에서 `Vector2D` 는 native, `FrameRate`·`UserPinInfo` 는 tagged 다.
- Python: `tags.py` B 형식 머리(`read_legacy_tag_header` — A 형식과 같은 TypeNode·Flags 로 바꿔 값 리더 한 벌), 배열 내부 태그(원소 0개여도 있음), 집합·맵 원소 구조체는 `containers["<소유자>.<속성>"]`, 표에 없는 구조체는 태그 스트림으로 읽어 보고 실패하거나 크기가 맞지 않으면 그 태그만 raw(`(struct-kind-unknown:…)`). `structkinds.py`(그 파일 버전으로 만든 표만 쓰고 없으면 거부 4). `exports.py` **`MetaData` 익스포트 접미부**: 1014 미만에서는 패키지 메타데이터가 익스포트다 — 5.1 `MetaData.cpp` `UMetaData::Serialize` = `TMap<FWeakObjectPtr, TMap<FName,FString>>`(약참조 = 패키지 인덱스 i32) + `FEditorObjectVersion >= RootMetaDataSupport`(3) 이면 `TMap<FName,FString>`. 게이트는 `tools/gen_version_gates.py` 로 엔진 소스에서 생성. 단위 테스트 6개 추가(총 27).
- 픽스처 생성기에 `Layout` 항목(WBP_Fix_Child + WBP_Fix_Layout v1·v2): M2 의 손 픽스처와 같은 위젯·위치·텍스트·변화(5.8 저장본을 Python 으로 덤프해 옮겼다). 5.8 의 기존 손 저장본은 그대로 둔다.
- §11.8 의 5.1 분기(DevNotes 없음, SoftObjectPath FString 서브 경로)와 §11.5 의 1010·1011 미만(Script 오프셋·접두 바이트 없음)은 M3/M4 코드가 이미 버전 게이트로 처리하고 있었다 — 5.1 저장본에서 그대로 맞았다.

검증 (5.1.1):
- 5.1 빌드: `RadarHost51Editor` 성공(경고는 엔진 헤더 C4996 하나).
- 픽스처: 5.1 에디터에서 `RadarUMGDiff.BuildFixtures All -force` → 21개 저장(FileVersionUE5 1008), ensure·컴파일 경고 없음.
- Python `dump`: 21개 모두 옛 태그를 표로 읽음. 커버리지 범위 안 **1.0000**, 전체 0.9464(남은 것은 `(struct-data:WidgetBlueprintGeneratedClass)` 13,845 · `(struct-data:Function)` 4,116 — §11.9 범위 밖). 표·MetaData 를 넣기 전은 0.81 이었다(`MetaData` 59,619 · `PropertyGuids` 맵 688).
- 일치: 5.1 커맨드릿 `-run=RadarUMGDiffDump -old=file: -new=file:` 15쌍 vs Python → 판정 불일치 **0**, 보류 0 (`Test/accuracy_report.md`, 5.1 브랜치). 네이티브 diff 는 빈 것이 아니다(Layout 삭제 3·변경 1·이동 1·이름 변경 1·슬롯 1 등).
- 5.8 회귀: 바뀐 Python 으로 5.8 픽스처 15쌍의 diff.json·양쪽 state.json 이 수정 전과 같다(producer·file 칸 제외, 다른 쌍 0/15). 단위 테스트 27개 3.9.7·3.11.8·3.14.7 OK.
- 뷰어(5.1 에디터, `RadarUMGDiff.Open … file:v1 file:v2`): WBP_Fix_Layout 나란히 — 요약 띠·계층(이름 변경?·이동 전 유령 행·삭제 취소선)·미리보기·오버레이(빗금 빨간 상자, 비활성 페이지 칩 "× PageBText", 이동 화살표)·상세(색 칩 FFFFFFFF → FFCB7CFF, DisplayLabel) 가 5.8 과 같다(`m5_layout_side_by_side.png`). data-only: 5.8 CLI(Python, 1018) diff.json 을 `RadarUMGDiff.OpenJson` 으로 5.1 뷰어에 — 미리보기 자리에 안내, 나머지 패널 동일(`m5_data_only_58json.png`). 스크린샷은 `C:\Work\RadarUMGDiff51\Saved\m5\shots\`.
- 5.8 쪽: 공통 커밋을 `5.8` 에 체리픽한 뒤 브로커 정식 빌드(`CL9Editor`) 성공, 경고 0 (`.claude/runs/build-broker/logs/build-20261018-181358.log`).
- 5.1 에디터 첫 실행 때 Windows 방화벽 허용 창이 떴다 — 보안 설정이라 세션이 누르지 않았다(뷰어 동작과 무관).

브랜치 정리: 공통 커밋(소스·Python·README)은 `5.8` 에 체리픽해 두 브랜치의 차이는 `data/struct_kinds_ue5.1.json`·픽스처 저장본·`Test/accuracy_report.md` 뿐이다. 5.1 브랜치는 GitHub 공개 때 다시 쓴 `5.8` 의 M0~M4 커밋(`dc1488e`)+README(`f36cd47`) 위로 옮겼다. push 는 하지 않았다(외부 공개 — 사용자 확인).
