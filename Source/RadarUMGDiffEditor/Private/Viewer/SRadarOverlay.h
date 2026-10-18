// Copyright Radar. All Rights Reserved.

#pragma once

// 오버레이 (dev 문서 §9.6): 매 프레임 두 미리보기의 살아 있는 지오메트리에서 상자를 다시 계산한다. 캡처해 두지 않는다.
// 바뀐 위젯 외곽선, 추가 외곽선, 이동 유령 상자, 지워진 위젯은 새 쪽의 "있던 자리"에 빗금 점선 빨간 상자.

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FRadarViewModel;
class SRadarPreviewPane;

class SRadarOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel, TSharedRef<SRadarPreviewPane> InPane);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedPtr<FRadarViewModel> Model;
	TWeakPtr<SRadarPreviewPane> Pane;

	bool OldRect(const FString& Id, FBox2D& Out) const;
	bool NewRect(const FString& Id, FBox2D& Out) const;
	/** 지워진 위젯의 옛 부모 사슬에서 새 쪽에도 있는 가장 가까운 조상. */
	FString SurvivingAncestor(const FString& RemovedId) const;
};
