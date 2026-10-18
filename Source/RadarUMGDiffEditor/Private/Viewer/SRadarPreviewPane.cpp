// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarPreviewPane.h"
#include "Viewer/SRadarOverlay.h"
#include "Viewer/RadarViewModel.h"
#include "RadarRevisionSource.h"

#include "Blueprint/UserWidget.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Settings/WidgetDesignerSettings.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/LayoutUtils.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

// ───────────────────────── SRadarZoomPan (UMGEditor SZoomPan.cpp 와 같은 배치) ─────────────────────────

void SRadarZoomPan::Construct(const FArguments& InArgs)
{
	bHasRelativeLayoutScale = true;
	ViewOffset = InArgs._ViewOffset;
	ZoomAmount = InArgs._ZoomAmount;
	ChildSlot[ InArgs._Content.Widget ];
}

void SRadarZoomPan::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const EVisibility ChildVisibility = ChildSlot.GetWidget()->GetVisibility();
	if (ArrangedChildren.Accepts(ChildVisibility))
	{
		const FMargin SlotPadding(ChildSlot.GetPadding());
		AlignmentArrangeResult XResult = AlignChild<Orient_Horizontal>(AllottedGeometry.Size.X, ChildSlot, SlotPadding, 1);
		AlignmentArrangeResult YResult = AlignChild<Orient_Vertical>(AllottedGeometry.Size.Y, ChildSlot, SlotPadding, 1);
		ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(
			ChildSlot.GetWidget(),
			FVector2D(XResult.Offset, YResult.Offset) - ViewOffset.Get(),
			ChildSlot.GetWidget()->GetDesiredSize(),
			ZoomAmount.Get()));
	}
}

float SRadarZoomPan::GetRelativeLayoutScale(int32 ChildIndex, float LayoutScaleMultiplier) const
{
	return ZoomAmount.Get();
}

// ───────────────────────── SRadarPreviewPane ─────────────────────────

void SRadarPreviewPane::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel)
{
	Model = InModel;
	Side = InArgs._Side;

	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	if (Model->IsDataOnly())
	{
		// 이 패널이 보이는 쪽의 리비전 (겹치기·뒤집기 패널은 새 쪽)
		const FRadarSideInfo& Info = Side == ERadarPaneSide::Old ? Model->Diff->Old : Model->Diff->New;
		const FString Which = !Info.Sha.IsEmpty() ? Info.Sha.Left(7) : (!Info.Revision.IsEmpty() ? TEXT("#") + Info.Revision
			: (!Info.Path.IsEmpty() ? FPaths::GetCleanFilename(Info.Path) : Info.Kind));
		Content = SNew(SBox).Padding(24).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center)
				.Text(FText::Format(LOCTEXT("DataOnly", "이 에디터는 리비전 {0} 를 로드할 수 없어 미리보기가 없습니다.\n생산자: {1}"),
					FText::FromString(Which.IsEmpty() ? FString(TEXT("?")) : Which), FText::FromString(Model->Diff->ProducerName)))
			];
	}
	else
	{
		Content = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SRadarZoomPan)
				.ViewOffset(this, &SRadarPreviewPane::GetViewOffset)
				.ZoomAmount(this, &SRadarPreviewPane::GetZoomAmount)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("UMGEditor.DesignerBackground"))
					.Padding(0)
					[
						SNew(SBox)
						.WidthOverride(this, &SRadarPreviewPane::AreaWidth)
						.HeightOverride(this, &SRadarPreviewPane::AreaHeight)
						[
							SNew(SDPIScaler)
							.DPIScale(this, &SRadarPreviewPane::GetPreviewDPI)
							[
								SNew(SBox)
								.WidthOverride(this, &SRadarPreviewPane::SizeWidth)
								.HeightOverride(this, &SRadarPreviewPane::SizeHeight)
								[
									SAssignNew(Host, SOverlay)
								]
							]
						]
					]
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SRadarOverlay, InModel, SharedThis(this))
			];
	}
	SDesignSurface::Construct(SDesignSurface::FArguments().AllowContinousZoomInterpolation(false)[ Content ]);
	RebuildHost();
}

