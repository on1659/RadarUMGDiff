// Copyright Radar. All Rights Reserved.

// 자동 검증·스크립트용 콘솔 명령. 메뉴 없이 같은 경로(피커 · 로더 · 창)를 탄다.
//   RadarUMGDiff.Picker <패키지>                     리비전 피커를 연다
//   RadarUMGDiff.Open <패키지> <이전> <이후>          두 리비전을 바로 비교한다
//       리비전: working | head | git:<sha> | scm:<리비전> | file:<경로>
//   RadarUMGDiff.OpenJson <diff.json>                 data-only 뷰어로 연다 (어느 생산자의 파일이든)
//   RadarUMGDiff.Sessions                            열린 세션과 메모리의 임시 위젯 블루프린트 수를 로그로
//   RadarUMGDiff.Dump <패키지> <이전> <이후> <폴더>     에디터 안에서 diff.json + 양쪽 state.json 을 쓴다
//   RadarUMGDiff.DumpHistory <폴더> [패키지...]        정확도 모드: git 인접 커밋 쌍마다 diff.json (패키지 없으면 /Game 의 모든 WBP)

#include "RadarUMGDiffEditorModule.h"
#include "RadarRevisionSource.h"
#include "RadarUMGDiffLog.h"
#include "RadarNativeDiffProducer.h"

