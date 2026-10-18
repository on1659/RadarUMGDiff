// Copyright Radar. All Rights Reserved.

#include "RadarDiffJson.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

const TCHAR* LexToString(ERadarKind Kind)
{
	switch (Kind)
	{
	case ERadarKind::Unchanged:       return TEXT("unchanged");
	case ERadarKind::Added:           return TEXT("added");
	case ERadarKind::Removed:         return TEXT("removed");
	case ERadarKind::Changed:         return TEXT("changed");
	case ERadarKind::Moved:           return TEXT("moved");
	case ERadarKind::RenamedProbable: return TEXT("renamed-probable");
	case ERadarKind::TextKeyOnly:     return TEXT("text-key-only");
	}
	return TEXT("changed");
}

const TCHAR* LexToString(ERadarArea Area)
{
	switch (Area)
	{
	case ERadarArea::Designer:    return TEXT("designer");
	case ERadarArea::Slot:        return TEXT("slot");
	case ERadarArea::Binding:     return TEXT("binding");
	case ERadarArea::Cdo:         return TEXT("cdo");
	case ERadarArea::Anim:        return TEXT("anim");
	case ERadarArea::GraphLogic:  return TEXT("graph-logic");
	case ERadarArea::GraphLayout: return TEXT("graph-layout");
	}
	return TEXT("designer");
}

const TCHAR* LexToString(ERadarSeverity Severity)
{
	switch (Severity)
	{
	case ERadarSeverity::Normal: return TEXT("normal");
	case ERadarSeverity::Low:    return TEXT("low");
	case ERadarSeverity::Info:   return TEXT("info");
	}
	return TEXT("normal");
}

const TCHAR* LexToString(ERadarValueState State)
{
	switch (State)
	{
	case ERadarValueState::Present: return TEXT("present");
	case ERadarValueState::Absent:  return TEXT("absent");
	case ERadarValueState::Default: return TEXT("default");
	}
	return TEXT("present");
}

namespace
{
	/** 들여쓰기 2 의 손 JSON 쓰개. 객체·배열 안 원소는 줄마다 하나. */
	class FWriter
	{
	public:
		FString Out;

		void BeginObject() { Value(); Out += TEXT("{"); Stack.Push({ true, 0 }); }
		void EndObject() { End(TEXT("}")); }
		void BeginArray() { Value(); Out += TEXT("["); Stack.Push({ false, 0 }); }
		void EndArray() { End(TEXT("]")); }

		void Key(const FString& K)
		{
			Separator();
			Quote(K);
			Out += TEXT(": ");
			bPendingKey = true;
		}

		void String(const FString& S) { Value(); Quote(S); }
		void Null() { Value(); Out += TEXT("null"); }
		void Bool(bool b) { Value(); Out += b ? TEXT("true") : TEXT("false"); }
		void Int(int64 V) { Value(); Out += LexToString(V); }
		void Number(double V)
		{
			Value();
			if (FMath::IsFinite(V) && FMath::Abs(V) < 1e15 && V == FMath::RoundToDouble(V))
			{
				Out += LexToString(static_cast<int64>(V));
			}
			else if (!FMath::IsFinite(V))
			{
				Out += TEXT("null");
			}
			else
			{
				Out += FString::Printf(TEXT("%.17g"), V);
			}
		}

		void Json(const TSharedPtr<FJsonValue>& V)
		{
			if (!V.IsValid())
			{
				Null();
				return;
			}
			switch (V->Type)
			{
			case EJson::None:
			case EJson::Null:    Null(); break;
			case EJson::Boolean: Bool(V->AsBool()); break;
			case EJson::Number:  Number(V->AsNumber()); break;
			case EJson::String:  String(V->AsString()); break;
			case EJson::Array:
				BeginArray();
				for (const TSharedPtr<FJsonValue>& Item : V->AsArray()) { Json(Item); }
				EndArray();
				break;
			case EJson::Object:
				BeginObject();
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : V->AsObject()->Values)
				{
					Key(Pair.Key);
					Json(Pair.Value);
				}
				EndObject();
				break;
			}
		}

