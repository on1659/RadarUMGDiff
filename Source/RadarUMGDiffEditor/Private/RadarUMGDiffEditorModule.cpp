// Copyright Radar. All Rights Reserved.

#include "RadarUMGDiffEditorModule.h"
#include "RadarUMGDiffLog.h"
#include "RadarRevisionSource.h"
#include "RadarEngineCompat.h"
#include "Viewer/SRadarRevisionPicker.h"

#include "WidgetBlueprint.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "RadarNativeDiffProducer.h"
#include "RadarDiffJson.h"
#include "Viewer/SRadarDiffWindow.h"
#include "Viewer/RadarViewModel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Styling/AppStyle.h"
#include "Modules/ModuleManager.h"
#include "RadarUMGDiffSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

DEFINE_LOG_CATEGORY(LogRadarUMGDiff);

void FRadarUMGDiffEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FRadarUMGDiffEditorModule::RegisterMenus));
}

void FRadarUMGDiffEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	for (const TSharedRef<FRadarDiffSession>& Session : FRadarDiffSession::Registry())
	{
		Session->Release();
	}
	FRadarDiffSession::Registry().Reset();
}

void FRadarUMGDiffEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// "ContentBrowser.AssetContextMenu.WidgetBlueprint" (ContentBrowserMenuContexts.cpp:115 이 클래스 이름으로 만든다)
	UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UWidgetBlueprint::StaticClass());
	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
	Section.AddDynamicEntry("RadarUMGDiff", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context || Context->SelectedAssets.Num() != 1)
		{
			return;
		}
		InSection.AddMenuEntry(
			"RadarUMGDiff",
			LOCTEXT("MenuLabel", "Radar UMG Diff..."),
			LOCTEXT("MenuTooltip", "이 위젯 블루프린트의 두 리비전을 렌더해서 비교합니다."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Diff"),
			FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
			{
				const UContentBrowserAssetContextMenuContext* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!Ctx)
				{
					return;
				}
				TArray<UWidgetBlueprint*> Loaded = Ctx->LoadSelectedObjects<UWidgetBlueprint>();
				if (Loaded.Num() == 1)
				{
					FRadarUMGDiffEditorModule::OpenRevisionPicker(Loaded[0]);
				}
			}));
	}));
}

void FRadarUMGDiffEditorModule::OpenRevisionPicker(UWidgetBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("PickerWindowTitle", "Radar UMG Diff — {0}"), FText::FromString(Blueprint->GetName())))
		.ClientSize(FVector2D(900.f, 560.f))
		.SupportsMinimize(false);
	Window->SetContent(SNew(SRadarRevisionPicker, Blueprint).ParentWindow(Window));
	FSlateApplication::Get().AddWindow(Window);
}

void FRadarUMGDiffEditorModule::OpenDiffWindow(const TSharedRef<FRadarDiffSession>& Session)
{
	// 입력 (b): 두 리비전 미리보기 UUserWidget 을 같은 월드에 만든다 (스파이크 2)
	FString PreviewError;
	if (!Session->CreatePreviews(PreviewError))
	{
		UE_LOG(LogRadarUMGDiff, Warning, TEXT("%s — data-only 로 엽니다"), *PreviewError);
	}
	// 입력 (a): 네이티브 생산자의 diff.json 모델
	TSharedRef<FRadarDiff> Diff = MakeShared<FRadarDiff>(FRadarNativeDiffProducer::Produce(*Session, true));
	SRadarDiffWindow::Open(MakeShared<FRadarViewModel>(Diff, Session));
}

bool FRadarUMGDiffEditorModule::RerunWithPython(const FRadarDiffSession& Session, FString& OutError)
{
	const URadarUMGDiffSettings& S = URadarUMGDiffSettings::Get();
	if (Session.OldFile.IsEmpty() || Session.NewFile.IsEmpty())
	{
		OutError = TEXT("두 리비전의 디스크 파일이 없습니다.");
		return false;
	}
	const FString Python = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / S.PythonExe);
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RadarUMGDiff"));
	const FString Cli = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Content/Python/umgdiff_cli.py"));
	const FString OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RadarUMGDiff/python")
		/ FString::Printf(TEXT("%s_%s"), *FPackageName::GetShortName(Session.AssetPackageName), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
	const FString Schema = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RadarUMGDiff/schema.json"));
	FString Args = FString::Printf(TEXT("-I \"%s\" diff \"%s\" \"%s\" -o \"%s\" --package %s"), *Cli, *Session.OldFile, *Session.NewFile, *OutDir, *Session.AssetPackageName);
	if (FPaths::FileExists(Schema))
	{
		Args += FString::Printf(TEXT(" --schema \"%s\""), *Schema);
	}
	int32 Code = -1;
	FString StdOut, StdErr;
	if (!FPlatformProcess::ExecProcess(*Python, *Args, &Code, &StdOut, &StdErr) || Code != 0)
	{
		OutError = FString::Printf(TEXT("Python 분석기 실패 (종료 코드 %d): %s"), Code, *StdErr.Left(600));
		return false;
	}
	UE_LOG(LogRadarUMGDiff, Display, TEXT("Python 분석기: %s"), *StdOut.TrimEnd());
	return OpenDiffJson(OutDir / TEXT("diff.json"));
}

bool FRadarUMGDiffEditorModule::OpenDiffJson(const FString& Path)
{
	TSharedRef<FRadarDiff> Diff = MakeShared<FRadarDiff>();
	FString Error;
	if (!RadarDiffJson::LoadDiff(Path, *Diff, Error))
	{
		UE_LOG(LogRadarUMGDiff, Warning, TEXT("diff.json 열기 실패: %s"), *Error);
		return false;
	}
	// 세션 없음 → data-only (§9.8)
	SRadarDiffWindow::Open(MakeShared<FRadarViewModel>(Diff, nullptr));
	return true;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRadarUMGDiffEditorModule, RadarUMGDiffEditor)
