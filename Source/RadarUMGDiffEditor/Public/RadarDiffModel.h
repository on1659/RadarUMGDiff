// Copyright Radar. All Rights Reserved.

#pragma once

// diff.json 과 1:1 인 C++ 모델 (dev 문서 §4·§5). Python radar_umgdiff/diff.py 와 같은 규칙을 쓴다.

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

enum class ERadarKind : uint8
{
	Unchanged,
	Added,
	Removed,
	Changed,
	Moved,
	RenamedProbable,
	TextKeyOnly,
};

enum class ERadarArea : uint8
{
	Designer,
	Slot,
	Binding,
	Cdo,
	Anim,
	GraphLogic,
	GraphLayout,
};

enum class ERadarSeverity : uint8
{
	Normal,
	Low,
	Info,
};

enum class ERadarValueState : uint8
{
	Present,
	Absent,
	Default,
};

RADARUMGDIFFEDITOR_API const TCHAR* LexToString(ERadarKind Kind);
RADARUMGDIFFEDITOR_API const TCHAR* LexToString(ERadarArea Area);
RADARUMGDIFFEDITOR_API const TCHAR* LexToString(ERadarSeverity Severity);
RADARUMGDIFFEDITOR_API const TCHAR* LexToString(ERadarValueState State);

/** 속성 값 하나 (한쪽 리비전). */
struct FRadarValue
{
	bool bSet = false;                       // false 면 JSON null (그쪽에 없음)
	ERadarValueState State = ERadarValueState::Present;
	FString Text;                            // ExportText 와 같은 사람용 문자열
	TSharedPtr<FJsonValue> Value;            // §11.7 "JSON" 열의 형
	FString Asset;                           // 오브젝트·소프트 참조면 경로
};

/** 속성 한 줄 (§5 properties[]). */
struct FRadarPropertyDiff
{
	FString Path;                            // 점 구분, 배열 [i]
	FString Category;
	FString Type;                            // <PropertyClass>[:<Struct|Enum|Inner>]
	ERadarKind Kind = ERadarKind::Changed;
	bool bMaybeDefault = false;
	bool bAuto = false;                      // ini AutoProperties (디자이너가 자동으로 바꾸기도 하는 값)
	FRadarValue Old;
	FRadarValue New;
	TArray<FString> ChangedComponents;       // 잎 구조체(ini LeafStructs)의 바뀐 멤버
};

struct FRadarOptionalString
{
	bool bSet = false;
	FString Value;
	FRadarOptionalString() = default;
	FRadarOptionalString(const FString& In) : bSet(true), Value(In) {}
};

struct FRadarRename
{
	FString From;
	double Confidence = 0.0;
	FString Evidence;                        // same-parent-index | property-set
	bool bGraphConfirmed = false;
};

struct FRadarSlotDiff
{
	FRadarOptionalString ClassOld;
	FRadarOptionalString ClassNew;
	TArray<FRadarPropertyDiff> Properties;
};

struct FRadarWidgetDiff
{
	FString Id;                              // WidgetTree.HealthBar
	ERadarKind Kind = ERadarKind::Unchanged;
	ERadarArea Area = ERadarArea::Designer;
	ERadarSeverity Severity = ERadarSeverity::Normal;
	FString Class;
	bool bMovedToo = false;                  // moved + changed 를 한 행에
	FRadarOptionalString LabelOld, LabelNew;
	FRadarOptionalString ParentOld, ParentNew;
	int32 IndexOld = INDEX_NONE;             // INDEX_NONE → null
	int32 IndexNew = INDEX_NONE;
	bool bHasOld = false;
	bool bHasNew = false;
	TArray<FString> Badges;
	TArray<FRadarPropertyDiff> Properties;
	FRadarSlotDiff Slot;
	TOptional<FRadarRename> Rename;
	FString StateRef;                        // removed/added 위젯: state.json objects[] 의 id
};

struct FRadarBindingDiff
{
	FString Id;                              // ObjectName:PropertyName
	ERadarKind Kind = ERadarKind::Changed;
	FRadarOptionalString OldFunction, OldKind, OldSourceProperty;
	FRadarOptionalString NewFunction, NewKind, NewSourceProperty;
};

/** 채널 키 하나 (프레임으로 짝짓는다). 값은 키의 Value, 나머지 멤버(보간·탄젠트)는 Properties. */
struct FRadarKeyDiff
{
	int32 Frame = 0;
	ERadarKind Kind = ERadarKind::Changed;
	FRadarValue Old;
	FRadarValue New;
	TArray<FRadarPropertyDiff> Properties;
};

/** 섹션의 채널 하나. 이름 = 섹션 속성 이름, 정적 배열의 1번 이후 원소는 이름[i] (0번은 이름만 — Python 태그 키와 같은 규칙). */
struct FRadarChannelDiff
{
	FString Name;
	FString Type;                            // 채널 구조체 이름 (MovieSceneFloatChannel …)
	ERadarKind Kind = ERadarKind::Changed;
	TArray<FRadarPropertyDiff> Properties;   // 채널 자체 값 (DefaultValue·외삽 등) — 양쪽에 다 있을 때만
	TArray<FRadarKeyDiff> Keys;
};

