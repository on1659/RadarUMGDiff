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
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "MovieScene.h"
#include "ToolMenus.h"
#include "IDetailsView.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Rendering/DrawElements.h"
#include "Layout/Geometry.h"
#include "Engine/UserInterfaceSettings.h"

// 5.1 = 브랜치 "5.1", 5.2 이상 = 브랜치 "5.8". 엔진 판정은 아래 매크로 하나로만 한다.
#define RADAR_UE51 (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 2)

#if !RADAR_UE51
	#include "AssetDefinition.h"            // FRevisionInfo (5.8: Editor/AssetDefinition/Public/AssetDefinition.h:227)
	#include "WidgetBlueprintEditorUtils.h"
	#include "DetailsDiff.h"
	#include "ContentBrowserMenuContexts.h"
	#include "Settings/WidgetDesignerSettings.h"
#else
	#include "IAssetTypeActions.h"          // FRevisionInfo (5.1: Developer/AssetTools/Public/IAssetTypeActions.h:28)
	#include "WidgetBlueprintEditorUtils.h"
	#include "Kismet2/BlueprintEditorUtils.h" // FMakeClassSpawnableOnScope (5.1: UnrealEd/Public/Kismet2/BlueprintEditorUtils.h:1938)
	#include "PropertyEditorModule.h"
	#include "Modules/ModuleManager.h"
	#include "Misc/ConfigCacheIni.h"
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

	/**
	 * 디자이너와 같은 방식으로 미리보기 UUserWidget 을 만든다 (5.8 FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint).
	 * 5.1 에는 그 함수가 없어 5.8 본문(UMGEditor/Private/WidgetBlueprintEditorUtils.cpp)을 옮긴다. 5.1 디자이너
	 * FWidgetBlueprintEditor::UpdatePreview(WidgetBlueprintEditor.cpp 1603~1660)와 같은 순서다. 최신 위젯 트리 고르기는
	 * 5.1 UpdatePreview 의 "루트가 없으면 트리를 가진 부모 클래스" 규칙을 쓴다(5.8 FindLatestWidgetTree 가 5.1 에 없다).
	 */
	inline UUserWidget* CreatePreviewWidget(UObject* Outer, UWidgetBlueprint* BP, EWidgetDesignFlags Flags)
	{
#if !RADAR_UE51
		FWidgetBlueprintEditorUtils::FCreateWidgetFromBlueprintParams Params;
		Params.FlagsToApply = Flags;
		return FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint(Outer, BP, Params);
#else
		check(Outer);
		check(BP);
		UUserWidget* Created = nullptr;
		{
			FMakeClassSpawnableOnScope TemporarilySpawnable(BP->GeneratedClass);
			Created = NewObject<UUserWidget>(Outer, BP->GeneratedClass);
		}
		Created->ClearFlags(RF_Transactional);
		Created->SetDesignerFlags(Flags);

		UWidgetTree* LatestWidgetTree = BP->WidgetTree;
		if (LatestWidgetTree->RootWidget == nullptr)
		{
			UWidgetBlueprintGeneratedClass* OwningClass = Created->GetWidgetTreeOwningClass();
			if (OwningClass && OwningClass != BP->GeneratedClass)
			{
				LatestWidgetTree = OwningClass->GetWidgetTreeArchetype();
			}
		}

		// 네임드 슬롯 내용: 자식→부모로 모으고, 부모→자식 순서의 맵으로 (5.8 본문과 같은 정렬)
		TArray<TTuple<FName, UWidget*>> NamedSlotContentArray;
		for (UWidgetBlueprint* It = BP; It; It = Cast<UWidgetBlueprint>(It->GeneratedClass->GetSuperClass()->ClassGeneratedBy))
		{
			TArray<FName> SlotNames;
			It->WidgetTree->GetSlotNames(SlotNames);
			for (int32 Index = SlotNames.Num() - 1; Index >= 0; Index--)
			{
				if (UWidget* Content = It->WidgetTree->GetContentForSlot(SlotNames[Index]))
				{
					NamedSlotContentArray.Add(TTuple<FName, UWidget*>(SlotNames[Index], Content));
				}
			}
		}
		TMap<FName, UWidget*> SortedNamedSlotContent;
		for (int32 Index = NamedSlotContentArray.Num() - 1; Index >= 0; Index--)
		{
			SortedNamedSlotContent.Add(NamedSlotContentArray[Index].Key, NamedSlotContentArray[Index].Value);
		}
		Created->DuplicateAndInitializeFromWidgetTree(LatestWidgetTree, SortedNamedSlotContent);
		Created->SetDesignerFlags(Flags);
		return Created;
#endif
	}

	/** 미리보기 정리 (5.8 FWidgetBlueprintEditorUtils::DestroyUserWidget, 5.1 FWidgetBlueprintEditor::DestroyPreview 1576~1577: MarkAsGarbage + ReleaseSlateResources). */
	inline void DestroyPreviewWidget(UUserWidget* Widget)
	{
		if (Widget)
		{
#if !RADAR_UE51
			FWidgetBlueprintEditorUtils::DestroyUserWidget(Widget);
#else
			Widget->MarkAsGarbage();
			Widget->ReleaseSlateResources(true);
#endif
		}
	}

	/** 패키지 안 오브젝트 순회. 5.8 은 EGetObjectsFlags, 5.1 은 bool bIncludeNestedObjects (UObjectHash.h:168). */
	inline void ForEachObjectInPackage(const UPackage* Package, TFunctionRef<bool(UObject*)> Operation, bool bIncludeNested)
	{
#if !RADAR_UE51
		ForEachObjectWithPackage(Package, Operation, bIncludeNested ? EGetObjectsFlags::IncludeNestedObjects : EGetObjectsFlags::None);
#else
		ForEachObjectWithPackage(Package, Operation, bIncludeNested);
#endif
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
			ForEachObjectInPackage(Package, [&BP](UObject* Obj)
			{
				BP = Cast<UWidgetBlueprint>(Obj);
				return BP == nullptr;
			}, false);
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
		ForEachObjectInPackage(Package, [](UObject* Obj)
		{
			Obj->ClearFlags(RF_Standalone);
			return true;
		}, true);
		Package->ClearFlags(RF_Standalone);
		ResetLoaders(Package);
	}

	/** 애니메이션 바인딩 목록. UWidgetAnimation::AnimationBindings — 5.8 WidgetAnimation.h:147, 5.1 같은 파일 :135 (GetBindings :112). */
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
#if !RADAR_UE51
		return TEXT("5.8");
