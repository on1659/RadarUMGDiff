// Copyright Radar. All Rights Reserved.

#include "RadarPropertyCompare.h"
#include "RadarUMGDiffSettings.h"
#include "RadarEngineCompat.h"

#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/FieldPathProperty.h"
#include "Internationalization/Text.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Dom/JsonObject.h"

FRadarCompareContext FRadarCompareContext::FromSettings()
{
	const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
	FRadarCompareContext Ctx;
	for (const FString& Name : S.IgnoredProperties) { Ctx.IgnoredProperties.Add(FName(*Name)); }
	for (const FString& Name : S.AutoProperties) { Ctx.AutoProperties.Add(FName(*Name)); }
	for (const FString& Name : S.LeafStructs) { Ctx.LeafStructs.Add(FName(*Name)); }
	Ctx.FloatTolerance = S.FloatTolerance;
	Ctx.DoubleTolerance = S.DoubleTolerance;
	return Ctx;
}

FString FRadarCompareContext::NormalizePath(const FString& Path) const
{
	FString Out = Path;
	for (const TPair<FString, FString>& Pair : PackageRemap)
	{
		if (!Pair.Key.IsEmpty() && Out.Contains(Pair.Key))
		{
			Out.ReplaceInline(*Pair.Key, *Pair.Value, ESearchCase::CaseSensitive);
		}
	}
	return Out;
}

FString FRadarCompareContext::ObjectPath(const UObject* Object) const
{
	return Object ? NormalizePath(Object->GetPathName()) : FString();
}

namespace RadarProperty
{
	static const FName NAME_RadarGuid(TEXT("Guid"));

	static int32 StaticDim(const FProperty* Property)
	{
		const int32 Elem = RadarCompat::ElementSizeOf(Property);
		return Elem > 0 ? FMath::Max(1, Property->GetSize() / Elem) : 1;
	}

	FString TypeString(const FProperty* Property)
	{
		const FString ClassName = Property->GetClass()->GetName();
		if (const FStructProperty* SP = CastField<FStructProperty>(Property))
		{
			return ClassName + TEXT(":") + SP->Struct->GetName();
		}
		if (const FEnumProperty* EP = CastField<FEnumProperty>(Property))
		{
			return ClassName + TEXT(":") + (EP->GetEnum() ? EP->GetEnum()->GetName() : FString());
		}
		if (const FByteProperty* BP = CastField<FByteProperty>(Property))
		{
			return BP->Enum ? ClassName + TEXT(":") + BP->Enum->GetName() : ClassName;
		}
		if (const FArrayProperty* AP = CastField<FArrayProperty>(Property))
		{
			return ClassName + TEXT(":") + TypeString(AP->Inner);
		}
		if (const FSetProperty* SetP = CastField<FSetProperty>(Property))
		{
			return ClassName + TEXT(":") + TypeString(SetP->ElementProp);
		}
		if (const FMapProperty* MP = CastField<FMapProperty>(Property))
		{
			return ClassName + TEXT(":") + TypeString(MP->KeyProp) + TEXT(",") + TypeString(MP->ValueProp);
		}
		return ClassName;
	}

	FString Category(const FProperty* Property)
	{
#if WITH_EDITORONLY_DATA
		const FString& Cat = Property->GetMetaData(TEXT("Category"));
		if (!Cat.IsEmpty())
		{
			return Cat;
		}
#endif
		return TEXT("기타");
	}

