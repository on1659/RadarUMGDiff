// Copyright Radar. All Rights Reserved.

// 픽스처 위젯 블루프린트 생성기 (dev 문서 §13). 콘솔: RadarUMGDiff.BuildFixtures [Anim|Graph|Text|Bind|Layout|All]
// 버전마다 앞 버전을 고쳐서(같은 GUID 를 유지해야 이동·이름 변경을 볼 수 있다) 저장하고,
// 저장된 .uasset 을 Plugins/RadarUMGDiff/Test/Fixtures/<이름>/v<N>/ 에 복사한다.
// 5.1 브랜치(M5)에서도 같은 명령으로 그 엔진이 저장한 픽스처를 다시 만든다.

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "RadarEngineCompat.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/CanvasPanel.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/WidgetSwitcher.h"
#include "Components/CanvasPanelSlot.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Engine/Texture2D.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ObjectTools.h"
#include "RadarUMGDiffLog.h"

namespace RadarFixtures
{
	static const TCHAR* Root = TEXT("/RadarUMGDiff/Test");
	/** -force: 이미 있는 픽스처 에셋을 지우고 다시 만든다 (이 생성기가 만든 /RadarUMGDiff/Test 의 에셋만). */
	static bool GForce = false;

	FString FixtureDir(const FString& Name, int32 Version)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RadarUMGDiff"));
		return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Test/Fixtures"), Name, FString::Printf(TEXT("v%d"), Version));
	}

	bool SavePackageOf(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *File, Args);
	}

	/** 컴파일·저장하고 픽스처 폴더에 복사한다. */
	bool Snapshot(UWidgetBlueprint* BP, int32 Version)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (!SavePackageOf(BP))
		{
			UE_LOG(LogRadarUMGDiff, Error, TEXT("픽스처 저장 실패: %s"), *BP->GetPathName());
			return false;
		}
		const FString Name = BP->GetName();
		const FString Src = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		const FString Dst = FPaths::Combine(FixtureDir(Name, Version), Name + TEXT(".uasset"));
		if (IFileManager::Get().Copy(*Dst, *Src) != COPY_OK)
		{
			UE_LOG(LogRadarUMGDiff, Error, TEXT("픽스처 복사 실패: %s → %s"), *Src, *Dst);
			return false;
		}
		UE_LOG(LogRadarUMGDiff, Display, TEXT("픽스처 %s v%d 저장: %s"), *Name, Version, *Dst);
		return true;
	}

	/** 새 위젯 블루프린트 (루트 기본 Canvas "Root"). 같은 이름이 이미 있으면 -force 일 때만 지우고 다시 만든다. */
	UWidgetBlueprint* Create(const FString& Name, UClass* RootClass = UCanvasPanel::StaticClass(), const TCHAR* RootName = TEXT("Root"))
	{
		const FString PackageName = FString::Printf(TEXT("%s/%s"), Root, *Name);
		if (FPackageName::DoesPackageExist(PackageName) || FindObject<UPackage>(nullptr, *PackageName))
		{
			UObject* Existing = GForce ? LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *Name)) : nullptr;
			if (!Existing || ObjectTools::ForceDeleteObjects({ Existing }, false) != 1)
			{
				UE_LOG(LogRadarUMGDiff, Error, TEXT("%s 가 이미 있습니다. -force 로 다시 만드세요."), *PackageName);
				return nullptr;
			}
		}
		UPackage* Package = CreatePackage(*PackageName);
		UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(), Package, FName(*Name),
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		if (!BP)
		{
			return nullptr;
		}
		UPanelWidget* RootPanel = BP->WidgetTree->ConstructWidget<UPanelWidget>(RootClass, RootName);
		BP->WidgetTree->RootWidget = RootPanel;
		RadarCompat::NotifyWidgetVariableAdded(BP, RootPanel->GetFName());
		FAssetRegistryModule::AssetCreated(BP);
		return BP;
	}

	template <typename T>
	T* Add(UWidgetBlueprint* BP, UPanelWidget* Parent, const TCHAR* Name)
	{
		T* W = BP->WidgetTree->ConstructWidget<T>(T::StaticClass(), FName(Name));
		Parent->AddChild(W);
		W->bIsVariable = true;
		// 에디터가 위젯·애니메이션을 더할 때처럼 이름 → GUID 표에 올린다 (WidgetBlueprintCompiler.cpp 792 의 ensure)
		RadarCompat::NotifyWidgetVariableAdded(BP, W->GetFName());
		return W;
	}

	UPanelWidget* RootOf(UWidgetBlueprint* BP)
	{
		return Cast<UPanelWidget>(BP->WidgetTree->RootWidget);
	}

	UWidget* Find(UWidgetBlueprint* BP, const TCHAR* Name)
	{
		return BP->WidgetTree->FindWidget(FName(Name));
	}

	// ── 애니메이션 ──

	UWidgetAnimation* NewAnimation(UWidgetBlueprint* BP, const TCHAR* Name, int32 EndDisplayFrame)
	{
		UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(BP, FName(Name), RF_Transactional);
		Anim->SetDisplayLabel(Name);
		Anim->MovieScene = NewObject<UMovieScene>(Anim, FName(Name), RF_Transactional);
		Anim->MovieScene->SetDisplayRate(FFrameRate(20, 1));
		const FFrameRate Tick = Anim->MovieScene->GetTickResolution();
		const FFrameNumber End = FFrameRate::TransformTime(FFrameTime(EndDisplayFrame), FFrameRate(20, 1), Tick).FloorToFrame();
		Anim->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(0, End + 1));
		BP->Animations.Add(Anim);
		RadarCompat::NotifyWidgetVariableAdded(BP, Anim->GetFName());
		return Anim;
	}

	/** 표시 프레임(20fps) → 틱 프레임. */
	FFrameNumber Ticks(UWidgetAnimation* Anim, int32 DisplayFrame)
	{
		return FFrameRate::TransformTime(FFrameTime(DisplayFrame), Anim->MovieScene->GetDisplayRate(), Anim->MovieScene->GetTickResolution()).FloorToFrame();
	}

	FGuid Bind(UWidgetAnimation* Anim, UWidget* Widget)
	{
		const FGuid Guid = Anim->MovieScene->AddPossessable(Widget->GetName(), Widget->GetClass());
		FWidgetAnimationBinding B;
		B.WidgetName = Widget->GetFName();
		B.AnimationGuid = Guid;
		B.bIsRootWidget = false;
		Anim->AnimationBindings.Add(B);
		return Guid;
	}

	UMovieSceneSection* AddTrack(UWidgetAnimation* Anim, const FGuid& Guid, UClass* TrackClass, const TCHAR* Property, int32 EndDisplayFrame)
	{
		UMovieScenePropertyTrack* Track = Cast<UMovieScenePropertyTrack>(Anim->MovieScene->AddTrack(TrackClass, Guid));
		Track->SetPropertyNameAndPath(FName(Property), Property);
		UMovieSceneSection* Section = Track->CreateNewSection();
		Section->SetRange(TRange<FFrameNumber>(0, Ticks(Anim, EndDisplayFrame) + 1));
		Track->AddSection(*Section);
		return Section;
	}

	/** 섹션의 float 채널 (리플렉션 — diff 쪽과 같은 방식). */
	FMovieSceneFloatChannel* Channel(UMovieSceneSection* Section, const TCHAR* Property, int32 Index = 0)
	{
		const FStructProperty* P = CastField<FStructProperty>(Section->GetClass()->FindPropertyByName(FName(Property)));
		return P ? P->ContainerPtrToValuePtr<FMovieSceneFloatChannel>(Section, Index) : nullptr;
	}

	void Keys(UWidgetAnimation* Anim, FMovieSceneFloatChannel* C, std::initializer_list<TPair<int32, float>> In)
	{
		for (const TPair<int32, float>& K : In)
		{
			C->AddCubicKey(Ticks(Anim, K.Key), K.Value);
		}
	}

	UWidgetAnimation* FindAnimation(UWidgetBlueprint* BP, const TCHAR* Name)
	{
		for (UWidgetAnimation* A : BP->Animations)
		{
			if (A && A->GetName() == Name) { return A; }
		}
		return nullptr;
	}

	UMovieSceneSection* FindSection(UWidgetAnimation* Anim, const TCHAR* Widget, UClass* TrackClass)
	{
		for (const FWidgetAnimationBinding& B : Anim->AnimationBindings)
		{
			if (B.WidgetName != FName(Widget)) { continue; }
			if (UMovieSceneTrack* T = Anim->MovieScene->FindTrack(TrackClass, B.AnimationGuid))
			{
				return T->GetAllSections().Num() > 0 ? T->GetAllSections()[0] : nullptr;
			}
		}
		return nullptr;
	}

	int32 KeyIndexAt(UWidgetAnimation* Anim, FMovieSceneFloatChannel* C, int32 DisplayFrame)
	{
		return C->GetData().GetTimes().IndexOfByKey(Ticks(Anim, DisplayFrame));
	}

	bool BuildAnim()
	{
		const TCHAR* Name = TEXT("WBP_Fix_Anim");
		UWidgetBlueprint* BP = Create(Name);
		if (!BP) { return false; }
		UBorder* Panel = Add<UBorder>(BP, RootOf(BP), TEXT("Panel"));
		UTextBlock* Label = Add<UTextBlock>(BP, RootOf(BP), TEXT("Label"));
		Label->SetText(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_ANIM_LABEL"), TEXT("움직이는 글자")));
		UImage* Icon = Add<UImage>(BP, RootOf(BP), TEXT("Icon"));

		// v1: FadeIn (Panel 위치·Label 색·Panel 불투명도), Pulse (Icon 크기) — 키 4개씩
		UWidgetAnimation* FadeIn = NewAnimation(BP, TEXT("FadeIn"), 30);
		{
			UMovieSceneSection* T = AddTrack(FadeIn, Bind(FadeIn, Panel), UMovieScene2DTransformTrack::StaticClass(), TEXT("RenderTransform"), 30);
			Keys(FadeIn, Channel(T, TEXT("Translation"), 0), { {0, 0.f}, {10, 40.f}, {20, 80.f}, {30, 120.f} });
			UMovieSceneSection* C = AddTrack(FadeIn, Bind(FadeIn, Label), UMovieSceneColorTrack::StaticClass(), TEXT("ColorAndOpacity"), 30);
			Keys(FadeIn, Channel(C, TEXT("RedCurve")), { {0, 1.f}, {10, 0.8f}, {20, 0.6f}, {30, 0.4f} });
			Keys(FadeIn, Channel(C, TEXT("AlphaCurve")), { {0, 0.f}, {10, 0.5f}, {20, 1.f}, {30, 1.f} });
			const FGuid PanelGuid = FadeIn->AnimationBindings[0].AnimationGuid;
			UMovieSceneSection* O = AddTrack(FadeIn, PanelGuid, UMovieSceneFloatTrack::StaticClass(), TEXT("RenderOpacity"), 30);
			Keys(FadeIn, Channel(O, TEXT("FloatCurve")), { {0, 0.f}, {10, 0.3f}, {20, 0.7f}, {30, 1.f} });
		}
		UWidgetAnimation* Pulse = NewAnimation(BP, TEXT("Pulse"), 15);
		{
			UMovieSceneSection* T = AddTrack(Pulse, Bind(Pulse, Icon), UMovieScene2DTransformTrack::StaticClass(), TEXT("RenderTransform"), 15);
			Keys(Pulse, Channel(T, TEXT("Scale"), 0), { {0, 1.f}, {5, 1.2f}, {10, 1.f}, {15, 1.2f} });
			Keys(Pulse, Channel(T, TEXT("Scale"), 1), { {0, 1.f}, {5, 1.2f}, {10, 1.f}, {15, 1.2f} });
		}
		if (!Snapshot(BP, 1)) { return false; }

		// v2: 키 값 — FadeIn 위치 20프레임 80→90, 불투명도 10프레임 보간 Cubic→Constant, Pulse Scale.Y 15프레임 1.2→1.3
		{
			FMovieSceneFloatChannel* X = Channel(FindSection(FadeIn, TEXT("Panel"), UMovieScene2DTransformTrack::StaticClass()), TEXT("Translation"), 0);
			X->GetData().GetValues()[KeyIndexAt(FadeIn, X, 20)].Value = 90.f;
			FMovieSceneFloatChannel* Op = Channel(FindSection(FadeIn, TEXT("Panel"), UMovieSceneFloatTrack::StaticClass()), TEXT("FloatCurve"));
			Op->GetData().GetValues()[KeyIndexAt(FadeIn, Op, 10)].InterpMode = RCIM_Constant;
			FMovieSceneFloatChannel* SY = Channel(FindSection(Pulse, TEXT("Icon"), UMovieScene2DTransformTrack::StaticClass()), TEXT("Scale"), 1);
			SY->GetData().GetValues()[KeyIndexAt(Pulse, SY, 15)].Value = 1.3f;
		}
		if (!Snapshot(BP, 2)) { return false; }

		// v3: 프레임 이동 — FadeIn 불투명도 30프레임 키를 45로, 섹션 끝도 45로
		{
			UMovieSceneSection* OpSection = FindSection(FadeIn, TEXT("Panel"), UMovieSceneFloatTrack::StaticClass());
			FMovieSceneFloatChannel* Op = Channel(OpSection, TEXT("FloatCurve"));
			Op->GetData().MoveKey(KeyIndexAt(FadeIn, Op, 30), Ticks(FadeIn, 45));
			OpSection->SetRange(TRange<FFrameNumber>(0, Ticks(FadeIn, 45) + 1));
		}
		if (!Snapshot(BP, 3)) { return false; }

		// v4: 트랙 삭제(FadeIn 의 Label 색) + 애니메이션 추가(SlideOut)
		{
			for (const FWidgetAnimationBinding& B : FadeIn->AnimationBindings)
			{
				if (B.WidgetName == FName(TEXT("Label")))
				{
					if (UMovieSceneTrack* T = FadeIn->MovieScene->FindTrack(UMovieSceneColorTrack::StaticClass(), B.AnimationGuid))
					{
						FadeIn->MovieScene->RemoveTrack(*T);
					}
				}
			}
			UWidgetAnimation* Slide = NewAnimation(BP, TEXT("SlideOut"), 20);
			UMovieSceneSection* T = AddTrack(Slide, Bind(Slide, Panel), UMovieScene2DTransformTrack::StaticClass(), TEXT("RenderTransform"), 20);
			Keys(Slide, Channel(T, TEXT("Translation"), 1), { {0, 0.f}, {5, -20.f}, {10, -60.f}, {20, -200.f} });
		}
		return Snapshot(BP, 4);
	}

	// ── 그래프 ──

	FEdGraphPinType PinType(FName Category, UObject* SubObject = nullptr, EPinContainerType Container = EPinContainerType::None)
	{
		FEdGraphPinType T;
		T.PinCategory = Category;
		T.PinSubCategoryObject = SubObject;
		T.ContainerType = Container;
		return T;
	}

	FEdGraphPinType RealType()
	{
		FEdGraphPinType T = PinType(UEdGraphSchema_K2::PC_Real);
		T.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		return T;
	}

	template <typename T>
	T* FindNode(UEdGraph* Graph)
	{
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (T* Typed = Cast<T>(N)) { return Typed; }
		}
		return nullptr;
	}

	UEdGraph* AddFunction(UWidgetBlueprint* BP, const TCHAR* Name)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, FName(Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, Graph, true, nullptr);
		return Graph;
	}

	UK2Node_FunctionResult* AddResult(UEdGraph* Graph, int32 X)
	{
		FGraphNodeCreator<UK2Node_FunctionResult> Creator(*Graph);
		UK2Node_FunctionResult* Result = Creator.CreateNode();
		Result->NodePosX = X;
		Creator.Finalize();
		if (UK2Node_FunctionEntry* Entry = FindNode<UK2Node_FunctionEntry>(Graph))
		{
			Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		}
		return Result;
	}

	UK2Node_CallFunction* AddCall(UEdGraph* Graph, int32 X, int32 Y, TFunctionRef<void(UK2Node_CallFunction*)> Setup)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Call = Creator.CreateNode();
		Setup(Call);
		Call->NodePosX = X;
		Call->NodePosY = Y;
		Creator.Finalize();
		return Call;
	}

	bool BuildGraph()
	{
		const TCHAR* Name = TEXT("WBP_Fix_Graph");
		UWidgetBlueprint* BP = Create(Name);
		if (!BP) { return false; }
		Add<UTextBlock>(BP, RootOf(BP), TEXT("ScoreText"));

		// v1: 변수 3개(int · TMap<Name,int> · TArray<Vector2D>), 함수 AddScore(Amount → NewScore), 커스텀 이벤트 OnHit(Damage) → AddScore 호출
		FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Score"), PinType(UEdGraphSchema_K2::PC_Int), TEXT("3"));
		FEdGraphPinType MapType = PinType(UEdGraphSchema_K2::PC_Name, nullptr, EPinContainerType::Map);
		MapType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Int;
		FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Tags"), MapType);
		FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Points"), PinType(UEdGraphSchema_K2::PC_Struct, TBaseStructure<FVector2D>::Get(), EPinContainerType::Array));

		UEdGraph* Func = AddFunction(BP, TEXT("AddScore"));
		FindNode<UK2Node_FunctionEntry>(Func)->CreateUserDefinedPin(TEXT("Amount"), PinType(UEdGraphSchema_K2::PC_Int), EGPD_Output);
		AddResult(Func, 400)->CreateUserDefinedPin(TEXT("NewScore"), PinType(UEdGraphSchema_K2::PC_Int), EGPD_Input);

		UEdGraph* Events = FBlueprintEditorUtils::FindEventGraph(BP);
		FGraphNodeCreator<UK2Node_CustomEvent> EventCreator(*Events);
		UK2Node_CustomEvent* OnHit = EventCreator.CreateNode();
		OnHit->CustomFunctionName = TEXT("OnHit");
		OnHit->NodePosX = 0;
		OnHit->NodePosY = 400;
		EventCreator.Finalize();
		OnHit->CreateUserDefinedPin(TEXT("Damage"), RealType(), EGPD_Output);

		FKismetEditorUtilities::CompileBlueprint(BP);
		UK2Node_CallFunction* CallAdd = AddCall(Events, 300, 400, [](UK2Node_CallFunction* N) { N->FunctionReference.SetSelfMember(TEXT("AddScore")); });
		OnHit->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(CallAdd->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		CallAdd->FindPinChecked(TEXT("Amount"))->DefaultValue = TEXT("5");
		if (!Snapshot(BP, 1)) { return false; }

		// v2: 노드 추가 — AddScore 뒤에 PrintString
		UK2Node_CallFunction* Print = AddCall(Events, 600, 400, [](UK2Node_CallFunction* N)
		{
			N->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
		});
		CallAdd->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(Print->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		if (!Snapshot(BP, 2)) { return false; }

		// v3: 핀 기본값 — Amount 5 → 10
		CallAdd->FindPinChecked(TEXT("Amount"))->DefaultValue = TEXT("10");
		if (!Snapshot(BP, 3)) { return false; }

		// v4: 노드 이동만(OnHit X +200) + 변수 이름 변경(Score → TotalScore, VarGuid 유지)
		OnHit->NodePosX += 200;
		FBlueprintEditorUtils::RenameMemberVariable(BP, TEXT("Score"), TEXT("TotalScore"));
		if (!Snapshot(BP, 4)) { return false; }

		// v5: 변수 형 변경 — Points 의 원소 구조체 Vector2D → Vector (핀 타입 구조 JSON 비교용)
		FBlueprintEditorUtils::ChangeMemberVariableType(BP, TEXT("Points"), PinType(UEdGraphSchema_K2::PC_Struct, TBaseStructure<FVector>::Get(), EPinContainerType::Array));
		return Snapshot(BP, 5);
	}

	// ── 텍스트 ──

	static const TCHAR* StringTableId = TEXT("/RadarUMGDiff/Test/ST_Fix.ST_Fix");

	bool EnsureStringTable()
	{
		const FString PackageName = FString::Printf(TEXT("%s/ST_Fix"), Root);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			return LoadObject<UStringTable>(nullptr, StringTableId) != nullptr;
		}
		UPackage* Package = CreatePackage(*PackageName);
		UStringTable* Table = NewObject<UStringTable>(Package, TEXT("ST_Fix"), RF_Public | RF_Standalone);
		Table->GetMutableStringTable()->SetNamespace(TEXT("RadarFix"));
		RadarCompat::SetStringTableSource(Table, TEXT("Greeting"), TEXT("안녕하세요"));
		RadarCompat::SetStringTableSource(Table, TEXT("Farewell"), TEXT("안녕히 가세요"));
		FAssetRegistryModule::AssetCreated(Table);
		return SavePackageOf(Table);
	}

	bool BuildText()
	{
		if (!EnsureStringTable()) { return false; }
		const TCHAR* Name = TEXT("WBP_Fix_Text");
		UWidgetBlueprint* BP = Create(Name);
		if (!BP) { return false; }
		UTexture2D* DefaultTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));

		// v1: 한국어 DisplayLabel, 일반 텍스트, 스트링테이블 텍스트, 텍스처 브러시, 소프트 텍스처 변수
		UTextBlock* Title = Add<UTextBlock>(BP, RootOf(BP), TEXT("Title"));
		Title->SetDisplayLabel(TEXT("제목 텍스트"));
		Title->SetText(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_TITLE_1"), TEXT("시작하기")));
		UTextBlock* Greeting = Add<UTextBlock>(BP, RootOf(BP), TEXT("Greeting"));
		Greeting->SetDisplayLabel(TEXT("인사말"));
		Greeting->SetText(FText::FromStringTable(FName(StringTableId), TEXT("Greeting")));
		UImage* Picture = Add<UImage>(BP, RootOf(BP), TEXT("Picture"));
		Picture->SetBrushResourceObject(DefaultTex);
		FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Thumb"), PinType(UEdGraphSchema_K2::PC_SoftObject, UTexture2D::StaticClass()),
			TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		if (!Snapshot(BP, 1)) { return false; }

		// v2: 텍스트 변경 (같은 키, 원문만)
		Title->SetText(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_TITLE_1"), TEXT("지금 시작")));
		if (!Snapshot(BP, 2)) { return false; }

		// v3: 키만 변경 (원문 같음) — Python 만 text-key-only 로 낸다
		Title->SetText(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_TITLE_2"), TEXT("지금 시작")));
		if (!Snapshot(BP, 3)) { return false; }

		// v4: 테이블 키 변경 + 텍스처 교체 (브러시·소프트 변수 기본값)
		Greeting->SetText(FText::FromStringTable(FName(StringTableId), TEXT("Farewell")));
		Picture->SetBrushResourceObject(WhiteTex);
		for (FBPVariableDescription& V : BP->NewVariables)
		{
			if (V.VarName == FName(TEXT("Thumb")))
			{
				V.DefaultValue = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
			}
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		if (!Snapshot(BP, 4)) { return false; }

		// v5: 생성 텍스트 히스토리 — 이름 포맷(AsNumber·정수 인자), 순서 포맷(AsPercent·AsDate·AsCurrency), 대문자 변환
		UTextBlock* Score = Add<UTextBlock>(BP, RootOf(BP), TEXT("Score"));
		auto ScoreText = [](int32 Count)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Count"), FText::AsNumber(Count));
			Args.Add(TEXT("Max"), 7);
			return FText::Format(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_SCORE_FMT"), TEXT("{Count}점 / {Max}")), Args);
		};
		Score->SetText(ScoreText(10));
		UTextBlock* Stamp = Add<UTextBlock>(BP, RootOf(BP), TEXT("Stamp"));
		Stamp->SetText(FText::Format(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_STAMP_FMT"), TEXT("{0} · {1} · {2}")),
			FText::AsPercent(0.5f), FText::AsDate(FDateTime(2026, 10, 18), EDateTimeStyle::Short, TEXT("UTC")), FText::AsCurrencyBase(1234, TEXT("KRW"))));
		UTextBlock* Upper = Add<UTextBlock>(BP, RootOf(BP), TEXT("Upper"));
		Upper->SetText(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_UPPER"), TEXT("hello")).ToUpper());
		if (!Snapshot(BP, 5)) { return false; }

		// v6: 포맷 인자 값만 변경 (Count 10 → 20)
		Score->SetText(ScoreText(20));
		return Snapshot(BP, 6);
	}

	// ── 바인딩 ──

	UEdGraph* AddTextGetter(UWidgetBlueprint* BP, const TCHAR* Name, const TCHAR* Text)
	{
		UEdGraph* Graph = AddFunction(BP, Name);
		UK2Node_FunctionResult* Result = AddResult(Graph, 400);
		UEdGraphPin* Ret = Result->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, PinType(UEdGraphSchema_K2::PC_Text), EGPD_Input);
		Ret->DefaultTextValue = RadarCompat::MakeLocalizedText(TEXT(""), FString::Printf(TEXT("FIX_%s"), Name), Text);
		if (UK2Node_FunctionEntry* Entry = FindNode<UK2Node_FunctionEntry>(Graph))
		{
			Entry->AddExtraFlags(FUNC_BlueprintPure | FUNC_Const);
		}
		return Graph;
	}

	FDelegateEditorBinding FunctionBinding(UWidgetBlueprint* BP, const TCHAR* Widget, const TCHAR* Property, const TCHAR* Function)
	{
		FDelegateEditorBinding B;
		B.ObjectName = Widget;
		B.PropertyName = Property;
		B.FunctionName = Function;
		B.Kind = EBindingKind::Function;
		if (UFunction* F = BP->SkeletonGeneratedClass ? BP->SkeletonGeneratedClass->FindFunctionByName(FName(Function)) : nullptr)
		{
			B.SourcePath = FEditorPropertyPath(TArray<FFieldVariant>{ FFieldVariant(F) });
			UBlueprint::GetGuidFromClassByFieldName<UFunction>(F->GetOwnerClass(), F->GetFName(), B.MemberGuid);
		}
		return B;
	}

	bool BuildBind()
	{
		const TCHAR* Name = TEXT("WBP_Fix_Bind");
		UWidgetBlueprint* BP = Create(Name);
		if (!BP) { return false; }

		// v1: Status.Text ← 함수 GetStatusText, Bar.Percent ← 속성 Health
		Add<UTextBlock>(BP, RootOf(BP), TEXT("Status"));
		Add<UProgressBar>(BP, RootOf(BP), TEXT("Bar"));
		FEdGraphPinType FloatType = PinType(UEdGraphSchema_K2::PC_Real);
		FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Health"), FloatType, TEXT("0.5"));
		AddTextGetter(BP, TEXT("GetStatusText"), TEXT("정상"));
		AddTextGetter(BP, TEXT("GetStatusText2"), TEXT("위험"));
		FKismetEditorUtilities::CompileBlueprint(BP);
		BP->Bindings.Add(FunctionBinding(BP, TEXT("Status"), TEXT("Text"), TEXT("GetStatusText")));
		{
			FDelegateEditorBinding B;
			B.ObjectName = TEXT("Bar");
			B.PropertyName = TEXT("Percent");
			B.SourceProperty = TEXT("Health");
			B.Kind = EBindingKind::Property;
			if (FProperty* P = BP->SkeletonGeneratedClass ? BP->SkeletonGeneratedClass->FindPropertyByName(TEXT("Health")) : nullptr)
			{
				B.SourcePath = FEditorPropertyPath(TArray<FFieldVariant>{ FFieldVariant(P) });
				UBlueprint::GetGuidFromClassByFieldName<FProperty>(BP->SkeletonGeneratedClass, P->GetFName(), B.MemberGuid);
			}
			BP->Bindings.Add(B);
		}
		if (!Snapshot(BP, 1)) { return false; }

		// v2: 바인딩 함수 변경 GetStatusText → GetStatusText2
		BP->Bindings.RemoveAll([](const FDelegateEditorBinding& B) { return B.ObjectName == TEXT("Status"); });
		BP->Bindings.Add(FunctionBinding(BP, TEXT("Status"), TEXT("Text"), TEXT("GetStatusText2")));
		if (!Snapshot(BP, 2)) { return false; }

		// v3: 바인딩 삭제 (Bar.Percent)
		BP->Bindings.RemoveAll([](const FDelegateEditorBinding& B) { return B.ObjectName == TEXT("Bar"); });
		return Snapshot(BP, 3);
	}

	// ── 레이아웃 (M2 의 손 픽스처를 생성기로 옮김, §13) ──

	template <typename T>
	T* AddAt(UWidgetBlueprint* BP, UPanelWidget* Parent, const TCHAR* Name, const FMargin& Offsets)
	{
		T* W = Add<T>(BP, Parent, Name);
		W->SetDisplayLabel(Name);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(W->Slot))
		{
			CanvasSlot->SetOffsets(Offsets);
		}
		return W;
	}

	UTextBlock* AddLabel(UWidgetBlueprint* BP, UPanelWidget* Parent, const TCHAR* Name, const TCHAR* Text, const FMargin& Offsets = FMargin())
	{
		UTextBlock* W = AddAt<UTextBlock>(BP, Parent, Name, Offsets);
		W->SetText(RadarCompat::MakeLocalizedText(TEXT(""), FString::Printf(TEXT("FIX_LAYOUT_%s"), Name), Text));
		return W;
	}

	/** 위젯을 지운다 — 에디터 삭제처럼 트리에서 떼고 임시 패키지로 옮겨 저장본에 남지 않게 한다. */
	void RemoveWidget(UWidgetBlueprint* BP, const TCHAR* Name)
	{
		if (UWidget* W = Find(BP, Name))
		{
			BP->WidgetTree->RemoveWidget(W);
			W->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			RadarCompat::NotifyWidgetVariableRemoved(BP, FName(Name));
		}
	}

	/** 중첩 UserWidget 용: 루트 Border 하나 + 글자 */
	UWidgetBlueprint* BuildChild()
	{
		const TCHAR* Name = TEXT("WBP_Fix_Child");
		UWidgetBlueprint* BP = Create(Name, UBorder::StaticClass(), TEXT("CardBorder"));
		if (!BP) { return nullptr; }
		RootOf(BP)->SetDisplayLabel(TEXT("CardBorder"));
		AddLabel(BP, RootOf(BP), TEXT("CardText"), TEXT("카드"));
		return Snapshot(BP, 1) ? BP : nullptr;
	}

	bool BuildLayout()
	{
		UWidgetBlueprint* Child = BuildChild();
		if (!Child) { return false; }
		const TCHAR* Name = TEXT("WBP_Fix_Layout");
		UWidgetBlueprint* BP = Create(Name);
		if (!BP) { return false; }
		UPanelWidget* Canvas = RootOf(BP);
		Canvas->SetDisplayLabel(TEXT("Root"));

		// v1: Canvas 안 9개(+ 버튼 글자·스위처 두 페이지·그룹 글자), 중첩 UserWidget, 비활성 스위처 페이지, Collapsed 위젯
		AddLabel(BP, Canvas, TEXT("Title"), TEXT("제목 v1"), FMargin(40, 40, 400, 60));
		AddAt<UImage>(BP, Canvas, TEXT("Icon"), FMargin(40, 120, 64, 64));
		UButton* Ok = AddAt<UButton>(BP, Canvas, TEXT("OkButton"), FMargin(40, 220, 200, 50));
		AddLabel(BP, Ok, TEXT("OkLabel"), TEXT("확인"));
		UProgressBar* Hp = AddAt<UProgressBar>(BP, Canvas, TEXT("HpBar"), FMargin(40, 300, 300, 24));
		Hp->SetPercent(0.7f);
		Hp->SetFillColorAndOpacity(FLinearColor(0.f, 0.5f, 1.f, 1.f));
		UTextBlock* Hidden = AddLabel(BP, Canvas, TEXT("HiddenNote"), TEXT("숨김 메모"), FMargin(40, 340, 300, 30));
		Hidden->SetVisibility(ESlateVisibility::Collapsed);
		UWidgetSwitcher* Pages = AddAt<UWidgetSwitcher>(BP, Canvas, TEXT("Pages"), FMargin(400, 120, 300, 200));
		UBorder* PageA = AddAt<UBorder>(BP, Pages, TEXT("PageA"), FMargin());
		AddLabel(BP, PageA, TEXT("PageAText"), TEXT("페이지 A"));
		UBorder* PageB = AddAt<UBorder>(BP, Pages, TEXT("PageB"), FMargin());
		AddLabel(BP, PageB, TEXT("PageBText"), TEXT("페이지 B (비활성)"));
		UOverlay* Group = AddAt<UOverlay>(BP, Canvas, TEXT("Group"), FMargin(400, 360, 300, 100));
		AddLabel(BP, Group, TEXT("GroupText"), TEXT("그룹"));
		{
			UUserWidget* Card = BP->WidgetTree->ConstructWidget<UUserWidget>(Child->GeneratedClass.Get(), TEXT("ChildCard"));
			Canvas->AddChild(Card);
			Card->bIsVariable = true;
			Card->SetDisplayLabel(TEXT("ChildCard"));
			RadarCompat::NotifyWidgetVariableAdded(BP, Card->GetFName());
			Cast<UCanvasPanelSlot>(Card->Slot)->SetOffsets(FMargin(40, 420, 200, 80));
		}
		AddLabel(BP, Canvas, TEXT("Footer"), TEXT("꼬리말"), FMargin(40, 520, 600, 30));
		if (!Snapshot(BP, 1)) { return false; }

		// v2: 이름 변경(같은 자리, Title → Heading, 색 변경) · 이동+슬롯 클래스 변경(Icon: Canvas → Group[0]) · 삭제 3 · 텍스트 변경
		UTextBlock* Title = Cast<UTextBlock>(Find(BP, TEXT("Title")));
		Title->Rename(TEXT("Heading"));
		Title->SetDisplayLabel(TEXT("Heading"));
		RadarCompat::NotifyWidgetVariableRenamed(BP, TEXT("Title"), TEXT("Heading"));
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.6f, 0.2f, 1.f)));
		UWidget* Icon = Find(BP, TEXT("Icon"));
		Icon->RemoveFromParent();
		Group->InsertChildAt(0, Icon);
		RemoveWidget(BP, TEXT("HpBar"));
		RemoveWidget(BP, TEXT("HiddenNote"));
		RemoveWidget(BP, TEXT("PageBText"));
		Cast<UTextBlock>(Find(BP, TEXT("OkLabel")))->SetText(RadarCompat::MakeLocalizedText(TEXT(""), TEXT("FIX_LAYOUT_OkLabel2"), TEXT("확인하기")));
		return Snapshot(BP, 2);
	}

	void Run(const TArray<FString>& Args)
	{
		GForce = Args.Contains(TEXT("-force"));
		const FString Which = Args.Num() > 0 && !Args[0].StartsWith(TEXT("-")) ? Args[0] : FString(TEXT("All"));
		const bool bAll = Which.Equals(TEXT("All"), ESearchCase::IgnoreCase);
		struct FEntry { const TCHAR* Key; bool (*Fn)(); };
		const FEntry Entries[] = { { TEXT("Anim"), &BuildAnim }, { TEXT("Graph"), &BuildGraph }, { TEXT("Text"), &BuildText }, { TEXT("Bind"), &BuildBind }, { TEXT("Layout"), &BuildLayout } };
		for (const FEntry& E : Entries)
		{
			if (bAll || Which.Equals(E.Key, ESearchCase::IgnoreCase))
			{
				const bool bOk = E.Fn();
				UE_LOG(LogRadarUMGDiff, Display, TEXT("픽스처 %s: %s"), E.Key, bOk ? TEXT("완료") : TEXT("실패"));
			}
		}
	}

	FAutoConsoleCommand GBuildFixturesCommand(TEXT("RadarUMGDiff.BuildFixtures"),
		TEXT("픽스처 위젯 블루프린트를 버전별로 만든다 (dev 문서 §13): [Anim|Graph|Text|Bind|Layout|All] [-force]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}
