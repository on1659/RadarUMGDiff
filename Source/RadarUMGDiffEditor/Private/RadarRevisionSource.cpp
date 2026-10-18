// Copyright Radar. All Rights Reserved.

#include "RadarRevisionSource.h"
#include "RadarEngineCompat.h"
#include "RadarUMGDiffSettings.h"
#include "RadarUMGDiffLog.h"

#include "PreviewScene.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "UObject/Linker.h"
#include "UObject/UObjectHash.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

const TCHAR* LexToString(ERadarRevisionKind Kind)
{
	switch (Kind)
	{
	case ERadarRevisionKind::Scm:     return TEXT("scm");
	case ERadarRevisionKind::Commit:  return TEXT("commit");
	case ERadarRevisionKind::Head:    return TEXT("head");
	case ERadarRevisionKind::Working: return TEXT("working");
	case ERadarRevisionKind::File:    return TEXT("file");
	}
	return TEXT("working");
}

FText FRadarRevision::GetLabel() const
{
	switch (Kind)
	{
	case ERadarRevisionKind::Working:
		return bDirty ? LOCTEXT("LabelWorkingDirty", "작업 사본 (수정됨)") : LOCTEXT("LabelWorking", "작업 사본");
	case ERadarRevisionKind::Head:
		return FText::Format(LOCTEXT("LabelHead", "HEAD {0}"), FText::FromString(ShortId));
	case ERadarRevisionKind::Commit:
		return FText::Format(LOCTEXT("LabelCommit", "{0} {1}"), FText::FromString(ShortId), FText::FromString(Description));
	case ERadarRevisionKind::Scm:
		return FText::Format(LOCTEXT("LabelScm", "#{0} (CL {1}) {2}"), FText::FromString(ShortId), FText::AsNumber(Changelist, &FNumberFormattingOptions::DefaultNoGrouping()), FText::FromString(Description));
	case ERadarRevisionKind::File:
		return FText::FromString(FPaths::GetCleanFilename(LocalFile));
	}
	return FText::GetEmpty();
}

namespace RadarRevisionSources
{
	TArray<TSharedRef<IRadarRevisionSource>> MakeAll()
	{
		return { MakeProvider(), MakeGit(), MakeFiles() };
	}

	FString MakeTempFilePath(const FString& Id, const FString& AssetFile)
	{
		FString SafeId = Id;
		for (TCHAR& C : SafeId)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-'))
			{
				C = TEXT('_');
			}
		}
		const FString Dir = URadarUMGDiffSettings::Get().GetTempDirAbsolute();
		return FPaths::Combine(Dir, FString::Printf(TEXT("temp-%s-%s"), *SafeId, *FPaths::GetCleanFilename(AssetFile)));
	}
}

FString URadarUMGDiffSettings::GetTempDirAbsolute() const
{
	FString Dir = FPaths::IsRelative(TempDir) ? FPaths::Combine(FPaths::ProjectDir(), TempDir) : TempDir;
	Dir = FPaths::ConvertRelativePathToFull(Dir);
	FPaths::NormalizeDirectoryName(Dir);
	return Dir + TEXT("/");
}

// ───────────────────────── 세션 ─────────────────────────

FRadarDiffSession::FRadarDiffSession() = default;

FRadarDiffSession::~FRadarDiffSession()
{
	Release();
}

TArray<TSharedRef<FRadarDiffSession>>& FRadarDiffSession::Registry()
{
	static TArray<TSharedRef<FRadarDiffSession>> Sessions;
	return Sessions;
}

void FRadarDiffSession::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(OldPackage);
	Collector.AddReferencedObject(NewPackage);
	Collector.AddReferencedObject(OldBlueprint);
	Collector.AddReferencedObject(NewBlueprint);
	Collector.AddReferencedObject(OldPreview);
	Collector.AddReferencedObject(NewPreview);
}

bool FRadarDiffSession::CreatePreviews(FString& OutError)
{
	if (!PreviewScene)
	{
		PreviewScene = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues());
	}
	UWorld* World = PreviewScene->GetWorld();

	EWidgetDesignFlags Flags = EWidgetDesignFlags::Designing;
	if (URadarUMGDiffSettings::Get().bExecutePreConstruct)
	{
		Flags |= EWidgetDesignFlags::ExecutePreConstruct;
	}

	if (OldBlueprint && !OldPreview)
	{
		OldPreview = RadarCompat::CreatePreviewWidget(World, OldBlueprint, Flags);
	}
	if (NewBlueprint && !NewPreview)
	{
		NewPreview = RadarCompat::CreatePreviewWidget(World, NewBlueprint, Flags);
	}
	if (!OldPreview || !NewPreview)
	{
		OutError = FString::Printf(TEXT("미리보기 생성 실패 (이전 %s, 이후 %s)"), OldPreview ? TEXT("성공") : TEXT("실패"), NewPreview ? TEXT("성공") : TEXT("실패"));
		return false;
	}
	return true;
}

