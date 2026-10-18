// Copyright Radar. All Rights Reserved.

#pragma once

// 애니메이션 탭·그래프 탭 (dev 문서 §9.7).

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FRadarViewModel;

/** 한 줄: 애니메이션/트랙, 또는 그래프/노드. */
struct FRadarTabRow
{
	FString Id;          // 선택 id (애니메이션 이름, "그래프/노드GUID")
	FString Label;
	FString Detail;
	int32 Depth = 0;
	bool bSelectable = true;
};

class SRadarAnimationTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarAnimationTab) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel);
	void Rebuild();
private:
	TSharedPtr<FRadarViewModel> Model;
	TArray<TSharedPtr<FRadarTabRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FRadarTabRow>>> List;
};

class SRadarGraphTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarGraphTab) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel);
	void Rebuild();
private:
	TSharedPtr<FRadarViewModel> Model;
	TArray<TSharedPtr<FRadarTabRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FRadarTabRow>>> List;
	FReply OpenBlueprintDiff();
};