UUserWidget* SRadarPreviewPane::PreviewFor(bool bOld) const
{
	const TSharedPtr<FRadarDiffSession>& S = Model->Session;
	return S.IsValid() ? (bOld ? S->OldPreview.Get() : S->NewPreview.Get()) : nullptr;
}

void SRadarPreviewPane::DetachPreviews()
{
	if (Host.IsValid())
	{
		Host->ClearChildren();
	}
}

void SRadarPreviewPane::SetSide(ERadarPaneSide InSide)
{
	Side = InSide;
	RebuildHost();
}

void SRadarPreviewPane::RebuildHost()
{
	if (!Host.IsValid())
	{
		return;
	}
	Host->ClearChildren();
	UUserWidget* NewP = PreviewFor(false);
	UUserWidget* OldP = PreviewFor(true);
	if (Side == ERadarPaneSide::Both)
	{
		if (NewP) { Host->AddSlot()[ NewP->TakeWidget() ]; }
		if (OldP)
		{
			Host->AddSlot()
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
				.ColorAndOpacity_Lambda([this]() { return FLinearColor(1, 1, 1, Model->OverlayOpacity); })
				.Visibility(EVisibility::HitTestInvisible)
				[
					OldP->TakeWidget()
				]
			];
		}
	}
	else if (UUserWidget* P = PreviewFor(Side == ERadarPaneSide::Old))
	{
		Host->AddSlot()[ P->TakeWidget() ];
	}
}

FVector2D SRadarPreviewPane::GetPreviewSize() const
{
	UUserWidget* P = PreviewFor(false);
	const FUintVector2 Res = GetDefault<UWidgetDesignerSettings>()->DefaultPreviewResolution;
	const FVector2D Preview(Res.X, Res.Y);
	if (!P)
	{
		return Preview;
	}
	const TSharedPtr<SWidget> Root = P->GetCachedWidget();
	const FVector2D Desired = Root.IsValid() ? FVector2D(Root->GetDesiredSize()) : FVector2D::ZeroVector;
	// 두 패널 모두 새 쪽의 DesignSizeMode/DesignTimeSize (goal: "forced to the same preview area")
	return FWidgetBlueprintEditorUtils::GetWidgetPreviewAreaAndSize(P, Desired, Preview, P->DesignSizeMode, TOptional<FVector2D>()).Get<1>();
}

FVector2D SRadarPreviewPane::GetAreaSize() const
{
	UUserWidget* P = PreviewFor(false);
	const FUintVector2 Res = GetDefault<UWidgetDesignerSettings>()->DefaultPreviewResolution;
	const FVector2D Preview(Res.X, Res.Y);
	if (!P)
	{
		return Preview;
	}
	const TSharedPtr<SWidget> Root = P->GetCachedWidget();
	const FVector2D Desired = Root.IsValid() ? FVector2D(Root->GetDesiredSize()) : FVector2D::ZeroVector;
	return FWidgetBlueprintEditorUtils::GetWidgetPreviewAreaAndSize(P, Desired, Preview, P->DesignSizeMode, TOptional<FVector2D>()).Get<0>();
}

float SRadarPreviewPane::GetPreviewDPI() const
{
	// 디자이너와 같은 공식 (SDesignerView::GetPreviewDPIScale → GetWidgetPreviewDPIScale(위젯, 미리보기 해상도))
	UUserWidget* P = PreviewFor(false);
	const FUintVector2 Res = GetDefault<UWidgetDesignerSettings>()->DefaultPreviewResolution;
	return P ? FWidgetBlueprintEditorUtils::GetWidgetPreviewDPIScale(P, FVector2D(Res.X, Res.Y)) : 1.f;
}

FSlateRect SRadarPreviewPane::ComputeAreaBounds() const
{
	const FVector2D Area = GetAreaSize() * GetPreviewDPI();
	return FSlateRect(0, 0, Area.X, Area.Y);
}