/** 트랙 안 섹션 하나 (트랙 안 인덱스로 짝짓는다). */
struct FRadarSectionDiff
{
	int32 Index = 0;
	ERadarKind Kind = ERadarKind::Changed;
	bool bHasRangeOld = false, bHasRangeNew = false;
	TOptional<int32> RangeOld[2], RangeNew[2];   // 열린 경계는 unset
	TArray<FRadarPropertyDiff> Properties;   // 채널·범위·서명 밖의 섹션 속성
	TArray<FRadarChannelDiff> Channels;
};

struct FRadarTrackDiff
{
	FString Id;                              // <바인딩 이름 또는 Root>/<트랙 클래스>
	ERadarKind Kind = ERadarKind::Changed;
	int32 SectionsOld = INDEX_NONE;
	int32 SectionsNew = INDEX_NONE;
	TArray<FRadarSectionDiff> Sections;
};

struct FRadarAnimationDiff
{
	FString Id;
	ERadarKind Kind = ERadarKind::Changed;
	TArray<FRadarTrackDiff> Tracks;
};

struct FRadarPinDiff
{
	FString PinId;
	FString Name;
	ERadarKind Kind = ERadarKind::Changed;
	TArray<FRadarPropertyDiff> Properties;   // DefaultValue / DefaultObject / DefaultTextValue / LinkedTo / PinType
};

struct FRadarNodeDiff
{
	FString Guid;
	FString Class;
	FString Title;
	ERadarKind Kind = ERadarKind::Changed;
	ERadarArea Area = ERadarArea::GraphLogic;
	TArray<FRadarPropertyDiff> Properties;
	TArray<FRadarPinDiff> Pins;
};

struct FRadarVariableDiff
{
	FString Name;
	FString VarGuid;
	ERadarKind Kind = ERadarKind::Changed;
	FString RenamedFrom;
	TArray<FRadarPropertyDiff> Properties;
};

struct FRadarGraphDiff
{
	FString Id;
	ERadarKind Kind = ERadarKind::Changed;
	int32 LogicAdded = 0, LogicRemoved = 0, LogicChanged = 0, LayoutMoved = 0;
	TArray<FRadarNodeDiff> Nodes;
};

struct FRadarNote
{
	FString Code;
	FString Widget;
	ERadarSeverity Severity = ERadarSeverity::Info;
};

/** 리비전 한쪽의 출처 (§5 old/new). */
struct FRadarSideInfo
{
	FString Kind;                            // scm | commit | head | working | file
	FString Sha, Head, Author, Subject, Date;
	FString Provider, Revision, User, Description;
	int32 Changelist = INDEX_NONE;
	FString Path;
	bool bDirty = false;
	int32 FileVersionUE5 = 0;
	TArray<TPair<FString, int32>> CustomVersions;  // GUID(엔진 표기) → 값
	FString DesignSizeMode;
	FVector2D DesignSize = FVector2D::ZeroVector;
};

/** 한 리비전의 위젯 트리 (계층 패널의 변경 없는 행까지 그리기 위함 — diff.json 에는 바뀐 위젯만 있다). */
struct FRadarTreeSnapshot
{
	bool bValid = false;
	FString Root;
	TMap<FString, TArray<FString>> Children;   // 부모 id → 자식 id (부모 안 순서)
	TMap<FString, FString> Labels;
	TMap<FString, FString> Classes;
};

struct FRadarDiff
{
	FString ProducerName = TEXT("RadarUMGDiff.Native");
	FString ProducerKind = TEXT("native");
	FString Engine;
	FString PluginBranch;
	TArray<FString> Capabilities;

	FString PackageName;                     // /Game/UI/WBP_HUD
	FString RelPath;                         // Content/UI/WBP_HUD.uasset
	FString AssetName;
	FString ParentClass;

	FRadarSideInfo Old;
	FRadarSideInfo New;

	TArray<FRadarWidgetDiff> Widgets;
	TArray<FRadarBindingDiff> Bindings;
	TArray<FRadarPropertyDiff> Cdo;          // CDO 속성 줄 (BP 변수면 VarGuids 에 짝)
	TMap<FString, FString> CdoVarGuids;      // 속성 경로 루트 → VarGuid
	TArray<FRadarAnimationDiff> Animations;
	TArray<FRadarGraphDiff> Graphs;
	TArray<FRadarVariableDiff> Variables;
	TArray<FRadarNote> Notes;
	int32 TextKeyCount = 0;                  // Python 생산자의 textKeys 수 (네이티브는 0)

	// 직렬화하지 않는 뷰어용 정보
	FRadarTreeSnapshot OldTree;
	FRadarTreeSnapshot NewTree;
	FString SourcePath;                      // 파일에서 연 diff.json 경로 (data-only)
};

/** 한 리비전의 객체 단위 전체 상태 (네이티브 state.json). removed/added 위젯의 속성 표가 여기를 가리킨다. */
struct FRadarStateObject
{
	FString Id;
	FString Class;
	TArray<FRadarPropertyDiff> Properties;   // Old 만 채워 쓴다 (값 하나)
};

struct FRadarState
{
	FString PackageName;
	FRadarSideInfo Side;
	TArray<FRadarStateObject> Objects;
	FString RootWidget;
	TArray<TPair<FString, TArray<FString>>> Children;
};
