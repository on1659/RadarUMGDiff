// Copyright Radar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RadarStructKindsCommandlet.generated.h"

/**
 * 구조체 종류 표를 만든다 (dev 문서 §10.2, §11.6). FileVersionUE5 < 1012 의 옛 태그에는 구조체 몸체가 태그 스트림인지
 * 네이티브 바이트인지가 없어서, Python 분석기는 이 표로 정한다.
 *   UnrealEditor-Cmd.exe <Project> -run=RadarStructKinds [-out=<경로>]
 * 기본 출력: <Project>/Saved/RadarUMGDiff/struct_kinds.json. 결과를 Content/Python/radar_umgdiff/data/struct_kinds_<engine>.json 으로 복사해 커밋한다.
 * 종료 코드 0 성공, 2 쓰기 실패.
 */
UCLASS()
class URadarStructKindsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URadarStructKindsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
