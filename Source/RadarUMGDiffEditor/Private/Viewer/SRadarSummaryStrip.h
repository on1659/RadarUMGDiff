// Copyright Radar. All Rights Reserved.

#pragma once

// 요약 띠 (dev 문서 §9.2): 종류×영역 칩(누르면 필터), 검색, 변경만 / 변경 없음 흐리게 / 자동 값 숨기기.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FRadarViewModel;

class SRadarSummaryStrip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarSummaryStrip) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel);

private:
	TSharedPtr<FRadarViewModel> Model;
	TSharedRef<SWidget> MakeChip(const FString& Key, const FText& Label, int32 Count, bool bArea, const TCHAR* Capability);
	void Toggle(TSet<FString>& Set, const FString& Key);
};
