// Copyright Radar. All Rights Reserved.

// diff.json → FRadarDiff (data-only 뷰어, §9.8). 어느 생산자(네이티브·Python)의 파일이든 같은 스키마(§5)로 읽는다.

#include "RadarDiffJson.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	ERadarKind ParseKind(const FString& S)
	{
		if (S == TEXT("added")) return ERadarKind::Added;
		if (S == TEXT("removed")) return ERadarKind::Removed;
		if (S == TEXT("changed")) return ERadarKind::Changed;
		if (S == TEXT("moved")) return ERadarKind::Moved;
		if (S == TEXT("renamed-probable")) return ERadarKind::RenamedProbable;
		if (S == TEXT("text-key-only")) return ERadarKind::TextKeyOnly;
		return ERadarKind::Unchanged;
	}

	ERadarArea ParseArea(const FString& S)
	{
		if (S == TEXT("slot")) return ERadarArea::Slot;
		if (S == TEXT("binding")) return ERadarArea::Binding;
		if (S == TEXT("cdo")) return ERadarArea::Cdo;
		if (S == TEXT("anim")) return ERadarArea::Anim;
		if (S == TEXT("graph-logic")) return ERadarArea::GraphLogic;
		if (S == TEXT("graph-layout")) return ERadarArea::GraphLayout;
		return ERadarArea::Designer;
	}

	ERadarSeverity ParseSeverity(const FString& S)
	{
		if (S == TEXT("low")) return ERadarSeverity::Low;
		if (S == TEXT("info")) return ERadarSeverity::Info;
		return ERadarSeverity::Normal;
	}

	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		FString V;
		if (O.IsValid()) { O->TryGetStringField(Key, V); }
		return V;
	}

	FRadarOptionalString OptStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		FString V;
		if (O.IsValid() && O->HasTypedField<EJson::String>(Key) && O->TryGetStringField(Key, V))
		{
			return FRadarOptionalString(V);
		}
		return FRadarOptionalString();
	}

	int32 OptIndex(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		double V = 0;
		return (O.IsValid() && O->HasTypedField<EJson::Number>(Key) && O->TryGetNumberField(Key, V)) ? static_cast<int32>(V) : INDEX_NONE;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return (O.IsValid() && O->TryGetObjectField(Key, Out)) ? *Out : nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return (O.IsValid() && O->TryGetArrayField(Key, Out)) ? *Out : Empty;
	}

	FRadarValue ParseValue(const TSharedPtr<FJsonObject>& O)
	{
		FRadarValue V;
		if (!O.IsValid())
		{
			return V;
		}
		V.bSet = true;
		const FString State = Str(O, TEXT("state"));
		V.State = State == TEXT("absent") ? ERadarValueState::Absent : (State == TEXT("default") ? ERadarValueState::Default : ERadarValueState::Present);
		V.Text = Str(O, TEXT("text"));
		V.Value = O->TryGetField(TEXT("value"));
		V.Asset = Str(O, TEXT("asset"));
		return V;
	}

	void ParseProperties(const TArray<TSharedPtr<FJsonValue>>& In, TArray<FRadarPropertyDiff>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Item : In)
		{
			const TSharedPtr<FJsonObject> O = Item->AsObject();
			if (!O.IsValid()) { continue; }
			FRadarPropertyDiff& P = Out.AddDefaulted_GetRef();
			P.Path = Str(O, TEXT("path"));
			P.Category = Str(O, TEXT("category"));
			P.Type = Str(O, TEXT("type"));
			P.Kind = ParseKind(Str(O, TEXT("kind")));
			O->TryGetBoolField(TEXT("maybeDefault"), P.bMaybeDefault);
			O->TryGetBoolField(TEXT("auto"), P.bAuto);
			P.Old = ParseValue(Obj(O, TEXT("old")));
			P.New = ParseValue(Obj(O, TEXT("new")));
			for (const TSharedPtr<FJsonValue>& C : Arr(O, TEXT("changedComponents"))) { P.ChangedComponents.Add(C->AsString()); }
		}
	}

	void ParseRange(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, TOptional<int32> (&Out)[2])
	{
		const TArray<TSharedPtr<FJsonValue>>& A = Arr(O, Key);
		for (int32 i = 0; i < 2 && i < A.Num(); ++i)
		{
			if (A[i].IsValid() && A[i]->Type == EJson::Number) { Out[i] = static_cast<int32>(A[i]->AsNumber()); }
		}
	}

	void ParseSection(const TSharedPtr<FJsonObject>& O, FRadarSectionDiff& S)
	{
		if (!O.IsValid()) { return; }
		S.Index = FMath::Max(0, OptIndex(O, TEXT("index")));
		S.Kind = ParseKind(Str(O, TEXT("kind")));
		if (const TSharedPtr<FJsonObject> R = Obj(O, TEXT("range")))
		{
			S.bHasRangeOld = S.bHasRangeNew = true;
			ParseRange(R, TEXT("old"), S.RangeOld);
			ParseRange(R, TEXT("new"), S.RangeNew);
		}
		ParseProperties(Arr(O, TEXT("properties")), S.Properties);
		for (const TSharedPtr<FJsonValue>& CV : Arr(O, TEXT("channels")))
		{
			const TSharedPtr<FJsonObject> CO = CV->AsObject();
			if (!CO.IsValid()) { continue; }
			FRadarChannelDiff& C = S.Channels.AddDefaulted_GetRef();
			C.Name = Str(CO, TEXT("name"));
			C.Type = Str(CO, TEXT("type"));
			C.Kind = ParseKind(Str(CO, TEXT("kind")));
			ParseProperties(Arr(CO, TEXT("properties")), C.Properties);
			for (const TSharedPtr<FJsonValue>& KV : Arr(CO, TEXT("keys")))
			{
				const TSharedPtr<FJsonObject> KO = KV->AsObject();
				if (!KO.IsValid()) { continue; }
				FRadarKeyDiff& K = C.Keys.AddDefaulted_GetRef();
				K.Frame = OptIndex(KO, TEXT("frame"));
				K.Kind = ParseKind(Str(KO, TEXT("kind")));
				K.Old = ParseValue(Obj(KO, TEXT("old")));
				K.New = ParseValue(Obj(KO, TEXT("new")));
				ParseProperties(Arr(KO, TEXT("properties")), K.Properties);
			}
		}
	}

	void ParseSide(const TSharedPtr<FJsonObject>& O, FRadarSideInfo& S)
	{
		S.Kind = Str(O, TEXT("kind"));
		S.Sha = Str(O, TEXT("sha"));
		S.Date = Str(O, TEXT("date"));
		S.Author = Str(O, TEXT("author"));
		S.Subject = Str(O, TEXT("subject"));
		S.Provider = Str(O, TEXT("provider"));
		S.Revision = Str(O, TEXT("revision"));
		S.User = Str(O, TEXT("user"));
		S.Description = Str(O, TEXT("description"));
		S.Path = Str(O, TEXT("path"));
		S.Changelist = OptIndex(O, TEXT("changelist"));
		if (O.IsValid()) { O->TryGetBoolField(TEXT("dirty"), S.bDirty); }
	}

	/** state.json 의 widgetTree (+ 네이티브는 objects[] 의 클래스) → 트리 스냅샷. */
	void LoadTree(const FString& Path, FRadarTreeSnapshot& Snap)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject> Tree = Obj(Root, TEXT("widgetTree"));
		if (!Tree.IsValid())
		{
			return;
		}
		Snap.bValid = true;
		Snap.Root = Str(Tree, TEXT("root"));
		if (const TSharedPtr<FJsonObject> Kids = Obj(Tree, TEXT("children")))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Kids->Values)
			{
				TArray<FString>& Out = Snap.Children.Add(Pair.Key);
				for (const TSharedPtr<FJsonValue>& C : Pair.Value->AsArray()) { Out.Add(C->AsString()); }
			}
		}
		for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("objects")))
		{
			const TSharedPtr<FJsonObject> O = Item->AsObject();
			if (O.IsValid() && !Str(O, TEXT("id")).Contains(TEXT("/")))
			{
				Snap.Classes.Add(Str(O, TEXT("id")), Str(O, TEXT("class")));
			}
		}
	}
}