#else
		return TEXT("5.1");
#endif
	}

	/**
	 * 위젯·애니메이션 변수를 더했음을 블루프린트에 알린다. 5.8 은 UWidgetBlueprint::OnVariableAdded 로 WidgetVariableNameToGuidMap 에
	 * GUID 를 넣어야 컴파일러 ensure 가 나지 않는다(WidgetBlueprintCompiler.cpp 792). 5.1 에는 그 맵과 함수가 없다(UMGEditor 전체에 없음).
	 */
	inline void NotifyWidgetVariableAdded(UWidgetBlueprint* BP, FName VariableName)
	{
#if !RADAR_UE51
		BP->OnVariableAdded(VariableName);
#endif
	}

	/** 위젯 변수 이름 변경을 알린다 (5.8 UWidgetBlueprint::OnVariableRenamed — GUID 를 새 이름으로 옮긴다). 5.1 은 할 일 없음. */
	inline void NotifyWidgetVariableRenamed(UWidgetBlueprint* BP, FName OldName, FName NewName)
	{
#if !RADAR_UE51
		BP->OnVariableRenamed(OldName, NewName);
#endif
	}

	/** 위젯 변수 삭제를 알린다 (5.8 UWidgetBlueprint::OnVariableRemoved). 5.1 은 할 일 없음. */
	inline void NotifyWidgetVariableRemoved(UWidgetBlueprint* BP, FName VariableName)
	{
#if !RADAR_UE51
		BP->OnVariableRemoved(VariableName);
#endif
	}

	/** 네임스페이스·키를 가진 지역화 텍스트. 5.1 에는 FText::AsLocalizable_Advanced 가 없어 ChangeKey(Text.h:715)로 같은 Base 히스토리를 만든다. */
	inline FText MakeLocalizedText(const FString& Namespace, const FString& Key, const FString& Source)
	{
#if !RADAR_UE51
		return FText::AsLocalizable_Advanced(Namespace, Key, Source);
#else
		return FText::ChangeKey(Namespace, Key, FText::FromString(Source));
#endif
	}

	/** 스트링 테이블 원문 넣기. 5.8 은 (키, 원문, 개발 메모), 5.1 은 (키, 원문) (StringTableCore.h:117). */
	inline void SetStringTableSource(UStringTable* Table, const TCHAR* Key, const TCHAR* Source)
	{
#if !RADAR_UE51
		Table->GetMutableStringTable()->SetSourceString(Key, Source, FString());
#else
		Table->GetMutableStringTable()->SetSourceString(Key, Source);
#endif
	}

	/** 바인딩 없는(루트) 트랙. 5.8 UMovieScene::GetTracks, 5.1 GetMasterTracks (MovieScene.h:686). */
	inline const TArray<UMovieSceneTrack*>& GetRootTracks(const UMovieScene* MovieScene)
	{
#if !RADAR_UE51
		return MovieScene->GetTracks();
#else
		return MovieScene->GetMasterTracks();
#endif
	}

	/** FScriptSetHelper/FScriptMapHelper 반복자의 내부 인덱스. 5.8 GetInternalIndex(), 5.1 operator* (UnrealType.h 5060~). */
	template <typename IteratorType>
	inline int32 ContainerIndex(const IteratorType& It)
	{
#if !RADAR_UE51
		return It.GetInternalIndex();
#else
		return *It;
#endif
	}

	/** 속성 원소 크기. 5.8 GetElementSize(), 5.1 은 공개 멤버 ElementSize (UnrealType.h:226). */
	inline int32 ElementSizeOf(const FProperty* Property)
	{
#if !RADAR_UE51
		return Property->GetElementSize();
#else
		return Property->ElementSize;
#endif
	}

	/**
	 * 읽기 전용 상세 패널. 5.8 FDetailsDiff::CreateDetailsView(Kismet/Private/DetailsDiff.cpp:149). 5.1 에는 그 정적 함수가 없어
	 * 5.1 FDetailsDiff 생성자(DetailsDiff.cpp 14~40)와 같은 설정을 쓴다: 이름 칸 숨김, 편집 불가, 고급 속성 모두 표시.
	 */
	inline TSharedRef<IDetailsView> CreateReadOnlyDetailsView(const UObject* Object)
	{
#if !RADAR_UE51
		return FDetailsDiff::CreateDetailsView(Object);
#else
		FDetailsViewArgs Args;
		Args.bShowDifferingPropertiesOption = true;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		TSharedRef<IDetailsView> View = EditModule.CreateDetailView(Args);
		View->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateStatic([] { return false; }));
		View->ShowAllAdvancedProperties();
		View->SetObject(const_cast<UObject*>(Object));
		return View;