void SRadarPreviewPane::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SDesignSurface::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Model->IsDataOnly())
	{
		return;
	}
	if (!bInitialFit && AllottedGeometry.GetLocalSize().X > 1.f)
	{
		bInitialFit = true;
		if (Model->SharedZoomLevel == INDEX_NONE)
		{
			ZoomToFit(true);
			Model->SharedViewOffset = ViewOffset;
			Model->SharedZoomLevel = ZoomLevel;
			++Model->SharedViewVersion;
			SeenViewVersion = Model->SharedViewVersion;
			LastOffset = ViewOffset;
			LastZoom = ZoomLevel;
			return;
		}
	}
	// 보기 공유: 남이 바꿨으면 받아 오고, 내가 바꿨으면 내보낸다
	if (Model->SharedViewVersion != SeenViewVersion)
	{
		ViewOffset = Model->SharedViewOffset;
		if (Model->SharedZoomLevel != INDEX_NONE)
		{
			ZoomLevel = Model->SharedZoomLevel;
			PreviousZoomLevel = ZoomLevel;
		}
		SeenViewVersion = Model->SharedViewVersion;
	}
	else if (ViewOffset != LastOffset || ZoomLevel != LastZoom)
	{
		Model->SharedViewOffset = ViewOffset;
		Model->SharedZoomLevel = ZoomLevel;
		SeenViewVersion = ++Model->SharedViewVersion;
	}
	LastOffset = ViewOffset;
	LastZoom = ZoomLevel;
}

bool SRadarPreviewPane::GetRootLocalRect(bool bOld, const FString& WidgetId, FBox2D& OutRect) const
{
	UUserWidget* P = PreviewFor(bOld);
	if (!P)
	{
		return false;
	}
	FString Name = WidgetId;
	Name.Split(TEXT("."), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	const TSharedPtr<SWidget> Root = P->GetCachedWidget();
	UWidget* W = P->GetWidgetFromName(FName(*Name));
	const TSharedPtr<SWidget> S = W ? W->GetCachedWidget() : nullptr;
	if (!Root.IsValid() || !S.IsValid())
	{
		return false;
	}
	const FGeometry& RG = Root->GetPaintSpaceGeometry();
	const FGeometry& WG = S->GetPaintSpaceGeometry();
	if (WG.GetLocalSize().X <= 0.f || WG.GetLocalSize().Y <= 0.f || RG.Scale <= 0.f || !W->IsVisible())
	{
		return false;
	}
#if WITH_SLATE_DEBUGGING
	// 그려진 위젯만 지오메트리가 유효하다 (Collapsed·비활성 스위처 페이지·스크롤 밖)
	if (S->Debug_GetLastPaintFrame() + 1 < GFrameNumber)
	{
		return false;
	}
#endif
	const FVector2D Pos = (FVector2D(WG.GetAbsolutePosition()) - FVector2D(RG.GetAbsolutePosition())) / RG.Scale;
	const FVector2D Size = FVector2D(WG.GetAbsoluteSize()) / RG.Scale;
	OutRect = FBox2D(Pos, Pos + Size);
	return true;
}

FReply SRadarPreviewPane::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MouseLocal = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	bMouseInside = true;
	return SDesignSurface::OnMouseMove(MyGeometry, MouseEvent);
}

void SRadarPreviewPane::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	bMouseInside = false;
	SDesignSurface::OnMouseLeave(MouseEvent);
}

void SRadarPreviewPane::FocusOn(const FString& WidgetId)
{
	FBox2D Rect;
	const bool bOld = Side == ERadarPaneSide::Old;
	if (!GetRootLocalRect(bOld, WidgetId, Rect) && !GetRootLocalRect(!bOld, WidgetId, Rect))
	{
		return;
	}
	const FVector2D Center = Rect.GetCenter() * GetPreviewDPI();
	const FVector2D PaneSize = GetCachedGeometry().GetLocalSize();
	ViewOffset = Center * GetZoomAmount() - PaneSize * 0.5f;
	Model->SharedViewOffset = ViewOffset;
	Model->SharedZoomLevel = ZoomLevel;
	SeenViewVersion = ++Model->SharedViewVersion;
	LastOffset = ViewOffset;
}

#undef LOCTEXT_NAMESPACE
