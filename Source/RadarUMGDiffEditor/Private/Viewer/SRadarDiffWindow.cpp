// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarDiffWindow.h"
#include "Viewer/SRadarSummaryStrip.h"
#include "Viewer/SRadarHierarchy.h"
#include "Viewer/SRadarDetailsGrid.h"
#include "Viewer/SRadarPreviewPane.h"
#include "Viewer/SRadarTabs.h"
#include "RadarRevisionSource.h"
#include "RadarUMGDiffSettings.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "InputCoreTypes.h"
#include "RadarUMGDiffEditorModule.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

TSharedRef<SWindow> SRadarDiffWindow::Open(TSharedRef<FRadarViewModel> Model)
{
	const FRadarDiff& D = *Model->Diff;
	const FText Title = Model->Session.IsValid()
		? FText::Format(LOCTEXT("ViewerTitle", "Radar UMG Diff — {0}: {1} ↔ {2}"), FText::FromString(D.AssetName), Model->Session->OldRevision.GetLabel(), Model->Session->NewRevision.GetLabel())
		: FText::Format(LOCTEXT("ViewerTitleFile", "Radar UMG Diff — {0} ({1})"), FText::FromString(D.AssetName), FText::FromString(FPaths::GetCleanFilename(D.SourcePath)));
	TSharedRef<SWindow> Window = SNew(SWindow).Title(Title).ClientSize(FVector2D(1680, 980));
	Window->SetContent(SNew(SRadarDiffWindow, Model));
	FSlateApplication::Get().AddWindow(Window);
	if (Model->Session.IsValid())
	{
		Model->Session->AttachWindow(Window);
	}
	return Window;
}

SRadarDiffWindow::~SRadarDiffWindow()
{
	if (Model.IsValid())
	{
		Model->OnSelectionChanged.Remove(SelectionHandle);
		Model->OnFilterChanged.Remove(FilterHandle);
	}
}

FText SRadarDiffWindow::SideLabel(bool bOld) const
{
	const FRadarDiff& D = *Model->Diff;
	if (Model->Session.IsValid())
	{
		return (bOld ? Model->Session->OldRevision : Model->Session->NewRevision).GetLabel();
	}
	const FRadarSideInfo& S = bOld ? D.Old : D.New;
	const FString Id = !S.Sha.IsEmpty() ? S.Sha.Left(7) : (!S.Revision.IsEmpty() ? TEXT("#") + S.Revision : (!S.Path.IsEmpty() ? FPaths::GetCleanFilename(S.Path) : S.Kind));
	return FText::FromString(FString::Printf(TEXT("%s %s"), *Id, *(S.Subject.IsEmpty() ? S.Description : S.Subject)));
}

void SRadarDiffWindow::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel)
{
	Model = InModel;
	SelectionHandle = Model->OnSelectionChanged.AddSP(this, &SRadarDiffWindow::OnSelectionChanged);
	FilterHandle = Model->OnFilterChanged.AddSP(this, &SRadarDiffWindow::OnFilterChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ MakeTitleStrip() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(6, 4)[ SNew(SRadarSummaryStrip, InModel) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(6, 2)[ MakeToolbar() ]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(4)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.2f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(2)
				[
					SAssignNew(Hierarchy, SRadarHierarchy, InModel)
					.OnFocusRequested(FSimpleDelegate::CreateSP(this, &SRadarDiffWindow::FocusSelected))
				]
			]
			+ SSplitter::Slot().Value(0.5f)
			[
				SAssignNew(CenterSwitcher, SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(Model->Tab); })
				+ SWidgetSwitcher::Slot()[ SAssignNew(PreviewArea, SBox) ]
				+ SWidgetSwitcher::Slot()[ SAssignNew(AnimTab, SRadarAnimationTab, InModel) ]
				+ SWidgetSwitcher::Slot()[ SAssignNew(GraphTab, SRadarGraphTab, InModel) ]
			]
			+ SSplitter::Slot().Value(0.3f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(2)
				[
					SAssignNew(Details, SRadarDetailsGrid, InModel)
				]
			]
		]
	];
	RebuildPreviews();
	// 첫 바뀐 항목을 선택해 둔다
	if (Model->GetFlatList().Num() > 0)
	{
		Step(+1);
	}
}