#endif
	}

	/**
	 * 콘텐츠 브라우저의 위젯 블루프린트 우클릭 메뉴 "ContentBrowser.AssetContextMenu.WidgetBlueprint".
	 * 5.8 은 UE::ContentBrowser::ExtendToolMenu_AssetContextMenu, 5.1 은 같은 이름을 직접 확장한다
	 * (5.1 ContentBrowser/Private/AssetContextMenu.cpp 340~ RegisterMenuHierarchy 가 JoinMenuPaths(기본, 클래스 이름)으로 만든다).
	 */
	inline UToolMenu* ExtendWidgetBlueprintAssetMenu()
	{
#if !RADAR_UE51
		return UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UWidgetBlueprint::StaticClass());
#else
		return UToolMenus::Get()->ExtendMenu(UToolMenus::JoinMenuPaths(TEXT("ContentBrowser.AssetContextMenu"), UWidgetBlueprint::StaticClass()->GetFName()));
#endif
	}

	/** 로컬 좌표 사각형(크기·위치)의 페인트 지오메트리. 5.8 은 FVector2f 인자, 5.1 은 FVector2D 인자 (Geometry.h:320). */
	inline FPaintGeometry PaintGeometry(const FGeometry& Geo, const FVector2D& Size, const FVector2D& Offset)
	{
#if !RADAR_UE51
		return Geo.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Offset)));
#else
		return Geo.ToPaintGeometry(Size, FSlateLayoutTransform(Offset));
