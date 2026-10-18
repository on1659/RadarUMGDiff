// Copyright Radar. All Rights Reserved.

#include "Viewer/RadarViewModel.h"
#include "RadarRevisionSource.h"
#include "RadarUMGDiffSettings.h"

FRadarViewModel::FRadarViewModel(TSharedRef<FRadarDiff> InDiff, TSharedPtr<FRadarDiffSession> InSession)
	: Diff(InDiff)
	, Session(InSession)
{
	const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
	OverlayOpacity = S.OverlayOpacity;
	bChangedOnly = S.bChangedOnlyDefault;
	bDimUnchanged = S.bDimUnchangedDefault;
	bHideAuto = S.bHideAutoDefault;
	Mode = S.DefaultPreviewMode == TEXT("Overlay") ? ERadarPreviewMode::Overlay
		: (S.DefaultPreviewMode == TEXT("Flip") ? ERadarPreviewMode::Flip : ERadarPreviewMode::SideBySide);
	for (const FRadarWidgetDiff& W : Diff->Widgets)
	{
		WidgetById.Add(W.Id, &W);
	}
	Refresh();
}

bool FRadarViewModel::IsDataOnly() const
{
	return !Session.IsValid() || !Session->OldPreview || !Session->NewPreview;
}

bool FRadarViewModel::HasCapability(const TCHAR* Capability) const
{
	return Diff->Capabilities.Contains(Capability);
}

const FRadarWidgetDiff* FRadarViewModel::FindWidget(const FString& Id) const
{
	const FRadarWidgetDiff* const* W = WidgetById.Find(Id);
	return W ? *W : nullptr;
}

TSharedPtr<FRadarTreeNode> FRadarViewModel::FindNode(const FString& Id) const
{
	const TSharedPtr<FRadarTreeNode>* N = NodeById.Find(Id);
	return N ? *N : nullptr;
}

void FRadarViewModel::Refresh()
{
	BuildTree();
	ApplySearch();
	BuildFlat();
	if (CursorIndex >= Flat.Num())
	{
		CursorIndex = INDEX_NONE;
	}
}

static FString ShortName(const FString& Id)
{
	FString Left, Right;
	return Id.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd) ? Right : Id;
}

