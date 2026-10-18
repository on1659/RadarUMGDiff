// Copyright Radar. All Rights Reserved.

#pragma once

// 상세 3열의 값 렌더러 (dev 문서 §9.4): 색 칩, 성분 강조, 단어 단위 텍스트 diff, 애셋 짧은 이름, (기본)/? 표시.

#include "CoreMinimal.h"
#include "RadarDiffModel.h"

class SWidget;

namespace RadarValueRenderers
{
	/** 한쪽 값 칸. Other = 반대쪽 값(텍스트 diff·성분 강조용), bOld = 왼쪽(이전) 칸인지. */
	TSharedRef<SWidget> MakeValueCell(const FRadarPropertyDiff& Row, bool bOld);

	/** JSON 색 값({R,G,B,A} 0~1 또는 FColor 0~255) → 선형 색. 색이 아니면 false. */
	bool TryGetColor(const FString& Type, const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor);

	/** 단어 단위 LCS diff. 반환: (단어, 상태) 상태 0=같음 1=이 쪽에만. */
	TArray<TPair<FString, int32>> WordDiff(const FString& This, const FString& Other);

	/** 애셋 경로 → 짧은 이름 (/Game/UI/T_X.T_X → T_X). */
	FString ShortAssetName(const FString& Path);
}