bool FRadarDiffSession::IsPackageHeldByOtherSession(const UPackage* Package) const
{
	for (const TSharedRef<FRadarDiffSession>& Other : Registry())
	{
		if (&Other.Get() != this && !Other->bReleased && (Other->OldPackage == Package || Other->NewPackage == Package))
		{
			return true;
		}
	}
	return false;
}

void FRadarDiffSession::Release()
{
	if (bReleased)
	{
		return;
	}
	bReleased = true;

	RadarCompat::DestroyPreviewWidget(OldPreview);
	RadarCompat::DestroyPreviewWidget(NewPreview);
	OldPreview = nullptr;
	NewPreview = nullptr;
	OldBlueprint = nullptr;
	NewBlueprint = nullptr;

	// 임시 패키지: RF_Standalone 을 떼고 링커를 끊어(파일 핸들 해제) 다음 GC 가 거두게 한다.
	// 같은 리비전을 연 다른 세션이 아직 붙잡고 있으면 그 세션이 놓을 때까지 남는다.
	for (UPackage* Package : { OldPackage.Get(), NewPackage.Get() })
	{
		if (Package && !IsPackageHeldByOtherSession(Package))
		{
			RadarCompat::ReleaseTempPackage(Package);
		}
	}
	OldPackage = nullptr;
	NewPackage = nullptr;
	PreviewScene.Reset();

	if (!URadarUMGDiffSettings::Get().bKeepTempPackages)
	{
		for (const FString& File : TempFiles)
		{
			bool bShared = false;
			for (const TSharedRef<FRadarDiffSession>& Other : Registry())
			{
				bShared |= (&Other.Get() != this && !Other->bReleased && Other->TempFiles.Contains(File));
			}
			if (!bShared)
			{
				IFileManager::Get().Delete(*File, false, false, true);
			}
		}
	}
	TempFiles.Reset();

	if (GEngine)
	{
		GEngine->ForceGarbageCollection(true);
	}
	UE_LOG(LogRadarUMGDiff, Log, TEXT("세션 해제: %s"), *AssetPackageName);
}

void FRadarDiffSession::AttachWindow(const TSharedRef<SWindow>& Window)
{
	Windows.Add(Window);
	Window->GetOnWindowClosedEvent().AddSP(this, &FRadarDiffSession::OnWindowClosed);
	Registry().AddUnique(AsShared());
}

void FRadarDiffSession::OnWindowClosed(const TSharedRef<SWindow>& Window)
{
	Windows.RemoveAll([&Window](const TWeakPtr<SWindow>& W) { return !W.IsValid() || W.Pin() == Window; });
	if (Windows.Num() == 0)
	{
		TSharedRef<FRadarDiffSession> Self = AsShared();
		Release();
		Registry().Remove(Self);
	}
}

// ───────────────────────── 로더 ─────────────────────────

UWidgetBlueprint* FRadarRevisionLoader::LoadOne(const TSharedPtr<IRadarRevisionSource>& Source, const FRadarRevision& Revision,
	UWidgetBlueprint* WorkingBlueprint, const FString& AssetFile, FRadarDiffSession& Session,
	TObjectPtr<UPackage>& OutPackage, FString& OutError)
{
	if (Revision.Kind == ERadarRevisionKind::Working)
	{
		OutPackage = nullptr;
		(&OutPackage == &Session.OldPackage ? Session.OldFile : Session.NewFile) = AssetFile;
		return WorkingBlueprint;
	}
	if (!Source.IsValid())
	{
		OutError = TEXT("이 리비전을 받을 백엔드가 없습니다.");
		return nullptr;
	}

	FString TempFile;
	if (!Source->Fetch(Revision, AssetFile, TempFile, OutError))
	{
		return nullptr;
	}
	Session.TempFiles.AddUnique(TempFile);
	(&OutPackage == &Session.OldPackage ? Session.OldFile : Session.NewFile) = TempFile;

	const FPackagePath TempPath = FPackagePath::FromLocalPath(TempFile);
	const FPackagePath OriginalPath = FPackagePath::FromLocalPath(AssetFile);
	const FString TempPackageName = TempPath.GetPackageName();
	if (!FPackageName::IsTempPackage(TempPackageName))
	{
		OutError = FString::Printf(TEXT("임시 파일이 /Temp/ 마운트 밖입니다: %s → %s (ini TempDir 는 Saved/ 아래여야 합니다)"), *TempFile, *TempPackageName);
		return nullptr;
	}

	// 같은 리비전을 다시 열면 이미 로드된 패키지를 쓴다.
	UPackage* Package = FindPackage(nullptr, *TempPackageName);
	if (!Package)
	{
		Package = RadarCompat::LoadPackageForDiff(TempPath, OriginalPath);
	}
	if (!Package)
	{
		OutError = FString::Printf(TEXT("패키지 로드 실패: %s"), *TempFile);
		return nullptr;
	}
	OutPackage = Package;

	UWidgetBlueprint* BP = RadarCompat::FindWidgetBlueprintInPackage(Package);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("패키지 안에 위젯 블루프린트가 없습니다: %s"), *TempPackageName);
	}
	return BP;
}

