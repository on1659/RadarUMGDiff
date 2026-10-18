// Copyright Radar. All Rights Reserved.

#pragma once

// 뷰어 상태와 파생 데이터 (dev 문서 §9.1·9.3): 병합 계층, 필터, 다음/이전이 걷는 평탄 리스트.
// 입력 (a) diff.json 모델만 읽는다 — 미리보기·오버레이(입력 (b))는 SRadarPreviewPane/SRadarOverlay 가 세션에서 읽는다.

#include "CoreMinimal.h"
#include "RadarDiffModel.h"

class FRadarDiffSession;

enum class ERadarPreviewMode : uint8 { SideBySide, Overlay, Flip };
enum class ERadarTab : uint8 { Designer, Animation, Graph };

/** 계층 패널의 행 하나. */
struct FRadarTreeNode : public TSharedFromThis<FRadarTreeNode>
{
	enum class ERow : uint8 { Widget, Removed, Ghost, Bindings, Cdo };

	ERow Row = ERow::Widget;
	FString Id;                                   // 위젯 id (가짜 행은 "(bindings)" / "(cdo)")
	FString Label;
	FString Class;
	ERadarKind Kind = ERadarKind::Unchanged;
	bool bMovedToo = false;
	int32 DescendantChanges = 0;
	const FRadarWidgetDiff* Diff = nullptr;
	TArray<TSharedPtr<FRadarTreeNode>> Children;
	TWeakPtr<FRadarTreeNode> Parent;
	bool bSearchMatch = false;
};

/** 다음/이전이 걷는 항목. */
struct FRadarFlatItem
{
	ERadarTab Tab = ERadarTab::Designer;
	FString Id;
};

class FRadarViewModel : public TSharedFromThis<FRadarViewModel>
{
public:
	FRadarViewModel(TSharedRef<FRadarDiff> InDiff, TSharedPtr<FRadarDiffSession> InSession);

	TSharedRef<FRadarDiff> Diff;
	TSharedPtr<FRadarDiffSession> Session;        // null 이면 data-only (§9.8)

	// ── 상태 (FRadarViewState) ──
	ERadarPreviewMode Mode = ERadarPreviewMode::SideBySide;
	float OverlayOpacity = 0.5f;
	bool bChangedOnly = true;
	bool bDimUnchanged = true;
	bool bHideAuto = true;
	bool bFlipShowOld = false;
	TSet<FString> KindFilter;                     // 비면 전부
	TSet<FString> AreaFilter;                     // 비면 전부
	FString SearchText;
	FString SelectedId;
	int32 CursorIndex = INDEX_NONE;
	ERadarTab Tab = ERadarTab::Designer;

	// 두 미리보기가 공유하는 보기 (SDesignSurface 의 ViewOffset/ZoomLevel)
	FVector2D SharedViewOffset = FVector2D::ZeroVector;
	int32 SharedZoomLevel = INDEX_NONE;
	uint32 SharedViewVersion = 0;

	/** 마지막으로 유효했던 위젯 사각형 (미리보기 루트 기준). 뒤집기처럼 한쪽 패널이 그려지지 않을 때 쓴다. */
	TMap<FString, FBox2D> LastOldRects;
	TMap<FString, FBox2D> LastNewRects;

	bool IsDataOnly() const;
	bool HasCapability(const TCHAR* Capability) const;

	// ── 파생 ──
	const TArray<TSharedPtr<FRadarTreeNode>>& GetRoots() const { return Roots; }
	const TArray<FRadarFlatItem>& GetFlatList() const { return Flat; }
	const FRadarWidgetDiff* FindWidget(const FString& Id) const;
	TSharedPtr<FRadarTreeNode> FindNode(const FString& Id) const;

	/** 필터·검색에 따라 행을 보일지. */
	bool IsVisible(const TSharedPtr<FRadarTreeNode>& Node) const;
	/** 위젯 항목이 필터를 통과하는 "바뀐" 항목인지 (오버레이·다음/이전 공용). */
	bool PassesFilter(const FRadarWidgetDiff& W) const;
	/** 속성 줄이 보이는지 (자동 값 숨기기). */
	bool ShowProperty(const FRadarPropertyDiff& P) const { return !(bHideAuto && P.bAuto); }

	/** 필터·검색이 바뀐 뒤 다시 계산한다. */
	void Refresh();

	/** 다음/이전: 평탄 리스트를 한 칸 걷고 선택을 바꾼다. 반환 = 새 항목(없으면 null). */
	const FRadarFlatItem* Step(int32 Delta);

	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnSelectionChanged;
	FOnChanged OnFilterChanged;
	FOnChanged OnModeChanged;

	void Select(const FString& Id, ERadarTab InTab);

private:
	TArray<TSharedPtr<FRadarTreeNode>> Roots;
	TMap<FString, TSharedPtr<FRadarTreeNode>> NodeById;
	TMap<FString, const FRadarWidgetDiff*> WidgetById;
	TArray<FRadarFlatItem> Flat;

	void BuildTree();
	void CountDescendants(const TSharedPtr<FRadarTreeNode>& Node);
	void ApplySearch();
	bool MatchesSearch(const FRadarTreeNode& Node) const;
	void BuildFlat();
	bool WidgetChangedForFilter(const FRadarWidgetDiff* W) const;
};
