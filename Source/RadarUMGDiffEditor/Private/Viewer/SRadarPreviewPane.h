// Copyright Radar. All Rights Reserved.

#pragma once

// 미리보기 패널 (dev 문서 §9.5): SDesignSurface → SRadarZoomPan → SBox(영역) → SDPIScaler → SBox(크기) → UUserWidget::TakeWidget().
// 두 패널은 같은 디자인 크기(새 쪽 DesignSizeMode/DesignTimeSize)를 쓰고, 보기(ViewOffset·ZoomLevel)를 뷰 모델로 공유한다.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Designer/SDesignSurface.h"

class FRadarViewModel;
class UUserWidget;
class SBox;
class SOverlay;

/** UMGEditor 의 SZoomPan 과 같은 동작 (5.1 은 SZoomPan 이 Private 이라 플러그인이 직접 가진다, §3.3). */
class SRadarZoomPan : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarZoomPan) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(FVector2D, ViewOffset)
		SLATE_ATTRIBUTE(float, ZoomAmount)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual float GetRelativeLayoutScale(int32 ChildIndex, float LayoutScaleMultiplier) const override;

private:
	TAttribute<FVector2D> ViewOffset;
	TAttribute<float> ZoomAmount;
};

/** 패널이 보여 주는 쪽. */
enum class ERadarPaneSide : uint8 { Old, New, Both /* 겹치기: 새 쪽 위에 옛 쪽을 반투명으로 */ };

class SRadarPreviewPane : public SDesignSurface
{
public:
	SLATE_BEGIN_ARGS(SRadarPreviewPane) {}
		SLATE_ARGUMENT(ERadarPaneSide, Side)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel);

	/** 이 패널에 보일 쪽을 바꾼다 (모드 전환 — TakeWidget 은 부모를 하나만 가질 수 있어 다시 붙인다). */
	void SetSide(ERadarPaneSide InSide);
	ERadarPaneSide GetSide() const { return Side; }
	/** 미리보기 위젯을 떼어 낸다 (다른 패널이 같은 UUserWidget 을 붙이기 전에). */
	void DetachPreviews();

	/** 위젯 하나가 가운데 오도록 이동한다 (계층 더블클릭·다음/이전). */
	void FocusOn(const FString& WidgetId);

	/** 마우스 위치 (이 패널 로컬, 오버레이와 같은 좌표). 패널 밖이면 false. */
	bool GetMouseLocal(FVector2D& Out) const { Out = MouseLocal; return bMouseInside; }

	// SDesignSurface
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FSlateRect ComputeAreaBounds() const override;
	virtual int32 GetSnapGridSize() const override { return 8; }

	float GetPreviewDPI() const;
	FVector2D GetAreaSize() const;
	FVector2D GetPreviewSize() const;

	/** 미리보기 루트 기준(DPI·줌 전) 사각형. 그려지지 않았으면 false. */
	bool GetRootLocalRect(bool bOld, const FString& WidgetId, FBox2D& OutRect) const;

private:
	TSharedPtr<FRadarViewModel> Model;
	ERadarPaneSide Side = ERadarPaneSide::New;
	TSharedPtr<SOverlay> Host;
	bool bInitialFit = false;
	uint32 SeenViewVersion = 0;
	FVector2D LastOffset = FVector2D::ZeroVector;
	int32 LastZoom = INDEX_NONE;
	FVector2D MouseLocal = FVector2D::ZeroVector;
	bool bMouseInside = false;

	void RebuildHost();
	UUserWidget* PreviewFor(bool bOld) const;
	FOptionalSize AreaWidth() const { return static_cast<float>(GetAreaSize().X); }
	FOptionalSize AreaHeight() const { return static_cast<float>(GetAreaSize().Y); }
	FOptionalSize SizeWidth() const { return static_cast<float>(GetPreviewSize().X); }
	FOptionalSize SizeHeight() const { return static_cast<float>(GetPreviewSize().Y); }
};