#endif
	}

	/**
	 * 점선. 5.8 은 FSlateDrawElement::MakeDashedLines. 5.1 에는 그 함수가 없어(DrawElements.h) 각 변을 DashLength 간격의
	 * 실선 조각으로 나눠 MakeLines 로 그린다(켜짐·꺼짐 같은 길이).
	 */
	inline void MakeDashedLines(FSlateWindowElementList& Out, uint32 Layer, const FPaintGeometry& PaintGeometry, TArray<FVector2f> Points, const FLinearColor& Color, float Thickness, float DashLength)
	{
#if !RADAR_UE51
		FSlateDrawElement::MakeDashedLines(Out, Layer, PaintGeometry, MoveTemp(Points), ESlateDrawEffect::None, Color, Thickness, DashLength);
#else
		const float Dash = FMath::Max(DashLength, 1.f);
		for (int32 i = 0; i + 1 < Points.Num(); ++i)
		{
			const FVector2f A = Points[i];
			const FVector2f Delta = Points[i + 1] - A;
			const float Len = Delta.Size();
			if (Len <= 0.f)
			{
				continue;
			}
			const FVector2f Dir = Delta / Len;
			for (float T = 0.f; T < Len; T += 2.f * Dash)
			{
				TArray<FVector2f> Seg = { A + Dir * T, A + Dir * FMath::Min(T + Dash, Len) };
				FSlateDrawElement::MakeLines(Out, Layer, PaintGeometry, MoveTemp(Seg), ESlateDrawEffect::None, Color, true, Thickness);
			}
		}
#endif
	}

	/**
	 * 디자이너 기본 미리보기 해상도. 5.8 UWidgetDesignerSettings::DefaultPreviewResolution. 5.1 디자이너는
	 * 에디터 프로젝트별 ini [UMGEditor.Designer] PreviewWidth/PreviewHeight, 없으면 1280×720 (SDesignerView.cpp 217~219, 873~891).
	 */
	inline FVector2D DefaultPreviewResolution()
	{
#if !RADAR_UE51
		const FUintVector2 Res = GetDefault<UWidgetDesignerSettings>()->DefaultPreviewResolution;
		return FVector2D(Res.X, Res.Y);
#else
		int32 Width = 1280;
		int32 Height = 720;
		GConfig->GetInt(TEXT("UMGEditor.Designer"), TEXT("PreviewWidth"), Width, GEditorPerProjectIni);
		GConfig->GetInt(TEXT("UMGEditor.Designer"), TEXT("PreviewHeight"), Height, GEditorPerProjectIni);
		return FVector2D(Width, Height);
#endif
	}

	/**
	 * 미리보기 영역·크기 (디자인 크기 모드 반영). 5.8 은 FWidgetBlueprintEditorUtils::GetWidgetPreviewAreaAndSize.
	 * 5.1 에도 같은 함수가 있으나 FWidgetBlueprintEditorUtils 가 UMGEDITOR_API 가 아니라(5.1 WidgetBlueprintEditorUtils.h:21) 링크할 수 없어
	 * 5.1 본문(WidgetBlueprintEditorUtils.cpp 2068~2115)을 옮긴다.
	 */
	inline TTuple<FVector2D, FVector2D> WidgetPreviewAreaAndSize(UUserWidget* UserWidget, FVector2D DesiredSize, FVector2D PreviewSize, EDesignPreviewSizeMode SizeMode)
	{
#if !RADAR_UE51
		return FWidgetBlueprintEditorUtils::GetWidgetPreviewAreaAndSize(UserWidget, DesiredSize, PreviewSize, SizeMode, TOptional<FVector2D>());
#else
		FVector2D Size(PreviewSize.X, PreviewSize.Y);
		FVector2D Area(PreviewSize.X, PreviewSize.Y);
		if (UserWidget)
		{
			switch (SizeMode)
			{
			case EDesignPreviewSizeMode::Custom:
				Area = UserWidget->DesignTimeSize;
				if (Area.X == 0) { Area.X = DesiredSize.X; }
				if (Area.Y == 0) { Area.Y = DesiredSize.Y; }
				Size = Area;
				break;
			case EDesignPreviewSizeMode::CustomOnScreen:
				Size = UserWidget->DesignTimeSize;
				if (Size.X == 0) { Size.X = DesiredSize.X; }
				if (Size.Y == 0) { Size.Y = DesiredSize.Y; }
				return TTuple<FVector2D, FVector2D>(Area, Size);
			case EDesignPreviewSizeMode::Desired:
				Area = DesiredSize;
				Size = DesiredSize;
				return TTuple<FVector2D, FVector2D>(Area, Size);
			case EDesignPreviewSizeMode::DesiredOnScreen:
				Size = DesiredSize;
				return TTuple<FVector2D, FVector2D>(Area, Size);
			case EDesignPreviewSizeMode::FillScreen:
				break;
			}
		}
		return TTuple<FVector2D, FVector2D>(Area, Size);
#endif
	}

	/** 디자이너 미리보기 DPI 배율. 5.8 FWidgetBlueprintEditorUtils::GetWidgetPreviewDPIScale, 5.1 은 같은 이유로 본문(같은 파일 2117~2129)을 옮긴다. */
	inline float WidgetPreviewDPIScale(UUserWidget* UserWidget, FVector2D PreviewSize)
	{
#if !RADAR_UE51
		return FWidgetBlueprintEditorUtils::GetWidgetPreviewDPIScale(UserWidget, PreviewSize);
#else
		if (UserWidget && (UserWidget->DesignSizeMode == EDesignPreviewSizeMode::Custom || UserWidget->DesignSizeMode == EDesignPreviewSizeMode::Desired))
		{
			return 1.0f;
		}
		return GetDefault<UUserInterfaceSettings>(UUserInterfaceSettings::StaticClass())->GetDPIScaleBasedOnSize(FIntPoint(PreviewSize.X, PreviewSize.Y));
#endif
	}
}
