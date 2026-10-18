// Copyright Radar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "RadarRevisionSource.h"

class UWidgetBlueprint;
class SBox;

/**
 * 리비전 피커 (dev 문서 §9.9). 위에 백엔드 탭(가능한 것만, §8.0 순서), 가운데 목록, 아래 비교 버튼.
 * 목록 맨 위에는 항상 "작업 사본", 작업 사본이 더러우면 HEAD / 최신 체크인.
 */
class SRadarRevisionPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarRevisionPicker) {}
		SLATE_ARGUMENT(TWeakPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWidgetBlueprint* InBlueprint);

private:
	using FItem = TSharedPtr<FRadarRevision>;

	TWeakObjectPtr<UWidgetBlueprint> Blueprint;
	TWeakPtr<SWindow> ParentWindow;
	FString AssetFile;

	TArray<TSharedRef<IRadarRevisionSource>> Sources;   // 가능한 것만
	int32 ActiveSource = INDEX_NONE;

	TArray<FItem> Items;
	TSharedPtr<SListView<FItem>> List;
	TSharedPtr<SBox> BodyBox;
	FText StatusText;
	FString FileOld;
	FString FileNew;

	void SelectSource(int32 Index);
	void RebuildBody();
	TSharedRef<SWidget> MakeSourceTabs();
	TSharedRef<SWidget> MakeFilesForm();
	TSharedRef<ITableRow> OnGenerateRow(FItem Item, const TSharedRef<STableViewBase>& Owner);

	FReply OnCompareWithPrevious();
	FReply OnCompareWithWorking();
	FReply OnCompareSelected();
	FReply OnCompareFiles();
	FReply OnBrowse(bool bOld);
	FReply OnOpenJson();

	TSharedPtr<IRadarRevisionSource> SourceFor(const FRadarRevision& Revision) const;
	void OpenDiff(const FRadarRevision& Old, const FRadarRevision& New);
	FText GetFooterText() const;
};