TSharedRef<SWidget> SRadarDiffWindow::MakeTitleStrip()
{
	const FRadarDiff& D = *Model->Diff;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			.Text(FText::Format(LOCTEXT("TitleStrip", "{0}   이전: {1}   ↔   이후: {2}"), FText::FromString(D.AssetName), SideLabel(true), SideLabel(false)))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			.Text(FText::Format(LOCTEXT("ProducerStrip", "생산자 {0} ({1}{2}) · 리비전 출처: {3} → {4} · 기능: {5}"),
				FText::FromString(D.ProducerName), FText::FromString(D.ProducerKind), FText::FromString(D.Engine.IsEmpty() ? FString() : TEXT(", ") + D.Engine),
				FText::FromString(D.Old.Kind), FText::FromString(D.New.Kind), FText::FromString(FString::Join(D.Capabilities, TEXT(", ")))))
		];
	if (Model->IsDataOnly())
	{
		Box->AddSlot().AutoHeight()
		[
			SNew(STextBlock).ColorAndOpacity(FLinearColor(1.f, 0.75f, 0.3f))
			.Text(LOCTEXT("DataOnlyStrip", "data-only 모드: 미리보기·오버레이·전체 상세·Blueprint Diff 는 쓸 수 없습니다. 나머지 패널은 같습니다."))
		];
	}
	if (D.Old.DesignSizeMode != D.New.DesignSizeMode || !D.Old.DesignSize.Equals(D.New.DesignSize))
	{
		Box->AddSlot().AutoHeight()
		[
			SNew(STextBlock).ColorAndOpacity(FLinearColor(1.f, 0.75f, 0.3f))
			.Text(FText::Format(LOCTEXT("DesignSizeWarn", "두 리비전의 디자인 크기가 다릅니다 (이전 {0} {1}×{2}, 이후 {3} {4}×{5}). 두 미리보기 모두 이후 쪽 크기로 그립니다."),
				FText::FromString(D.Old.DesignSizeMode), FText::AsNumber(D.Old.DesignSize.X), FText::AsNumber(D.Old.DesignSize.Y),
				FText::FromString(D.New.DesignSizeMode), FText::AsNumber(D.New.DesignSize.X), FText::AsNumber(D.New.DesignSize.Y)))
		];
	}
	return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder")).Padding(FMargin(8, 6))[ Box ];
}

