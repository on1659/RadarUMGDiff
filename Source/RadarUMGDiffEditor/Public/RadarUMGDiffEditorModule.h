// Copyright Radar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UWidgetBlueprint;

class FRadarUMGDiffEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 리비전 피커 창을 연다. */
	static void OpenRevisionPicker(UWidgetBlueprint* Blueprint);

	/** 로드된 세션으로 비교 창(뷰어)을 연다. */
	static void OpenDiffWindow(const TSharedRef<class FRadarDiffSession>& Session);

	/** 어느 생산자의 diff.json 이든 data-only 뷰어로 연다 (§9.8). */
	static bool OpenDiffJson(const FString& Path);

	/** 같은 두 파일을 번들 Python 으로 분석해(umgdiff_cli.py diff) 그 diff.json 을 data-only 로 연다 (dev 문서 §10.3). */
	static bool RerunWithPython(const FRadarDiffSession& Session, FString& OutError);

private:
	void RegisterMenus();
};
