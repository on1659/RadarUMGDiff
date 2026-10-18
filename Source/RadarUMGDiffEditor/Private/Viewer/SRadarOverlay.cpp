// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarOverlay.h"
#include "Viewer/SRadarPreviewPane.h"
#include "Viewer/RadarViewModel.h"
#include "RadarRevisionSource.h"
#include "RadarUMGDiffSettings.h"

#include "Blueprint/UserWidget.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace
{
	struct FPainter
	{
		const FGeometry& Geo;
		FSlateWindowElementList& Out;
		int32 Layer;
		const URadarUMGDiffSettings& S;

		/** 루트 기준(DPI·줌 전) 사각형 → 이 오버레이의 로컬 좌표. */
		FBox2D ToLocal(const FBox2D& RootLocal, const FGeometry& RootPaint) const
		{
			const FVector2D AbsMin = FVector2D(RootPaint.GetAbsolutePosition()) + RootLocal.Min * RootPaint.Scale;
			const FVector2D AbsMax = FVector2D(RootPaint.GetAbsolutePosition()) + RootLocal.Max * RootPaint.Scale;
			return FBox2D(FVector2D(Geo.AbsoluteToLocal(AbsMin)), FVector2D(Geo.AbsoluteToLocal(AbsMax)));
		}

		void Rect(const FBox2D& R, const FLinearColor& Color, float Thickness, bool bDashed, int32 ExtraLayer = 0) const
		{
			TArray<FVector2f> Pts = { FVector2f(R.Min.X, R.Min.Y), FVector2f(R.Max.X, R.Min.Y), FVector2f(R.Max.X, R.Max.Y), FVector2f(R.Min.X, R.Max.Y), FVector2f(R.Min.X, R.Min.Y) };
			// 1px 어두운 테두리(halo) 위에 색 선 (§9.6: hue 만으로 구별하지 않게)
			TArray<FVector2f> Halo = Pts;
			FSlateDrawElement::MakeLines(Out, Layer + ExtraLayer, Geo.ToPaintGeometry(), MoveTemp(Halo), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.85f), true, Thickness + 2.f * S.HaloThickness);
			if (bDashed)
			{
				FSlateDrawElement::MakeDashedLines(Out, Layer + ExtraLayer + 1, Geo.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, Color, Thickness, S.RemovedBoxDashLength);
			}
			else
			{
				FSlateDrawElement::MakeLines(Out, Layer + ExtraLayer + 1, Geo.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, Color, true, Thickness);
			}
		}

		void Hatch(const FBox2D& R, const FLinearColor& Color) const
		{
			const float Step = 8.f;
			const float W = R.Max.X - R.Min.X, H = R.Max.Y - R.Min.Y;
			for (float d = Step; d < W + H; d += Step)
			{
				// x + y = d 인 대각선을 사각형 안으로 자른다
				FVector2f A(R.Min.X + FMath::Min(d, W), R.Min.Y + FMath::Max(0.f, d - W));
				FVector2f B(R.Min.X + FMath::Max(0.f, d - H), R.Min.Y + FMath::Min(d, H));
				FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), TArray<FVector2f>{ A, B }, ESlateDrawEffect::None, Color.CopyWithNewOpacity(0.35f), true, 1.f);
			}
		}

		void Fill(const FBox2D& R, const FLinearColor& Color, int32 ExtraLayer = 0) const
		{
			if (R.Max.X <= R.Min.X || R.Max.Y <= R.Min.Y)
			{
				return;
			}
			FSlateDrawElement::MakeBox(Out, Layer + ExtraLayer, Geo.ToPaintGeometry(FVector2f(R.Max - R.Min), FSlateLayoutTransform(FVector2f(R.Min))),
				FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
		}

		void Label(const FVector2D& At, const FString& Text, const FLinearColor& Color, int32 ExtraLayer = 3) const
		{
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
			const FVector2D Size(Text.Len() * 7.5f + 8.f, 16.f);
			Fill(FBox2D(At, At + Size), FLinearColor(0, 0, 0, 0.8f), ExtraLayer);
			FSlateDrawElement::MakeText(Out, Layer + ExtraLayer + 1, Geo.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(At + FVector2D(4, 1)))), Text, Font, ESlateDrawEffect::None, Color);
		}
	};

	/** 덮개에서 사각형들을 뚫은 나머지 칸 (x·y 경계로 나눈 격자 중 어느 사각형에도 안 든 칸). */
	void PunchedCells(const FBox2D& Area, const TArray<FBox2D>& Holes, TArray<FBox2D>& OutCells)
	{
		TArray<float> Xs = { static_cast<float>(Area.Min.X), static_cast<float>(Area.Max.X) };
		TArray<float> Ys = { static_cast<float>(Area.Min.Y), static_cast<float>(Area.Max.Y) };
		for (const FBox2D& H : Holes)
		{
			Xs.Add(FMath::Clamp<float>(H.Min.X, Area.Min.X, Area.Max.X)); Xs.Add(FMath::Clamp<float>(H.Max.X, Area.Min.X, Area.Max.X));
			Ys.Add(FMath::Clamp<float>(H.Min.Y, Area.Min.Y, Area.Max.Y)); Ys.Add(FMath::Clamp<float>(H.Max.Y, Area.Min.Y, Area.Max.Y));
		}
		Xs.Sort(); Ys.Sort();
		for (int32 i = 0; i + 1 < Xs.Num(); ++i)
		{
			for (int32 j = 0; j + 1 < Ys.Num(); ++j)
			{
				const FBox2D Cell(FVector2D(Xs[i], Ys[j]), FVector2D(Xs[i + 1], Ys[j + 1]));
				if (Cell.Max.X - Cell.Min.X < 0.5f || Cell.Max.Y - Cell.Min.Y < 0.5f)
				{
					continue;
				}
				const FVector2D C = Cell.GetCenter();
				bool bInHole = false;
				for (const FBox2D& H : Holes) { bInHole |= H.IsInside(C); }
				if (!bInHole) { OutCells.Add(Cell); }
			}
		}
	}
}

