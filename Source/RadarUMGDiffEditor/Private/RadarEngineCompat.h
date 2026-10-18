// Copyright Radar. All Rights Reserved.

#pragma once

// 엔진 버전 차이를 이 파일 하나에 모은다 (dev 문서 §3.3). 다른 파일에는 #if ENGINE_* 를 쓰지 않는다.

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Misc/PackagePath.h"
#include "Misc/PackageName.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/Package.h"
#include "Engine/Level.h"
#include "Components/Widget.h"
#include "UObject/UObjectHash.h"
#include "UObject/Linker.h"
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2
	#include "AssetDefinition.h"            // FRevisionInfo (5.8: Editor/AssetDefinition/Public/AssetDefinition.h:227)
	#include "WidgetBlueprintEditorUtils.h"
#else
	#error "ue 5.1 분기는 M5 에서 채운다 (dev 문서 §3.3 · §15 M5)"
#endif

class UUserWidget;
class UWidgetBlueprint;

namespace RadarCompat
{
	/**
	 * 옛 리비전 패키지를 diff 용으로 로드한다. 5.8 DiffUtils::LoadPackageForDiff(FPackagePath, FPackagePath) 본문
	 * (UnrealEd/Private/DiffUtils.cpp:1270) 을 그대로 옮겼다 — 5.1 에는 이 함수가 없어 두 브랜치가 같은 본문을 쓰기 위함.
	 * 임시 패키지(/Temp/ 마운트 = Saved/)가 아니면 보통 로드로 떨어지므로, 호출자는 반드시 Saved/ 아래 파일을 넘긴다.
	 */
	inline UPackage* LoadPackageForDiff(const FPackagePath& TempPackagePath, const FPackagePath& OriginalPackagePath)
	{
		if (!FPackageName::IsTempPackage(TempPackagePath.GetPackageName()))
		{
			return LoadPackage(nullptr, *TempPackagePath.GetPackageName(), LOAD_None);
		}

		FLinkerInstancingContext Context;
		if (!OriginalPackagePath.GetLocalFullPath().IsEmpty())
		{
			Context.AddTag(ULevel::DontLoadExternalObjectsTag);
			Context.AddPackageMapping(OriginalPackagePath.GetPackageFName(), TempPackagePath.GetPackageFName());
		}

		return LoadPackage(nullptr, *TempPackagePath.GetPackageName(),
			LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks, nullptr, &Context);
	}

	/** 디자이너와 같은 방식으로 미리보기 UUserWidget 을 만든다 (5.8 FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint). */
	inline UUserWidget* CreatePreviewWidget(UObject* Outer, UWidgetBlueprint* BP, EWidgetDesignFlags Flags)
	{
		FWidgetBlueprintEditorUtils::FCreateWidgetFromBlueprintParams Params;
		Params.FlagsToApply = Flags;
		return FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint(Outer, BP, Params);
	}

	/** 미리보기 정리 (5.8 FWidgetBlueprintEditorUtils::DestroyUserWidget: MarkAsGarbage + ReleaseSlateResources). */
	inline void DestroyPreviewWidget(UUserWidget* Widget)
	{
		if (Widget)
		{
			FWidgetBlueprintEditorUtils::DestroyUserWidget(Widget);
		}
	}

	/** 패키지 안의 위젯 블루프린트 (애셋 우선, 없으면 최상위 오브젝트 중 첫 번째). */
	inline UWidgetBlueprint* FindWidgetBlueprintInPackage(UPackage* Package)
	{
		if (!Package)
		{
			return nullptr;
		}
		UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(Package->FindAssetInPackage());
		if (!BP)
		{
			ForEachObjectWithPackage(Package, [&BP](UObject* Obj)
			{
				BP = Cast<UWidgetBlueprint>(Obj);
				return BP == nullptr;
			}, EGetObjectsFlags::None);
		}
		return BP;
	}

	/**
	 * 임시 패키지를 놓는다. 에디터 GC 는 RF_Standalone(에셋 기본 플래그)을 가진 오브젝트를 참조가 없어도 남겨 두므로
	 * (obj refs: "not currently reachable but it does have some of GARBAGE_COLLECTION_KEEPFLAGS set", 2026-10-18 실측)
	 * 패키지 안 오브젝트의 RF_Standalone 을 떼고 링커를 끊는다. 아직 참조가 남아 있으면 GC 가 거두지 않으므로 강제로 끊지는 않는다.
	 */
	inline void ReleaseTempPackage(UPackage* Package)
	{
		if (!Package || !FPackageName::IsTempPackage(Package->GetName()))
		{
			return;
		}
		ForEachObjectWithPackage(Package, [](UObject* Obj)
		{
			Obj->ClearFlags(RF_Standalone);
			return true;
		}, EGetObjectsFlags::IncludeNestedObjects);
		Package->ClearFlags(RF_Standalone);
		ResetLoaders(Package);
	}

	/** 애니메이션 바인딩 목록. 5.8 은 UWidgetAnimation::AnimationBindings(WidgetAnimation.h:147), 5.1 은 UWidgetBlueprint 쪽에 있다(§7.1-5). */
	inline const TArray<FWidgetAnimationBinding>& GetAnimationBindings(const UWidgetAnimation* Anim)
	{
		return Anim->GetBindings();
	}

	inline FString EngineVersionString()
	{
		return FString::Printf(TEXT("%d.%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION);
	}

	/** 플러그인 저장소의 브랜치 이름 (사용자 결정 2026-10-18: "5.8", "5.1"). */
	inline const TCHAR* PluginBranch()
	{
		return TEXT("5.8");
	}
}
