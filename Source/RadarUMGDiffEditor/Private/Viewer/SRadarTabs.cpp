// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarTabs.h"
#include "Viewer/RadarViewModel.h"
#include "RadarRevisionSource.h"
#include "RadarEngineCompat.h"

#include "SBlueprintDiff.h"
#include "WidgetBlueprint.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace
{
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FRadarTabRow> Row, const TSharedRef<STableViewBase>& Owner)
	{
		return SNew(STableRow<TSharedPtr<FRadarTabRow>>, Owner)
			.Padding(FMargin(4.f + 16.f * Row->Depth, 2.f, 4.f, 2.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(STextBlock).Text(FText::FromString(Row->Label)).Font(Row->Depth == 0 ? FAppStyle::GetFontStyle("BoldFont") : FAppStyle::GetFontStyle("NormalFont"))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
				[
					SNew(STextBlock).Text(FText::FromString(Row->Detail)).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
				]
			];
	}

	FString Sections(int32 Old, int32 New)
	{
		auto S = [](int32 V) { return V == INDEX_NONE ? FString(TEXT("—")) : FString::FromInt(V); };
		return FString::Printf(TEXT("섹션 %s → %s"), *S(Old), *S(New));
	}
}

void SRadarAnimationTab::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel)
{
	Model = InModel;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			.Text(Model->HasCapability(TEXT("anim-keys")) ? LOCTEXT("AnimKeys", "애니메이션 → 트랙 → 섹션 → 채널 → 키 (키 위치는 무비신 틱 단위)")
				: LOCTEXT("AnimListLevel", "이 생산자는 애니메이션을 목록 수준(추가·삭제·트랙·섹션 수)까지 냅니다. 키 단위 값은 anim-keys 를 선언한 생산자에서 보입니다."))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(List, SListView<TSharedPtr<FRadarTabRow>>)
			.ListItemsSource(&Rows)
			.OnGenerateRow_Static(&MakeRow)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FRadarTabRow> Row, ESelectInfo::Type Info)
			{
				if (Row.IsValid() && Row->bSelectable && Info != ESelectInfo::Direct) { Model->Select(Row->Id, ERadarTab::Animation); }
			})
		]
	];
	Rebuild();
}

void SRadarAnimationTab::Rebuild()
{
	Rows.Reset();
	auto Val = [](const FRadarValue& V) { return V.bSet ? V.Text : FString(TEXT("—")); };
	auto Range = [](const TOptional<int32> (&R)[2])
	{
		auto B = [](const TOptional<int32>& V) { return V.IsSet() ? FString::FromInt(V.GetValue()) : FString(TEXT("∞")); };
		return FString::Printf(TEXT("%s~%s"), *B(R[0]), *B(R[1]));
	};
	auto AddProps = [this, &Val](const FString& Id, const TArray<FRadarPropertyDiff>& Props, int32 Depth)
	{
		for (const FRadarPropertyDiff& P : Props)
		{
			Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ Id, P.Path, FString::Printf(TEXT("%s → %s"), *Val(P.Old), *Val(P.New)), Depth, true }));
		}
	};
	for (const FRadarAnimationDiff& A : Model->Diff->Animations)
	{
		Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ A.Id, A.Id, LexToString(A.Kind), 0, true }));
		for (const FRadarTrackDiff& T : A.Tracks)
		{
			Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ A.Id, T.Id, FString::Printf(TEXT("%s · %s"), LexToString(T.Kind), *Sections(T.SectionsOld, T.SectionsNew)), 1, true }));
			for (const FRadarSectionDiff& S : T.Sections)
			{
				FString Detail = LexToString(S.Kind);
				if (S.bHasRangeOld || S.bHasRangeNew)
				{
					Detail += FString::Printf(TEXT(" · 범위 %s → %s"), *Range(S.RangeOld), *Range(S.RangeNew));
				}
				Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ A.Id, FString::Printf(TEXT("섹션 %d"), S.Index), Detail, 2, true }));
				AddProps(A.Id, S.Properties, 3);
				for (const FRadarChannelDiff& C : S.Channels)
				{
					Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ A.Id, FString::Printf(TEXT("채널 %s"), *C.Name), FString::Printf(TEXT("%s · 키 %d"), *C.Type, C.Keys.Num()), 3, true }));
					AddProps(A.Id, C.Properties, 4);
					for (const FRadarKeyDiff& K : C.Keys)
					{
						Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ A.Id, FString::Printf(TEXT("틱 %d"), K.Frame),
							FString::Printf(TEXT("%s · %s → %s"), LexToString(K.Kind), *Val(K.Old), *Val(K.New)), 4, true }));
						AddProps(A.Id, K.Properties, 5);
					}
				}
			}
		}
	}
	if (Rows.Num() == 0)
	{
		Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ FString(), TEXT("바뀐 애니메이션이 없습니다."), FString(), 0, false }));
	}
	List->RequestListRefresh();
}

