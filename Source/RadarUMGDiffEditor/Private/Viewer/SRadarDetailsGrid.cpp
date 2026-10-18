// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarDetailsGrid.h"
#include "Viewer/RadarViewModel.h"
#include "Viewer/RadarValueRenderers.h"
#include "RadarRevisionSource.h"
#include "RadarPropertyCompare.h"
#include "RadarUMGDiffSettings.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "DetailsDiff.h"
#include "IDetailsView.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace
{
	const FName ColProperty(TEXT("Property"));
	const FName ColOld(TEXT("Old"));
	const FName ColNew(TEXT("New"));

	class SRadarDetailsRow : public SMultiColumnTableRow<TSharedPtr<FRadarDetailsRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SRadarDetailsRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<STableViewBase>& Owner, TSharedPtr<FRadarDetailsRow> InRow)
		{
			Row = InRow;
			SMultiColumnTableRow<TSharedPtr<FRadarDetailsRow>>::Construct(FSuperRowType::FArguments().Padding(FMargin(2.f, 2.f)), Owner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			if (Row->bHeader)
			{
				return Column == ColProperty
					? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(Row->Header)).Font(FAppStyle::GetFontStyle("BoldFont")).ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f)))
					: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget);
			}
			const FRadarPropertyDiff& P = Row->Prop;
			if (Column == ColProperty)
			{
				TSharedRef<SHorizontalBox> Cell = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(STextBlock).Text(FText::FromString(P.Path)).ToolTipText(FText::FromString(P.Type))
					];
				if (P.bAuto)
				{
					Cell->AddSlot().AutoWidth().Padding(4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("AutoTag", "자동")).ColorAndOpacity(FLinearColor(0.5f, 0.6f, 0.8f))
						.ToolTipText(LOCTEXT("AutoTip", "디자이너가 자동으로 바꾸기도 하는 값입니다 (ini AutoProperties)."))
					];
				}
				if (P.bMaybeDefault)
				{
					Cell->AddSlot().AutoWidth().Padding(4, 0)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("?"))).ColorAndOpacity(FLinearColor(1.f, 0.8f, 0.3f))
						.ToolTipText(LOCTEXT("MaybeDefaultTip", "한쪽은 저장되지 않은 값입니다. 스키마가 있으면 확정됩니다"))
					];
				}
				return Cell;
			}
			return RadarValueRenderers::MakeValueCell(P, Column == ColOld);
		}

	private:
		TSharedPtr<FRadarDetailsRow> Row;
	};

	FRadarPropertyDiff TextRow(const FString& Path, const FString& Category, const FRadarOptionalString& Old, const FRadarOptionalString& New)
	{
		FRadarPropertyDiff P;
		P.Path = Path;
		P.Category = Category;
		P.Type = TEXT("StrProperty");
		if (Old.bSet) { P.Old.bSet = true; P.Old.Text = Old.Value; }
		if (New.bSet) { P.New.bSet = true; P.New.Text = New.Value; }
		return P;
	}

	FRadarOptionalString IndexText(int32 V)
	{
		return V == INDEX_NONE ? FRadarOptionalString() : FRadarOptionalString(FString::FromInt(V));
	}
}

void SRadarDetailsGrid::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel)
{
	Model = InModel;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text_Lambda([this]() { return Title; })
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(List, SListView<TSharedPtr<FRadarDetailsRow>>)
			.ListItemsSource(&Rows)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SRadarDetailsGrid::OnGenerateRow)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(ColProperty).DefaultLabel(LOCTEXT("ColProperty", "속성")).FillWidth(0.34f)
				+ SHeaderRow::Column(ColOld).DefaultLabel(LOCTEXT("ColOld", "이전")).FillWidth(0.33f)
				+ SHeaderRow::Column(ColNew).DefaultLabel(LOCTEXT("ColNew", "이후")).FillWidth(0.33f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4).HAlign(HAlign_Left)
		[
			SNew(SButton)
			.Text(LOCTEXT("OpenFullDetails", "전체 상세 열기"))
			.ToolTipText(LOCTEXT("OpenFullDetailsTip", "엔진 디테일 패널 두 개(이전·이후)를 나란히 엽니다"))
			.IsEnabled(this, &SRadarDetailsGrid::CanOpenFullDetails)
			.OnClicked(this, &SRadarDetailsGrid::OnOpenFullDetails)
		]
	];
	Rebuild();
}

