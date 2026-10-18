// Copyright Radar. All Rights Reserved.

#pragma once

// 모델 ↔ diff.json / state.json (dev 문서 §5·§6). JsonUtilities 가 아니라 손으로 쓴 직렬화: 필드 순서 고정, 들여쓰기 2, UTF-8(BOM 없음), 줄 끝 \n.

#include "CoreMinimal.h"
#include "RadarDiffModel.h"

namespace RadarDiffJson
{
	RADARUMGDIFFEDITOR_API FString DiffToString(const FRadarDiff& Diff);
	RADARUMGDIFFEDITOR_API FString StateToString(const FRadarState& State, const FRadarDiff& ForProducer);
	RADARUMGDIFFEDITOR_API bool SaveDiff(const FRadarDiff& Diff, const FString& Path, FString& OutError);
	RADARUMGDIFFEDITOR_API bool SaveState(const FRadarState& State, const FRadarDiff& ForProducer, const FString& Path, FString& OutError);

	/** diff.json 을 모델로 읽는다 (data-only 뷰어). 옆에 old/new.state.json 이 있으면 위젯 트리도 채운다. */
	RADARUMGDIFFEDITOR_API bool LoadDiff(const FString& Path, FRadarDiff& OutDiff, FString& OutError);

	/** 요약 개수 (§5 summary). 뷰어 요약 띠와 Python summary 가 같은 규칙을 쓴다. */
	RADARUMGDIFFEDITOR_API void CountSummary(const FRadarDiff& Diff, TMap<FString, int32>& OutByKind, TMap<FString, int32>& OutByArea);
}
