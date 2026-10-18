// Copyright Radar. All Rights Reserved.

#pragma once

// 생산자 A: 로드된 두 UWidgetBlueprint → FRadarDiff + 양쪽 FRadarState (dev 문서 §7).

#include "CoreMinimal.h"
#include "RadarDiffModel.h"

class FRadarDiffSession;
class UWidgetBlueprint;
struct FRadarRevision;

struct RADARUMGDIFFEDITOR_API FRadarNativeDiffProducer
{
	/** bLiveObjects = 에디터 안에서 세션을 쥔 채 만든다 (capability "live-objects"). */
	static FRadarDiff Produce(const FRadarDiffSession& Session, bool bLiveObjects);

	/** 한쪽 리비전의 객체 단위 전체 상태. */
	static FRadarState DumpState(const FRadarDiffSession& Session, bool bOld);

	/** diff.json + (bWithState 면) old.state.json, new.state.json 을 OutDir 에 쓴다. */
	static bool WriteAll(const FRadarDiffSession& Session, const FString& OutDir, bool bWithState, bool bLiveObjects, FString& OutError);

	/**
	 * 정확도 모드 (dev 문서 §11.10 history): 패키지마다 git 히스토리의 인접 커밋 쌍(과거→최신)과, 작업 사본이 수정됐으면
	 * 최신 커밋→작업 사본을 diff 해 <OutDir>/<애셋>/<이전7>_<이후7 또는 working>/diff.json 에 쓴다. Packages 가 비면 /Game 의 모든 위젯 블루프린트.
	 * 반환: 쓴 diff.json 수. 실패한 쌍은 OutFailures 에.
	 */
	static int32 DumpHistory(TArray<FString> Packages, const FString& OutDir, bool bLiveObjects, TArray<FString>& OutFailures);
};