void SRadarOverlay::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel, TSharedRef<SRadarPreviewPane> InPane)
{
	Model = InModel;
	Pane = InPane;
	SetVisibility(EVisibility::HitTestInvisible);
}

bool SRadarOverlay::OldRect(const FString& Id, FBox2D& Out) const
{
	TSharedPtr<SRadarPreviewPane> P = Pane.Pin();
	if (P.IsValid() && P->GetRootLocalRect(true, Id, Out))
	{
		Model->LastOldRects.Add(Id, Out);
		return true;
	}
	if (const FBox2D* Last = Model->LastOldRects.Find(Id))
	{
		Out = *Last;
		return true;
	}
	return false;
}

bool SRadarOverlay::NewRect(const FString& Id, FBox2D& Out) const
{
	TSharedPtr<SRadarPreviewPane> P = Pane.Pin();
	if (P.IsValid() && P->GetRootLocalRect(false, Id, Out))
	{
		Model->LastNewRects.Add(Id, Out);
		return true;
	}
	if (const FBox2D* Last = Model->LastNewRects.Find(Id))
	{
		Out = *Last;
		return true;
	}
	return false;
}

FString SRadarOverlay::SurvivingAncestor(const FString& RemovedId) const
{
	const FRadarDiff& D = *Model->Diff;
	FString Cur = RemovedId;
	for (int32 Guard = 0; Guard < 128; ++Guard)
	{
		FString Parent;
		if (const FRadarWidgetDiff* W = Model->FindWidget(Cur); W && W->ParentOld.bSet)
		{
			Parent = W->ParentOld.Value;
		}
		else
		{
			for (const TPair<FString, TArray<FString>>& P : D.OldTree.Children)
			{
				if (P.Value.Contains(Cur)) { Parent = P.Key; break; }
			}
		}
		if (Parent.IsEmpty())
		{
			return D.NewTree.Root;
		}
		const FRadarWidgetDiff* PW = Model->FindWidget(Parent);
		if (!PW || PW->Kind != ERadarKind::Removed)
		{
			return Parent;
		}
		Cur = Parent;
	}
	return D.NewTree.Root;
}