		// 편의
		void KV(const TCHAR* K, const FString& V) { Key(K); String(V); }
		void KVInt(const TCHAR* K, int64 V) { Key(K); Int(V); }
		void KVOpt(const TCHAR* K, const FRadarOptionalString& V) { Key(K); if (V.bSet) { String(V.Value); } else { Null(); } }
		void KVIndex(const TCHAR* K, int32 V) { Key(K); if (V == INDEX_NONE) { Null(); } else { Int(V); } }

	private:
		struct FLevel { bool bObject; int32 Count; };
		TArray<FLevel> Stack;
		bool bPendingKey = false;

		void Indent() { Out += TEXT("\n"); for (int32 i = 0; i < Stack.Num(); ++i) { Out += TEXT("  "); } }
		void Separator()
		{
			if (Stack.Num() > 0)
			{
				if (Stack.Top().Count++ > 0) { Out += TEXT(","); }
				Indent();
			}
		}
		void Value()
		{
			if (bPendingKey) { bPendingKey = false; return; }
			Separator();
		}
		void End(const TCHAR* Close)
		{
			const bool bHadItems = Stack.Top().Count > 0;
			Stack.Pop();
			if (bHadItems) { Indent(); }
			Out += Close;
		}
		void Quote(const FString& S)
		{
			Out += TEXT("\"");
			for (const TCHAR C : S)
			{
				switch (C)
				{
				case TEXT('"'):  Out += TEXT("\\\""); break;
				case TEXT('\\'): Out += TEXT("\\\\"); break;
				case TEXT('\n'): Out += TEXT("\\n"); break;
				case TEXT('\r'): Out += TEXT("\\r"); break;
				case TEXT('\t'): Out += TEXT("\\t"); break;
				case TEXT('\b'): Out += TEXT("\\b"); break;
				case TEXT('\f'): Out += TEXT("\\f"); break;
				default:
					if (C < 0x20) { Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C)); }
					else { Out.AppendChar(C); }
				}
			}
			Out += TEXT("\"");
		}
	};

	void WriteValue(FWriter& W, const FRadarValue& V, bool bWithState = true)
	{
		if (!V.bSet)
		{
			W.Null();
			return;
		}
		W.BeginObject();
		if (bWithState) { W.KV(TEXT("state"), LexToString(V.State)); }
		W.KV(TEXT("text"), V.Text);
		W.Key(TEXT("value"));
		W.Json(V.Value);
		if (!V.Asset.IsEmpty()) { W.KV(TEXT("asset"), V.Asset); }
		W.EndObject();
	}

	void WriteProperty(FWriter& W, const FRadarPropertyDiff& P, bool bStateDump = false, const FString* VarGuid = nullptr)
	{
		W.BeginObject();
		W.KV(TEXT("path"), P.Path);
		W.KV(TEXT("category"), P.Category);
		W.KV(TEXT("type"), P.Type);
		if (!bStateDump)
		{
			W.KV(TEXT("kind"), LexToString(P.Kind));
			W.Key(TEXT("maybeDefault")); W.Bool(P.bMaybeDefault);
		}
		if (P.bAuto) { W.Key(TEXT("auto")); W.Bool(true); }
		if (bStateDump)
		{
			W.Key(TEXT("state")); W.String(LexToString(P.Old.State));
			W.KV(TEXT("text"), P.Old.Text);
			W.Key(TEXT("value")); W.Json(P.Old.Value);
		}
		else
		{
			W.Key(TEXT("old")); WriteValue(W, P.Old);
			W.Key(TEXT("new")); WriteValue(W, P.New);
			if (P.ChangedComponents.Num() > 0)
			{
				W.Key(TEXT("changedComponents"));
				W.BeginArray();
				for (const FString& C : P.ChangedComponents) { W.String(C); }
				W.EndArray();
			}
		}
		if (VarGuid) { W.KV(TEXT("varGuid"), *VarGuid); }
		W.EndObject();
	}

	void WriteProperties(FWriter& W, const TCHAR* Key, const TArray<FRadarPropertyDiff>& Props)
	{
		W.Key(Key);
		W.BeginArray();
		for (const FRadarPropertyDiff& P : Props) { WriteProperty(W, P); }
		W.EndArray();
	}

	void WriteRange(FWriter& W, const TOptional<int32> (&R)[2])
	{
		W.BeginArray();
		for (int32 i = 0; i < 2; ++i)
		{
			if (R[i].IsSet()) { W.Int(R[i].GetValue()); } else { W.Null(); }
		}
		W.EndArray();
	}

	void WriteSection(FWriter& W, const FRadarSectionDiff& S)
	{
		W.BeginObject();
		W.KVInt(TEXT("index"), S.Index);
		W.KV(TEXT("kind"), LexToString(S.Kind));
		if (S.bHasRangeOld || S.bHasRangeNew)
		{
			W.Key(TEXT("range"));
			W.BeginObject();
			W.Key(TEXT("old")); WriteRange(W, S.RangeOld);
			W.Key(TEXT("new")); WriteRange(W, S.RangeNew);
			W.EndObject();
		}
		WriteProperties(W, TEXT("properties"), S.Properties);
		W.Key(TEXT("channels"));
		W.BeginArray();
		for (const FRadarChannelDiff& C : S.Channels)
		{
			W.BeginObject();
			W.KV(TEXT("name"), C.Name);
			W.KV(TEXT("type"), C.Type);
			W.KV(TEXT("kind"), LexToString(C.Kind));
			WriteProperties(W, TEXT("properties"), C.Properties);
			W.Key(TEXT("keys"));
			W.BeginArray();
			for (const FRadarKeyDiff& K : C.Keys)
			{
				W.BeginObject();
				W.KVInt(TEXT("frame"), K.Frame);
				W.KV(TEXT("kind"), LexToString(K.Kind));
				W.Key(TEXT("old")); WriteValue(W, K.Old, false);
				W.Key(TEXT("new")); WriteValue(W, K.New, false);
				WriteProperties(W, TEXT("properties"), K.Properties);
				W.EndObject();
			}
			W.EndArray();
			W.EndObject();
		}
		W.EndArray();
		W.EndObject();
	}

	void WriteProducer(FWriter& W, const FRadarDiff& D)
	{
		W.Key(TEXT("producer"));
		W.BeginObject();
		W.KV(TEXT("name"), D.ProducerName);
		W.KV(TEXT("kind"), D.ProducerKind);
		W.KV(TEXT("engine"), D.Engine);
		W.KV(TEXT("pluginBranch"), D.PluginBranch);
		W.Key(TEXT("fileVersionUE5"));
		W.BeginObject(); W.KVInt(TEXT("old"), D.Old.FileVersionUE5); W.KVInt(TEXT("new"), D.New.FileVersionUE5); W.EndObject();
		W.Key(TEXT("customVersions"));
		W.BeginObject();
		for (const FRadarSideInfo* Side : { &D.Old, &D.New })
		{
			W.Key(Side == &D.Old ? TEXT("old") : TEXT("new"));
			W.BeginObject();
			for (const TPair<FString, int32>& V : Side->CustomVersions) { W.KVInt(*V.Key, V.Value); }
			W.EndObject();
		}
		W.EndObject();
		W.Key(TEXT("capabilities"));
		W.BeginArray();
		for (const FString& C : D.Capabilities) { W.String(C); }
		W.EndArray();
		W.EndObject();
	}

	void WriteSide(FWriter& W, const FRadarSideInfo& S)
	{
		W.BeginObject();
		W.KV(TEXT("kind"), S.Kind);
		if (S.Kind == TEXT("commit") || S.Kind == TEXT("head"))
		{
			W.KV(TEXT("sha"), S.Sha); W.KV(TEXT("date"), S.Date); W.KV(TEXT("author"), S.Author); W.KV(TEXT("subject"), S.Subject);
		}
		else if (S.Kind == TEXT("scm"))
		{
			W.KV(TEXT("provider"), S.Provider); W.KV(TEXT("revision"), S.Revision); W.KVIndex(TEXT("changelist"), S.Changelist);
			W.KV(TEXT("user"), S.User); W.KV(TEXT("description"), S.Description); W.KV(TEXT("date"), S.Date);
		}
		else if (S.Kind == TEXT("file"))
		{
			W.KV(TEXT("path"), S.Path);
		}
		else
		{
			W.Key(TEXT("dirty")); W.Bool(S.bDirty);
		}
		W.EndObject();
	}

	void WriteWidget(FWriter& W, const FRadarWidgetDiff& D)
	{
		W.BeginObject();
		W.KV(TEXT("id"), D.Id);
		W.KV(TEXT("kind"), LexToString(D.Kind));
		if (D.bMovedToo) { W.Key(TEXT("moved")); W.Bool(true); }
		W.KV(TEXT("area"), LexToString(D.Area));
		W.KV(TEXT("severity"), LexToString(D.Severity));
		W.KV(TEXT("class"), D.Class);
		if (D.Rename.IsSet())
		{
			W.Key(TEXT("rename"));
			W.BeginObject();
			W.KV(TEXT("from"), D.Rename->From);
			W.Key(TEXT("confidence")); W.Number(D.Rename->Confidence);
			W.KV(TEXT("evidence"), D.Rename->Evidence);
			W.Key(TEXT("graphConfirmed")); W.Bool(D.Rename->bGraphConfirmed);
			W.EndObject();
		}
		W.Key(TEXT("displayLabel")); W.BeginObject(); W.KVOpt(TEXT("old"), D.LabelOld); W.KVOpt(TEXT("new"), D.LabelNew); W.EndObject();
		W.Key(TEXT("parent")); W.BeginObject(); W.KVOpt(TEXT("old"), D.ParentOld); W.KVOpt(TEXT("new"), D.ParentNew); W.EndObject();
		W.Key(TEXT("index")); W.BeginObject(); W.KVIndex(TEXT("old"), D.bHasOld ? D.IndexOld : INDEX_NONE); W.KVIndex(TEXT("new"), D.bHasNew ? D.IndexNew : INDEX_NONE); W.EndObject();
		W.Key(TEXT("badges")); W.BeginArray(); for (const FString& B : D.Badges) { W.String(B); } W.EndArray();
		if (!D.StateRef.IsEmpty()) { W.KV(TEXT("stateRef"), D.StateRef); }
		WriteProperties(W, TEXT("properties"), D.Properties);
		W.Key(TEXT("slot"));
		W.BeginObject();
		W.Key(TEXT("class")); W.BeginObject(); W.KVOpt(TEXT("old"), D.Slot.ClassOld); W.KVOpt(TEXT("new"), D.Slot.ClassNew); W.EndObject();
		WriteProperties(W, TEXT("properties"), D.Slot.Properties);
		W.EndObject();
		W.EndObject();
	}

	void WriteFileHeader(FWriter& W, const FRadarDiff& D)
	{
		W.KVInt(TEXT("schemaVersion"), 1);
		WriteProducer(W, D);
	}
}

