// Copyright Radar. All Rights Reserved.

// (A) 엔진 소스 컨트롤 프로바이더 백엔드 (dev 문서 §8.1). 프로젝트가 무엇을 연결했든 인터페이스는 하나다.

#include "RadarRevisionSource.h"
#include "RadarUMGDiffLog.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISourceControlRevision.h"
#include "SourceControlOperations.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "Containers/StringConv.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace
{
	/**
	 * 비유니코드 Perforce 서버에 UTF-8 로 저장된 설명은 프로바이더를 거치며 바이트가 부호 확장돼 U+FF80~U+FFFF 로 들어온다
	 * (2026-10-18 로컬 p4d 실측: "￭ﾔﾄ" = ED 94 84 = '프'). ASCII 와 그 범위만으로 된 문자열이고 하위 바이트가 올바른 UTF-8 일 때만 되돌린다.
	 */
	FString RepairSignExtendedUtf8(const FString& In)
	{
		bool bAnyExtended = false;
		for (const TCHAR C : In)
		{
			if (C >= 0xFF80)
			{
				bAnyExtended = true;
			}
			else if (C >= 0x80)
			{
				return In;
			}
		}
		if (!bAnyExtended)
		{
			return In;
		}
		TArray<ANSICHAR> Bytes;
		Bytes.Reserve(In.Len() + 1);
		for (const TCHAR C : In)
		{
			Bytes.Add(static_cast<ANSICHAR>(C & 0xFF));
		}
		const auto Converted = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
		const FString Out(Converted.Length(), Converted.Get());
		// 되돌린 결과를 다시 UTF-8 로 바꿔 같은 바이트가 나와야 올바른 UTF-8 이었던 것이다.
		const auto Back = StringCast<UTF8CHAR>(*Out, Out.Len());
		if (Back.Length() != Bytes.Num() || FMemory::Memcmp(Back.Get(), Bytes.GetData(), Bytes.Num()) != 0)
		{
			return In;
		}
		return Out;
	}

	class FRadarProviderSource : public IRadarRevisionSource
	{
	public:
		virtual FName GetId() const override { return TEXT("scm"); }

		virtual FText GetDisplayName() const override
		{
			const ISourceControlModule& Module = ISourceControlModule::Get();
			const FName ProviderName = Module.IsEnabled() ? Module.GetProvider().GetName() : NAME_None;
			return FText::Format(LOCTEXT("ProviderName", "소스 컨트롤 ({0})"), FText::FromName(ProviderName));
		}

		virtual bool IsAvailableFor(const FString& AssetFile) override
		{
			ISourceControlModule& Module = ISourceControlModule::Get();
			if (!Module.IsEnabled() || !Module.GetProvider().IsAvailable())
			{
				return false;
			}
			// 프로바이더가 켜져 있어도 그 파일의 히스토리가 0개면 (그 VCS 관리 밖) git 으로 넘긴다.
			FString Error;
			return UpdateHistory(AssetFile, Error).IsValid() && CachedHistorySize > 0;
		}

		virtual TArray<FRadarRevision> GetHistory(const FString& AssetFile, FString& OutError) override
		{
			TArray<FRadarRevision> Result;
			FSourceControlStatePtr State = UpdateHistory(AssetFile, OutError);
			if (!State.IsValid())
			{
				return Result;
			}
			const FString ProviderName = ISourceControlModule::Get().GetProvider().GetName().ToString();
			for (int32 Index = 0; Index < State->GetHistorySize(); ++Index)
			{
				TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Rev = State->GetHistoryItem(Index);
				if (!Rev.IsValid())
				{
					continue;
				}
				FRadarRevision R;
				R.Kind = ERadarRevisionKind::Scm;
				R.Id = Rev->GetRevision();
				R.ShortId = Rev->GetRevision();
				R.Changelist = Rev->GetCheckInIdentifier();
				R.Date = Rev->GetDate();
				R.User = RepairSignExtendedUtf8(Rev->GetUserName());
				R.Description = RepairSignExtendedUtf8(Rev->GetDescription()).TrimStartAndEnd();
				R.ProviderName = ProviderName;
				Result.Add(MoveTemp(R));
			}
			return Result;
		}

		virtual bool IsWorkingCopyDirty(const FString& AssetFile) override
		{
			FString Error;
			FSourceControlStatePtr State = UpdateHistory(AssetFile, Error);
			if (!State.IsValid())
			{
				return false;
			}
			if (State->IsCheckedOut() || State->IsAdded() || State->IsModified())
			{
				return true;
			}
			// 체크아웃 없이 쓰기 가능한 워크스페이스(allwrite 등)는 상태에 수정이 안 드러난다 → 최신 리비전 바이트와 비교한다.
			if (State->GetHistorySize() == 0)
			{
				return false;
			}
			TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Latest = State->GetHistoryItem(0);
			FString Temp = RadarRevisionSources::MakeTempFilePath(TEXT("scm") + Latest->GetRevision(), AssetFile);
			if (!IFileManager::Get().FileExists(*Temp) && !Latest->Get(Temp))
			{
				return false;
			}
			const FMD5Hash A = FMD5Hash::HashFile(*Temp);
			const FMD5Hash B = FMD5Hash::HashFile(*AssetFile);
			return !(A == B);
		}

		virtual bool Fetch(const FRadarRevision& Revision, const FString& AssetFile, FString& OutTempFile, FString& OutError) override
		{
			FSourceControlStatePtr State = UpdateHistory(AssetFile, OutError);
			if (!State.IsValid())
			{
				return false;
			}
			TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Rev = State->FindHistoryRevision(Revision.Id);
			if (!Rev.IsValid())
			{
				OutError = FString::Printf(TEXT("프로바이더 히스토리에 리비전 %s 가 없습니다."), *Revision.Id);
				return false;
			}
			// 프로바이더가 받은 파일 이름을 그대로 넘기면 그 경로에 쓴다 (Perforce: print -o, Git: show > 파일).
			OutTempFile = RadarRevisionSources::MakeTempFilePath(TEXT("scm") + Revision.Id, AssetFile);
			if (IFileManager::Get().FileExists(*OutTempFile))
			{
				return true;
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutTempFile), true);
			FString Target = OutTempFile;
			if (!Rev->Get(Target) || !IFileManager::Get().FileExists(*Target))
			{
				OutError = FString::Printf(TEXT("프로바이더가 리비전 %s 를 받지 못했습니다."), *Revision.Id);
				return false;
			}
			if (Target != OutTempFile)
			{
				IFileManager::Get().Move(*OutTempFile, *Target);
			}
			return true;
		}

	private:
		FString CachedFile;
		int32 CachedHistorySize = 0;
		bool bCacheValid = false;

		/** 히스토리를 한 번 받아 둔다 (피커가 같은 파일로 여러 번 묻는다). */
		FSourceControlStatePtr UpdateHistory(const FString& AssetFile, FString& OutError)
		{
			ISourceControlModule& Module = ISourceControlModule::Get();
			if (!Module.IsEnabled())
			{
				OutError = TEXT("소스 컨트롤이 연결돼 있지 않습니다.");
				return nullptr;
			}
			ISourceControlProvider& Provider = Module.GetProvider();
			const FString AbsFile = FPaths::ConvertRelativePathToFull(AssetFile);
			if (!bCacheValid || CachedFile != AbsFile)
			{
				TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FUpdateStatus>();
				Op->SetUpdateHistory(true);
				if (Provider.Execute(Op, TArray<FString>{ AbsFile }, EConcurrency::Synchronous) != ECommandResult::Succeeded)
				{
					OutError = TEXT("프로바이더 히스토리 조회 실패.");
					bCacheValid = false;
					return nullptr;
				}
				CachedFile = AbsFile;
				bCacheValid = true;
			}
			FSourceControlStatePtr State = Provider.GetState(AbsFile, EStateCacheUsage::Use);
			CachedHistorySize = State.IsValid() ? State->GetHistorySize() : 0;
			return State;
		}
	};
}

TSharedRef<IRadarRevisionSource> RadarRevisionSources::MakeProvider()
{
	return MakeShared<FRadarProviderSource>();
}

#undef LOCTEXT_NAMESPACE
