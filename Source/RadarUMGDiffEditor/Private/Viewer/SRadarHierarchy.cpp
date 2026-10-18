// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarHierarchy.h"
#include "Viewer/RadarValueRenderers.h"
#include "RadarUMGDiffSettings.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace
{
	FLinearColor KindColor(const FRadarTreeNode& N)
	{
		const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
		if (N.Row == FRadarTreeNode::ERow::Ghost) { return FLinearColor(0.45f, 0.45f, 0.5f); }
		if (N.Row == FRadarTreeNode::ERow::Removed) { return S.ColorRemoved; }
		switch (N.Kind)
		{
		case ERadarKind::Added:           return S.ColorAdded;
		case ERadarKind::Removed:         return S.ColorRemoved;
		case ERadarKind::Moved:           return S.ColorMoved;
		case ERadarKind::Changed:
		case ERadarKind::RenamedProbable: return S.ColorChanged;
		default:                          return FLinearColor(0.85f, 0.85f, 0.85f);
		}
	}

	FText KindTag(const FRadarTreeNode& N)
	{
		if (N.Row == FRadarTreeNode::ERow::Ghost) { return LOCTEXT("TagGhost", "이동 전"); }
		if (N.Row == FRadarTreeNode::ERow::Removed) { return LOCTEXT("TagRemoved", "삭제"); }
		switch (N.Kind)
		{
		case ERadarKind::Added:           return LOCTEXT("TagAdded", "추가");
		case ERadarKind::Moved:           return LOCTEXT("TagMoved", "이동");
		case ERadarKind::RenamedProbable: return LOCTEXT("TagRenamed", "이름 변경?");
		case ERadarKind::Changed:         return N.bMovedToo ? LOCTEXT("TagChangedMoved", "변경·이동") : LOCTEXT("TagChanged", "변경");
		default:                          return FText::GetEmpty();
		}
	}
}

void SRadarHierarchy::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel)
{
	Model = InModel;
	OnFocusRequested = InArgs._OnFocusRequested;
	ChildSlot
	[
		SAssignNew(Tree, STreeView<FNode>)
		.TreeItemsSource(&VisibleRoots)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SRadarHierarchy::OnGenerateRow)
		.OnGetChildren(this, &SRadarHierarchy::OnGetChildren)
		.OnSelectionChanged(this, &SRadarHierarchy::OnSelectionChanged)
		.OnMouseButtonDoubleClick_Lambda([this](FNode) { OnFocusRequested.ExecuteIfBound(); })
	];
	Rebuild();
}

void SRadarHierarchy::Rebuild()
{
	VisibleRoots.Reset();
	for (const FNode& R : Model->GetRoots())
	{
		if (Model->IsVisible(R)) { VisibleRoots.Add(R); }
	}
	Tree->RequestTreeRefresh();
	ExpandAll(VisibleRoots);
	SyncSelection();
}

void SRadarHierarchy::ExpandAll(const TArray<FNode>& Nodes)
{
	for (const FNode& N : Nodes)
	{
		Tree->SetItemExpansion(N, true);
		TArray<FNode> Kids;
		OnGetChildren(N, Kids);
		ExpandAll(Kids);
	}
}

void SRadarHierarchy::OnGetChildren(FNode Node, TArray<FNode>& Out)
{
	for (const FNode& C : Node->Children)
	{
		if (Model->IsVisible(C)) { Out.Add(C); }
	}
}

void SRadarHierarchy::SyncSelection()
{
	TGuardValue<bool> Guard(bSyncing, true);
	FNode N = Model->FindNode(Model->SelectedId);
	if (N.IsValid() && Model->Tab == ERadarTab::Designer)
	{
		Tree->SetSelection(N, ESelectInfo::Direct);
		Tree->RequestScrollIntoView(N);
	}
	else
	{
		Tree->ClearSelection();
	}
}

void SRadarHierarchy::OnSelectionChanged(FNode Node, ESelectInfo::Type Info)
{
	if (bSyncing || !Node.IsValid())
	{
		return;
	}
	Model->Select(Node->Id, ERadarTab::Designer);
}

TSharedRef<ITableRow> SRadarHierarchy::OnGenerateRow(FNode Node, const TSharedRef<STableViewBase>& Owner)
{
	const FLinearColor Color = KindColor(*Node);
	const bool bChanged = Node->Kind != ERadarKind::Unchanged || Node->Row != FRadarTreeNode::ERow::Widget;
	const bool bHatch = Node->Row == FRadarTreeNode::ERow::Removed;
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
	if (bChanged)
	{
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(Color.CopyWithNewOpacity(0.25f))
			.Padding(FMargin(4, 0))
			[
				SNew(STextBlock).Text(KindTag(*Node)).ColorAndOpacity(Color).Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	}
	FTextBlockStyle LabelStyle = FAppStyle::GetWidgetStyle<FTextBlockStyle>("NormalText");
	if (bHatch)
	{
		// 취소선은 리소스 이름이 있는 이미지 브러시여야 그려진다 (PlainTextLayoutMarshaller.cpp:45) — WhiteBrush 는 색 브러시라 안 된다
		LabelStyle.SetStrikeBrush(*FCoreStyle::Get().GetBrush("GenericWhiteBox"));
	}
	Box->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Node->Label))
		.TextStyle(&LabelStyle)
		.ColorAndOpacity(bChanged ? Color : FLinearColor(0.85f, 0.85f, 0.85f))
		.HighlightText_Lambda([this]() { return FText::FromString(Model->SearchText); })
		.ToolTipText(FText::FromString(Node->Id + TEXT("\n") + Node->Class))
	];
	if (!Node->Class.IsEmpty())
	{
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
		[
			SNew(STextBlock).Text(FText::FromString(RadarValueRenderers::ShortAssetName(Node->Class))).ColorAndOpacity(FLinearColor(0.45f, 0.45f, 0.45f)).Font(FAppStyle::GetFontStyle("SmallFont"))
		];
	}
	if (Node->DescendantChanges > 0)
	{
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.2f, 0.2f, 0.2f)).Padding(FMargin(4, 0))
			[
				SNew(STextBlock).Text(FText::AsNumber(Node->DescendantChanges)).Font(FAppStyle::GetFontStyle("SmallFont"))
				.ToolTipText(LOCTEXT("DescTip", "자손 중 바뀐 위젯 수"))
			]
		];
	}
	return SNew(STableRow<FNode>, Owner)
		.Padding(FMargin(2, 1))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(bHatch ? "Checkerboard" : "NoBorder"))
			.BorderBackgroundColor(bHatch ? Color.CopyWithNewOpacity(0.18f) : FLinearColor::Transparent)
			.Padding(0)
			[
				Box
			]
		];
}

#undef LOCTEXT_NAMESPACE