TSharedRef<SWidget> SRadarDiffWindow::MakeToolbar()
{
	auto TabButton = [this](ERadarTab Tab, FText Label) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked_Lambda([this, Tab]() { return Model->Tab == Tab ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Tab](ECheckBoxState) { Model->Tab = Tab; OnSelectionChanged(); })
			[ SNew(SBox).Padding(FMargin(10, 3))[ SNew(STextBlock).Text(Label) ] ];
	};
	auto ModeButton = [this](ERadarPreviewMode Mode, FText Label, FText Tip) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(Tip)
			.IsEnabled_Lambda([this]() { return !Model->IsDataOnly(); })
			.IsChecked_Lambda([this, Mode]() { return Model->Mode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Mode](ECheckBoxState)
			{
				// 이미 뒤집기면 한 번 더 눌러 이전/이후를 바꾼다 (키와 같은 동작)
				if (Mode == ERadarPreviewMode::Flip && Model->Mode == ERadarPreviewMode::Flip) { ToggleFlip(); }
				else { SetMode(Mode); }
			})
			[ SNew(SBox).Padding(FMargin(10, 3))[ SNew(STextBlock).Text(Label) ] ];
	};
	const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)[ TabButton(ERadarTab::Designer, LOCTEXT("TabDesigner", "디자이너")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)[ TabButton(ERadarTab::Animation, LOCTEXT("TabAnim", "애니메이션")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)[ TabButton(ERadarTab::Graph, LOCTEXT("TabGraph", "그래프")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)[ ModeButton(ERadarPreviewMode::SideBySide, LOCTEXT("ModeSide", "나란히"), LOCTEXT("ModeSideTip", "두 리비전을 좌우로, 이동·확대를 같이")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)[ ModeButton(ERadarPreviewMode::Overlay, LOCTEXT("ModeOverlay", "겹치기"), LOCTEXT("ModeOverlayTip", "이후 위에 이전을 반투명으로")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[ ModeButton(ERadarPreviewMode::Flip, FText::Format(LOCTEXT("ModeFlip", "뒤집기 ({0})"), FText::FromString(S.FlipKey)), LOCTEXT("ModeFlipTip", "한 패널만 보이고 키로 이전/이후를 바꾼다")) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("Opacity", "불투명도")).Visibility_Lambda([this]() { return Model->Mode == ERadarPreviewMode::Overlay ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(120).Visibility_Lambda([this]() { return Model->Mode == ERadarPreviewMode::Overlay ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SSlider).Value_Lambda([this]() { return Model->OverlayOpacity; }).OnValueChanged_Lambda([this](float V) { Model->OverlayOpacity = V; })
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNullWidget::NullWidget ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 12, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("RerunPython", "Python 분석기로 다시 만들기"))
			.ToolTipText(LOCTEXT("RerunPythonTip", "같은 두 파일을 번들 Python 분석기(umgdiff_cli.py)로 다시 분석해 그 결과를 data-only 창으로 엽니다. 작업 사본은 디스크에 저장된 내용입니다."))
			.IsEnabled_Lambda([this]() { return !Model->IsDataOnly(); })
			.OnClicked_Lambda([this]()
			{
				FString Error;
				if (Model->Session.IsValid() && !FRadarUMGDiffEditorModule::RerunWithPython(*Model->Session, Error))
				{
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
				}
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
		[
			SNew(STextBlock).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			.Text_Lambda([this]() { return FText::Format(LOCTEXT("Cursor", "{0} / {1}"), FText::AsNumber(Model->CursorIndex + 1), FText::AsNumber(Model->GetFlatList().Num())); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
		[
			SNew(SButton).Text(FText::Format(LOCTEXT("Prev", "이전 ({0})"), FText::FromString(S.PrevKey))).OnClicked_Lambda([this]() { Step(-1); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
		[
			SNew(SButton).Text(FText::Format(LOCTEXT("Next", "다음 ({0})"), FText::FromString(S.NextKey))).OnClicked_Lambda([this]() { Step(+1); return FReply::Handled(); })
		];
}

TSharedRef<SWidget> SRadarDiffWindow::PaneWithLabel(TSharedRef<SRadarPreviewPane> Pane, const TAttribute<FText>& Label)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
		[
			SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle("BoldFont"))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[ Pane ];
}

void SRadarDiffWindow::RebuildPreviews()
{
	for (const TSharedPtr<SRadarPreviewPane>& P : Panes)
	{
		P->DetachPreviews();
	}
	Panes.Reset();
	const TSharedRef<FRadarViewModel> M = Model.ToSharedRef();
	if (Model->Mode == ERadarPreviewMode::SideBySide)
	{
		TSharedRef<SRadarPreviewPane> OldP = SNew(SRadarPreviewPane, M).Side(ERadarPaneSide::Old);
		TSharedRef<SRadarPreviewPane> NewP = SNew(SRadarPreviewPane, M).Side(ERadarPaneSide::New);
		Panes = { OldP, NewP };
		PreviewArea->SetContent(
			SNew(SSplitter)
			+ SSplitter::Slot()[ PaneWithLabel(OldP, FText::Format(LOCTEXT("OldLabel", "이전 · {0}"), SideLabel(true))) ]
			+ SSplitter::Slot()[ PaneWithLabel(NewP, FText::Format(LOCTEXT("NewLabel", "이후 · {0}"), SideLabel(false))) ]);
	}
	else if (Model->Mode == ERadarPreviewMode::Overlay)
	{
		TSharedRef<SRadarPreviewPane> P = SNew(SRadarPreviewPane, M).Side(ERadarPaneSide::Both);
		Panes = { P };
		PreviewArea->SetContent(PaneWithLabel(P, LOCTEXT("OverlayLabel", "겹치기 · 이후 위에 이전(반투명)")));
	}
	else
	{
		TSharedRef<SRadarPreviewPane> P = SNew(SRadarPreviewPane, M).Side(Model->bFlipShowOld ? ERadarPaneSide::Old : ERadarPaneSide::New);
		Panes = { P };
		TWeakPtr<SRadarDiffWindow> WeakSelf = SharedThis(this);
		PreviewArea->SetContent(PaneWithLabel(P, TAttribute<FText>::CreateLambda([WeakSelf]()
		{
			TSharedPtr<SRadarDiffWindow> This = WeakSelf.Pin();
			return This.IsValid() ? FText::Format(LOCTEXT("FlipLabel", "뒤집기 · {0} · {1}"), This->Model->bFlipShowOld ? LOCTEXT("FlipOld", "이전") : LOCTEXT("FlipNew", "이후"), This->SideLabel(This->Model->bFlipShowOld)) : FText::GetEmpty();
		})));
	}
}

void SRadarDiffWindow::SetMode(ERadarPreviewMode Mode)
{
	if (Model->Mode != Mode)
	{
		Model->Mode = Mode;
		RebuildPreviews();
	}
}

void SRadarDiffWindow::OnSelectionChanged()
{
	Hierarchy->SyncSelection();
	Details->Rebuild();
}

void SRadarDiffWindow::OnFilterChanged()
{
	Hierarchy->Rebuild();
	Details->Rebuild();
	AnimTab->Rebuild();
	GraphTab->Rebuild();
}

void SRadarDiffWindow::FocusSelected()
{
	if (Model->Tab != ERadarTab::Designer)
	{
		return;
	}
	for (const TSharedPtr<SRadarPreviewPane>& P : Panes)
	{
		P->FocusOn(Model->SelectedId);
		break;   // 보기는 공유되므로 한 패널만 옮기면 된다
	}
}

void SRadarDiffWindow::Step(int32 Delta)
{
	if (Model->Step(Delta))
	{
		FocusSelected();
	}
}

void SRadarDiffWindow::ToggleFlip()
{
	Model->bFlipShowOld = !Model->bFlipShowOld;
	if (Panes.Num() == 1)
	{
		Panes[0]->SetSide(Model->bFlipShowOld ? ERadarPaneSide::Old : ERadarPaneSide::New);
	}
}

FReply SRadarDiffWindow::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(InKeyEvent.GetUserIndex());
	if (Focused.IsValid() && Focused->GetType().ToString().Contains(TEXT("EditableText")))
	{
		return FReply::Unhandled();   // 검색칸에 입력 중
	}
	const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
	const FKey Key = InKeyEvent.GetKey();
	if (Key == FKey(*S.NextKey))
	{
		Step(+1);
		return FReply::Handled();
	}
	if (Key == FKey(*S.PrevKey))
	{
		Step(-1);
		return FReply::Handled();
	}
	if (Key == FKey(*S.FlipKey) && Model->Mode == ERadarPreviewMode::Flip)
	{
		ToggleFlip();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
