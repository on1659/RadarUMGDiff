// Copyright Radar. All Rights Reserved.

// (C) 파일 두 개 백엔드 (dev 문서 §8.3). 어떤 방법으로든 얻은 .uasset 을 Saved/Diff 아래로 복사해서 로드한다.
// 원본 자리에서 로드하면 /Temp/ 규칙이 깨지고 이름이 충돌한다.

#include "RadarRevisionSource.h"

#include "HAL/FileManager.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace
{
	class FRadarFilesSource : public IRadarRevisionSource
	{
	public:
		virtual FName GetId() const override { return TEXT("file"); }
		virtual FText GetDisplayName() const override { return LOCTEXT("FilesName", "파일 두 개"); }
		virtual bool IsAvailableFor(const FString& AssetFile) override { return true; }
		virtual TArray<FRadarRevision> GetHistory(const FString& AssetFile, FString& OutError) override { return {}; }
		virtual bool IsWorkingCopyDirty(const FString& AssetFile) override { return false; }

		virtual bool Fetch(const FRadarRevision& Revision, const FString& AssetFile, FString& OutTempFile, FString& OutError) override
		{
			if (!IFileManager::Get().FileExists(*Revision.LocalFile))
			{
				OutError = FString::Printf(TEXT("파일이 없습니다: %s"), *Revision.LocalFile);
				return false;
			}
			const FString Hash8 = LexToString(FMD5Hash::HashFile(*Revision.LocalFile)).Left(8);
			OutTempFile = RadarRevisionSources::MakeTempFilePath(TEXT("file-") + Hash8, AssetFile);
			if (IFileManager::Get().FileExists(*OutTempFile))
			{
				return true;
			}
			if (IFileManager::Get().Copy(*OutTempFile, *Revision.LocalFile) != COPY_OK)
			{
				OutError = FString::Printf(TEXT("복사 실패: %s → %s"), *Revision.LocalFile, *OutTempFile);
				return false;
			}
			return true;
		}
	};
}

TSharedRef<IRadarRevisionSource> RadarRevisionSources::MakeFiles()
{
	return MakeShared<FRadarFilesSource>();
}

#undef LOCTEXT_NAMESPACE
