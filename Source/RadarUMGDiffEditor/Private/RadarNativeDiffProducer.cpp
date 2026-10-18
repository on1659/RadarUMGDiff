// Copyright Radar. All Rights Reserved.

#include "RadarNativeDiffProducer.h"
#include "RadarPropertyCompare.h"
#include "RadarRevisionSource.h"
#include "RadarDiffJson.h"
#include "RadarEngineCompat.h"
#include "RadarUMGDiffSettings.h"
#include "RadarUMGDiffLog.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneChannel.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

namespace
{
	struct FWidgetInfo
	{
		UWidget* Widget = nullptr;
		FString Id;
		FString Parent;
		int32 Index = INDEX_NONE;
		FString Label;
		FString Class;
	};

	struct FTree
	{
		TMap<FString, FWidgetInfo> Map;
		TArray<FString> Order;          // 트리 순회 순서
		FString Root;
	};

	FTree CollectWidgets(UWidgetBlueprint* BP, const FRadarCompareContext& Ctx)
	{
		FTree Tree;
		if (!BP || !BP->WidgetTree)
		{
			return Tree;
		}
		if (BP->WidgetTree->RootWidget)
		{
			Tree.Root = BP->WidgetTree->RootWidget->GetPathName(BP);
		}
		BP->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			FWidgetInfo I;
			I.Widget = W;
			I.Id = W->GetPathName(BP);
			if (UPanelWidget* Parent = W->GetParent())
			{
				I.Parent = Parent->GetPathName(BP);
				I.Index = Parent->GetChildIndex(W);
			}
#if WITH_EDITORONLY_DATA
			I.Label = W->GetDisplayLabel();
#endif
			I.Class = Ctx.ObjectPath(W->GetClass());
			Tree.Order.Add(I.Id);
			Tree.Map.Add(I.Id, MoveTemp(I));
		});
		return Tree;
	}

	/** 같은 부모 아래에서, 양쪽에 같은 부모로 남아 있는 형제들 사이의 순서 (앞 형제가 지워져 인덱스가 밀린 것은 이동이 아니다). */
	int32 RelativeIndex(const FTree& Self, const FTree& Other, const FString& Id)
	{
		const FWidgetInfo& Me = Self.Map[Id];
		TArray<TPair<int32, FString>> Siblings;
		for (const TPair<FString, FWidgetInfo>& Pair : Self.Map)
		{
			const FWidgetInfo* There = Other.Map.Find(Pair.Key);
			if (Pair.Value.Parent == Me.Parent && There && There->Parent == Me.Parent)
			{
				Siblings.Emplace(Pair.Value.Index, Pair.Key);
			}
		}
		Siblings.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
		return Siblings.IndexOfByPredicate([&Id](const TPair<int32, FString>& P) { return P.Value == Id; });
	}

	FRadarCompareContext MakeContext(const FRadarDiffSession& Session)
	{
		FRadarCompareContext Ctx = FRadarCompareContext::FromSettings();
		for (const UPackage* Package : { Session.OldPackage.Get(), Session.NewPackage.Get() })
		{
			if (Package && Package->GetName() != Session.AssetPackageName)
			{
				Ctx.PackageRemap.Emplace(Package->GetName(), Session.AssetPackageName);
			}
		}
		return Ctx;
	}

	void FillSide(FRadarSideInfo& Side, const FRadarRevision& R, UWidgetBlueprint* BP)
	{
		Side.Kind = LexToString(R.Kind);
		switch (R.Kind)
		{
		case ERadarRevisionKind::Commit:
		case ERadarRevisionKind::Head:
			Side.Sha = R.Id;
			Side.Author = R.User;
			Side.Subject = R.Description;
			Side.Date = R.Date.ToIso8601();
			break;
		case ERadarRevisionKind::Scm:
			Side.Provider = R.ProviderName;
			Side.Revision = R.Id;
			Side.Changelist = R.Changelist;
			Side.User = R.User;
			Side.Description = R.Description;
			Side.Date = R.Date.ToIso8601();
			break;
		case ERadarRevisionKind::File:
			Side.Path = R.LocalFile;
			break;
		case ERadarRevisionKind::Working:
			Side.bDirty = R.bDirty;
			break;
		}
		if (BP)
		{
			const UPackage* Package = BP->GetOutermost();
			Side.FileVersionUE5 = Package->GetLinkerPackageVersion().FileVersionUE5;
			for (const FCustomVersion& V : Package->GetLinkerCustomVersions().GetAllVersions())
			{
				Side.CustomVersions.Emplace(V.Key.ToString(EGuidFormats::Digits), V.Version);
			}
#if WITH_EDITORONLY_DATA
			if (BP->GeneratedClass)
			{
				if (const UUserWidget* CDO = Cast<UUserWidget>(BP->GeneratedClass->GetDefaultObject()))
				{
					Side.DesignSize = CDO->DesignTimeSize;
					Side.DesignSizeMode = StaticEnum<EDesignPreviewSizeMode>()->GetNameStringByValue(static_cast<int64>(CDO->DesignSizeMode));
				}
			}
#endif
		}
	}

	void DiffSlots(UWidget* OldW, UWidget* NewW, FRadarCompareContext Ctx, FRadarSlotDiff& Out)
	{
		UPanelSlot* OldSlot = OldW ? OldW->Slot.Get() : nullptr;
		UPanelSlot* NewSlot = NewW ? NewW->Slot.Get() : nullptr;
		if (OldSlot) { Out.ClassOld = Ctx.ObjectPath(OldSlot->GetClass()); }
		if (NewSlot) { Out.ClassNew = Ctx.ObjectPath(NewSlot->GetClass()); }
		if (OldSlot && NewSlot)
		{
			Ctx.StructuralProperties = { TEXT("Parent"), TEXT("Content") };
			RadarProperty::DiffObjects(OldSlot, NewSlot, FString(), Ctx, Out.Properties);
		}
	}

	FRadarWidgetDiff MakeOneSided(const FWidgetInfo& I, bool bAdded, const FRadarCompareContext& Ctx)
	{
		FRadarWidgetDiff D;
		D.Id = I.Id;
		D.Kind = bAdded ? ERadarKind::Added : ERadarKind::Removed;
		D.Class = I.Class;
		D.StateRef = I.Id;
		if (bAdded)
		{
			D.bHasNew = true;
			D.LabelNew = I.Label;
			if (!I.Parent.IsEmpty()) { D.ParentNew = I.Parent; }
			D.IndexNew = I.Index;
		}
		else
		{
			D.bHasOld = true;
			D.LabelOld = I.Label;
			if (!I.Parent.IsEmpty()) { D.ParentOld = I.Parent; }
			D.IndexOld = I.Index;
		}
		FRadarPropertyDiff& Star = D.Properties.AddDefaulted_GetRef();
		Star.Path = TEXT("*");
		Star.Kind = D.Kind;
		FRadarValue& V = bAdded ? Star.New : Star.Old;
		V.bSet = true;
		V.Text = TEXT("…전체 상태 덤프 참조…");
		DiffSlots(bAdded ? nullptr : I.Widget, bAdded ? I.Widget : nullptr, Ctx, D.Slot);
		return D;
	}

	void DiffWidgets(UWidgetBlueprint* OldBP, UWidgetBlueprint* NewBP, const FRadarCompareContext& BaseCtx, FRadarDiff& Diff)
	{
		const FTree Old = CollectWidgets(OldBP, BaseCtx);
		const FTree New = CollectWidgets(NewBP, BaseCtx);

		FRadarCompareContext Ctx = BaseCtx;
		Ctx.StructuralProperties = { TEXT("Slot"), TEXT("Slots") };

		TArray<FString> Removed, Added;
		// 새 트리 순서로 → 그다음 지워진 것
		for (const FString& Id : New.Order)
		{
			const FWidgetInfo& N = New.Map[Id];
			const FWidgetInfo* O = Old.Map.Find(Id);
			if (!O)
			{
				Added.Add(Id);
				continue;
			}
			FRadarWidgetDiff D;
			D.Id = Id;
			D.Class = N.Class;
			D.bHasOld = D.bHasNew = true;
			D.LabelOld = O->Label;
			D.LabelNew = N.Label;
			if (!O->Parent.IsEmpty()) { D.ParentOld = O->Parent; }
			if (!N.Parent.IsEmpty()) { D.ParentNew = N.Parent; }
			D.IndexOld = O->Index;
			D.IndexNew = N.Index;
			if (O->Class != N.Class)
			{
				FRadarPropertyDiff& Row = D.Properties.AddDefaulted_GetRef();
				Row.Path = TEXT("(class)");
				Row.Type = TEXT("Class");
				Row.Category = TEXT("기타");
				Row.Old.bSet = Row.New.bSet = true;
				Row.Old.Text = O->Class;
				Row.New.Text = N.Class;
				Row.Old.Value = MakeShared<FJsonValueString>(O->Class);
				Row.New.Value = MakeShared<FJsonValueString>(N.Class);
			}
			RadarProperty::DiffObjects(O->Widget, N.Widget, FString(), Ctx, D.Properties);
			DiffSlots(O->Widget, N.Widget, BaseCtx, D.Slot);

			const bool bMoved = O->Parent != N.Parent || RelativeIndex(Old, New, Id) != RelativeIndex(New, Old, Id);
			const bool bProps = D.Properties.Num() > 0;
			const bool bSlotChanged = D.Slot.Properties.Num() > 0 || D.Slot.ClassOld.Value != D.Slot.ClassNew.Value;
			if (!bProps && !bSlotChanged && !bMoved)
			{
				continue;
			}
			if (bProps)
			{
				D.Kind = ERadarKind::Changed;
				D.bMovedToo = bMoved;
				D.Area = ERadarArea::Designer;
			}
			else
			{
				D.Kind = bMoved ? ERadarKind::Moved : ERadarKind::Changed;
				D.Area = ERadarArea::Slot;
			}
			// 바뀐 것이 DisplayLabel 뿐이면 저심각도 (§4.4)
			if (bProps && !bSlotChanged && !bMoved && D.Properties.Num() == 1 && D.Properties[0].Path == TEXT("DisplayLabel"))
			{
				D.Severity = ERadarSeverity::Low;
			}
			Diff.Widgets.Add(MoveTemp(D));
		}
		for (const FString& Id : Old.Order)
		{
			if (!New.Map.Contains(Id))
			{
				Removed.Add(Id);
			}
		}

		// 이름 변경 추정 (§7.3): 같은 클래스의 지워진 것 × 추가된 것
		const double Threshold = URadarUMGDiffSettings::Get().RenameThreshold;
		struct FCandidate { FString R, A; double Score; FString Evidence; };
		TArray<FCandidate> Candidates;
		for (const FString& R : Removed)
		{
			const FWidgetInfo& RI = Old.Map[R];
			for (const FString& A : Added)
			{
				const FWidgetInfo& AI = New.Map[A];
				if (RI.Class != AI.Class)
				{
					continue;
				}
				if (RI.Parent == AI.Parent && RI.Index == AI.Index)
				{
					Candidates.Add({ R, A, 1.0, TEXT("same-parent-index") });
					continue;
				}
				int32 OldN = 0, NewN = 0, Same = 0;
				RadarProperty::CountPresent(RI.Widget, AI.Widget, Ctx, OldN, NewN, Same);
				const int32 MaxN = FMath::Max(OldN, NewN);
				const double Score = MaxN > 0 ? static_cast<double>(Same) / MaxN : 0.0;
				if (Score >= Threshold)
				{
					Candidates.Add({ R, A, Score, TEXT("property-set") });
				}
			}
		}
		Candidates.StableSort([](const FCandidate& X, const FCandidate& Y) { return X.Score > Y.Score; });
		TSet<FString> UsedR, UsedA;
		for (const FCandidate& C : Candidates)
		{
			if (UsedR.Contains(C.R) || UsedA.Contains(C.A))
			{
				continue;
			}
			UsedR.Add(C.R);
			UsedA.Add(C.A);
			const FWidgetInfo& RI = Old.Map[C.R];
			const FWidgetInfo& AI = New.Map[C.A];
			FRadarWidgetDiff D;
			D.Id = C.A;
			D.Kind = ERadarKind::RenamedProbable;
			D.Class = AI.Class;
			D.bHasOld = D.bHasNew = true;
			D.LabelOld = RI.Label;
			D.LabelNew = AI.Label;
			if (!RI.Parent.IsEmpty()) { D.ParentOld = RI.Parent; }
			if (!AI.Parent.IsEmpty()) { D.ParentNew = AI.Parent; }
			D.IndexOld = RI.Index;
			D.IndexNew = AI.Index;
			FRadarRename Rename;
			Rename.From = C.R;
			Rename.Confidence = C.Score;
			Rename.Evidence = C.Evidence;
			D.Rename = Rename;
			RadarProperty::DiffObjects(RI.Widget, AI.Widget, FString(), Ctx, D.Properties);
			DiffSlots(RI.Widget, AI.Widget, BaseCtx, D.Slot);
			Diff.Widgets.Add(MoveTemp(D));
		}
		for (const FString& A : Added)
		{
			if (!UsedA.Contains(A))
			{
				Diff.Widgets.Add(MakeOneSided(New.Map[A], true, BaseCtx));
			}
		}
		for (const FString& R : Removed)
		{
			if (!UsedR.Contains(R))
			{
				Diff.Widgets.Add(MakeOneSided(Old.Map[R], false, BaseCtx));
			}
		}

		// 중첩 UserWidget 은 프로젝트의 현재 버전으로 그려진다 → 정보 배지
		TSet<FString> Noted;
		for (const FTree* T : { &New, &Old })
		{
			for (const TPair<FString, FWidgetInfo>& Pair : T->Map)
			{
				if (Pair.Value.Widget->IsA<UUserWidget>() && !Noted.Contains(Pair.Key))
				{
					Noted.Add(Pair.Key);
					Diff.Notes.Add({ TEXT("nested-userwidget-current-version"), Pair.Key, ERadarSeverity::Info });
				}
			}
		}
	}

	void DiffBindings(UWidgetBlueprint* OldBP, UWidgetBlueprint* NewBP, FRadarDiff& Diff)
	{
#if WITH_EDITORONLY_DATA
		auto Key = [](const FDelegateEditorBinding& B) { return B.ObjectName + TEXT(":") + B.PropertyName.ToString(); };
		auto KindText = [](const FDelegateEditorBinding& B) { return B.Kind == EBindingKind::Function ? FString(TEXT("Function")) : FString(TEXT("Property")); };
		TMap<FString, const FDelegateEditorBinding*> OldMap, NewMap;
		for (const FDelegateEditorBinding& B : OldBP->Bindings) { OldMap.Add(Key(B), &B); }
		for (const FDelegateEditorBinding& B : NewBP->Bindings) { NewMap.Add(Key(B), &B); }
		for (const FDelegateEditorBinding& B : NewBP->Bindings)
		{
			const FString Id = Key(B);
			const FDelegateEditorBinding* const* O = OldMap.Find(Id);
			FRadarBindingDiff D;
			D.Id = Id;
			D.NewFunction = B.FunctionName.ToString();
			D.NewKind = KindText(B);
			D.NewSourceProperty = B.SourceProperty.ToString();
			if (!O)
			{
				D.Kind = ERadarKind::Added;
			}
			else
			{
				D.OldFunction = (*O)->FunctionName.ToString();
				D.OldKind = KindText(**O);
				D.OldSourceProperty = (*O)->SourceProperty.ToString();
				if (D.OldFunction.Value == D.NewFunction.Value && D.OldKind.Value == D.NewKind.Value && D.OldSourceProperty.Value == D.NewSourceProperty.Value)
				{
					continue;
				}
				D.Kind = ERadarKind::Changed;
			}
			Diff.Bindings.Add(MoveTemp(D));
		}
		for (const FDelegateEditorBinding& B : OldBP->Bindings)
		{
			if (!NewMap.Contains(Key(B)))
			{
				FRadarBindingDiff D;
				D.Id = Key(B);
				D.Kind = ERadarKind::Removed;
				D.OldFunction = B.FunctionName.ToString();
				D.OldKind = KindText(B);
				D.OldSourceProperty = B.SourceProperty.ToString();
				Diff.Bindings.Add(MoveTemp(D));
			}
		}
#endif
	}

	void DiffCdo(UWidgetBlueprint* OldBP, UWidgetBlueprint* NewBP, const FRadarCompareContext& Ctx, FRadarDiff& Diff)
	{
		const UObject* OldCDO = OldBP->GeneratedClass ? OldBP->GeneratedClass->GetDefaultObject() : nullptr;
		const UObject* NewCDO = NewBP->GeneratedClass ? NewBP->GeneratedClass->GetDefaultObject() : nullptr;
		if (OldCDO && NewCDO)
		{
			RadarProperty::DiffObjects(OldCDO, NewCDO, FString(), Ctx, Diff.Cdo);
		}
		for (const UWidgetBlueprint* BP : { NewBP, OldBP })
		{
			for (const FBPVariableDescription& V : BP->NewVariables)
			{
				Diff.CdoVarGuids.FindOrAdd(V.VarName.ToString(), V.VarGuid.ToString(EGuidFormats::Digits));
			}
		}
	}

	void DiffVariables(UWidgetBlueprint* OldBP, UWidgetBlueprint* NewBP, const FRadarCompareContext& Ctx, FRadarDiff& Diff)
	{
		TMap<FGuid, const FBPVariableDescription*> OldMap;
		for (const FBPVariableDescription& V : OldBP->NewVariables) { OldMap.Add(V.VarGuid, &V); }
		TSet<FGuid> Seen;
		for (const FBPVariableDescription& V : NewBP->NewVariables)
		{
			Seen.Add(V.VarGuid);
			FRadarVariableDiff D;
			D.Name = V.VarName.ToString();
			D.VarGuid = V.VarGuid.ToString(EGuidFormats::Digits);
			if (const FBPVariableDescription* const* O = OldMap.Find(V.VarGuid))
			{
				RadarProperty::DiffStruct(FBPVariableDescription::StaticStruct(), *O, &V, nullptr, nullptr, FString(), Ctx, D.Properties);
				if (D.Properties.Num() == 0)
				{
					continue;
				}
				D.Kind = ERadarKind::Changed;
				if ((*O)->VarName != V.VarName)
				{
					D.RenamedFrom = (*O)->VarName.ToString();
				}
			}
			else
			{
				D.Kind = ERadarKind::Added;
			}
			Diff.Variables.Add(MoveTemp(D));
		}
		for (const FBPVariableDescription& V : OldBP->NewVariables)
		{
			if (!Seen.Contains(V.VarGuid))
			{
				FRadarVariableDiff D;
				D.Name = V.VarName.ToString();
				D.VarGuid = V.VarGuid.ToString(EGuidFormats::Digits);
				D.Kind = ERadarKind::Removed;
				Diff.Variables.Add(MoveTemp(D));
			}
		}
	}

	// ── 애니메이션 (§7.4) ──
	struct FTrackRef
	{
		FString Id;
		const UMovieSceneTrack* Track = nullptr;
	};

	/** 애니메이션 하나의 트랙 목록: "<바인딩 위젯 이름 또는 Root>/<트랙 클래스>" (같은 이름이 또 있으면 #2, #3 …). */
	TArray<FTrackRef> CollectTracks(UWidgetAnimation* Anim)
	{
		TArray<FTrackRef> Out;
		if (!Anim || !Anim->MovieScene)
		{
			return Out;
		}
		TSet<FString> Used;
		auto AddTrack = [&Out, &Used](const FString& Owner, const UMovieSceneTrack* Track)
		{
			FString Id = Owner + TEXT("/") + Track->GetClass()->GetName();
			int32 N = 2;
			while (Used.Contains(Id))
			{
				Id = FString::Printf(TEXT("%s/%s#%d"), *Owner, *Track->GetClass()->GetName(), N++);
			}
			Used.Add(Id);
			Out.Add({ Id, Track });
		};
		for (const UMovieSceneTrack* Track : Anim->MovieScene->GetTracks())
		{
			AddTrack(TEXT("Root"), Track);
		}
		TMap<FGuid, FString> Names;
		for (const FWidgetAnimationBinding& B : RadarCompat::GetAnimationBindings(Anim))
		{
			Names.Add(B.AnimationGuid, B.bIsRootWidget ? FString(TEXT("Root")) : B.WidgetName.ToString());
		}
		for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Anim->MovieScene.Get())->GetBindings())
		{
			const FString* Name = Names.Find(Binding.GetObjectGuid());
			const FString Owner = Name ? *Name : Binding.GetObjectGuid().ToString(EGuidFormats::Digits);
			for (const UMovieSceneTrack* Track : Binding.GetTracks())
			{
				AddTrack(Owner, Track);
			}
		}
		return Out;
	}

	/** 섹션의 채널 속성 (FMovieSceneChannel 을 상속한 구조체). 정적 배열은 원소마다. */
	struct FChannelRef
	{
		FString Name;
		const FStructProperty* Property = nullptr;
		int32 ArrayIndex = 0;
	};

	TArray<FChannelRef> ChannelsOf(const UClass* Class)
	{
		TArray<FChannelRef> Out;
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FStructProperty* SP = CastField<FStructProperty>(*It);
			if (!SP || !SP->Struct->IsChildOf(FMovieSceneChannel::StaticStruct()))
			{
				continue;
			}
			for (int32 i = 0; i < SP->ArrayDim; ++i)
			{
				// 0번 원소는 이름만, 그 뒤는 이름[i] — Python 은 태그의 ArrayIndex 만 보므로 정적 배열 크기를 모른다
				Out.Add({ i == 0 ? SP->GetName() : FString::Printf(TEXT("%s[%d]"), *SP->GetName(), i), SP, i });
			}
		}
		return Out;
	}

	/** 범위 경계: 열려 있으면 값 없음. 경계 종류(포함·제외)도 비교에 쓴다. */
	struct FBound { bool bOpen = true; bool bInclusive = false; int32 Value = 0; };

	void RangeOf(const UMovieSceneSection* S, FBound& Lo, FBound& Hi)
	{
		const TRange<FFrameNumber> R = S->GetRange();
		Lo.bOpen = R.GetLowerBound().IsOpen();
		Lo.bInclusive = R.GetLowerBound().IsInclusive();
		Lo.Value = Lo.bOpen ? 0 : R.GetLowerBoundValue().Value;
		Hi.bOpen = R.GetUpperBound().IsOpen();
		Hi.bInclusive = R.GetUpperBound().IsInclusive();
		Hi.Value = Hi.bOpen ? 0 : R.GetUpperBoundValue().Value;
	}

	bool SameBound(const FBound& A, const FBound& B)
	{
		return A.bOpen == B.bOpen && (A.bOpen || (A.bInclusive == B.bInclusive && A.Value == B.Value));
	}

	/** 채널 구조체의 키 배열 두 개(Times·Values) — Float/Double/Bool/Integer… 채널이 모두 이 이름을 쓴다. */
	struct FKeyArrays
	{
		const FArrayProperty* Times = nullptr;
		const FArrayProperty* Values = nullptr;
	};

	FKeyArrays KeyArraysOf(const UScriptStruct* Struct)
	{
		FKeyArrays K;
		K.Times = CastField<FArrayProperty>(Struct->FindPropertyByName(TEXT("Times")));
		K.Values = CastField<FArrayProperty>(Struct->FindPropertyByName(TEXT("Values")));
		return K;
	}

	/** 키 하나의 값: 키 구조체의 Value 멤버, 원시 값 키면 그 값. */
	void KeyValue(const FProperty* Inner, const void* Ptr, const FRadarCompareContext& Ctx, FRadarValue& Out)
	{
		if (const FStructProperty* SP = CastField<FStructProperty>(Inner))
		{
			if (const FProperty* VP = SP->Struct->FindPropertyByName(TEXT("Value")))
			{
				Out = RadarProperty::MakeValue(VP, VP->ContainerPtrToValuePtr<void>(Ptr), nullptr, Ctx);
				return;
			}
		}
		Out = RadarProperty::MakeValue(Inner, Ptr, nullptr, Ctx);
	}

	/** 키 값과 나머지 멤버(보간·탄젠트)를 비교한다. KeyCtx 는 Value·PaddingByte 를 구조 속성으로 뺀 문맥. */
	bool DiffKeyBody(const FProperty* Inner, const void* O, const void* N, const FRadarCompareContext& KeyCtx, FRadarKeyDiff& Out)
	{
		const FStructProperty* SP = CastField<FStructProperty>(Inner);
		const FProperty* VP = SP ? SP->Struct->FindPropertyByName(TEXT("Value")) : nullptr;
		if (VP)
		{
			const bool bValue = !RadarProperty::Equal(VP, VP->ContainerPtrToValuePtr<void>(O), VP->ContainerPtrToValuePtr<void>(N), KeyCtx);
			RadarProperty::DiffStruct(SP->Struct, O, N, nullptr, nullptr, FString(), KeyCtx, Out.Properties);
			return bValue || Out.Properties.Num() > 0;
		}
		return !RadarProperty::Equal(Inner, O, N, KeyCtx);
	}

	bool DiffChannel(const FChannelRef& C, const void* OldContainer, const void* NewContainer, const void* OldArch, const void* NewArch,
		const FRadarCompareContext& Ctx, FRadarChannelDiff& Out)
	{
		const UScriptStruct* Struct = C.Property->Struct;
		const void* OC = C.Property->ContainerPtrToValuePtr<void>(OldContainer, C.ArrayIndex);
		const void* NC = C.Property->ContainerPtrToValuePtr<void>(NewContainer, C.ArrayIndex);
		Out.Name = C.Name;
		Out.Type = Struct->GetName();
		Out.Kind = ERadarKind::Changed;

		// 채널 자체 값: 두 쪽 다 저장된 채널일 때만 (아키타입과 같은 채널은 파일에 없다 — Python 이 보는 집합과 같게)
		const void* OA = OldArch ? C.Property->ContainerPtrToValuePtr<void>(OldArch, C.ArrayIndex) : nullptr;
		const void* NA = NewArch ? C.Property->ContainerPtrToValuePtr<void>(NewArch, C.ArrayIndex) : nullptr;
		const bool bOldSaved = !OA || !RadarProperty::Equal(C.Property, OC, OA, Ctx);
		const bool bNewSaved = !NA || !RadarProperty::Equal(C.Property, NC, NA, Ctx);
		if (bOldSaved && bNewSaved)
		{
			FRadarCompareContext ChannelCtx = Ctx;
			ChannelCtx.StructuralProperties = { TEXT("Times"), TEXT("Values"), TEXT("KeyHandles"), TEXT("bShowCurve") };
			RadarProperty::DiffStruct(Struct, OC, NC, nullptr, nullptr, FString(), ChannelCtx, Out.Properties);
		}

		const FKeyArrays K = KeyArraysOf(Struct);
		if (K.Times && K.Values)
		{
			FScriptArrayHelper OT(K.Times, K.Times->ContainerPtrToValuePtr<void>(OC));
			FScriptArrayHelper NT(K.Times, K.Times->ContainerPtrToValuePtr<void>(NC));
			FScriptArrayHelper OV(K.Values, K.Values->ContainerPtrToValuePtr<void>(OC));
			FScriptArrayHelper NV(K.Values, K.Values->ContainerPtrToValuePtr<void>(NC));
			FRadarCompareContext KeyCtx = Ctx;
			KeyCtx.StructuralProperties = { TEXT("Value"), TEXT("PaddingByte") };
			auto FrameAt = [](FScriptArrayHelper& H, int32 i) { return reinterpret_cast<const FFrameNumber*>(H.GetRawPtr(i))->Value; };
			// 같은 프레임에 키가 여럿이면 등장 순서로 짝짓는다
			TMap<int32, TArray<int32>> OldByFrame;
			for (int32 i = 0; i < OT.Num() && i < OV.Num(); ++i) { OldByFrame.FindOrAdd(FrameAt(OT, i)).Add(i); }
			TMap<int32, int32> Used;
			for (int32 i = 0; i < NT.Num() && i < NV.Num(); ++i)
			{
				const int32 Frame = FrameAt(NT, i);
				int32& Next = Used.FindOrAdd(Frame);
				const TArray<int32>* Olds = OldByFrame.Find(Frame);
				FRadarKeyDiff Key;
				Key.Frame = Frame;
				if (Olds && Next < Olds->Num())
				{
					const int32 j = (*Olds)[Next++];
					if (!DiffKeyBody(K.Values->Inner, OV.GetRawPtr(j), NV.GetRawPtr(i), KeyCtx, Key))
					{
						continue;
					}
					Key.Kind = ERadarKind::Changed;
					KeyValue(K.Values->Inner, OV.GetRawPtr(j), Ctx, Key.Old);
				}
				else
				{
					Key.Kind = ERadarKind::Added;
				}
				KeyValue(K.Values->Inner, NV.GetRawPtr(i), Ctx, Key.New);
				Out.Keys.Add(MoveTemp(Key));
			}
			for (const TPair<int32, TArray<int32>>& Pair : OldByFrame)
			{
				const int32* Next = Used.Find(Pair.Key);
				for (int32 n = Next ? *Next : 0; n < Pair.Value.Num(); ++n)
				{
					FRadarKeyDiff Key;
					Key.Frame = Pair.Key;
					Key.Kind = ERadarKind::Removed;
					KeyValue(K.Values->Inner, OV.GetRawPtr(Pair.Value[n]), Ctx, Key.Old);
					Out.Keys.Add(MoveTemp(Key));
				}
			}
			Out.Keys.StableSort([](const FRadarKeyDiff& A, const FRadarKeyDiff& B) { return A.Frame < B.Frame; });
		}
		return Out.Properties.Num() > 0 || Out.Keys.Num() > 0;
	}

	bool DiffSection(const UMovieSceneSection* O, const UMovieSceneSection* N, int32 Index, const FRadarCompareContext& Ctx, FRadarSectionDiff& Out)
	{
		Out.Index = Index;
		Out.Kind = ERadarKind::Changed;
		FBound OL, OH, NL, NH;
		RangeOf(O, OL, OH);
		RangeOf(N, NL, NH);
		if (!SameBound(OL, NL) || !SameBound(OH, NH))
		{
			Out.bHasRangeOld = Out.bHasRangeNew = true;
			if (!OL.bOpen) { Out.RangeOld[0] = OL.Value; }
			if (!OH.bOpen) { Out.RangeOld[1] = OH.Value; }
			if (!NL.bOpen) { Out.RangeNew[0] = NL.Value; }
			if (!NH.bOpen) { Out.RangeNew[1] = NH.Value; }
		}
		const TArray<FChannelRef> OldChannels = ChannelsOf(O->GetClass());
		const TArray<FChannelRef> NewChannels = ChannelsOf(N->GetClass());
		FRadarCompareContext SectionCtx = Ctx;
		SectionCtx.StructuralProperties = { TEXT("SectionRange"), TEXT("Signature") };
		for (const FChannelRef& C : OldChannels) { SectionCtx.StructuralProperties.Add(C.Property->GetFName()); }
		for (const FChannelRef& C : NewChannels) { SectionCtx.StructuralProperties.Add(C.Property->GetFName()); }
		RadarProperty::DiffObjects(O, N, FString(), SectionCtx, Out.Properties);
		if (O->GetClass() == N->GetClass())
		{
			for (const FChannelRef& C : NewChannels)
			{
				FRadarChannelDiff CD;
				if (DiffChannel(C, O, N, O->GetArchetype(), N->GetArchetype(), Ctx, CD))
				{
					Out.Channels.Add(MoveTemp(CD));
				}
			}
		}
		return Out.bHasRangeOld || Out.Properties.Num() > 0 || Out.Channels.Num() > 0;
	}

	void DiffAnimations(UWidgetBlueprint* OldBP, UWidgetBlueprint* NewBP, const FRadarCompareContext& Ctx, FRadarDiff& Diff)
	{
#if WITH_EDITORONLY_DATA
		TMap<FString, UWidgetAnimation*> OldMap, NewMap;
		for (UWidgetAnimation* A : OldBP->Animations) { if (A) { OldMap.Add(A->GetName(), A); } }
		for (UWidgetAnimation* A : NewBP->Animations) { if (A) { NewMap.Add(A->GetName(), A); } }
		for (const TPair<FString, UWidgetAnimation*>& Pair : NewMap)
		{
			FRadarAnimationDiff D;
			D.Id = Pair.Key;
			UWidgetAnimation* const* O = OldMap.Find(Pair.Key);
			const TArray<FTrackRef> NewTracks = CollectTracks(Pair.Value);
			const TArray<FTrackRef> OldTracks = O ? CollectTracks(*O) : TArray<FTrackRef>();
			TMap<FString, const UMovieSceneTrack*> OldById;
			for (const FTrackRef& T : OldTracks) { OldById.Add(T.Id, T.Track); }
			for (const FTrackRef& T : NewTracks)
			{
				const UMovieSceneTrack* const* OT = OldById.Find(T.Id);
				const TArray<UMovieSceneSection*>& NS = T.Track->GetAllSections();
				FRadarTrackDiff TD;
				TD.Id = T.Id;
				TD.SectionsNew = NS.Num();
				if (!OT)
				{
					TD.Kind = ERadarKind::Added;
					D.Tracks.Add(MoveTemp(TD));
					continue;
				}
				const TArray<UMovieSceneSection*>& OS = (*OT)->GetAllSections();
				TD.SectionsOld = OS.Num();
				TD.Kind = ERadarKind::Changed;
				const int32 Common = FMath::Min(OS.Num(), NS.Num());
				for (int32 i = 0; i < Common; ++i)
				{
					FRadarSectionDiff SD;
					if (OS[i] && NS[i] && DiffSection(OS[i], NS[i], i, Ctx, SD))
					{
						TD.Sections.Add(MoveTemp(SD));
					}
				}
				for (int32 i = Common; i < OS.Num(); ++i) { FRadarSectionDiff SD; SD.Index = i; SD.Kind = ERadarKind::Removed; TD.Sections.Add(MoveTemp(SD)); }
				for (int32 i = Common; i < NS.Num(); ++i) { FRadarSectionDiff SD; SD.Index = i; SD.Kind = ERadarKind::Added; TD.Sections.Add(MoveTemp(SD)); }
				if (TD.Sections.Num() > 0)
				{
					D.Tracks.Add(MoveTemp(TD));
				}
			}
			TSet<FString> NewIds;
			for (const FTrackRef& T : NewTracks) { NewIds.Add(T.Id); }
			for (const FTrackRef& T : OldTracks)
			{
				if (!NewIds.Contains(T.Id))
				{
					FRadarTrackDiff TD;
					TD.Id = T.Id;
					TD.Kind = ERadarKind::Removed;
					TD.SectionsOld = T.Track->GetAllSections().Num();
					D.Tracks.Add(MoveTemp(TD));
				}
			}
			if (!O)
			{
				D.Kind = ERadarKind::Added;
			}
			else if (D.Tracks.Num() > 0)
			{
				D.Kind = ERadarKind::Changed;
			}
			else
			{
				continue;
			}
			Diff.Animations.Add(MoveTemp(D));
		}
		for (const TPair<FString, UWidgetAnimation*>& Pair : OldMap)
		{
			if (!NewMap.Contains(Pair.Key))
			{
				FRadarAnimationDiff D;
				D.Id = Pair.Key;
				D.Kind = ERadarKind::Removed;
				Diff.Animations.Add(MoveTemp(D));
			}
		}
#endif
	}

	// ── 그래프 (§7.5) ──
	static const TSet<FName>& LayoutProperties()
	{
		static const TSet<FName> Names = { TEXT("NodePosX"), TEXT("NodePosY"), TEXT("NodeWidth"), TEXT("NodeHeight"),
			TEXT("NodeComment"), TEXT("bCommentBubbleVisible"), TEXT("bCommentBubblePinned"), TEXT("CommentColor"), TEXT("bCommentBubbleMakeVisible") };
		return Names;
	}

	FString PinKey(const UEdGraphPin* Pin)
	{
		const UEdGraphNode* Owner = Pin ? Pin->GetOwningNodeUnchecked() : nullptr;
		return FString::Printf(TEXT("%s/%s"), Owner ? *Owner->NodeGuid.ToString(EGuidFormats::Digits) : TEXT("?"), Pin ? *Pin->PinId.ToString(EGuidFormats::Digits) : TEXT("?"));
	}

	struct FPinField
	{
		FString Key;
		FString Text;                       // 비교와 화면에 쓰는 문자열
		TSharedPtr<FJsonValue> Value;       // diff.json value (Python 이 파일에서 같은 형으로 만든다)
	};

	/** 구조체 값을 멤버 이름 → JSON 으로 (저장되지 않는 멤버는 뺀다 — Python 이 읽는 멤버 집합과 같게). */
	TSharedPtr<FJsonValue> StructJson(const UScriptStruct* Struct, const void* Ptr, const FRadarCompareContext& Ctx)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!RadarProperty::ShouldSkip(*It, Ctx))
			{
				Obj->SetField(It->GetName(), RadarProperty::ToJson(*It, It->ContainerPtrToValuePtr<void>(Ptr), Ctx));
			}
		}
		return MakeShared<FJsonValueObject>(Obj);
	}

	void PinFields(const UEdGraphPin* Pin, const FRadarCompareContext& Ctx, TArray<FPinField>& Out)
	{
		Out.Add({ TEXT("DefaultValue"), Pin->DefaultValue, MakeShared<FJsonValueString>(Pin->DefaultValue) });
		const FString ObjPath = Pin->DefaultObject ? Ctx.ObjectPath(Pin->DefaultObject) : FString();
		Out.Add({ TEXT("DefaultObject"), ObjPath, ObjPath.IsEmpty() ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>()) : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(ObjPath)) });
		const FString* Source = FTextInspector::GetSourceString(Pin->DefaultTextValue);
		const FString TextSource = Source ? *Source : Pin->DefaultTextValue.ToString();
		Out.Add({ TEXT("DefaultTextValue"), TextSource, MakeShared<FJsonValueString>(TextSource) });
		TArray<FString> Links;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			Links.Add(PinKey(Linked));
		}
		Links.Sort();
		TArray<TSharedPtr<FJsonValue>> LinkValues;
		for (const FString& L : Links) { LinkValues.Add(MakeShared<FJsonValueString>(L)); }
		Out.Add({ TEXT("LinkedTo"), FString::Join(Links, TEXT(", ")), MakeShared<FJsonValueArray>(LinkValues) });
		FString TypeText;
		FEdGraphPinType::StaticStruct()->ExportText(TypeText, &Pin->PinType, nullptr, nullptr, PPF_None, nullptr);
		Out.Add({ TEXT("PinType"), Ctx.NormalizePath(TypeText), StructJson(FEdGraphPinType::StaticStruct(), &Pin->PinType, Ctx) });
	}

	void DiffPins(const UEdGraphNode* OldNode, const UEdGraphNode* NewNode, const FRadarCompareContext& Ctx, TArray<FRadarPinDiff>& Out)
	{
		TMap<FGuid, const UEdGraphPin*> OldPins;
		if (OldNode) { for (const UEdGraphPin* P : OldNode->Pins) { if (P) { OldPins.Add(P->PinId, P); } } }
		TSet<FGuid> Seen;
		auto Rows = [&Ctx](const UEdGraphPin* O, const UEdGraphPin* N, TArray<FRadarPropertyDiff>& Rows)
		{
			TArray<FPinField> OF, NF;
			if (O) { PinFields(O, Ctx, OF); }
			if (N) { PinFields(N, Ctx, NF); }
			const int32 Count = FMath::Max(OF.Num(), NF.Num());
			for (int32 i = 0; i < Count; ++i)
			{
				if (O && N && OF[i].Text == NF[i].Text)
				{
					continue;
				}
				FRadarPropertyDiff& R = Rows.AddDefaulted_GetRef();
				R.Path = O ? OF[i].Key : NF[i].Key;
				R.Category = TEXT("핀");
				R.Type = R.Path == TEXT("LinkedTo") ? TEXT("ArrayProperty:StrProperty") : (R.Path == TEXT("PinType") ? TEXT("StructProperty:EdGraphPinType") : TEXT("StrProperty"));
				R.Kind = O && N ? ERadarKind::Changed : (N ? ERadarKind::Added : ERadarKind::Removed);
				if (O) { R.Old.bSet = true; R.Old.Text = OF[i].Text; R.Old.Value = OF[i].Value; }
				if (N) { R.New.bSet = true; R.New.Text = NF[i].Text; R.New.Value = NF[i].Value; }
			}
		};
		if (NewNode)
		{
			for (const UEdGraphPin* N : NewNode->Pins)
			{
				if (!N) { continue; }
				Seen.Add(N->PinId);
				const UEdGraphPin* const* O = OldPins.Find(N->PinId);
				FRadarPinDiff D;
				D.PinId = N->PinId.ToString(EGuidFormats::Digits);
				D.Name = N->PinName.ToString();
				D.Kind = O ? ERadarKind::Changed : ERadarKind::Added;
				if (O)
				{
					Rows(*O, N, D.Properties);
					if (D.Properties.Num() == 0) { continue; }
				}
				Out.Add(MoveTemp(D));
			}
		}
		for (const TPair<FGuid, const UEdGraphPin*>& Pair : OldPins)
		{
			if (!Seen.Contains(Pair.Key))
			{
				FRadarPinDiff D;
				D.PinId = Pair.Key.ToString(EGuidFormats::Digits);
				D.Name = Pair.Value->PinName.ToString();
				D.Kind = ERadarKind::Removed;
				Out.Add(MoveTemp(D));
			}
		}
	}

	TMap<FString, UEdGraph*> CollectGraphs(UWidgetBlueprint* BP)
	{
		TMap<FString, UEdGraph*> Out;
		for (const TArray<TObjectPtr<UEdGraph>>* List : { &BP->UbergraphPages, &BP->FunctionGraphs, &BP->MacroGraphs, &BP->DelegateSignatureGraphs })
		{
			for (UEdGraph* G : *List)
			{
				if (G) { Out.Add(G->GetName(), G); }
			}
		}
		return Out;
	}

	FRadarNodeDiff MakeNode(const UEdGraphNode* Node, ERadarKind Kind, const FRadarCompareContext& Ctx)
	{
		FRadarNodeDiff D;
		D.Guid = Node->NodeGuid.ToString(EGuidFormats::Digits);
		D.Class = Ctx.ObjectPath(Node->GetClass());
		D.Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		D.Kind = Kind;
		D.Area = ERadarArea::GraphLogic;
		return D;
	}

	void DiffGraphs(UWidgetBlueprint* OldBP, UWidgetBlueprint* NewBP, const FRadarCompareContext& BaseCtx, FRadarDiff& Diff)
	{
		FRadarCompareContext Ctx = BaseCtx;
		Ctx.StructuralProperties = { TEXT("NodeGuid"), TEXT("DeprecatedPins") };
		const TMap<FString, UEdGraph*> OldGraphs = CollectGraphs(OldBP);
		const TMap<FString, UEdGraph*> NewGraphs = CollectGraphs(NewBP);

		auto DiffOne = [&](const FString& Id, UEdGraph* OG, UEdGraph* NG)
		{
			FRadarGraphDiff G;
			G.Id = Id;
			TMap<FGuid, UEdGraphNode*> OldNodes;
			if (OG) { for (UEdGraphNode* N : OG->Nodes) { if (N) { OldNodes.Add(N->NodeGuid, N); } } }
			TSet<FGuid> Seen;
			if (NG)
			{
				for (UEdGraphNode* N : NG->Nodes)
				{
					if (!N) { continue; }
					Seen.Add(N->NodeGuid);
					UEdGraphNode* const* O = OldNodes.Find(N->NodeGuid);
					if (!O)
					{
						FRadarNodeDiff D = MakeNode(N, ERadarKind::Added, Ctx);
						++G.LogicAdded;
						G.Nodes.Add(MoveTemp(D));
						continue;
					}
					FRadarNodeDiff D = MakeNode(N, ERadarKind::Changed, Ctx);
					RadarProperty::DiffObjects(*O, N, FString(), Ctx, D.Properties);
					DiffPins(*O, N, Ctx, D.Pins);
					if (D.Properties.Num() == 0 && D.Pins.Num() == 0)
					{
						continue;
					}
					bool bLayoutOnly = D.Pins.Num() == 0;
					for (const FRadarPropertyDiff& Row : D.Properties)
					{
						FString Root = Row.Path;
						Root.Split(TEXT("."), &Root, nullptr);
						bLayoutOnly &= LayoutProperties().Contains(FName(*Root));
					}
					D.Area = bLayoutOnly ? ERadarArea::GraphLayout : ERadarArea::GraphLogic;
					(bLayoutOnly ? G.LayoutMoved : G.LogicChanged)++;
					G.Nodes.Add(MoveTemp(D));
				}
			}
			for (const TPair<FGuid, UEdGraphNode*>& Pair : OldNodes)
			{
				if (!Seen.Contains(Pair.Key))
				{
					G.Nodes.Add(MakeNode(Pair.Value, ERadarKind::Removed, Ctx));
					++G.LogicRemoved;
				}
			}
			G.Kind = !OG ? ERadarKind::Added : (!NG ? ERadarKind::Removed : ERadarKind::Changed);
			if (G.Kind == ERadarKind::Changed && G.Nodes.Num() == 0)
			{
				return;
			}
			Diff.Graphs.Add(MoveTemp(G));
		};
		for (const TPair<FString, UEdGraph*>& Pair : NewGraphs)
		{
			UEdGraph* const* O = OldGraphs.Find(Pair.Key);
			DiffOne(Pair.Key, O ? *O : nullptr, Pair.Value);
		}
		for (const TPair<FString, UEdGraph*>& Pair : OldGraphs)
		{
			if (!NewGraphs.Contains(Pair.Key))
			{
				DiffOne(Pair.Key, Pair.Value, nullptr);
			}
		}

		// 이름 변경 추정의 그래프 확인 (§7.3-3): 변수 노드의 MemberName 이 옛 이름 → 새 이름으로 바뀐 것이 실제로 있으면
		for (FRadarWidgetDiff& W : Diff.Widgets)
		{
			if (!W.Rename.IsSet())
			{
				continue;
			}
			FString OldName, NewName;
			W.Rename->From.Split(TEXT("."), nullptr, &OldName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			W.Id.Split(TEXT("."), nullptr, &NewName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			for (const FRadarGraphDiff& G : Diff.Graphs)
			{
				for (const FRadarNodeDiff& N : G.Nodes)
				{
					for (const FRadarPropertyDiff& Row : N.Properties)
					{
						if (Row.Path == TEXT("VariableReference.MemberName") && Row.Old.Text == OldName && Row.New.Text == NewName)
						{
							W.Rename->bGraphConfirmed = true;
						}
					}
				}
			}
		}
	}
}

FRadarDiff FRadarNativeDiffProducer::Produce(const FRadarDiffSession& Session, bool bLiveObjects)
{
	FRadarDiff Diff;
	Diff.Engine = RadarCompat::EngineVersionString();
	Diff.PluginBranch = RadarCompat::PluginBranch();
	if (bLiveObjects)
	{
		Diff.Capabilities.Add(TEXT("live-objects"));
	}
	Diff.Capabilities.Append({ TEXT("default-state"), TEXT("graph"), TEXT("anim-keys") });

	UWidgetBlueprint* OldBP = Session.OldBlueprint;
	UWidgetBlueprint* NewBP = Session.NewBlueprint;
	Diff.PackageName = Session.AssetPackageName;
	Diff.AssetName = FPackageName::GetLongPackageAssetName(Session.AssetPackageName);
	FString Rel = Session.AssetFile;
	FPaths::MakePathRelativeTo(Rel, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Diff.RelPath = Rel;

	const FRadarCompareContext Ctx = MakeContext(Session);
	if (NewBP && NewBP->ParentClass)
	{
		Diff.ParentClass = Ctx.ObjectPath(NewBP->ParentClass);
	}
	FillSide(Diff.Old, Session.OldRevision, OldBP);
	FillSide(Diff.New, Session.NewRevision, NewBP);
	if (!OldBP || !NewBP)
	{
		return Diff;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FTree Tree = CollectWidgets(Side == 0 ? OldBP : NewBP, Ctx);
		FRadarTreeSnapshot& Snap = Side == 0 ? Diff.OldTree : Diff.NewTree;
		Snap.bValid = true;
		Snap.Root = Tree.Root;
		TMap<FString, TArray<TPair<int32, FString>>> Kids;
		for (const FString& Id : Tree.Order)
		{
			const FWidgetInfo& I = Tree.Map[Id];
			Snap.Labels.Add(Id, I.Label);
			Snap.Classes.Add(Id, I.Class);
			if (!I.Parent.IsEmpty())
			{
				Kids.FindOrAdd(I.Parent).Emplace(I.Index, Id);
			}
		}
		for (TPair<FString, TArray<TPair<int32, FString>>>& K : Kids)
		{
			K.Value.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
			TArray<FString>& Out = Snap.Children.Add(K.Key);
			for (const TPair<int32, FString>& C : K.Value) { Out.Add(C.Value); }
		}
	}
	DiffWidgets(OldBP, NewBP, Ctx, Diff);
	DiffBindings(OldBP, NewBP, Diff);
	DiffCdo(OldBP, NewBP, Ctx, Diff);
	DiffVariables(OldBP, NewBP, Ctx, Diff);
	DiffAnimations(OldBP, NewBP, Ctx, Diff);
	DiffGraphs(OldBP, NewBP, Ctx, Diff);
	return Diff;
}

FRadarState FRadarNativeDiffProducer::DumpState(const FRadarDiffSession& Session, bool bOld)
{
	FRadarState State;
	State.PackageName = Session.AssetPackageName;
	UWidgetBlueprint* BP = bOld ? Session.OldBlueprint : Session.NewBlueprint;
	FillSide(State.Side, bOld ? Session.OldRevision : Session.NewRevision, BP);
	if (!BP)
	{
		return State;
	}
	FRadarCompareContext Ctx = MakeContext(Session);
	const FTree Tree = CollectWidgets(BP, Ctx);
	State.RootWidget = Tree.Root;
	TMap<FString, TArray<TPair<int32, FString>>> Children;
	for (const FString& Id : Tree.Order)
	{
		const FWidgetInfo& I = Tree.Map[Id];
		FRadarStateObject& Obj = State.Objects.AddDefaulted_GetRef();
		Obj.Id = Id;
		Obj.Class = I.Class;
		Ctx.StructuralProperties = { TEXT("Slot"), TEXT("Slots") };
		RadarProperty::DumpStruct(I.Widget->GetClass(), I.Widget, I.Widget->GetArchetype(), FString(), Ctx, Obj.Properties);
		if (UPanelSlot* Slot = I.Widget->Slot)
		{
			FRadarStateObject& SlotObj = State.Objects.AddDefaulted_GetRef();
			SlotObj.Id = Id + TEXT("/Slot");
			SlotObj.Class = Ctx.ObjectPath(Slot->GetClass());
			Ctx.StructuralProperties = { TEXT("Parent"), TEXT("Content") };
			RadarProperty::DumpStruct(Slot->GetClass(), Slot, Slot->GetArchetype(), FString(), Ctx, SlotObj.Properties);
		}
		if (!I.Parent.IsEmpty())
		{
			Children.FindOrAdd(I.Parent).Emplace(I.Index, Id);
		}
	}
	for (TPair<FString, TArray<TPair<int32, FString>>>& Pair : Children)
	{
		Pair.Value.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
		TArray<FString> Ids;
		for (const TPair<int32, FString>& C : Pair.Value) { Ids.Add(C.Value); }
		State.Children.Emplace(Pair.Key, MoveTemp(Ids));
	}
	return State;
}

bool FRadarNativeDiffProducer::WriteAll(const FRadarDiffSession& Session, const FString& OutDir, bool bWithState, bool bLiveObjects, FString& OutError)
{
	IFileManager::Get().MakeDirectory(*OutDir, true);
	const FRadarDiff Diff = Produce(Session, bLiveObjects);
	if (!RadarDiffJson::SaveDiff(Diff, FPaths::Combine(OutDir, TEXT("diff.json")), OutError))
	{
		return false;
	}
	if (bWithState)
	{
		if (!RadarDiffJson::SaveState(DumpState(Session, true), Diff, FPaths::Combine(OutDir, TEXT("old.state.json")), OutError)
			|| !RadarDiffJson::SaveState(DumpState(Session, false), Diff, FPaths::Combine(OutDir, TEXT("new.state.json")), OutError))
		{
			return false;
		}
	}
	UE_LOG(LogRadarUMGDiff, Display, TEXT("diff.json 기록: %s (위젯 %d, 바인딩 %d, CDO %d, 애니메이션 %d, 그래프 %d)"),
		*OutDir, Diff.Widgets.Num(), Diff.Bindings.Num(), Diff.Cdo.Num(), Diff.Animations.Num(), Diff.Graphs.Num());
	return true;
}

int32 FRadarNativeDiffProducer::DumpHistory(TArray<FString> Packages, const FString& OutDir, bool bLiveObjects, TArray<FString>& OutFailures)
{
	if (Packages.Num() == 0)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, true);
		for (const FAssetData& A : Assets)
		{
			if (A.PackageName.ToString().StartsWith(TEXT("/Game/")))
			{
				Packages.Add(A.PackageName.ToString());
			}
		}
		Packages.Sort();
	}
	int32 Written = 0;
	TSharedRef<IRadarRevisionSource> Git = RadarRevisionSources::MakeGit();
	TSharedPtr<IRadarRevisionSource> GitPtr = Git;
	for (const FString& PackageName : Packages)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, *(PackageName + TEXT(".") + AssetName));
		FString AssetFile;
		if (!BP || !FPackageName::TryConvertLongPackageNameToFilename(PackageName, AssetFile, FPackageName::GetAssetPackageExtension()))
		{
			OutFailures.Add(PackageName + TEXT(": 로드 실패"));
			continue;
		}
		AssetFile = FPaths::ConvertRelativePathToFull(AssetFile);
		if (!Git->IsAvailableFor(AssetFile))
		{
			continue;
		}
		FString Error;
		TArray<FRadarRevision> History = Git->GetHistory(AssetFile, Error);   // 최신 → 과거
		TArray<TPair<FRadarRevision, FRadarRevision>> Pairs;
		for (int32 i = History.Num() - 1; i > 0; --i)
		{
			Pairs.Emplace(History[i], History[i - 1]);
		}
		if (History.Num() > 0 && Git->IsWorkingCopyDirty(AssetFile))
		{
			FRadarRevision Working;
			Working.Kind = ERadarRevisionKind::Working;
			Working.bDirty = true;
			Pairs.Emplace(History[0], Working);
		}
		for (const TPair<FRadarRevision, FRadarRevision>& Pair : Pairs)
		{
			const bool bWorking = Pair.Value.Kind == ERadarRevisionKind::Working;
			const FString NewId = bWorking ? FString(TEXT("working")) : Pair.Value.ShortId;
			const FString Dir = FPaths::Combine(OutDir, AssetName, Pair.Key.ShortId + TEXT("_") + NewId);
			TSharedPtr<FRadarDiffSession> Session = FRadarRevisionLoader::Load(GitPtr, Pair.Key, bWorking ? nullptr : GitPtr, Pair.Value, BP, AssetFile, Error);
			if (!Session.IsValid() || !WriteAll(*Session, Dir, false, bLiveObjects, Error))
			{
				OutFailures.Add(FString::Printf(TEXT("%s %s→%s: %s"), *PackageName, *Pair.Key.ShortId, *NewId, *Error));
			}
			else
			{
				++Written;
			}
			if (Session.IsValid())
			{
				Session->Release();
			}
		}
	}
	UE_LOG(LogRadarUMGDiff, Display, TEXT("정확도 모드: diff.json %d개, 실패 %d개 → %s"), Written, OutFailures.Num(), *OutDir);
	for (const FString& F : OutFailures)
	{
		UE_LOG(LogRadarUMGDiff, Warning, TEXT("  실패: %s"), *F);
	}
	return Written;
}
