// Copyright Radar. All Rights Reserved.

// (B) git CLI 백엔드 (dev 문서 §8.2). 프로바이더가 없거나 그 파일에 프로바이더 히스토리가 없을 때 쓴다.

#include "RadarRevisionSource.h"
#include "RadarGitCli.h"
#include "RadarUMGDiffSettings.h"
#include "RadarUMGDiffLog.h"

#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace RadarGit
{
	static FString GitExe()
	{
		const FString& Exe = URadarUMGDiffSettings::Get().GitExe;
		return Exe.IsEmpty() ? FString(TEXT("git")) : Exe;
	}

	FString Quote(const FString& Arg)
	{
		return FString::Printf(TEXT("\"%s\""), *Arg.Replace(TEXT("\""), TEXT("\\\"")));
	}

	int32 Run(const FString& Args, const FString& WorkDir, FString& OutStdOut, FString* OutStdErr)
	{
		int32 Code = -1;
		FString StdErr;
		if (!FPlatformProcess::ExecProcess(*GitExe(), *Args, &Code, &OutStdOut, &StdErr, *WorkDir))
		{
			Code = -1;
		}
		if (OutStdErr)
		{
			*OutStdErr = StdErr;
		}
		return Code;
	}

	int32 RunBinary(const FString& Args, const FString& WorkDir, TArray<uint8>& OutBytes, FString* OutStdErr)
	{
		void* OutRead = nullptr;
		void* OutWrite = nullptr;
		void* ErrRead = nullptr;
		void* ErrWrite = nullptr;
		if (!FPlatformProcess::CreatePipe(OutRead, OutWrite) || !FPlatformProcess::CreatePipe(ErrRead, ErrWrite))
		{
			return -1;
		}
		// stdout 과 stderr 를 다른 파이프로 받아야 경고 문구가 바이너리에 섞이지 않는다.
		FProcHandle Proc = FPlatformProcess::CreateProc(*GitExe(), *Args, false, true, true, nullptr, 0, *WorkDir, OutWrite, nullptr, ErrWrite);
		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(OutRead, OutWrite);
			FPlatformProcess::ClosePipe(ErrRead, ErrWrite);
			return -1;
		}
		TArray<uint8> ErrBytes;
		auto Drain = [&]()
		{
			bool bAny = false;
			TArray<uint8> Chunk;
			if (FPlatformProcess::ReadPipeToArray(OutRead, Chunk) && Chunk.Num() > 0)
			{
				OutBytes.Append(Chunk);
				bAny = true;
			}
			Chunk.Reset();
			if (FPlatformProcess::ReadPipeToArray(ErrRead, Chunk) && Chunk.Num() > 0)
			{
				ErrBytes.Append(Chunk);
				bAny = true;
			}
			return bAny;
		};
		while (FPlatformProcess::IsProcRunning(Proc))
		{
			if (!Drain())
			{
				FPlatformProcess::Sleep(0.001f);
			}
		}
		while (Drain())
		{
		}
		int32 Code = -1;
		FPlatformProcess::GetProcReturnCode(Proc, &Code);
		FPlatformProcess::CloseProc(Proc);
		FPlatformProcess::ClosePipe(OutRead, OutWrite);
		FPlatformProcess::ClosePipe(ErrRead, ErrWrite);
		if (OutStdErr && ErrBytes.Num() > 0)
		{
			ErrBytes.Add(0);
			*OutStdErr = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(ErrBytes.GetData()));
		}
		return Code;
	}

	FString FindRepoRoot(const FString& AbsFile)
	{
		FString Out;
		if (Run(TEXT("rev-parse --show-toplevel"), FPaths::GetPath(AbsFile), Out) != 0)
		{
			return FString();
		}
		Out.TrimStartAndEndInline();
		return Out;
	}

	FString MakeRepoRelative(const FString& RepoRoot, const FString& AbsFile)
	{
		FString Rel = FPaths::ConvertRelativePathToFull(AbsFile);
		FString Root = FPaths::ConvertRelativePathToFull(RepoRoot);
		FPaths::NormalizeFilename(Rel);
		FPaths::NormalizeDirectoryName(Root);
		FPaths::MakePathRelativeTo(Rel, *(Root + TEXT("/")));
		return Rel;
	}
}

namespace
{
	class FRadarGitSource : public IRadarRevisionSource
	{
	public:
		virtual FName GetId() const override { return TEXT("git"); }
		virtual FText GetDisplayName() const override { return LOCTEXT("GitName", "git"); }

		virtual bool IsAvailableFor(const FString& AssetFile) override
		{
			return !RadarGit::FindRepoRoot(AssetFile).IsEmpty();
		}