bool RadarDiffJson::LoadDiff(const FString& Path, FRadarDiff& D, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("파일을 읽지 못했습니다: %s"), *Path);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("JSON 이 아닙니다: %s"), *Path);
		return false;
	}
	double Schema = 0;
	if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || static_cast<int32>(Schema) != 1)
	{
		OutError = TEXT("diff.json schemaVersion 1 이 아닙니다.");
		return false;
	}
	D.SourcePath = Path;
	if (const TSharedPtr<FJsonObject> P = Obj(Root, TEXT("producer")))
	{
		D.ProducerName = Str(P, TEXT("name"));
		D.ProducerKind = Str(P, TEXT("kind"));
		D.Engine = Str(P, TEXT("engine"));
		D.PluginBranch = Str(P, TEXT("pluginBranch"));
		for (const TSharedPtr<FJsonValue>& C : Arr(P, TEXT("capabilities"))) { D.Capabilities.Add(C->AsString()); }
		if (const TSharedPtr<FJsonObject> FV = Obj(P, TEXT("fileVersionUE5")))
		{
			D.Old.FileVersionUE5 = FMath::Max(0, OptIndex(FV, TEXT("old")));
			D.New.FileVersionUE5 = FMath::Max(0, OptIndex(FV, TEXT("new")));
		}
	}
	if (const TSharedPtr<FJsonObject> A = Obj(Root, TEXT("asset")))
	{
		D.PackageName = Str(A, TEXT("packageName"));
		D.RelPath = Str(A, TEXT("relPath"));
		D.AssetName = Str(A, TEXT("assetName"));
		D.ParentClass = Str(A, TEXT("parentClass"));
	}
	ParseSide(Obj(Root, TEXT("old")), D.Old);
	ParseSide(Obj(Root, TEXT("new")), D.New);
	if (const TSharedPtr<FJsonObject> DS = Obj(Root, TEXT("designSize")))
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const TSharedPtr<FJsonObject> S = Obj(DS, Side == 0 ? TEXT("old") : TEXT("new"));
			FRadarSideInfo& Info = Side == 0 ? D.Old : D.New;
			Info.DesignSizeMode = Str(S, TEXT("mode"));
			const TArray<TSharedPtr<FJsonValue>>& Size = Arr(S, TEXT("size"));
			if (Size.Num() == 2) { Info.DesignSize = FVector2D(Size[0]->AsNumber(), Size[1]->AsNumber()); }
		}
	}

	for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("widgets")))
	{
		const TSharedPtr<FJsonObject> O = Item->AsObject();
		if (!O.IsValid()) { continue; }
		FRadarWidgetDiff& W = D.Widgets.AddDefaulted_GetRef();
		W.Id = Str(O, TEXT("id"));
		W.Kind = ParseKind(Str(O, TEXT("kind")));
		O->TryGetBoolField(TEXT("moved"), W.bMovedToo);
		W.Area = ParseArea(Str(O, TEXT("area")));
		W.Severity = ParseSeverity(Str(O, TEXT("severity")));
		W.Class = Str(O, TEXT("class"));
		if (const TSharedPtr<FJsonObject> R = Obj(O, TEXT("rename")))
		{
			FRadarRename Rename;
			Rename.From = Str(R, TEXT("from"));
			R->TryGetNumberField(TEXT("confidence"), Rename.Confidence);
			Rename.Evidence = Str(R, TEXT("evidence"));
			R->TryGetBoolField(TEXT("graphConfirmed"), Rename.bGraphConfirmed);
			W.Rename = Rename;
		}
		W.LabelOld = OptStr(Obj(O, TEXT("displayLabel")), TEXT("old"));
		W.LabelNew = OptStr(Obj(O, TEXT("displayLabel")), TEXT("new"));
		W.ParentOld = OptStr(Obj(O, TEXT("parent")), TEXT("old"));
		W.ParentNew = OptStr(Obj(O, TEXT("parent")), TEXT("new"));
		W.IndexOld = OptIndex(Obj(O, TEXT("index")), TEXT("old"));
		W.IndexNew = OptIndex(Obj(O, TEXT("index")), TEXT("new"));
		W.bHasOld = W.Kind != ERadarKind::Added;
		W.bHasNew = W.Kind != ERadarKind::Removed;
		for (const TSharedPtr<FJsonValue>& B : Arr(O, TEXT("badges"))) { W.Badges.Add(B->AsString()); }
		W.StateRef = Str(O, TEXT("stateRef"));
		ParseProperties(Arr(O, TEXT("properties")), W.Properties);
		if (const TSharedPtr<FJsonObject> S = Obj(O, TEXT("slot")))
		{
			W.Slot.ClassOld = OptStr(Obj(S, TEXT("class")), TEXT("old"));
			W.Slot.ClassNew = OptStr(Obj(S, TEXT("class")), TEXT("new"));
			ParseProperties(Arr(S, TEXT("properties")), W.Slot.Properties);
		}
	}
	for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("bindings")))
	{
		const TSharedPtr<FJsonObject> O = Item->AsObject();
		FRadarBindingDiff& B = D.Bindings.AddDefaulted_GetRef();
		B.Id = Str(O, TEXT("id"));
		B.Kind = ParseKind(Str(O, TEXT("kind")));
		const TSharedPtr<FJsonObject> OldO = Obj(O, TEXT("old"));
		const TSharedPtr<FJsonObject> NewO = Obj(O, TEXT("new"));
		B.OldFunction = OptStr(OldO, TEXT("function")); B.OldKind = OptStr(OldO, TEXT("kind")); B.OldSourceProperty = OptStr(OldO, TEXT("sourceProperty"));
		B.NewFunction = OptStr(NewO, TEXT("function")); B.NewKind = OptStr(NewO, TEXT("kind")); B.NewSourceProperty = OptStr(NewO, TEXT("sourceProperty"));
	}
	ParseProperties(Arr(Root, TEXT("cdo")), D.Cdo);
	for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("cdo")))
	{
		const TSharedPtr<FJsonObject> O = Item->AsObject();
		FString Guid;
		if (O.IsValid() && O->TryGetStringField(TEXT("varGuid"), Guid))
		{
			FString RootName = Str(O, TEXT("path"));
			RootName.Split(TEXT("."), &RootName, nullptr);
			RootName.Split(TEXT("["), &RootName, nullptr);
			D.CdoVarGuids.Add(RootName, Guid);
		}
	}
	for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("variables")))
	{
		const TSharedPtr<FJsonObject> O = Item->AsObject();
		FRadarVariableDiff& V = D.Variables.AddDefaulted_GetRef();
		V.Name = Str(O, TEXT("name"));
		V.VarGuid = Str(O, TEXT("varGuid"));
		V.Kind = ParseKind(Str(O, TEXT("kind")));
		V.RenamedFrom = Str(O, TEXT("renamedFrom"));
		ParseProperties(Arr(O, TEXT("properties")), V.Properties);
	}
	for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("animations")))
	{
		const TSharedPtr<FJsonObject> O = Item->AsObject();
		FRadarAnimationDiff& A = D.Animations.AddDefaulted_GetRef();
		A.Id = Str(O, TEXT("id"));
		A.Kind = ParseKind(Str(O, TEXT("kind")));
		for (const TSharedPtr<FJsonValue>& T : Arr(O, TEXT("tracks")))
		{
			const TSharedPtr<FJsonObject> TO = T->AsObject();
			FRadarTrackDiff& Track = A.Tracks.AddDefaulted_GetRef();
			Track.Id = Str(TO, TEXT("id"));
			Track.Kind = ParseKind(Str(TO, TEXT("kind")));
			Track.SectionsOld = OptIndex(Obj(TO, TEXT("sectionCount")), TEXT("old"));
			Track.SectionsNew = OptIndex(Obj(TO, TEXT("sectionCount")), TEXT("new"));
			for (const TSharedPtr<FJsonValue>& S : Arr(TO, TEXT("sections")))
			{
				ParseSection(S->AsObject(), Track.Sections.AddDefaulted_GetRef());
			}
		}
	}
	for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("graphs")))
	{
		const TSharedPtr<FJsonObject> O = Item->AsObject();
		FRadarGraphDiff& G = D.Graphs.AddDefaulted_GetRef();
		G.Id = Str(O, TEXT("id"));
		G.Kind = ParseKind(Str(O, TEXT("kind")));
		G.LogicAdded = FMath::Max(0, OptIndex(Obj(O, TEXT("logic")), TEXT("added")));
		G.LogicRemoved = FMath::Max(0, OptIndex(Obj(O, TEXT("logic")), TEXT("removed")));
		G.LogicChanged = FMath::Max(0, OptIndex(Obj(O, TEXT("logic")), TEXT("changed")));
		G.LayoutMoved = FMath::Max(0, OptIndex(Obj(O, TEXT("layout")), TEXT("moved")));
		for (const TSharedPtr<FJsonValue>& N : Arr(O, TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> NO = N->AsObject();
			FRadarNodeDiff& Node = G.Nodes.AddDefaulted_GetRef();
			Node.Guid = Str(NO, TEXT("guid"));
			Node.Class = Str(NO, TEXT("class"));
			Node.Title = Str(NO, TEXT("title"));
			Node.Kind = ParseKind(Str(NO, TEXT("kind")));
			Node.Area = ParseArea(Str(NO, TEXT("area")));
			ParseProperties(Arr(NO, TEXT("properties")), Node.Properties);
			for (const TSharedPtr<FJsonValue>& P : Arr(NO, TEXT("pins")))
			{
				const TSharedPtr<FJsonObject> PO = P->AsObject();
				FRadarPinDiff& Pin = Node.Pins.AddDefaulted_GetRef();
				Pin.PinId = Str(PO, TEXT("pinId"));
				Pin.Name = Str(PO, TEXT("name"));
				Pin.Kind = ParseKind(Str(PO, TEXT("kind")));
				ParseProperties(Arr(PO, TEXT("properties")), Pin.Properties);
			}
		}
	}
	D.TextKeyCount = Arr(Root, TEXT("textKeys")).Num();
	for (const TSharedPtr<FJsonValue>& Item : Arr(Root, TEXT("notes")))
	{
		const TSharedPtr<FJsonObject> O = Item->AsObject();
		D.Notes.Add({ Str(O, TEXT("code")), Str(O, TEXT("widget")), ParseSeverity(Str(O, TEXT("severity"))) });
	}

	const FString Dir = FPaths::GetPath(Path);
	LoadTree(FPaths::Combine(Dir, TEXT("old.state.json")), D.OldTree);
	LoadTree(FPaths::Combine(Dir, TEXT("new.state.json")), D.NewTree);
	return true;
}
