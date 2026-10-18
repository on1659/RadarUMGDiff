// Copyright Radar. All Rights Reserved.

#include "Commandlet/RadarUMGDiffDumpCommandlet.h"
#include "RadarRevisionSource.h"
#include "RadarNativeDiffProducer.h"
#include "RadarUMGDiffLog.h"

#include "WidgetBlueprint.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

URadarUMGDiffDumpCommandlet::URadarUMGDiffDumpCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 URadarUMGDiffDumpCommandlet::Main(const FString& Params)
{
	FString Asset, OldSpec, NewSpec, OutDir, Provider;
	FParse::Value(*Params, TEXT("asset="), Asset);
	FParse::Value(*Params, TEXT("old="), OldSpec);
	FParse::Value(*Params, TEXT("new="), NewSpec);
	FParse::Value(*Params, TEXT("out="), OutDir);
	FParse::Value(*Params, TEXT("scmprovider="), Provider);
	const bool bState = FParse::Param(*Params, TEXT("state"));
	if (FParse::Param(*Params, TEXT("history")) && !OutDir.IsEmpty())
	{
		// 정확도 모드: -asset 이 없으면 /Game 의 모든 위젯 블루프린트
		TArray<FString> Packages;
		if (!Asset.IsEmpty())
		{
			Packages.Add(Asset);
		}
		TArray<FString> Failures;
		FRadarNativeDiffProducer::DumpHistory(Packages, FPaths::ConvertRelativePathToFull(OutDir), false, Failures);
		return Failures.Num() == 0 ? 0 : 2;
	}
	if (Asset.IsEmpty() || OldSpec.IsEmpty() || NewSpec.IsEmpty() || OutDir.IsEmpty())
	{
		UE_LOG(LogRadarUMGDiff, Error, TEXT("사용법: -run=RadarUMGDiffDump -asset=<패키지> -old=<리비전> -new=<리비전> -out=<폴더> [-state] [-scmprovider=<이름>]"));
		return 3;
	}

	if (!Provider.IsEmpty())
	{
		// 커맨드릿은 소스 컨트롤 프로바이더를 스스로 켜지 않는다 → 이름으로 켜고 초기화 (설정은 SourceControlSettings.ini 의 서버·사용자·워크스페이스)
		ISourceControlModule& Module = ISourceControlModule::Get();
		Module.SetProvider(FName(*Provider));
		Module.GetProvider().Init(true);
		if (!Module.IsEnabled() || !Module.GetProvider().IsAvailable())
		{
			UE_LOG(LogRadarUMGDiff, Error, TEXT("소스 컨트롤 프로바이더 %s 에 연결하지 못했습니다."), *Provider);
			return 2;
		}
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(Asset);
	UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, *(Asset + TEXT(".") + AssetName));
	FString AssetFile;
	if (!BP || !FPackageName::TryConvertLongPackageNameToFilename(Asset, AssetFile, FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogRadarUMGDiff, Error, TEXT("위젯 블루프린트를 로드하지 못했습니다: %s"), *Asset);
		return 2;
	}
	AssetFile = FPaths::ConvertRelativePathToFull(AssetFile);

	FRadarRevision Old, New;
	TSharedPtr<IRadarRevisionSource> OldSource, NewSource;
	FString Error;
	if (!RadarRevisionSources::ResolveSpec(OldSpec, AssetFile, Old, OldSource, Error)
		|| !RadarRevisionSources::ResolveSpec(NewSpec, AssetFile, New, NewSource, Error))
	{
		UE_LOG(LogRadarUMGDiff, Error, TEXT("%s"), *Error);
		return 3;
	}
	if (New.Kind == ERadarRevisionKind::Working && NewSource == nullptr)
	{
		// 작업 사본의 "수정됨" 표시는 git 기준으로 채운다 (diff.json new.dirty)
		TSharedRef<IRadarRevisionSource> Git = RadarRevisionSources::MakeGit();
		New.bDirty = Git->IsAvailableFor(AssetFile) && Git->IsWorkingCopyDirty(AssetFile);
	}

	TSharedPtr<FRadarDiffSession> Session = FRadarRevisionLoader::Load(OldSource, Old, NewSource, New, BP, AssetFile, Error);
	if (!Session.IsValid())
	{
		UE_LOG(LogRadarUMGDiff, Error, TEXT("리비전 로드 실패: %s"), *Error);
		return 2;
	}
	const bool bOk = FRadarNativeDiffProducer::WriteAll(*Session, FPaths::ConvertRelativePathToFull(OutDir), bState, false, Error);
	Session->Release();
	if (!bOk)
	{
		UE_LOG(LogRadarUMGDiff, Error, TEXT("%s"), *Error);
		return 2;
	}
	return 0;
}