void RadarDiffJson::CountSummary(const FRadarDiff& D, TMap<FString, int32>& ByKind, TMap<FString, int32>& ByArea)
{
	for (const TCHAR* K : { TEXT("added"), TEXT("removed"), TEXT("changed"), TEXT("moved"), TEXT("renamed-probable"), TEXT("text-key-only") }) { ByKind.Add(K, 0); }
	for (const TCHAR* A : { TEXT("designer"), TEXT("slot"), TEXT("binding"), TEXT("cdo"), TEXT("anim"), TEXT("graph-logic"), TEXT("graph-layout") }) { ByArea.Add(A, 0); }
	auto K = [&ByKind](ERadarKind Kind) { if (int32* N = ByKind.Find(LexToString(Kind))) { ++*N; } };
	for (const FRadarWidgetDiff& W : D.Widgets)
	{
		K(W.Kind);
		if (W.bMovedToo) { ++ByKind[TEXT("moved")]; }
		const bool bSlot = W.Slot.Properties.Num() > 0 || W.Slot.ClassOld.Value != W.Slot.ClassNew.Value || W.Kind == ERadarKind::Moved || W.bMovedToo;
		const bool bDesigner = W.Kind == ERadarKind::Added || W.Kind == ERadarKind::Removed || W.Kind == ERadarKind::RenamedProbable || W.Properties.Num() > 0;
		if (bDesigner) { ++ByArea[TEXT("designer")]; }
		if (bSlot && W.Kind != ERadarKind::Added && W.Kind != ERadarKind::Removed) { ++ByArea[TEXT("slot")]; }
	}
	for (const FRadarBindingDiff& B : D.Bindings) { K(B.Kind); ++ByArea[TEXT("binding")]; }
	for (const FRadarPropertyDiff& P : D.Cdo) { K(P.Kind); ++ByArea[TEXT("cdo")]; }
	for (const FRadarVariableDiff& V : D.Variables) { K(V.Kind); ++ByArea[TEXT("cdo")]; }
	for (const FRadarAnimationDiff& A : D.Animations) { K(A.Kind); ++ByArea[TEXT("anim")]; }
	for (const FRadarGraphDiff& G : D.Graphs)
	{
		for (const FRadarNodeDiff& N : G.Nodes)
		{
			K(N.Kind);
			++ByArea[LexToString(N.Area)];
		}
	}
}