void FRadarViewModel::BuildTree()
{
	Roots.Reset();
	NodeById.Reset();
	const FRadarTreeSnapshot& NewTree = Diff->NewTree;
	const FRadarTreeSnapshot& OldTree = Diff->OldTree;

	auto MakeNode = [this](const FString& Id, const FRadarTreeSnapshot& Tree, FRadarTreeNode::ERow Row) -> TSharedPtr<FRadarTreeNode>
	{
		TSharedPtr<FRadarTreeNode> N = MakeShared<FRadarTreeNode>();
		N->Row = Row;
		N->Id = Id;
		const FString* Label = Tree.Labels.Find(Id);
		N->Label = (Label && !Label->IsEmpty()) ? *Label : ShortName(Id);
		const FString* Class = Tree.Classes.Find(Id);
		N->Class = Class ? *Class : FString();
		if (const FRadarWidgetDiff* W = FindWidget(Id))
		{
			N->Diff = W;
			N->Kind = W->Kind;
			N->bMovedToo = W->bMovedToo;
			if (N->Class.IsEmpty()) { N->Class = W->Class; }
			if (W->Rename.IsSet())
			{
				N->Label = FString::Printf(TEXT("%s → %s (추정 %.2f)"), *ShortName(W->Rename->From), *N->Label, W->Rename->Confidence);
			}
		}
		return N;
	};

	// 1. 새 트리 (추가 위젯은 새 인덱스에 그대로 있다)
	TFunction<void(const FString&, const TSharedPtr<FRadarTreeNode>&)> AddNew = [&](const FString& Id, const TSharedPtr<FRadarTreeNode>& Parent)
	{
		TSharedPtr<FRadarTreeNode> N = MakeNode(Id, NewTree, FRadarTreeNode::ERow::Widget);
		N->Parent = Parent;
		NodeById.Add(Id, N);
		(Parent.IsValid() ? Parent->Children : Roots).Add(N);
		if (const TArray<FString>* Kids = NewTree.Children.Find(Id))
		{
			for (const FString& K : *Kids) { AddNew(K, N); }
		}
	};
	if (NewTree.bValid && !NewTree.Root.IsEmpty())
	{
		AddNew(NewTree.Root, nullptr);
	}
	else
	{
		// 트리 정보가 없으면(state.json 없이 연 diff.json) 바뀐 위젯만 평평하게
		for (const FRadarWidgetDiff& W : Diff->Widgets)
		{
			if (W.Kind != ERadarKind::Removed)
			{
				TSharedPtr<FRadarTreeNode> N = MakeNode(W.Id, NewTree, FRadarTreeNode::ERow::Widget);
				NodeById.Add(W.Id, N);
				Roots.Add(N);
			}
		}
	}

	// 2. 지워진 위젯: 옛 부모가 살아 있으면 그 자식 목록의 옛 인덱스 자리에 (옛 트리 순서로 — 부모가 먼저)
	TArray<const FRadarWidgetDiff*> Removed;
	for (const FRadarWidgetDiff& W : Diff->Widgets)
	{
		if (W.Kind == ERadarKind::Removed) { Removed.Add(&W); }
	}
	auto OldDepth = [&OldTree](const FString& Id)
	{
		int32 Depth = 0;
		FString Cur = Id;
		for (int32 Guard = 0; Guard < 256; ++Guard)
		{
			bool bFound = false;
			for (const TPair<FString, TArray<FString>>& P : OldTree.Children)
			{
				if (P.Value.Contains(Cur)) { Cur = P.Key; ++Depth; bFound = true; break; }
			}
			if (!bFound) { break; }
		}
		return Depth;
	};
	Removed.Sort([&OldDepth](const FRadarWidgetDiff& A, const FRadarWidgetDiff& B) { return OldDepth(A.Id) < OldDepth(B.Id); });
	for (const FRadarWidgetDiff* W : Removed)
	{
		TSharedPtr<FRadarTreeNode> N = MakeNode(W->Id, OldTree, FRadarTreeNode::ERow::Removed);
		if (const FString* Label = OldTree.Labels.Find(W->Id); (!Label || Label->IsEmpty()) && W->LabelOld.bSet && !W->LabelOld.Value.IsEmpty())
		{
			N->Label = W->LabelOld.Value;
		}
		TSharedPtr<FRadarTreeNode> Parent = W->ParentOld.bSet ? FindNode(W->ParentOld.Value) : nullptr;
		N->Parent = Parent;
		TArray<TSharedPtr<FRadarTreeNode>>& Siblings = Parent.IsValid() ? Parent->Children : Roots;
		Siblings.Insert(N, FMath::Clamp(W->IndexOld, 0, Siblings.Num()));
		NodeById.Add(W->Id, N);
	}

	// 3. 이동: 옛 자리에 유령 행
	for (const FRadarWidgetDiff& W : Diff->Widgets)
	{
		if ((W.Kind == ERadarKind::Moved || W.bMovedToo) && W.ParentOld.bSet)
		{
			TSharedPtr<FRadarTreeNode> Parent = FindNode(W.ParentOld.Value);
			if (!Parent.IsValid()) { continue; }
			TSharedPtr<FRadarTreeNode> Ghost = MakeNode(W.Id, OldTree, FRadarTreeNode::ERow::Ghost);
			Ghost->Label = FString::Printf(TEXT("%s  → %s[%d] 로 이동"), *Ghost->Label, *ShortName(W.ParentNew.Value), W.IndexNew);
			Ghost->Parent = Parent;
			Parent->Children.Insert(Ghost, FMath::Clamp(W.IndexOld, 0, Parent->Children.Num()));
		}
	}

	// 4. 바인딩·CDO 가짜 행 (맨 위)
	if (Diff->Cdo.Num() + Diff->Variables.Num() > 0)
	{
		TSharedPtr<FRadarTreeNode> N = MakeShared<FRadarTreeNode>();
		N->Row = FRadarTreeNode::ERow::Cdo;
		N->Id = TEXT("(cdo)");
		N->Label = FString::Printf(TEXT("클래스 기본값 · 변수 (%d)"), Diff->Cdo.Num() + Diff->Variables.Num());
		N->Kind = ERadarKind::Changed;
		Roots.Insert(N, 0);
		NodeById.Add(N->Id, N);
	}
	if (Diff->Bindings.Num() > 0)
	{
		TSharedPtr<FRadarTreeNode> N = MakeShared<FRadarTreeNode>();
		N->Row = FRadarTreeNode::ERow::Bindings;
		N->Id = TEXT("(bindings)");
		N->Label = FString::Printf(TEXT("바인딩 (%d)"), Diff->Bindings.Num());
		N->Kind = ERadarKind::Changed;
		Roots.Insert(N, 0);
		NodeById.Add(N->Id, N);
	}
	for (const TSharedPtr<FRadarTreeNode>& R : Roots)
	{
		CountDescendants(R);
	}
}

