// Copyright Radar. All Rights Reserved.

#pragma once

// FProperty 순회 · 비교 · 텍스트화 · JSON 값 (dev 문서 §7.2). Python diff.py 와 같은 규칙.

#include "CoreMinimal.h"
#include "RadarDiffModel.h"

class UObject;
class UStruct;
class FProperty;

struct FRadarCompareContext
{
	/** 임시 패키지 이름 → 원래 패키지 이름 (같은 패키지 안 참조가 리비전마다 다른 이름으로 보이지 않게). */
	TArray<TPair<FString, FString>> PackageRemap;

	TSet<FName> IgnoredProperties;
	TSet<FName> AutoProperties;
	TSet<FName> LeafStructs;
	/** 구조 정보라 속성 비교에서 빼는 이름 (Slot, Slots, Parent, Content …). 호출자가 대상마다 채운다. */
	TSet<FName> StructuralProperties;
	double FloatTolerance = 1e-6;
	double DoubleTolerance = 1e-12;

	static FRadarCompareContext FromSettings();

	FString NormalizePath(const FString& Path) const;
	FString ObjectPath(const UObject* Object) const;
};

namespace RadarProperty
{
	FString TypeString(const FProperty* Property);
	FString Category(const FProperty* Property);
	bool ShouldSkip(const FProperty* Property, const FRadarCompareContext& Ctx);
	bool IsLeafStruct(const FProperty* Property, const FRadarCompareContext& Ctx);

	/** 값 하나의 JSON (§11.7 형). ValuePtr 는 속성 값 자체(컨테이너 아님). */
	TSharedPtr<FJsonValue> ToJson(const FProperty* Property, const void* ValuePtr, const FRadarCompareContext& Ctx);
	FString ToText(const FProperty* Property, const void* ValuePtr, const FRadarCompareContext& Ctx);

	/** 정규화한 값으로 같은지 (허용 오차, FText 는 source+테이블, 오브젝트는 정규화 경로). */
	bool Equal(const FProperty* Property, const void* A, const void* B, const FRadarCompareContext& Ctx);
	/** 값 하나 (state·text·value·asset). DefaultPtr 가 있고 같으면 state = default. */
	FRadarValue MakeValue(const FProperty* Property, const void* ValuePtr, const void* DefaultPtr, const FRadarCompareContext& Ctx);

	/**
	 * 두 컨테이너의 속성 차이를 줄로 모은다. Defaults 는 각 쪽의 아키타입 컨테이너(없으면 null) — default 상태 판정용.
	 * 잎이 아닌 구조체는 멤버로 내려가고, 배열은 인덱스 단위로 비교한다.
	 */
	void DiffStruct(const UStruct* Struct, const void* OldContainer, const void* NewContainer,
		const void* OldDefaults, const void* NewDefaults, const FString& Prefix,
		const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out);

	/**
	 * 두 오브젝트를 속성 이름으로 짝지어 비교한다 (클래스가 달라도 된다 — 리비전마다 따로 로드된 BlueprintGeneratedClass,
	 * 같은 이름의 위젯이 클래스를 바꾼 경우). 한쪽에만 있는 속성은 Added/Removed 줄. 아키타입이 default 판정 기준이다.
	 */
	void DiffObjects(const UObject* OldObject, const UObject* NewObject, const FString& Prefix,
		const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out);

	/** 한쪽의 전체 속성 (state.json). Old 칸만 채운다. */
	void DumpStruct(const UStruct* Struct, const void* Container, const void* Defaults, const FString& Prefix,
		const FRadarCompareContext& Ctx, TArray<FRadarPropertyDiff>& Out);

	/** 아키타입과 다른(직렬화될) 속성 수와, 두 쪽이 같은 값으로 가진 수 — 이름 변경 추정 점수용. 이름으로 짝짓는다. */
	void CountPresent(const UObject* OldObject, const UObject* NewObject, const FRadarCompareContext& Ctx,
		int32& OutOldPresent, int32& OutNewPresent, int32& OutSamePresent);
}