		virtual TArray<FRadarRevision> GetHistory(const FString& AssetFile, FString& OutError) override
		{
			TArray<FRadarRevision> Result;
			const FString Root = RadarGit::FindRepoRoot(AssetFile);
			if (Root.IsEmpty())
			{
				OutError = TEXT("git 저장소가 아닙니다.");
				return Result;
			}
			const FString Rel = RadarGit::MakeRepoRelative(Root, AssetFile);
			// 레코드 구분 \x1e, 필드 구분 \x1f. --name-only 로 그 커밋에서의 경로도 받는다 (--follow 이름 변경 대비).
			const FString Args = FString::Printf(
				TEXT("-c core.quotepath=off log --follow --name-only --format=%%x1e%%H%%x1f%%h%%x1f%%aI%%x1f%%an%%x1f%%s -- %s"),
				*RadarGit::Quote(Rel));
			FString Out, Err;
			if (RadarGit::Run(Args, Root, Out, &Err) != 0)
			{
				OutError = FString::Printf(TEXT("git log 실패: %s"), *Err);
				return Result;
			}
			TArray<FString> Records;
			Out.ParseIntoArray(Records, TEXT("\x1e"), true);
			for (const FString& Record : Records)
			{
				TArray<FString> Lines;
				Record.ParseIntoArrayLines(Lines, true);
				if (Lines.Num() == 0)
				{
					continue;
				}
				TArray<FString> Fields;
				Lines[0].ParseIntoArray(Fields, TEXT("\x1f"), false);
				if (Fields.Num() < 5)
				{
					continue;
				}
				FRadarRevision R;
				R.Kind = ERadarRevisionKind::Commit;
				R.Id = Fields[0];
				R.ShortId = Fields[1];
				FDateTime::ParseIso8601(*Fields[2], R.Date);
				R.User = Fields[3];
				R.Description = Fields[4];
				R.RepoPath = Lines.Num() > 1 ? Lines[1].TrimStartAndEnd() : Rel;
				Result.Add(MoveTemp(R));
			}
			return Result;
		}

		virtual bool IsWorkingCopyDirty(const FString& AssetFile) override
		{
			const FString Root = RadarGit::FindRepoRoot(AssetFile);
			if (Root.IsEmpty())
			{
				return false;
			}
			FString Out;
			RadarGit::Run(FString::Printf(TEXT("status --porcelain -- %s"), *RadarGit::Quote(RadarGit::MakeRepoRelative(Root, AssetFile))), Root, Out);
			return !Out.TrimStartAndEnd().IsEmpty();
		}

		virtual bool Fetch(const FRadarRevision& Revision, const FString& AssetFile, FString& OutTempFile, FString& OutError) override
		{
			const FString Root = RadarGit::FindRepoRoot(AssetFile);
			if (Root.IsEmpty())
			{
				OutError = TEXT("git 저장소가 아닙니다.");
				return false;
			}
			const FString RepoPath = Revision.RepoPath.IsEmpty() ? RadarGit::MakeRepoRelative(Root, AssetFile) : Revision.RepoPath;
			const FString Sha7 = Revision.ShortId.IsEmpty() ? Revision.Id.Left(7) : Revision.ShortId;
			OutTempFile = RadarRevisionSources::MakeTempFilePath(Sha7, AssetFile);
			if (IFileManager::Get().FileExists(*OutTempFile))
			{
				return true;
			}
			TArray<uint8> Bytes;
			FString Err;
			const FString Args = FString::Printf(TEXT("-c core.quotepath=off show %s"), *RadarGit::Quote(Revision.Id + TEXT(":") + RepoPath));
			if (RadarGit::RunBinary(Args, Root, Bytes, &Err) != 0 || Bytes.Num() == 0)
			{
				OutError = FString::Printf(TEXT("git show %s:%s 실패: %s"), *Sha7, *RepoPath, *Err);
				return false;
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutTempFile), true);
			if (!FFileHelper::SaveArrayToFile(Bytes, *OutTempFile))
			{
				OutError = FString::Printf(TEXT("임시 파일 쓰기 실패: %s"), *OutTempFile);
				return false;
			}
			return true;
		}
	};
}

TSharedRef<IRadarRevisionSource> RadarRevisionSources::MakeGit()
{
	return MakeShared<FRadarGitSource>();
}

/** HEAD 리비전 (작업 사본이 더러울 때 피커가 붙인다). */
bool RadarRevisionSources::MakeGitHead(const FString& AssetFile, FRadarRevision& OutHead)
{
	const FString Root = RadarGit::FindRepoRoot(AssetFile);
	if (Root.IsEmpty())
	{
		return false;
	}
	FString Out;
	if (RadarGit::Run(TEXT("log -1 --format=%H%x1f%h%x1f%aI%x1f%an%x1f%s HEAD"), Root, Out) != 0)
	{
		return false;
	}
	TArray<FString> Fields;
	Out.TrimStartAndEnd().ParseIntoArray(Fields, TEXT("\x1f"), false);
	if (Fields.Num() < 5)
	{
		return false;
	}
	OutHead.Kind = ERadarRevisionKind::Head;
	OutHead.Id = Fields[0];
	OutHead.ShortId = Fields[1];
	FDateTime::ParseIso8601(*Fields[2], OutHead.Date);
	OutHead.User = Fields[3];
	OutHead.Description = Fields[4];
	OutHead.RepoPath = RadarGit::MakeRepoRelative(Root, AssetFile);
	return true;
}

#undef LOCTEXT_NAMESPACE