void SRadarGraphTab::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel)
{
	Model = InModel;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
				.Text(LOCTEXT("GraphHelp", "그래프별 로직/배치 변경 수입니다. 노드 단위 상세는 엔진 Blueprint Diff 에서 봅니다."))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenBlueprintDiff", "Blueprint Diff에서 열기"))
				.IsEnabled_Lambda([this]() { return !Model->IsDataOnly(); })
				.OnClicked(this, &SRadarGraphTab::OpenBlueprintDiff)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(List, SListView<TSharedPtr<FRadarTabRow>>)
			.ListItemsSource(&Rows)
			.OnGenerateRow_Static(&MakeRow)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FRadarTabRow> Row, ESelectInfo::Type Info)
			{
				if (Row.IsValid() && Row->bSelectable && Info != ESelectInfo::Direct) { Model->Select(Row->Id, ERadarTab::Graph); }
			})
		]
	];
	Rebuild();
}

void SRadarGraphTab::Rebuild()
{
	Rows.Reset();
	for (const FRadarGraphDiff& G : Model->Diff->Graphs)
	{
		Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ FString(), G.Id,
			FString::Printf(TEXT("%s · 로직 +%d −%d ~%d · 배치 %d"), LexToString(G.Kind), G.LogicAdded, G.LogicRemoved, G.LogicChanged, G.LayoutMoved), 0, false }));
		for (const FRadarNodeDiff& N : G.Nodes)
		{
			Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ G.Id + TEXT("/") + N.Guid, N.Title.IsEmpty() ? N.Guid : N.Title,
				FString::Printf(TEXT("%s · %s"), LexToString(N.Kind), LexToString(N.Area)), 1, true }));
		}
	}
	if (Rows.Num() == 0)
	{
		Rows.Add(MakeShared<FRadarTabRow>(FRadarTabRow{ FString(),
			Model->HasCapability(TEXT("graph")) ? FString(TEXT("바뀐 그래프가 없습니다.")) : FString(TEXT("이 생산자는 그래프 비교를 내지 않습니다.")), FString(), 0, false }));
	}
	List->RequestListRefresh();
}

FReply SRadarGraphTab::OpenBlueprintDiff()
{
	const TSharedPtr<FRadarDiffSession>& Session = Model->Session;
	if (!Session.IsValid() || !Session->OldBlueprint || !Session->NewBlueprint)
	{
		return FReply::Handled();
	}
	FRevisionInfo OldRev = FRevisionInfo::InvalidRevision();
	FRevisionInfo NewRev = FRevisionInfo::InvalidRevision();
	OldRev.Revision = Session->OldRevision.ShortId;
	NewRev.Revision = Session->NewRevision.ShortId;
	TSharedPtr<SWindow> Window = SBlueprintDiff::CreateDiffWindow(
		FText::Format(LOCTEXT("BpDiffTitle", "{0}: {1} ↔ {2}"), FText::FromString(Model->Diff->AssetName), Session->OldRevision.GetLabel(), Session->NewRevision.GetLabel()),
		Session->OldBlueprint, Session->NewBlueprint, OldRev, NewRev);
	if (Window.IsValid())
	{
		Session->AttachWindow(Window.ToSharedRef());   // Blueprint Diff 창이 열려 있는 동안 세션을 살려 둔다 (§8.6)
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
