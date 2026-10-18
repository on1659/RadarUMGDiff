// Copyright Radar. All Rights Reserved.

// 스키마 덤프 (goal 문서 "Schema dump", dev 문서 §11 M4). 콘솔: RadarUMGDiff.DumpSchema [출력 경로]
// Python 분석기의 "현재 상태 완성" 층 — 태그에 없는 속성(아키타입 값)을 채우고 maybeDefault 줄을 확정한다. 해석의 전제가 아니다.
// 에디터 Python 의 unreal 모듈은 BlueprintVisible 속성만 노출하므로(PythonScriptPlugin PyGenUtil.cpp IsScriptExposedProperty)
// 저장되는 모든 UPROPERTY 의 기본값은 C++ 리플렉션으로 뽑는다. editor/dump_schema.py 는 이 명령을 부르는 입구다.
// 값의 JSON 형은 diff.json 과 같다 (RadarProperty::ToJson) — Python 이 파일에서 읽은 값과 그대로 비교한다.

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"

#include "RadarPropertyCompare.h"
#include "RadarEngineCompat.h"
#include "RadarUMGDiffLog.h"

namespace RadarSchema
{
	/** 스키마에 넣을 네이티브 클래스의 뿌리 — 위젯 블루프린트 패키지에 익스포트로 나오는 것들. */
	TArray<UClass*> RootClasses()
	{
		return { UWidget::StaticClass(), UPanelSlot::StaticClass(), UWidgetTree::StaticClass(), UWidgetAnimation::StaticClass(),
			UMovieScene::StaticClass(), UMovieSceneTrack::StaticClass(), UMovieSceneSection::StaticClass(),
			UEdGraph::StaticClass(), UEdGraphNode::StaticClass(), UBlueprint::StaticClass() };
	}

	void CollectStructs(const FProperty* P, TSet<const UScriptStruct*>& Out)
	{
		if (const FStructProperty* SP = CastField<FStructProperty>(P))
		{
			bool bAlready = false;
			Out.Add(SP->Struct, &bAlready);
			if (!bAlready)
			{
				for (TFieldIterator<FProperty> It(SP->Struct); It; ++It) { CollectStructs(*It, Out); }
			}
		}
		else if (const FArrayProperty* AP = CastField<FArrayProperty>(P)) { CollectStructs(AP->Inner, Out); }
		else if (const FSetProperty* SetP = CastField<FSetProperty>(P)) { CollectStructs(SetP->ElementProp, Out); }
		else if (const FMapProperty* MP = CastField<FMapProperty>(P)) { CollectStructs(MP->KeyProp, Out); CollectStructs(MP->ValueProp, Out); }
	}

	/** 형 하나: 속성 목록(저장 여부 포함)과 기본값. Container 는 CDO 또는 기본 생성한 구조체. */
	TSharedRef<FJsonObject> TypeJson(const UStruct* Type, const void* Container, const FRadarCompareContext& Ctx)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (const UStruct* Super = Type->GetSuperStruct())
		{
			Obj->SetStringField(TEXT("super"), Super->GetPathName());
		}
		TArray<TSharedPtr<FJsonValue>> Props;
		TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			const FProperty* P = *It;
			const bool bSaved = !P->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_SkipSerialization);
			TSharedRef<FJsonObject> PO = MakeShared<FJsonObject>();
			PO->SetStringField(TEXT("name"), P->GetName());
			PO->SetStringField(TEXT("type"), RadarProperty::TypeString(P));
			PO->SetNumberField(TEXT("arrayDim"), P->ArrayDim);
			PO->SetStringField(TEXT("category"), RadarProperty::Category(P));
			PO->SetBoolField(TEXT("saved"), bSaved);
			PO->SetStringField(TEXT("owner"), P->GetOwnerStruct() ? P->GetOwnerStruct()->GetName() : FString());
			Props.Add(MakeShared<FJsonValueObject>(PO));
			if (!bSaved || !Container)
			{
				continue;
			}
			for (int32 i = 0; i < P->ArrayDim; ++i)
			{
				const FString Key = P->ArrayDim > 1 ? FString::Printf(TEXT("%s[%d]"), *P->GetName(), i) : P->GetName();
				Defaults->SetField(Key, RadarProperty::ToJson(P, P->ContainerPtrToValuePtr<void>(Container, i), Ctx));
			}
		}
		Obj->SetArrayField(TEXT("properties"), Props);
		Obj->SetObjectField(TEXT("defaults"), Defaults);
		return Obj;
	}

	void Run(const TArray<FString>& Args)
	{
		FString Out = Args.Num() > 0 ? Args[0] : FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RadarUMGDiff/schema.json"));
		Out.TrimQuotesInline();
		const FRadarCompareContext Ctx = FRadarCompareContext::FromSettings();
		const TArray<UClass*> Roots = RootClasses();

		TSharedRef<FJsonObject> Classes = MakeShared<FJsonObject>();
		TSet<const UScriptStruct*> Structs;
		int32 NumClasses = 0;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C->HasAnyClassFlags(CLASS_Native) || C->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			const bool bWanted = Roots.ContainsByPredicate([C](const UClass* R) { return C->IsChildOf(R); });
			if (!bWanted)
			{
				continue;
			}
			Classes->SetObjectField(C->GetPathName(), TypeJson(C, C->GetDefaultObject(), Ctx));
			for (TFieldIterator<FProperty> P(C); P; ++P) { CollectStructs(*P, Structs); }
			++NumClasses;
		}

		TSharedRef<FJsonObject> StructObjs = MakeShared<FJsonObject>();
		for (const UScriptStruct* S : Structs)
		{
			TArray<uint8, TAlignedHeapAllocator<16>> Buffer;
			Buffer.SetNumZeroed(FMath::Max(S->GetStructureSize(), 1));
			S->InitializeStruct(Buffer.GetData());
			StructObjs->SetObjectField(S->GetPathName(), TypeJson(S, Buffer.GetData(), Ctx));
			S->DestroyStruct(Buffer.GetData());
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schemaVersion"), 1);
		Root->SetStringField(TEXT("engine"), RadarCompat::EngineVersionString());
		Root->SetStringField(TEXT("pluginBranch"), RadarCompat::PluginBranch());
		Root->SetObjectField(TEXT("classes"), Classes);
		Root->SetObjectField(TEXT("structs"), StructObjs);
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);
		if (FFileHelper::SaveStringToFile(Text, *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogRadarUMGDiff, Display, TEXT("스키마 기록: %s (클래스 %d, 구조체 %d)"), *Out, NumClasses, Structs.Num());
		}
		else
		{
			UE_LOG(LogRadarUMGDiff, Error, TEXT("스키마를 쓰지 못했습니다: %s"), *Out);
		}
	}

	FAutoConsoleCommand GDumpSchemaCommand(TEXT("RadarUMGDiff.DumpSchema"),
		TEXT("Python 분석기용 스키마(클래스·구조체 속성과 기본값)를 쓴다: [출력 경로, 기본 Saved/RadarUMGDiff/schema.json]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}
