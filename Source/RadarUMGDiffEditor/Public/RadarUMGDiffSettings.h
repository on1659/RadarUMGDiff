// Copyright Radar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RadarUMGDiffSettings.generated.h"

/**
 * RadarUMGDiff 의 조절값. 정본은 플러그인 폴더의 Config/DefaultRadarUMGDiff.ini 이고,
 * 빌드 없이 ini 만 고쳐 에디터를 다시 켜면 반영된다. Python 분석기도 같은 파일을 읽는다(dev 문서 §12).
 */
UCLASS(config = RadarUMGDiff, meta = (DisplayName = "Radar UMG Diff"))
class RADARUMGDIFFEDITOR_API URadarUMGDiffSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const URadarUMGDiffSettings& Get() { return *GetDefault<URadarUMGDiffSettings>(); }

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// ── 오버레이 색 ──
	UPROPERTY(config, EditAnywhere, Category = "색")
	FLinearColor ColorAdded = FLinearColor(0.25f, 0.85f, 0.35f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "색")
	FLinearColor ColorRemoved = FLinearColor(0.95f, 0.2f, 0.2f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "색")
	FLinearColor ColorChanged = FLinearColor(1.f, 0.8f, 0.15f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "색")
	FLinearColor ColorMoved = FLinearColor(0.3f, 0.55f, 1.f, 1.f);

	UPROPERTY(config, EditAnywhere, Category = "색")
	FLinearColor ColorSelected = FLinearColor::White;

	// ── 선·불투명도 ──
	UPROPERTY(config, EditAnywhere, Category = "오버레이")
	float OutlineThickness = 2.f;

	UPROPERTY(config, EditAnywhere, Category = "오버레이")
	float HaloThickness = 1.f;

	UPROPERTY(config, EditAnywhere, Category = "오버레이")
	float RemovedBoxDashLength = 6.f;

	UPROPERTY(config, EditAnywhere, Category = "오버레이")
	float DimUnchangedOpacity = 0.55f;

	UPROPERTY(config, EditAnywhere, Category = "오버레이")
	float OverlayOpacity = 0.5f;

	/** SideBySide | Overlay | Flip */
	UPROPERTY(config, EditAnywhere, Category = "미리보기")
	FString DefaultPreviewMode = TEXT("SideBySide");

	UPROPERTY(config, EditAnywhere, Category = "미리보기")
	bool bExecutePreConstruct = false;

	// ── diff 규칙 (Python 분석기와 공유) ──
	UPROPERTY(config, EditAnywhere, Category = "비교 규칙")
	double RenameThreshold = 0.8;

	UPROPERTY(config, EditAnywhere, Category = "비교 규칙")
	double FloatTolerance = 1e-6;

	UPROPERTY(config, EditAnywhere, Category = "비교 규칙")
	double DoubleTolerance = 1e-12;

	/** 비교에서 아예 빼는 속성 이름(엔진 내부 값). */
	UPROPERTY(config, EditAnywhere, Category = "비교 규칙")
	TArray<FString> IgnoredProperties;

	/** 디자이너가 자동으로 바꾸기도 하는 속성. diff 에는 auto 표시를 달아 내고, 뷰어 토글로 보이거나 숨긴다. */
	UPROPERTY(config, EditAnywhere, Category = "비교 규칙")
	TArray<FString> AutoProperties;

	/**
	 * 한 줄(성분 강조)로 보이는 잎 구조체 이름. 여기 없는 구조체는 멤버로 내려가 멤버마다 한 줄이 된다.
	 * 두 생산자가 같은 줄 경로를 내도록 Python 도 이 목록을 쓴다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "비교 규칙")
	TArray<FString> LeafStructs;

	// ── 임시 파일 ──
	/** 프로젝트 기준 상대 경로. /Temp/ 마운트(Saved/) 아래여야 LOAD_ForDiff 경로를 탄다. */
	UPROPERTY(config, EditAnywhere, Category = "임시 파일")
	FString TempDir = TEXT("Saved/Diff");

	UPROPERTY(config, EditAnywhere, Category = "임시 파일")
	bool bKeepTempPackages = false;

	// ── 뷰어 기본값·키 ──
	UPROPERTY(config, EditAnywhere, Category = "뷰어")
	FString NextKey = TEXT("F8");

	UPROPERTY(config, EditAnywhere, Category = "뷰어")
	FString PrevKey = TEXT("F7");

	UPROPERTY(config, EditAnywhere, Category = "뷰어")
	FString FlipKey = TEXT("SpaceBar");

	UPROPERTY(config, EditAnywhere, Category = "뷰어")
	bool bChangedOnlyDefault = true;

	UPROPERTY(config, EditAnywhere, Category = "뷰어")
	bool bDimUnchangedDefault = true;

	UPROPERTY(config, EditAnywhere, Category = "뷰어")
	bool bHideAutoDefault = true;

	// ── 외부 실행 파일 ──
	/** 엔진 폴더 기준 상대 경로 또는 절대 경로. */
	UPROPERTY(config, EditAnywhere, Category = "외부 도구")
	FString PythonExe = TEXT("Binaries/ThirdParty/Python3/Win64/python.exe");

	UPROPERTY(config, EditAnywhere, Category = "외부 도구")
	FString GitExe = TEXT("git");

	/** TempDir 의 절대 경로(끝에 / 포함). */
	FString GetTempDirAbsolute() const;
};