#include "HAL/IConsoleManager.h"
#include "WidgetBlueprint.h"
#include "UObject/UObjectIterator.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	UWidgetBlueprint* LoadWorkingBlueprint(const FString& PackageName, FString& OutAssetFile)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, *(PackageName + TEXT(".") + AssetName));
		if (BP && FPackageName::TryConvertLongPackageNameToFilename(PackageName, OutAssetFile, FPackageName::GetAssetPackageExtension()))
		{
			OutAssetFile = FPaths::ConvertRelativePathToFull(OutAssetFile);
		}
		return BP;
	}

	void PickerCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("사용법: RadarUMGDiff.Picker <패키지>"));
			return;
		}
		FString AssetFile;
		if (UWidgetBlueprint* BP = LoadWorkingBlueprint(Args[0], AssetFile))
		{
			FRadarUMGDiffEditorModule::OpenRevisionPicker(BP);
		}
		else
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("위젯 블루프린트를 찾지 못했습니다: %s"), *Args[0]);
		}
	}

	void OpenCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 3)
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("사용법: RadarUMGDiff.Open <패키지> <이전> <이후>  (working | head | git:<sha> | scm:<리비전> | file:<경로>)"));
			return;
		}
		FString AssetFile;
		UWidgetBlueprint* BP = LoadWorkingBlueprint(Args[0], AssetFile);
		if (!BP)
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("위젯 블루프린트를 찾지 못했습니다: %s"), *Args[0]);
			return;
		}
		FRadarRevision Old, New;
		TSharedPtr<IRadarRevisionSource> OldSource, NewSource;
		FString Error;
		if (!RadarRevisionSources::ResolveSpec(Args[1], AssetFile, Old, OldSource, Error) || !RadarRevisionSources::ResolveSpec(Args[2], AssetFile, New, NewSource, Error))
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("%s"), *Error);
			return;
		}
		TSharedPtr<FRadarDiffSession> Session = FRadarRevisionLoader::Load(OldSource, Old, NewSource, New, BP, AssetFile, Error);
		if (!Session.IsValid())
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("비교 열기 실패: %s"), *Error);
			return;
		}
		FRadarUMGDiffEditorModule::OpenDiffWindow(Session.ToSharedRef());
	}

	void DumpCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 4)
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("사용법: RadarUMGDiff.Dump <패키지> <이전> <이후> <폴더>"));
			return;
		}
		FString AssetFile;
		UWidgetBlueprint* BP = LoadWorkingBlueprint(Args[0], AssetFile);
		if (!BP)
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("위젯 블루프린트를 찾지 못했습니다: %s"), *Args[0]);
			return;
		}
		FRadarRevision Old, New;
		TSharedPtr<IRadarRevisionSource> OldSource, NewSource;
		FString Error;
		if (!RadarRevisionSources::ResolveSpec(Args[1], AssetFile, Old, OldSource, Error) || !RadarRevisionSources::ResolveSpec(Args[2], AssetFile, New, NewSource, Error))
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("%s"), *Error);
			return;
		}
		TSharedPtr<FRadarDiffSession> Session = FRadarRevisionLoader::Load(OldSource, Old, NewSource, New, BP, AssetFile, Error);
		if (!Session.IsValid() || !FRadarNativeDiffProducer::WriteAll(*Session, FPaths::ConvertRelativePathToFull(Args[3]), true, true, Error))
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("diff.json 실패: %s"), *Error);
		}
		if (Session.IsValid())
		{
			Session->Release();
		}
	}

	void DumpHistoryCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("사용법: RadarUMGDiff.DumpHistory <폴더> [패키지...]"));
			return;
		}
		TArray<FString> Packages(Args);
		Packages.RemoveAt(0);
		TArray<FString> Failures;
		FRadarNativeDiffProducer::DumpHistory(Packages, FPaths::ConvertRelativePathToFull(Args[0]), true, Failures);
	}

	void OpenJsonCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogRadarUMGDiff, Warning, TEXT("사용법: RadarUMGDiff.OpenJson <diff.json>"));
			return;
		}
		FRadarUMGDiffEditorModule::OpenDiffJson(FPaths::ConvertRelativePathToFull(FString::Join(Args, TEXT(" ")).TrimQuotes()));
	}

	void SessionsCommand(const TArray<FString>& Args)
	{
		const TArray<TSharedRef<FRadarDiffSession>>& Sessions = FRadarDiffSession::Registry();
		UE_LOG(LogRadarUMGDiff, Display, TEXT("열린 세션 %d개"), Sessions.Num());
		for (const TSharedRef<FRadarDiffSession>& S : Sessions)
		{
			UE_LOG(LogRadarUMGDiff, Display, TEXT("  %s : %s ↔ %s (창 %d)"), *S->AssetPackageName,
				*GetPathNameSafe(S->OldBlueprint), *GetPathNameSafe(S->NewBlueprint), S->Windows.Num());
		}
		int32 TempCount = 0;
		for (TObjectIterator<UWidgetBlueprint> It; It; ++It)
		{
			if (FPackageName::IsTempPackage(It->GetOutermost()->GetName()))
			{
				++TempCount;
				UE_LOG(LogRadarUMGDiff, Display, TEXT("  메모리의 임시 위젯 블루프린트: %s%s"), *It->GetPathName(),
					IsValid(*It) ? TEXT("") : TEXT(" (garbage)"));
			}
		}
		UE_LOG(LogRadarUMGDiff, Display, TEXT("메모리의 임시 위젯 블루프린트 %d개"), TempCount);
	}

	FAutoConsoleCommand GRadarPickerCommand(TEXT("RadarUMGDiff.Picker"), TEXT("RadarUMGDiff 리비전 피커를 연다: <패키지>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PickerCommand));
	FAutoConsoleCommand GRadarOpenCommand(TEXT("RadarUMGDiff.Open"), TEXT("두 리비전을 바로 비교한다: <패키지> <이전> <이후>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OpenCommand));
	FAutoConsoleCommand GRadarDumpCommand(TEXT("RadarUMGDiff.Dump"), TEXT("diff.json 과 양쪽 state.json 을 쓴다: <패키지> <이전> <이후> <폴더>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCommand));
	FAutoConsoleCommand GRadarDumpHistoryCommand(TEXT("RadarUMGDiff.DumpHistory"), TEXT("정확도 모드: git 인접 커밋 쌍마다 diff.json: <폴더> [패키지...]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpHistoryCommand));
	FAutoConsoleCommand GRadarOpenJsonCommand(TEXT("RadarUMGDiff.OpenJson"), TEXT("diff.json 을 data-only 뷰어로 연다: <경로>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OpenJsonCommand));
	FAutoConsoleCommand GRadarSessionsCommand(TEXT("RadarUMGDiff.Sessions"), TEXT("열린 비교 세션과 메모리의 임시 위젯 블루프린트를 로그로"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SessionsCommand));
}