	bool ShouldSkip(const FProperty* Property, const FRadarCompareContext& Ctx)
	{
		// 패키지에 저장되지 않는 속성은 비교하지 않는다 (Python 은 태그만 본다 — 두 생산자가 같은 집합을 보게).
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_SkipSerialization))
		{
			return true;
		}
		const FName Name = Property->GetFName();
		return Ctx.IgnoredProperties.Contains(Name) || Ctx.StructuralProperties.Contains(Name);
	}

	bool IsLeafStruct(const FProperty* Property, const FRadarCompareContext& Ctx)
	{
		const FStructProperty* SP = CastField<FStructProperty>(Property);
		return SP && Ctx.LeafStructs.Contains(SP->Struct->GetFName());
	}

	static bool NearlyEqual(double A, double B, double Tol)
	{
		if (A == B)
		{
			return true;
		}
		return FMath::Abs(A - B) <= Tol * FMath::Max(FMath::Abs(A), FMath::Abs(B));
	}

	static TSharedPtr<FJsonValue> TextToJson(const FText& Text)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		const FString* Source = FTextInspector::GetSourceString(Text);
		Obj->SetStringField(TEXT("source"), Source ? *Source : Text.ToString());
		Obj->SetStringField(TEXT("namespace"), FTextInspector::GetNamespace(Text).Get(FString()));
		Obj->SetStringField(TEXT("key"), FTextInspector::GetKey(Text).Get(FString()));
		FName TableId;
		FString TableKey;
		if (FTextInspector::GetTableIdAndKey(Text, TableId, TableKey))
		{
			Obj->SetStringField(TEXT("table"), TableId.ToString());
			Obj->SetStringField(TEXT("tableKey"), TableKey);
		}
		else
		{
			Obj->SetField(TEXT("table"), MakeShared<FJsonValueNull>());
			Obj->SetField(TEXT("tableKey"), MakeShared<FJsonValueNull>());
		}
		return MakeShared<FJsonValueObject>(Obj);
	}

	static bool TextEqual(const FText& A, const FText& B)
	{
		FName TableA, TableB;
		FString KeyA, KeyB;
		const bool bTableA = FTextInspector::GetTableIdAndKey(A, TableA, KeyA);
		const bool bTableB = FTextInspector::GetTableIdAndKey(B, TableB, KeyB);
		if (bTableA != bTableB || (bTableA && (TableA != TableB || KeyA != KeyB)))
		{
			return false;
		}
		const FString* SA = FTextInspector::GetSourceString(A);
		const FString* SB = FTextInspector::GetSourceString(B);
		return (SA ? *SA : A.ToString()).Equals(SB ? *SB : B.ToString(), ESearchCase::CaseSensitive);
	}

	static FString EnumValueName(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameByValue(Value).ToString() : LexToString(Value);
	}

	TSharedPtr<FJsonValue> ToJson(const FProperty* Property, const void* ValuePtr, const FRadarCompareContext& Ctx)
	{
		if (const FBoolProperty* BoolP = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(BoolP->GetPropertyValue(ValuePtr));
		}
		if (const FEnumProperty* EP = CastField<FEnumProperty>(Property))
		{
			const int64 V = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			return MakeShared<FJsonValueString>(EnumValueName(EP->GetEnum(), V));
		}
		if (const FNumericProperty* NP = CastField<FNumericProperty>(Property))
		{
			if (UEnum* Enum = NP->GetIntPropertyEnum())
			{
				return MakeShared<FJsonValueString>(EnumValueName(Enum, NP->GetSignedIntPropertyValue(ValuePtr)));
			}
			if (NP->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NP->GetFloatingPointPropertyValue(ValuePtr));
			}
			return MakeShared<FJsonValueNumber>(static_cast<double>(NP->GetSignedIntPropertyValue(ValuePtr)));
		}
		if (const FNameProperty* NameP = CastField<FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(NameP->GetPropertyValue(ValuePtr).ToString());
		}
		if (const FStrProperty* StrP = CastField<FStrProperty>(Property))
		{
			return MakeShared<FJsonValueString>(StrP->GetPropertyValue(ValuePtr));
		}
		if (const FTextProperty* TextP = CastField<FTextProperty>(Property))
		{
			return TextToJson(TextP->GetPropertyValue(ValuePtr));
		}
		if (const FSoftObjectProperty* SoftP = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr& Ptr = SoftP->GetPropertyValue(ValuePtr);
			const FString Path = Ctx.NormalizePath(Ptr.ToSoftObjectPath().ToString());
			return Path.IsEmpty() ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>()) : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Path));
		}
		if (const FObjectPropertyBase* ObjP = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Obj = ObjP->GetObjectPropertyValue(ValuePtr);
			return Obj ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Ctx.ObjectPath(Obj))) : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
		}
		if (const FInterfaceProperty* IP = CastField<FInterfaceProperty>(Property))
		{
			const UObject* Obj = IP->GetPropertyValue(ValuePtr).GetObject();
			return Obj ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Ctx.ObjectPath(Obj))) : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
		}
		if (const FStructProperty* SP = CastField<FStructProperty>(Property))
		{
			const FName StructName = SP->Struct->GetFName();
			if (StructName == NAME_RadarGuid)
			{
				return MakeShared<FJsonValueString>(static_cast<const FGuid*>(ValuePtr)->ToString(EGuidFormats::Digits));
			}
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
			{
				const int32 Dim = StaticDim(*It);
				for (int32 i = 0; i < Dim; ++i)
				{
					const FString Key = Dim > 1 ? FString::Printf(TEXT("%s[%d]"), *It->GetName(), i) : It->GetName();
					Obj->SetField(Key, ToJson(*It, It->ContainerPtrToValuePtr<void>(ValuePtr, i), Ctx));
				}
			}
			return MakeShared<FJsonValueObject>(Obj);
		}
		if (const FArrayProperty* AP = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(AP, ValuePtr);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				Items.Add(ToJson(AP->Inner, Helper.GetRawPtr(i), Ctx));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (const FSetProperty* SetP = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(SetP, ValuePtr);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (FScriptSetHelper::FIterator It(Helper); It; ++It)
			{
				Items.Add(ToJson(SetP->ElementProp, Helper.GetElementPtr(RadarCompat::ContainerIndex(It)), Ctx));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (const FMapProperty* MP = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MP, ValuePtr);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				TSharedRef<FJsonObject> Pair = MakeShared<FJsonObject>();
				Pair->SetField(TEXT("key"), ToJson(MP->KeyProp, Helper.GetKeyPtr(RadarCompat::ContainerIndex(It)), Ctx));
				Pair->SetField(TEXT("value"), ToJson(MP->ValueProp, Helper.GetValuePtr(RadarCompat::ContainerIndex(It)), Ctx));
				Items.Add(MakeShared<FJsonValueObject>(Pair));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		// 델리게이트·필드 경로 등: 정규화한 텍스트
		return MakeShared<FJsonValueString>(ToText(Property, ValuePtr, Ctx));
	}

	FString ToText(const FProperty* Property, const void* ValuePtr, const FRadarCompareContext& Ctx)
	{
		FString Out;
		Property->ExportText_Direct(Out, ValuePtr, nullptr, nullptr, PPF_None);
		return Ctx.NormalizePath(Out);
	}

	bool Equal(const FProperty* Property, const void* A, const void* B, const FRadarCompareContext& Ctx)
	{
		if (const FNumericProperty* NP = CastField<FNumericProperty>(Property))
		{
			if (NP->IsFloatingPoint() && !NP->GetIntPropertyEnum())
			{
				const double Tol = Property->IsA<FDoubleProperty>() ? Ctx.DoubleTolerance : Ctx.FloatTolerance;
				return NearlyEqual(NP->GetFloatingPointPropertyValue(A), NP->GetFloatingPointPropertyValue(B), Tol);
			}
			return Property->Identical(A, B, PPF_None);
		}
		if (const FTextProperty* TextP = CastField<FTextProperty>(Property))
		{
			return TextEqual(TextP->GetPropertyValue(A), TextP->GetPropertyValue(B));
		}
		if (const FSoftObjectProperty* SoftP = CastField<FSoftObjectProperty>(Property))
		{
			return Ctx.NormalizePath(SoftP->GetPropertyValue(A).ToSoftObjectPath().ToString()) == Ctx.NormalizePath(SoftP->GetPropertyValue(B).ToSoftObjectPath().ToString());
		}
		if (const FObjectPropertyBase* ObjP = CastField<FObjectPropertyBase>(Property))
		{
			return Ctx.ObjectPath(ObjP->GetObjectPropertyValue(A)) == Ctx.ObjectPath(ObjP->GetObjectPropertyValue(B));
		}
		if (const FInterfaceProperty* IP = CastField<FInterfaceProperty>(Property))
		{
			return Ctx.ObjectPath(IP->GetPropertyValue(A).GetObject()) == Ctx.ObjectPath(IP->GetPropertyValue(B).GetObject());
		}
		if (const FStructProperty* SP = CastField<FStructProperty>(Property))
		{
			for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
			{
				const int32 Dim = StaticDim(*It);
				for (int32 i = 0; i < Dim; ++i)
				{
					if (!Equal(*It, It->ContainerPtrToValuePtr<void>(A, i), It->ContainerPtrToValuePtr<void>(B, i), Ctx))
					{
						return false;
					}
				}
			}
			// 리플렉션 멤버가 없는 네이티브 구조체는 엔진 비교로
			return SP->Struct->ChildProperties ? true : Property->Identical(A, B, PPF_DeepComparison);
		}
		if (const FArrayProperty* AP = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper HA(AP, A), HB(AP, B);
			if (HA.Num() != HB.Num())
			{
				return false;
			}
			for (int32 i = 0; i < HA.Num(); ++i)
			{
				if (!Equal(AP->Inner, HA.GetRawPtr(i), HB.GetRawPtr(i), Ctx))
				{
					return false;
				}
			}
			return true;
		}
		if (Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>() || Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>())
		{
			// 오브젝트 경로가 섞일 수 있으므로 정규화한 텍스트로 비교
			return ToText(Property, A, Ctx) == ToText(Property, B, Ctx);
		}
		return Property->Identical(A, B, PPF_DeepComparison);
	}

	FRadarValue MakeValue(const FProperty* Property, const void* ValuePtr, const void* DefaultPtr, const FRadarCompareContext& Ctx)
	{
		FRadarValue V;
		V.bSet = true;
		V.State = (DefaultPtr && Equal(Property, ValuePtr, DefaultPtr, Ctx)) ? ERadarValueState::Default : ERadarValueState::Present;
		V.Text = ToText(Property, ValuePtr, Ctx);
		V.Value = ToJson(Property, ValuePtr, Ctx);
		if ((Property->IsA<FObjectPropertyBase>() || Property->IsA<FInterfaceProperty>()) && V.Value.IsValid() && V.Value->Type == EJson::String)
		{
			V.Asset = V.Value->AsString();
		}
		return V;
	}

	static void LeafComponents(const FStructProperty* SP, const void* A, const void* B, const FRadarCompareContext& Ctx, TArray<FString>& Out)
	{
		for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
		{
			if (!Equal(*It, It->ContainerPtrToValuePtr<void>(A), It->ContainerPtrToValuePtr<void>(B), Ctx))
			{
				Out.Add(It->GetName());
			}
		}
	}

	static void DiffValue(const FProperty* Property, const void* OldPtr, const void* NewPtr, const void* OldDef, const void* NewDef,
		const FString& Path, const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out);

	static void AddRow(const FProperty* Property, const void* OldPtr, const void* NewPtr, const void* OldDef, const void* NewDef,
		const FString& Path, ERadarKind Kind, const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out)
	{
		FRadarPropertyDiff& Row = Out.AddDefaulted_GetRef();
		Row.Path = Path;
		Row.Category = Category(Property);
		Row.Type = TypeString(Property);
		Row.Kind = Kind;
		Row.bAuto = Ctx.AutoProperties.Contains(Property->GetFName());
		if (OldPtr) { Row.Old = MakeValue(Property, OldPtr, OldDef, Ctx); }
		if (NewPtr) { Row.New = MakeValue(Property, NewPtr, NewDef, Ctx); }
		if (OldPtr && NewPtr && IsLeafStruct(Property, Ctx))
		{
			LeafComponents(CastFieldChecked<FStructProperty>(Property), OldPtr, NewPtr, Ctx, Row.ChangedComponents);
		}
	}

	static void DiffValue(const FProperty* Property, const void* OldPtr, const void* NewPtr, const void* OldDef, const void* NewDef,
		const FString& Path, const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out)
	{
		if (Equal(Property, OldPtr, NewPtr, Ctx))
		{
			return;
		}
		const FStructProperty* SP = CastField<FStructProperty>(Property);
		if (SP && !IsLeafStruct(Property, Ctx) && SP->Struct->ChildProperties && SP->Struct->GetFName() != NAME_RadarGuid)
		{
			DiffStruct(SP->Struct, OldPtr, NewPtr, OldDef, NewDef, Path + TEXT("."), Ctx, Out);
			return;
		}
		if (const FArrayProperty* AP = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper HO(AP, OldPtr), HN(AP, NewPtr);
			const int32 Common = FMath::Min(HO.Num(), HN.Num());
			for (int32 i = 0; i < Common; ++i)
			{
				DiffValue(AP->Inner, HO.GetRawPtr(i), HN.GetRawPtr(i), nullptr, nullptr, FString::Printf(TEXT("%s[%d]"), *Path, i), Ctx, Out);
			}
			for (int32 i = Common; i < HO.Num(); ++i)
			{
				AddRow(AP->Inner, HO.GetRawPtr(i), nullptr, nullptr, nullptr, FString::Printf(TEXT("%s[%d]"), *Path, i), ERadarKind::Removed, Ctx, Out);
			}
			for (int32 i = Common; i < HN.Num(); ++i)
			{
				AddRow(AP->Inner, nullptr, HN.GetRawPtr(i), nullptr, nullptr, FString::Printf(TEXT("%s[%d]"), *Path, i), ERadarKind::Added, Ctx, Out);
			}
			return;
		}
		AddRow(Property, OldPtr, NewPtr, OldDef, NewDef, Path, ERadarKind::Changed, Ctx, Out);
	}

	void DiffStruct(const UStruct* Struct, const void* OldContainer, const void* NewContainer,
		const void* OldDefaults, const void* NewDefaults, const FString& Prefix,
		const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* P = *It;
			if (ShouldSkip(P, Ctx))
			{
				continue;
			}
			const int32 Dim = StaticDim(P);
			for (int32 i = 0; i < Dim; ++i)
			{
				const FString Path = Prefix + (Dim > 1 ? FString::Printf(TEXT("%s[%d]"), *P->GetName(), i) : P->GetName());
				DiffValue(P, P->ContainerPtrToValuePtr<void>(OldContainer, i), P->ContainerPtrToValuePtr<void>(NewContainer, i),
					OldDefaults ? P->ContainerPtrToValuePtr<void>(OldDefaults, i) : nullptr,
					NewDefaults ? P->ContainerPtrToValuePtr<void>(NewDefaults, i) : nullptr, Path, Ctx, Out);
			}
		}
	}

	void DumpStruct(const UStruct* Struct, const void* Container, const void* Defaults, const FString& Prefix,
		const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* P = *It;
			if (ShouldSkip(P, Ctx))
			{
				continue;
			}
			const int32 Dim = StaticDim(P);
			for (int32 i = 0; i < Dim; ++i)
			{
				FRadarPropertyDiff& Row = Out.AddDefaulted_GetRef();
				Row.Path = Prefix + (Dim > 1 ? FString::Printf(TEXT("%s[%d]"), *P->GetName(), i) : P->GetName());
				Row.Category = Category(P);
				Row.Type = TypeString(P);
				Row.Kind = ERadarKind::Unchanged;
				Row.bAuto = Ctx.AutoProperties.Contains(P->GetFName());
				Row.Old = MakeValue(P, P->ContainerPtrToValuePtr<void>(Container, i), Defaults ? P->ContainerPtrToValuePtr<void>(Defaults, i) : nullptr, Ctx);
			}
		}
	}

	static bool SameShape(const FProperty* A, const FProperty* B)
	{
		return A && B && A->GetClass() == B->GetClass() && A->SameType(B);
	}

	/** 값이 그 형의 0/빈 값인지 — 한쪽 클래스에만 있는 속성은 이때 파일에 저장되지 않으므로 줄을 내지 않는다(Python 과 같은 집합). */
	static bool IsZeroValue(const FProperty* P, const void* ValuePtr, const FRadarCompareContext& Ctx)
	{
		TArray<uint8, TAlignedHeapAllocator<16>> Buffer;
		Buffer.SetNumZeroed(FMath::Max(RadarCompat::ElementSizeOf(P), 1));
		P->InitializeValue(Buffer.GetData());
		const bool bZero = Equal(P, ValuePtr, Buffer.GetData(), Ctx);
		P->DestroyValue(Buffer.GetData());
		return bZero;
	}

	/**
	 * 이 값이 패키지에 저장되지 않는 값인지: 아키타입에 그 속성이 있으면 아키타입 값과 같을 때, 없으면 그 형의 0값일 때.
	 * 한쪽 클래스에만 있는 속성은 이때 줄을 내지 않는다 (Python 은 저장된 태그만 본다 — 2026-10-18 WBP_Pause 슬롯 Padding 실측).
	 */
	static bool IsUnsaved(const FProperty* P, const void* ValuePtr, const void* DefaultValuePtr, const FRadarCompareContext& Ctx)
	{
		return DefaultValuePtr ? Equal(P, ValuePtr, DefaultValuePtr, Ctx) : IsZeroValue(P, ValuePtr, Ctx);
	}

	/** 아키타입이 그 속성을 가질 때만 기본값 포인터 (BP 가 추가한 속성은 부모 CDO 에 없다 — 엔진 assertion 방지). */
	static const void* DefaultPtr(const FProperty* P, const UObject* Def)
	{
		return (Def && Def->IsA(P->GetOwnerClass())) ? P->ContainerPtrToValuePtr<void>(Def) : nullptr;
	}

	void DiffObjects(const UObject* OldObject, const UObject* NewObject, const FString& Prefix,
		const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out)
	{
		const UObject* OldDef = OldObject->GetArchetype();
		const UObject* NewDef = NewObject->GetArchetype();
		const UClass* OldClass = OldObject->GetClass();
		const UClass* NewClass = NewObject->GetClass();
		if (OldClass == NewClass && (!OldDef || OldDef->IsA(OldClass)) && (!NewDef || NewDef->IsA(NewClass)))
		{
			DiffStruct(OldClass, OldObject, NewObject, OldDef, NewDef, Prefix, Ctx, Out);
			return;
		}
		for (TFieldIterator<FProperty> It(OldClass); It; ++It)
		{
			const FProperty* P = *It;
			if (ShouldSkip(P, Ctx))
			{
				continue;
			}
			const FProperty* Q = NewClass->FindPropertyByName(P->GetFName());
			const void* OldPtr = P->ContainerPtrToValuePtr<void>(OldObject);
			const void* OldDefPtr = DefaultPtr(P, OldDef);
			if (!SameShape(P, Q))
			{
				if (!IsUnsaved(P, OldPtr, OldDefPtr, Ctx))
				{
					AddRow(P, OldPtr, nullptr, OldDefPtr, nullptr, Prefix + P->GetName(), ERadarKind::Removed, Ctx, Out);
				}
				if (Q && !ShouldSkip(Q, Ctx) && !IsUnsaved(Q, Q->ContainerPtrToValuePtr<void>(NewObject), DefaultPtr(Q, NewDef), Ctx))
				{
					AddRow(Q, nullptr, Q->ContainerPtrToValuePtr<void>(NewObject), nullptr, DefaultPtr(Q, NewDef), Prefix + Q->GetName(), ERadarKind::Added, Ctx, Out);
				}
				continue;
			}
			// 같은 형이면 값 배치도 같다 — P 로 두 쪽을 읽는다 (Q 의 오프셋을 써야 하므로 포인터는 각자 구한다)
			const void* NewPtr = Q->ContainerPtrToValuePtr<void>(NewObject);
			const void* NewDefPtr = DefaultPtr(Q, NewDef);
			DiffValue(P, OldPtr, NewPtr, OldDefPtr, NewDefPtr, Prefix + P->GetName(), Ctx, Out);
		}
		for (TFieldIterator<FProperty> It(NewClass); It; ++It)
		{
			const FProperty* Q = *It;
			if (ShouldSkip(Q, Ctx) || OldClass->FindPropertyByName(Q->GetFName()) || IsUnsaved(Q, Q->ContainerPtrToValuePtr<void>(NewObject), DefaultPtr(Q, NewDef), Ctx))
			{
				continue;
			}
			AddRow(Q, nullptr, Q->ContainerPtrToValuePtr<void>(NewObject), nullptr, DefaultPtr(Q, NewDef), Prefix + Q->GetName(), ERadarKind::Added, Ctx, Out);
		}
	}

	void CountPresent(const UObject* OldObject, const UObject* NewObject, const FRadarCompareContext& Ctx,
		int32& OutOldPresent, int32& OutNewPresent, int32& OutSamePresent)
	{
		OutOldPresent = OutNewPresent = OutSamePresent = 0;
		const UObject* OldDef = OldObject->GetArchetype();
		const UObject* NewDef = NewObject->GetArchetype();
		auto IsPresent = [&Ctx](const FProperty* P, const UObject* Obj, const UObject* Def)
		{
			const void* D = DefaultPtr(P, Def);
			return !D || !Equal(P, P->ContainerPtrToValuePtr<void>(Obj), D, Ctx);
		};
		for (TFieldIterator<FProperty> It(OldObject->GetClass()); It; ++It)
		{
			const FProperty* P = *It;
			if (ShouldSkip(P, Ctx))
			{
				continue;
			}
			const bool bOld = IsPresent(P, OldObject, OldDef);
			OutOldPresent += bOld ? 1 : 0;
			const FProperty* Q = NewObject->GetClass()->FindPropertyByName(P->GetFName());
			if (bOld && SameShape(P, Q) && IsPresent(Q, NewObject, NewDef)
				&& Equal(P, P->ContainerPtrToValuePtr<void>(OldObject), Q->ContainerPtrToValuePtr<void>(NewObject), Ctx))
			{
				++OutSamePresent;
			}
		}
		for (TFieldIterator<FProperty> It(NewObject->GetClass()); It; ++It)
		{
			if (!ShouldSkip(*It, Ctx) && IsPresent(*It, NewObject, NewDef))
			{
				++OutNewPresent;
			}
		}
	}
}