int32 SRadarOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TSharedPtr<SRadarPreviewPane> P = Pane.Pin();
	const TSharedPtr<FRadarDiffSession>& Session = Model->Session;
	if (!P.IsValid() || !Session.IsValid() || !Session->NewPreview || !Session->OldPreview || Model->Tab != ERadarTab::Designer)
	{
		return LayerId;
	}
	const ERadarPaneSide Side = P->GetSide();
	const bool bOldPane = Side == ERadarPaneSide::Old;
	UUserWidget* PaneRootWidget = bOldPane ? Session->OldPreview.Get() : Session->NewPreview.Get();
	const TSharedPtr<SWidget> RootSlate = PaneRootWidget->GetCachedWidget();
	if (!RootSlate.IsValid())
	{
		return LayerId;
	}
	const FGeometry& RootPaint = RootSlate->GetPaintSpaceGeometry();
	const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
	FPainter Paint{ AllottedGeometry, OutDrawElements, LayerId + 1, S };

	// 마우스는 패널의 OnMouseMove 가 준 로컬 좌표 (오버레이는 패널을 꽉 채우므로 같은 좌표다).
	// 커서 위치를 이 지오메트리로 직접 바꾸면 페인트 공간(창 좌표)과 데스크톱 좌표가 섞여 어긋났다(2026-10-18).
	FVector2D Mouse;
	const bool bMouse = P->GetMouseLocal(Mouse);
	TArray<FBox2D> Highlighted;
	TArray<TPair<FBox2D, FString>> HoverLabels;

	for (const FRadarWidgetDiff& W : Model->Diff->Widgets)
	{
		if (!Model->PassesFilter(W))
		{
			continue;
		}
		const bool bSelected = W.Id == Model->SelectedId;
		const float Thick = S.OutlineThickness * (bSelected ? 2.f : 1.f);
		auto ColorOr = [&](const FLinearColor& C) { return bSelected ? S.ColorSelected : C; };
		FBox2D R;
		switch (W.Kind)
		{
		case ERadarKind::Added:
			if (!bOldPane && NewRect(W.Id, R))
			{
				const FBox2D L = Paint.ToLocal(R, RootPaint);
				Paint.Rect(L, ColorOr(S.ColorAdded), Thick, false);
				Highlighted.Add(L);
			}
			break;
		case ERadarKind::Changed:
		case ERadarKind::RenamedProbable:
		{
			const bool bHave = bOldPane ? OldRect(W.Rename.IsSet() ? W.Rename->From : W.Id, R) : NewRect(W.Id, R);
			if (bHave)
			{
				const FBox2D L = Paint.ToLocal(R, RootPaint);
				Paint.Rect(L, ColorOr(S.ColorChanged), Thick, false);
				Highlighted.Add(L);
			}
			if (!W.bMovedToo) { break; }
		}
		// 바뀌고 이동한 위젯은 아래 이동 표시도 그린다
		[[fallthrough]];
		case ERadarKind::Moved:
		{
			FBox2D OldR, NewR;
			if (bOldPane)
			{
				if (OldRect(W.Id, OldR))
				{
					const FBox2D L = Paint.ToLocal(OldR, RootPaint);
					Paint.Rect(L, ColorOr(S.ColorMoved), Thick, false);
					Highlighted.Add(L);
				}
				break;
			}
			if (NewRect(W.Id, NewR))
			{
				const FBox2D L = Paint.ToLocal(NewR, RootPaint);
				if (W.Kind == ERadarKind::Moved) { Paint.Rect(L, ColorOr(S.ColorMoved), Thick, false); }
				Highlighted.Add(L);
				// 옛 자리: 옛 부모 안 오프셋을 새 쪽 옛 부모 자리에 → 점선 유령 상자 + 화살표
				FBox2D OldParentOld, OldParentNew;
				if (OldRect(W.Id, OldR) && W.ParentOld.bSet && OldRect(W.ParentOld.Value, OldParentOld) && NewRect(W.ParentOld.Value, OldParentNew))
				{
					const FVector2D Offset = OldR.Min - OldParentOld.Min;
					const FBox2D Ghost(OldParentNew.Min + Offset, OldParentNew.Min + Offset + OldR.GetSize());
					const FBox2D GL = Paint.ToLocal(Ghost, RootPaint);
					Paint.Rect(GL, S.ColorMoved.CopyWithNewOpacity(0.7f), 1.f, true);
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(),
						TArray<FVector2f>{ FVector2f(GL.GetCenter()), FVector2f(L.GetCenter()) }, ESlateDrawEffect::None, S.ColorMoved, true, 1.f);
				}
			}
			break;
		}
		case ERadarKind::Removed:
		{
			if (bOldPane)
			{
				if (OldRect(W.Id, R))
				{
					const FBox2D L = Paint.ToLocal(R, RootPaint);
					Paint.Rect(L, ColorOr(S.ColorRemoved), Thick, false);
					Highlighted.Add(L);
				}
				break;
			}
			// 새 쪽: 살아 있는 조상의 새 사각형 + (옛 오프셋, 옛 크기) 에 빗금 점선 빨간 상자
			FString Ancestor = SurvivingAncestor(W.Id);
			FBox2D AncOld, AncNew;
			const FString Label = W.LabelOld.bSet && !W.LabelOld.Value.IsEmpty() ? W.LabelOld.Value : W.Id;
			// 살아 있는 조상도 그려지지 않았으면(비활성 스위처 페이지 등) 그려진 조상이 나올 때까지 더 올라간다
			bool bAncestorDrawn = NewRect(Ancestor, AncNew);
			for (int32 Guard = 0; !bAncestorDrawn && Guard < 128 && Ancestor != Model->Diff->NewTree.Root; ++Guard)
			{
				Ancestor = SurvivingAncestor(Ancestor);
				bAncestorDrawn = NewRect(Ancestor, AncNew);
			}
			if (!bAncestorDrawn)
			{
				break;
			}
			if (OldRect(W.Id, R) && OldRect(Ancestor, AncOld))
			{
				const FVector2D Offset = R.Min - AncOld.Min;
				const FBox2D Box(AncNew.Min + Offset, AncNew.Min + Offset + R.GetSize());
				const FBox2D L = Paint.ToLocal(Box, RootPaint);
				Paint.Fill(L, S.ColorRemoved.CopyWithNewOpacity(0.12f));
				Paint.Hatch(L, S.ColorRemoved);
				Paint.Rect(L, ColorOr(S.ColorRemoved), Thick, true, 1);
				Paint.Label(L.Min - FVector2D(0, 0), TEXT("✕"), S.ColorRemoved, 4);
				Highlighted.Add(L);
				HoverLabels.Emplace(L, FString::Printf(TEXT("%s 가 여기 있었음"), *Label));
			}
			else
			{
				// 옛 지오메트리가 없다(Collapsed·비활성 스위처 페이지·스크롤 밖) → 조상 가장자리에 표시 칩
				const FBox2D AL = Paint.ToLocal(AncNew, RootPaint);
				const FVector2D At(AL.Max.X - 8.f, AL.Min.Y + 2.f + 18.f * (HoverLabels.Num() % 6));
				Paint.Label(At, FString::Printf(TEXT("✕ %s"), *Label), S.ColorRemoved, 4);
				HoverLabels.Emplace(FBox2D(At, At + FVector2D(160, 16)), FString::Printf(TEXT("%s 가 여기 있었음 (지오메트리 없음)"), *Label));
			}
			break;
		}
		default:
			break;
		}
	}

	// 변경 없음 흐리게: 덮개를 깔고 바뀐 상자만 뚫는다
	if (Model->bDimUnchanged && Highlighted.Num() > 0)
	{
		const FBox2D All = Paint.ToLocal(FBox2D(FVector2D::ZeroVector, FVector2D(RootPaint.GetLocalSize())), RootPaint);
		TArray<FBox2D> Cells;
		PunchedCells(All, Highlighted, Cells);
		for (const FBox2D& C : Cells)
		{
			Paint.Fill(C, FLinearColor(0, 0, 0, 1.f - S.DimUnchangedOpacity), -1);
		}
	}

	// 마우스가 올라간 지워진 상자의 라벨
	for (const TPair<FBox2D, FString>& H : HoverLabels)
	{
		if (bMouse && H.Key.IsInside(Mouse))
		{
			Paint.Label(Mouse + FVector2D(12, 12), H.Value, FLinearColor::White, 6);
			break;
		}
	}
	return LayerId + 10;
}

#undef LOCTEXT_NAMESPACE