void SRadarDetailsGrid::AddGroup(const FString& Prefix, const TArray<FRadarPropertyDiff>& Props)
{
	// 분류별로 묶되 처음 나온 순서를 지킨다
	TArray<FString> Order;
	TMap<FString, TArray<const FRadarPropertyDiff*>> ByCat;
	for (const FRadarPropertyDiff& P : Props)
	{
		if (!Model->ShowProperty(P))
		{
			continue;
		}
		const FString Cat = Prefix + (P.Category.IsEmpty() ? FString(TEXT("기타")) : P.Category);
		if (!ByCat.Contains(Cat)) { Order.Add(Cat); }
		ByCat.FindOrAdd(Cat).Add(&P);
	}
	for (const FString& Cat : Order)
	{
		TSharedPtr<FRadarDetailsRow> H = MakeShared<FRadarDetailsRow>();
		H->bHeader = true;
		H->Header = Cat;
		Rows.Add(H);
		for (const FRadarPropertyDiff* P : ByCat[Cat])
		{
			TSharedPtr<FRadarDetailsRow> R = MakeShared<FRadarDetailsRow>();
			R->Prop = *P;
			Rows.Add(R);
		}
	}
}

/** 지워진/추가된 위젯: 세션이 있으면 살아 있는 오브젝트의 전체 속성(아키타입과 다른 것만)을 한쪽 열에. */
void SRadarDetailsGrid::AddLiveDump(bool bOld, const FString& Id)
{
	const TSharedPtr<FRadarDiffSession>& Session = Model->Session;
	UWidgetBlueprint* BP = Session.IsValid() ? (bOld ? Session->OldBlueprint.Get() : Session->NewBlueprint.Get()) : nullptr;
	if (!BP || !BP->WidgetTree)
	{
		return;
	}
	FString Name = Id;
	Name.Split(TEXT("."), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	UWidget* W = BP->WidgetTree->FindWidget(FName(*Name));
	if (!W)
	{
		return;
	}
	FRadarCompareContext Ctx = FRadarCompareContext::FromSettings();
	Ctx.StructuralProperties = { TEXT("Slot"), TEXT("Slots") };
	TArray<FRadarPropertyDiff> Dump;
	RadarProperty::DumpStruct(W->GetClass(), W, W->GetArchetype(), FString(), Ctx, Dump);
	TArray<FRadarPropertyDiff> Present;
	for (FRadarPropertyDiff& P : Dump)
	{
		if (P.Old.State != ERadarValueState::Present) { continue; }
		if (!bOld) { P.New = P.Old; P.Old = FRadarValue(); }
		P.Kind = bOld ? ERadarKind::Removed : ERadarKind::Added;
		Present.Add(P);
	}
	AddGroup(bOld ? TEXT("지워진 위젯 · ") : TEXT("추가된 위젯 · "), Present);
}

void SRadarDetailsGrid::Rebuild()
{
	Rows.Reset();
	const FString& Id = Model->SelectedId;
	Title = FText::FromString(Id);
	const FRadarDiff& D = *Model->Diff;
	if (Model->Tab == ERadarTab::Designer)
	{
		if (Id == TEXT("(bindings)"))
		{
			TArray<FRadarPropertyDiff> Props;
			for (const FRadarBindingDiff& B : D.Bindings)
			{
				FRadarPropertyDiff P = TextRow(B.Id + TEXT(" 함수"), TEXT("바인딩"), B.OldFunction, B.NewFunction);
				P.Kind = B.Kind;
				Props.Add(P);
				FRadarPropertyDiff K = TextRow(B.Id + TEXT(" 종류"), TEXT("바인딩"), B.OldKind, B.NewKind);
				K.Kind = B.Kind;
				Props.Add(K);
			}
			AddGroup(FString(), Props);
		}
		else if (Id == TEXT("(cdo)"))
		{
			AddGroup(TEXT("클래스 기본값 · "), D.Cdo);
			for (const FRadarVariableDiff& V : D.Variables)
			{
				TArray<FRadarPropertyDiff> Props = V.Properties;
				if (Props.Num() == 0)
				{
					Props.Add(TextRow(V.Name, TEXT("변수"), V.Kind == ERadarKind::Added ? FRadarOptionalString() : FRadarOptionalString(V.Name),
						V.Kind == ERadarKind::Removed ? FRadarOptionalString() : FRadarOptionalString(V.Name)));
				}
				AddGroup(FString::Printf(TEXT("변수 %s%s · "), *V.Name, V.RenamedFrom.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (← %s)"), *V.RenamedFrom)), Props);
			}
		}
		else if (const FRadarWidgetDiff* W = Model->FindWidget(Id))
		{
			Title = FText::FromString(FString::Printf(TEXT("%s  [%s]"), *Id, LexToString(W->Kind)));
			TArray<FRadarPropertyDiff> Tree;
			if (W->ParentOld.Value != W->ParentNew.Value || W->IndexOld != W->IndexNew)
			{
				Tree.Add(TextRow(TEXT("부모"), TEXT("계층"), W->ParentOld, W->ParentNew));
				Tree.Add(TextRow(TEXT("인덱스"), TEXT("계층"), IndexText(W->IndexOld), IndexText(W->IndexNew)));
			}
			if (W->Slot.ClassOld.Value != W->Slot.ClassNew.Value)
			{
				Tree.Add(TextRow(TEXT("슬롯 클래스"), TEXT("계층"), W->Slot.ClassOld, W->Slot.ClassNew));
			}
			AddGroup(FString(), Tree);
			if (W->Kind == ERadarKind::Removed || W->Kind == ERadarKind::Added)
			{
				const int32 Before = Rows.Num();
				AddLiveDump(W->Kind == ERadarKind::Removed, Id);
				if (Rows.Num() == Before)
				{
					AddGroup(FString(), W->Properties);   // 세션 없음 → "*" 줄
				}
			}
			else
			{
				AddGroup(FString(), W->Properties);
			}
			AddGroup(TEXT("슬롯 · "), W->Slot.Properties);
		}
	}
	else if (Model->Tab == ERadarTab::Animation)
	{
		for (const FRadarAnimationDiff& A : D.Animations)
		{
			if (A.Id != Id) { continue; }
			TArray<FRadarPropertyDiff> Props;
			for (const FRadarTrackDiff& T : A.Tracks)
			{
				FRadarPropertyDiff P = TextRow(T.Id + TEXT(" 섹션 수"), TEXT("트랙"), IndexText(T.SectionsOld), IndexText(T.SectionsNew));
				P.Kind = T.Kind;
				Props.Add(P);
			}
			Title = FText::FromString(FString::Printf(TEXT("%s  [%s]"), *A.Id, LexToString(A.Kind)));
			AddGroup(FString(), Props);
		}
	}
	else
	{
		for (const FRadarGraphDiff& G : D.Graphs)
		{
			for (const FRadarNodeDiff& N : G.Nodes)
			{
				if (G.Id + TEXT("/") + N.Guid != Id) { continue; }
				Title = FText::FromString(FString::Printf(TEXT("%s · %s  [%s]"), *G.Id, *N.Title, LexToString(N.Kind)));
				AddGroup(TEXT("노드 · "), N.Properties);
				for (const FRadarPinDiff& Pin : N.Pins)
				{
					TArray<FRadarPropertyDiff> Props = Pin.Properties;
					if (Props.Num() == 0)
					{
						Props.Add(TextRow(Pin.Name, TEXT("핀"), Pin.Kind == ERadarKind::Added ? FRadarOptionalString() : FRadarOptionalString(LexToString(Pin.Kind)),
							Pin.Kind == ERadarKind::Removed ? FRadarOptionalString() : FRadarOptionalString(LexToString(Pin.Kind))));
					}
					AddGroup(FString::Printf(TEXT("핀 %s · "), *Pin.Name), Props);
				}
			}
		}
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SRadarDetailsGrid::OnGenerateRow(TSharedPtr<FRadarDetailsRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SRadarDetailsRow, Owner, Row);
}

bool SRadarDetailsGrid::CanOpenFullDetails() const
{
	return !Model->IsDataOnly() && Model->Tab == ERadarTab::Designer && Model->FindWidget(Model->SelectedId) != nullptr;
}

FReply SRadarDetailsGrid::OnOpenFullDetails()
{
	const TSharedPtr<FRadarDiffSession>& Session = Model->Session;
	if (!Session.IsValid())
	{
		return FReply::Handled();
	}
	FString Name = Model->SelectedId;
	Name.Split(TEXT("."), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	const FRadarWidgetDiff* W = Model->FindWidget(Model->SelectedId);
	FString OldName = Name;
	if (W && W->Rename.IsSet())
	{
		W->Rename->From.Split(TEXT("."), nullptr, &OldName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	}
	UWidget* OldW = Session->OldBlueprint && Session->OldBlueprint->WidgetTree ? Session->OldBlueprint->WidgetTree->FindWidget(FName(*OldName)) : nullptr;
	UWidget* NewW = Session->NewBlueprint && Session->NewBlueprint->WidgetTree ? Session->NewBlueprint->WidgetTree->FindWidget(FName(*Name)) : nullptr;
	auto Pane = [](UObject* Obj, FText Label) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)[ SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle("BoldFont")) ]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				Obj ? StaticCastSharedRef<SWidget>(FDetailsDiff::CreateDetailsView(Obj)) : StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("NoObject", "(없음)")))
			];
	};
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("FullDetailsTitle", "전체 상세 — {0}"), FText::FromString(Model->SelectedId)))
		.ClientSize(FVector2D(1000, 760));
	Window->SetContent(
		SNew(SSplitter)
		+ SSplitter::Slot()[ Pane(OldW, LOCTEXT("OldPane", "이전")) ]
		+ SSplitter::Slot()[ Pane(NewW, LOCTEXT("NewPane", "이후")) ]);
	FSlateApplication::Get().AddWindow(Window);
	Session->AttachWindow(Window);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