FString RadarDiffJson::DiffToString(const FRadarDiff& D)
{
	FWriter W;
	W.BeginObject();
	WriteFileHeader(W, D);

	W.Key(TEXT("asset"));
	W.BeginObject();
	W.KV(TEXT("packageName"), D.PackageName);
	W.KV(TEXT("relPath"), D.RelPath);
	W.KV(TEXT("assetName"), D.AssetName);
	W.KV(TEXT("parentClass"), D.ParentClass);
	W.EndObject();

	W.Key(TEXT("old")); WriteSide(W, D.Old);
	W.Key(TEXT("new")); WriteSide(W, D.New);

	W.Key(TEXT("designSize"));
	W.BeginObject();
	for (const FRadarSideInfo* S : { &D.Old, &D.New })
	{
		W.Key(S == &D.Old ? TEXT("old") : TEXT("new"));
		W.BeginObject();
		W.KV(TEXT("mode"), S->DesignSizeMode);
		W.Key(TEXT("size")); W.BeginArray(); W.Number(S->DesignSize.X); W.Number(S->DesignSize.Y); W.EndArray();
		W.EndObject();
	}
	W.EndObject();

	TMap<FString, int32> ByKind, ByArea;
	CountSummary(D, ByKind, ByArea);
	W.Key(TEXT("summary"));
	W.BeginObject();
	W.Key(TEXT("byKind")); W.BeginObject(); for (const TPair<FString, int32>& P : ByKind) { W.KVInt(*P.Key, P.Value); } W.EndObject();
	W.Key(TEXT("byArea")); W.BeginObject(); for (const TPair<FString, int32>& P : ByArea) { W.KVInt(*P.Key, P.Value); } W.EndObject();
	W.EndObject();

	W.Key(TEXT("widgets"));
	W.BeginArray();
	for (const FRadarWidgetDiff& Wd : D.Widgets) { WriteWidget(W, Wd); }
	W.EndArray();

	W.Key(TEXT("bindings"));
	W.BeginArray();
	for (const FRadarBindingDiff& B : D.Bindings)
	{
		W.BeginObject();
		W.KV(TEXT("id"), B.Id);
		W.KV(TEXT("kind"), LexToString(B.Kind));
		W.KV(TEXT("area"), TEXT("binding"));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const bool bOld = Side == 0;
			W.Key(bOld ? TEXT("old") : TEXT("new"));
			const FRadarOptionalString& F = bOld ? B.OldFunction : B.NewFunction;
			if (!F.bSet) { W.Null(); continue; }
			W.BeginObject();
			W.KVOpt(TEXT("function"), F);
			W.KVOpt(TEXT("kind"), bOld ? B.OldKind : B.NewKind);
			W.KVOpt(TEXT("sourceProperty"), bOld ? B.OldSourceProperty : B.NewSourceProperty);
			W.EndObject();
		}
		W.EndObject();
	}
	W.EndArray();

	W.Key(TEXT("cdo"));
	W.BeginArray();
	for (const FRadarPropertyDiff& P : D.Cdo)
	{
		// 속성 줄 + 루트가 BP 변수면 varGuid
		FString Root = P.Path;
		Root.Split(TEXT("."), &Root, nullptr);
		Root.Split(TEXT("["), &Root, nullptr);
		WriteProperty(W, P, false, D.CdoVarGuids.Find(Root));
	}
	W.EndArray();

	W.Key(TEXT("variables"));
	W.BeginArray();
	for (const FRadarVariableDiff& V : D.Variables)
	{
		W.BeginObject();
		W.KV(TEXT("name"), V.Name);
		W.KV(TEXT("varGuid"), V.VarGuid);
		W.KV(TEXT("kind"), LexToString(V.Kind));
		W.KV(TEXT("area"), TEXT("cdo"));
		if (!V.RenamedFrom.IsEmpty()) { W.KV(TEXT("renamedFrom"), V.RenamedFrom); }
		WriteProperties(W, TEXT("properties"), V.Properties);
		W.EndObject();
	}
	W.EndArray();

	W.Key(TEXT("animations"));
	W.BeginArray();
	for (const FRadarAnimationDiff& A : D.Animations)
	{
		W.BeginObject();
		W.KV(TEXT("id"), A.Id);
		W.KV(TEXT("kind"), LexToString(A.Kind));
		W.KV(TEXT("area"), TEXT("anim"));
		W.Key(TEXT("tracks"));
		W.BeginArray();
		for (const FRadarTrackDiff& T : A.Tracks)
		{
			W.BeginObject();
			W.KV(TEXT("id"), T.Id);
			W.KV(TEXT("kind"), LexToString(T.Kind));
			W.Key(TEXT("sectionCount")); W.BeginObject(); W.KVIndex(TEXT("old"), T.SectionsOld); W.KVIndex(TEXT("new"), T.SectionsNew); W.EndObject();
			W.Key(TEXT("sections"));
			W.BeginArray();
			for (const FRadarSectionDiff& S : T.Sections) { WriteSection(W, S); }
			W.EndArray();
			W.EndObject();
		}
		W.EndArray();
		W.EndObject();
	}
	W.EndArray();

	W.Key(TEXT("graphs"));
	W.BeginArray();
	for (const FRadarGraphDiff& G : D.Graphs)
	{
		W.BeginObject();
		W.KV(TEXT("id"), G.Id);
		W.KV(TEXT("kind"), LexToString(G.Kind));
		W.Key(TEXT("logic")); W.BeginObject(); W.KVInt(TEXT("added"), G.LogicAdded); W.KVInt(TEXT("removed"), G.LogicRemoved); W.KVInt(TEXT("changed"), G.LogicChanged); W.EndObject();
		W.Key(TEXT("layout")); W.BeginObject(); W.KVInt(TEXT("moved"), G.LayoutMoved); W.EndObject();
		W.Key(TEXT("nodes"));
		W.BeginArray();
		for (const FRadarNodeDiff& N : G.Nodes)
		{
			W.BeginObject();
			W.KV(TEXT("guid"), N.Guid);
			W.KV(TEXT("class"), N.Class);
			W.KV(TEXT("title"), N.Title);
			W.KV(TEXT("kind"), LexToString(N.Kind));
			W.KV(TEXT("area"), LexToString(N.Area));
			WriteProperties(W, TEXT("properties"), N.Properties);
			W.Key(TEXT("pins"));
			W.BeginArray();
			for (const FRadarPinDiff& P : N.Pins)
			{
				W.BeginObject();
				W.KV(TEXT("pinId"), P.PinId);
				W.KV(TEXT("name"), P.Name);
				W.KV(TEXT("kind"), LexToString(P.Kind));
				WriteProperties(W, TEXT("properties"), P.Properties);
				W.EndObject();
			}
			W.EndArray();
			W.EndObject();
		}
		W.EndArray();
		W.EndObject();
	}
	W.EndArray();

	W.Key(TEXT("textKeys")); W.BeginArray(); W.EndArray();

	W.Key(TEXT("notes"));
	W.BeginArray();
	for (const FRadarNote& N : D.Notes)
	{
		W.BeginObject();
		W.KV(TEXT("code"), N.Code);
		W.KV(TEXT("widget"), N.Widget);
		W.KV(TEXT("severity"), LexToString(N.Severity));
		W.EndObject();
	}
	W.EndArray();

	W.EndObject();
	return W.Out + TEXT("\n");
}

