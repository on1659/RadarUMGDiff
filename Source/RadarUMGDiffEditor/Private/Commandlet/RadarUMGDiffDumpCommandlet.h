// Copyright Radar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RadarUMGDiffDumpCommandlet.generated.h"

/**
 * UI 없이 네이티브 생산자를 돈다 (dev 문서 §10.1).
 *   UnrealEditor-Cmd.exe <Project> -run=RadarUMGDiffDump -asset=/Game/UI/WBP_HUD -old=git:347f315 -new=working -out=<dir> [-state] [-scmprovider=Perforce]
 * 정확도 모드: -history -out=<dir> [-asset=<패키지>] — 모든 WBP(또는 그 패키지)의 git 인접 커밋 쌍마다 diff.json.
 * 리비전: working | head | git:<sha> | scm:<리비전> | file:<경로>
 * 종료 코드 0 성공, 2 로드 실패, 3 인자 오류.
 */
UCLASS()
class URadarUMGDiffDumpCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URadarUMGDiffDumpCommandlet();
	virtual int32 Main(const FString& Params) override;
};
