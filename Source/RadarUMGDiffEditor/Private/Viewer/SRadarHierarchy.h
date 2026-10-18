// Copyright Radar. All Rights Reserved.

#pragma once

// 병합 계층 (dev 문서 §9.3): 새 트리 기준, 지워진 위젯은 옛 인덱스에 빗금/취소 행, 이동은 옛 자리에 유령 행.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Viewer/RadarViewModel.h"

class SRadarHierarchy : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarHierarchy) {}
		SLATE_EVENT(FSimpleDelegate, OnFocusRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel);
	void Rebuild();
	void SyncSelection();

private:
	using FNode = TSharedPtr<FRadarTreeNode>;
	TSharedPtr<FRadarViewModel> Model;
	TSharedPtr<STreeView<FNode>> Tree;
	TArray<FNode> VisibleRoots;
	FSimpleDelegate OnFocusRequested;
	bool bSyncing = false;

	TSharedRef<ITableRow> OnGenerateRow(FNode Node, const TSharedRef<STableViewBase>& Owner);
	void OnGetChildren(FNode Node, TArray<FNode>& Out);
	void OnSelectionChanged(FNode Node, ESelectInfo::Type Info);
	void ExpandAll(const TArray<FNode>& Nodes);
};