bool FRadarViewModel::WidgetChangedForFilter(const FRadarWidgetDiff* W) const
{
	return W && PassesFilter(*W);
}

bool FRadarViewModel::PassesFilter(const FRadarWidgetDiff& W) const
{
	if (W.Kind == ERadarKind::Unchanged)
	{
		return false;
	}
	if (KindFilter.Num() > 0 && !KindFilter.Contains(LexToString(W.Kind)) && !(W.bMovedToo && KindFilter.Contains(TEXT("moved"))))
	{
		return false;
	}
	if (AreaFilter.Num() > 0)
	{
		const bool bSlot = W.Slot.Properties.Num() > 0 || W.Kind == ERadarKind::Moved || W.bMovedToo;
		const bool bDesigner = W.Area == ERadarArea::Designer;
		if (!((bDesigner && AreaFilter.Contains(TEXT("designer"))) || (bSlot && AreaFilter.Contains(TEXT("slot")))))
		{
			return false;
		}
	}
	if (bHideAuto && W.Kind == ERadarKind::Changed && !W.bMovedToo && W.Slot.Properties.Num() == 0)
	{
		// 바뀐 속성이 전부 자동 값이면 바뀌지 않은 것으로 본다
		bool bAnyReal = false;
		for (const FRadarPropertyDiff& P : W.Properties) { bAnyReal |= !P.bAuto; }
		if (!bAnyReal) { return false; }
	}
	return true;
}

void FRadarViewModel::CountDescendants(const TSharedPtr<FRadarTreeNode>& Node)
{
	Node->DescendantChanges = 0;
	for (const TSharedPtr<FRadarTreeNode>& C : Node->Children)
	{
		CountDescendants(C);
		const bool bChanged = C->Row != FRadarTreeNode::ERow::Ghost && WidgetChangedForFilter(C->Diff);
		Node->DescendantChanges += C->DescendantChanges + (bChanged ? 1 : 0);
	}
}

bool FRadarViewModel::MatchesSearch(const FRadarTreeNode& Node) const
{
	if (SearchText.IsEmpty())
	{
		return false;
	}
	if (Node.Id.Contains(SearchText) || Node.Label.Contains(SearchText))
	{
		return true;
	}
	if (Node.Diff)
	{
		auto RowMatch = [this](const FRadarPropertyDiff& P)
		{
			return P.Path.Contains(SearchText) || (P.Old.bSet && P.Old.Text.Contains(SearchText)) || (P.New.bSet && P.New.Text.Contains(SearchText));
		};
		for (const FRadarPropertyDiff& P : Node.Diff->Properties) { if (RowMatch(P)) { return true; } }
		for (const FRadarPropertyDiff& P : Node.Diff->Slot.Properties) { if (RowMatch(P)) { return true; } }
	}
	return false;
}

void FRadarViewModel::ApplySearch()
{
	for (const TPair<FString, TSharedPtr<FRadarTreeNode>>& P : NodeById)
	{
		P.Value->bSearchMatch = MatchesSearch(*P.Value);
	}
}

