// Copyright Radar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** git CLI 호출 (dev 문서 §8.2). 플러그인·설정 없이 PATH(또는 ini GitExe)의 git 만 있으면 된다. */
namespace RadarGit
{
	/** 텍스트 출력(UTF-8 디코드). 반환값 = 종료 코드, 실행 실패면 -1. */
	int32 Run(const FString& Args, const FString& WorkDir, FString& OutStdOut, FString* OutStdErr = nullptr);

	/** 바이너리 출력 그대로 (git show <sha>:<path>). 반환값 = 종료 코드. */
	int32 RunBinary(const FString& Args, const FString& WorkDir, TArray<uint8>& OutBytes, FString* OutStdErr = nullptr);

	/** 에셋 파일이 속한 저장소 루트 (없으면 빈 문자열). */
	FString FindRepoRoot(const FString& AbsFile);

	/** 저장소 루트 기준 상대 경로, 슬래시 구분. */
	FString MakeRepoRelative(const FString& RepoRoot, const FString& AbsFile);

	/** 인자 하나를 따옴표로 감싼다. */
	FString Quote(const FString& Arg);
}
