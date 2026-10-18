// Copyright Radar. All Rights Reserved.

#pragma once

// 상세 = 속성 | 이전 | 이후 3열 (dev 문서 §9.4). 선택한 항목의 바뀐 속성만, 분류(category)별로 묶는다.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "RadarDiffModel.h"

class FRadarViewModel;

struct FRadarDetailsRow
{
	bool bHeader = false;
	FString Header;
	FRadarPropertyDiff Prop;
};

class SRadarDetailsGrid : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarDetailsGrid) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel);
	void Rebuild();

private:
	TSharedPtr<FRadarViewModel> Model;
	TArray<TSharedPtr<FRadarDetailsRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FRadarDetailsRow>>> List;
	FText Title;

	void AddGroup(const FString& Prefix, const TArray<FRadarPropertyDiff>& Props);
	void AddLiveDump(bool bOld, const FString& Id);
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FRadarDetailsRow> Row, const TSharedRef<STableViewBase>& Owner);
	FReply OnOpenFullDetails();
	bool CanOpenFullDetails() const;
};