bool FRadarViewModel::IsVisible(const TSharedPtr<FRadarTreeNode>& Node) const
{
	if (!SearchText.IsEmpty())
	{
		// 검색 중: 일치한 행과 그 조상만
		TFunction<bool(const TSharedPtr<FRadarTreeNode>&)> AnyMatch = [&](const TSharedPtr<FRadarTreeNode>& N)
		{
			if (N->bSearchMatch) { return true; }
			for (const TSharedPtr<FRadarTreeNode>& C : N->Children) { if (AnyMatch(C)) { return true; } }
			return false;
		};
		return AnyMatch(Node);
	}
	if (Node->Row == FRadarTreeNode::ERow::Bindings || Node->Row == FRadarTreeNode::ERow::Cdo)
	{
		return AreaFilter.Num() == 0 || AreaFilter.Contains(Node->Row == FRadarTreeNode::ERow::Bindings ? TEXT("binding") : TEXT("cdo"));
	}
	if (!bChangedOnly)
	{
		return true;
	}
	return WidgetChangedForFilter(Node->Diff) || Node->DescendantChanges > 0;
}

void FRadarViewModel::BuildFlat()
{
	Flat.Reset();
	// 디자이너: 계층 순서로 바뀐 위젯, 그다음 바인딩·CDO
	TFunction<void(const TSharedPtr<FRadarTreeNode>&)> Walk = [&](const TSharedPtr<FRadarTreeNode>& N)
	{
		if (N->Row == FRadarTreeNode::ERow::Bindings || N->Row == FRadarTreeNode::ERow::Cdo)
		{
			if (IsVisible(N)) { Flat.Add({ ERadarTab::Designer, N->Id }); }
		}
		else if (N->Row != FRadarTreeNode::ERow::Ghost && WidgetChangedForFilter(N->Diff))
		{
			Flat.Add({ ERadarTab::Designer, N->Id });
		}
		for (const TSharedPtr<FRadarTreeNode>& C : N->Children) { Walk(C); }
	};
	for (const TSharedPtr<FRadarTreeNode>& R : Roots) { Walk(R); }
	// 트리 밖에 남은 바뀐 위젯(트리 정보 없음)
	for (const FRadarWidgetDiff& W : Diff->Widgets)
	{
		if (!NodeById.Contains(W.Id) && PassesFilter(W)) { Flat.Add({ ERadarTab::Designer, W.Id }); }
	}
	if (AreaFilter.Num() == 0 || AreaFilter.Contains(TEXT("anim")))
	{
		for (const FRadarAnimationDiff& A : Diff->Animations) { Flat.Add({ ERadarTab::Animation, A.Id }); }
	}
	for (const FRadarGraphDiff& G : Diff->Graphs)
	{
		for (const FRadarNodeDiff& N : G.Nodes)
		{
			const bool bLogic = N.Area == ERadarArea::GraphLogic;
			if (AreaFilter.Num() == 0 || AreaFilter.Contains(bLogic ? TEXT("graph-logic") : TEXT("graph-layout")))
			{
				Flat.Add({ ERadarTab::Graph, G.Id + TEXT("/") + N.Guid });
			}
		}
	}
}

void FRadarViewModel::Select(const FString& Id, ERadarTab InTab)
{
	SelectedId = Id;
	Tab = InTab;
	CursorIndex = Flat.IndexOfByPredicate([&Id](const FRadarFlatItem& I) { return I.Id == Id; });
	OnSelectionChanged.Broadcast();
}

const FRadarFlatItem* FRadarViewModel::Step(int32 Delta)
{
	if (Flat.Num() == 0)
	{
		return nullptr;
	}
	CursorIndex = CursorIndex == INDEX_NONE ? (Delta > 0 ? 0 : Flat.Num() - 1) : (CursorIndex + Delta + Flat.Num()) % Flat.Num();
	const FRadarFlatItem& Item = Flat[CursorIndex];
	SelectedId = Item.Id;
	Tab = Item.Tab;
	OnSelectionChanged.Broadcast();
	return &Item;
}