TSharedPtr<FRadarDiffSession> FRadarRevisionLoader::Load(
	const TSharedPtr<IRadarRevisionSource>& OldSource, const FRadarRevision& OldRevision,
	const TSharedPtr<IRadarRevisionSource>& NewSource, const FRadarRevision& NewRevision,
	UWidgetBlueprint* WorkingBlueprint, const FString& AssetFile, FString& OutError)
{
	TSharedRef<FRadarDiffSession> Session = MakeShared<FRadarDiffSession>();
	Session->OldRevision = OldRevision;
	Session->NewRevision = NewRevision;
	Session->AssetFile = AssetFile;
	Session->AssetPackageName = WorkingBlueprint ? WorkingBlueprint->GetOutermost()->GetName() : FString();

	Session->OldBlueprint = LoadOne(OldSource, OldRevision, WorkingBlueprint, AssetFile, *Session, Session->OldPackage, OutError);
	if (!Session->OldBlueprint)
	{
		return nullptr;
	}
	Session->NewBlueprint = LoadOne(NewSource, NewRevision, WorkingBlueprint, AssetFile, *Session, Session->NewPackage, OutError);
	if (!Session->NewBlueprint)
	{
		return nullptr;
	}
	UE_LOG(LogRadarUMGDiff, Log, TEXT("두 리비전 로드: %s [%s] ↔ [%s]"), *Session->AssetPackageName,
		*Session->OldBlueprint->GetPathName(), *Session->NewBlueprint->GetPathName());
	return Session;
}

/** 리비전 지정 문자열 → 리비전 + 그것을 받을 백엔드. */
bool RadarRevisionSources::ResolveSpec(const FString& Spec, const FString& AssetFile, FRadarRevision& OutRev, TSharedPtr<IRadarRevisionSource>& OutSource, FString& OutError)
{
	FString Kind = Spec, Value;
	Spec.Split(TEXT(":"), &Kind, &Value);
	Kind = Kind.ToLower();

	if (Kind == TEXT("working"))
	{
		OutRev.Kind = ERadarRevisionKind::Working;
		return true;
	}
	if (Kind == TEXT("head"))
	{
		OutSource = RadarRevisionSources::MakeGit();
		return RadarRevisionSources::MakeGitHead(AssetFile, OutRev) || (OutError = TEXT("git HEAD 를 얻지 못했습니다."), false);
	}
	if (Kind == TEXT("file"))
	{
		OutSource = RadarRevisionSources::MakeFiles();
		OutRev.Kind = ERadarRevisionKind::File;
		OutRev.LocalFile = Value.TrimQuotes();
		return true;
	}
	if (Kind == TEXT("git") || Kind == TEXT("scm"))
	{
		OutSource = Kind == TEXT("git") ? RadarRevisionSources::MakeGit() : RadarRevisionSources::MakeProvider();
		if (!OutSource->IsAvailableFor(AssetFile))
		{
			OutError = FString::Printf(TEXT("%s 백엔드를 이 에셋에 쓸 수 없습니다."), *Kind);
			return false;
		}
		const FString Want = Value.TrimChar(TEXT('#'));
		for (FRadarRevision& R : OutSource->GetHistory(AssetFile, OutError))
		{
			const bool bMatch = Kind == TEXT("git") ? R.Id.StartsWith(Want) : (R.Id == Want);
			if (bMatch)
			{
				OutRev = MoveTemp(R);
				return true;
			}
		}
		OutError = FString::Printf(TEXT("%s 히스토리에 %s 가 없습니다. %s"), *Kind, *Value, *OutError);
		return false;
	}
	OutError = FString::Printf(TEXT("모르는 리비전 지정: %s (working | head | git:<sha> | scm:<리비전> | file:<경로>)"), *Spec);
	return false;
}


#undef LOCTEXT_NAMESPACE