FString RadarDiffJson::StateToString(const FRadarState& S, const FRadarDiff& ForProducer)
{
	FWriter W;
	W.BeginObject();
	WriteFileHeader(W, ForProducer);
	W.Key(TEXT("file"));
	W.BeginObject();
	W.KV(TEXT("packageName"), S.PackageName);
	W.KVInt(TEXT("fileVersionUE5"), S.Side.FileVersionUE5);
	W.EndObject();
	W.Key(TEXT("revision")); WriteSide(W, S.Side);
	W.Key(TEXT("widgetTree"));
	W.BeginObject();
	W.KV(TEXT("root"), S.RootWidget);
	W.Key(TEXT("children"));
	W.BeginObject();
	for (const TPair<FString, TArray<FString>>& Pair : S.Children)
	{
		W.Key(Pair.Key);
		W.BeginArray();
		for (const FString& C : Pair.Value) { W.String(C); }
		W.EndArray();
	}
	W.EndObject();
	W.EndObject();
	W.Key(TEXT("objects"));
	W.BeginArray();
	for (const FRadarStateObject& O : S.Objects)
	{
		W.BeginObject();
		W.KV(TEXT("id"), O.Id);
		W.KV(TEXT("class"), O.Class);
		W.Key(TEXT("properties"));
		W.BeginArray();
		for (const FRadarPropertyDiff& P : O.Properties) { WriteProperty(W, P, true); }
		W.EndArray();
		W.EndObject();
	}
	W.EndArray();
	W.EndObject();
	return W.Out + TEXT("\n");
}

static bool SaveText(const FString& Text, const FString& Path, FString& OutError)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("파일 쓰기 실패: %s"), *Path);
		return false;
	}
	return true;
}

bool RadarDiffJson::SaveDiff(const FRadarDiff& Diff, const FString& Path, FString& OutError)
{
	return SaveText(DiffToString(Diff), Path, OutError);
}

bool RadarDiffJson::SaveState(const FRadarState& State, const FRadarDiff& ForProducer, const FString& Path, FString& OutError)
{
	return SaveText(StateToString(State, ForProducer), Path, OutError);
}
