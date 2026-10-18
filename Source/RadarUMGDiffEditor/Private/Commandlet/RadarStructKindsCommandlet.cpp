// Copyright Radar. All Rights Reserved.

#include "Commandlet/RadarStructKindsCommandlet.h"
#include "RadarEngineCompat.h"
#include "RadarUMGDiffLog.h"

#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/ObjectVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

URadarStructKindsCommandlet::URadarStructKindsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	/**
	 * 패키지 저장(태그 직렬화)에서 구조체 몸체가 어떻게 쓰이는가. UScriptStruct::SerializeItem (5.1 Class.cpp 2747~, 5.8 같은 함수):
	 * STRUCT_SerializeNative 면 CppStructOps::Serialize(네이티브 바이트), 아니면 UseBinarySerialization — 저장 아카이브는
	 * WantBinaryPropertySerialization 이 false 라 STRUCT_Immutable 일 때만 SerializeBin(바이트), 나머지는 태그 스트림.
	 * Serialize 가 false 를 돌려 태그로 떨어지는 구조체는 플래그만으로는 알 수 없다(파이썬 쪽이 크기 검증으로 걸러낸다).
	 */
	const TCHAR* KindOf(const UScriptStruct* Struct)
	{
		if (Struct->StructFlags & STRUCT_SerializeNative)
		{
			return TEXT("native");
		}
		if (Struct->StructFlags & STRUCT_Immutable)
		{
			return TEXT("immutable");
		}
		return TEXT("tagged");
	}
}

int32 URadarStructKindsCommandlet::Main(const FString& Params)
{
	FString OutPath;
	if (!FParse::Value(*Params, TEXT("out="), OutPath))
	{
		OutPath = FPaths::ProjectSavedDir() / TEXT("RadarUMGDiff") / TEXT("struct_kinds.json");
	}
	OutPath = FPaths::ConvertRelativePathToFull(OutPath);

	// 태그에는 짧은 이름만 있다. 같은 짧은 이름이 다른 종류로 둘 이상이면 표에서 빼고 conflicts 에 적는다(파이썬은 모름으로 다룬다).
	TMap<FString, TArray<TPair<FString, FString>>> ByName;   // 짧은 이름 → [(전체 경로, 종류)]
	int32 Count = 0;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct->GetOutermost()->GetName().StartsWith(TEXT("/Script/")))
		{
			continue;   // 사용자 정의 구조체(UserDefinedStruct)는 언제나 태그 스트림이고 표에 없을 때 파이썬이 그렇게 다룬다
		}
		Struct->PrepareCppStructOps();
		ByName.FindOrAdd(Struct->GetName()).Add(TPair<FString, FString>(Struct->GetPathName(), KindOf(Struct)));
		++Count;
	}
	ByName.KeySort(TLess<FString>());

	TSharedRef<FJsonObject> Kinds = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Conflicts = MakeShared<FJsonObject>();
	for (const TPair<FString, TArray<TPair<FString, FString>>>& Entry : ByName)
	{
		bool bSame = true;
		for (const TPair<FString, FString>& P : Entry.Value)
		{
			bSame &= P.Value == Entry.Value[0].Value;
		}
		if (bSame)
		{
			Kinds->SetStringField(Entry.Key, Entry.Value[0].Value);
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const TPair<FString, FString>& P : Entry.Value)
			{
				Items.Add(MakeShared<FJsonValueString>(P.Key + TEXT("=") + P.Value));
			}
			Conflicts->SetArrayField(Entry.Key, Items);
		}
	}

	// 옛 형식의 집합·맵 태그에는 원소 구조체 이름이 없다(내부 태그는 배열에만 — 5.1 PropertyArray.cpp 210). 네이티브 클래스·구조체의
	// 집합·맵 속성마다 원소 구조체를 적는다: "<소유 클래스·구조체 짧은 이름>.<속성>" → {key|value|element: 구조체 이름}. 상속 속성 포함.
	TSharedRef<FJsonObject> Containers = MakeShared<FJsonObject>();
	auto StructOf = [](const FProperty* P) -> FString
	{
		const FStructProperty* SP = CastField<FStructProperty>(P);
		return SP && SP->Struct ? SP->Struct->GetName() : FString();
	};
	TArray<FString> ContainerKeys;
	TMap<FString, TSharedPtr<FJsonObject>> ContainerEntries;
	for (TObjectIterator<UStruct> It; It; ++It)
	{
		UStruct* Owner = *It;
		if (!Owner->GetOutermost()->GetName().StartsWith(TEXT("/Script/")) || Owner->IsA<UFunction>())
		{
			continue;
		}
		for (TFieldIterator<FProperty> PIt(Owner, EFieldIteratorFlags::IncludeSuper); PIt; ++PIt)
		{
			TSharedPtr<FJsonObject> Entry;
			if (const FMapProperty* MP = CastField<FMapProperty>(*PIt))
			{
				const FString K = StructOf(MP->KeyProp), V = StructOf(MP->ValueProp);
				if (!K.IsEmpty() || !V.IsEmpty())
				{
					Entry = MakeShared<FJsonObject>();
					if (!K.IsEmpty()) { Entry->SetStringField(TEXT("key"), K); }
					if (!V.IsEmpty()) { Entry->SetStringField(TEXT("value"), V); }
				}
			}
			else if (const FSetProperty* SetP = CastField<FSetProperty>(*PIt))
			{
				const FString E = StructOf(SetP->ElementProp);
				if (!E.IsEmpty())
				{
					Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("element"), E);
				}
			}
			if (Entry)
			{
				const FString Key = Owner->GetName() + TEXT(".") + PIt->GetName();
				if (!ContainerEntries.Contains(Key))
				{
					ContainerKeys.Add(Key);
				}
				ContainerEntries.Add(Key, Entry);
			}
		}
	}
	ContainerKeys.Sort();
	for (const FString& Key : ContainerKeys)
	{
		Containers->SetObjectField(Key, ContainerEntries[Key]);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("engine"), RadarCompat::EngineVersionString());
	Root->SetNumberField(TEXT("fileVersionUE5"), GPackageFileUEVersion.FileVersionUE5);
	Root->SetStringField(TEXT("generatedBy"), TEXT("RadarStructKinds commandlet"));
	Root->SetObjectField(TEXT("kinds"), Kinds);
	Root->SetObjectField(TEXT("conflicts"), Conflicts);
	Root->SetObjectField(TEXT("containers"), Containers);

	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	if (!FFileHelper::SaveStringToFile(Text, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogRadarUMGDiff, Error, TEXT("구조체 종류 표를 쓰지 못했습니다: %s"), *OutPath);
		return 2;
	}
	UE_LOG(LogRadarUMGDiff, Display, TEXT("구조체 종류 표: %s (구조체 %d, 이름 %d, 충돌 %d, 집합·맵 속성 %d)"), *OutPath, Count, Kinds->Values.Num(), Conflicts->Values.Num(), Containers->Values.Num());
	return 0;
}
