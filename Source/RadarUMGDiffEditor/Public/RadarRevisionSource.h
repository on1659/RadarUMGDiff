// Copyright Radar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class UPackage;
class UWidgetBlueprint;
class UUserWidget;
class FPreviewScene;
class SWindow;

/** 리비전 하나. 어느 백엔드가 만들었는지 Kind 로 숨기지 않는다 (diff.json old/new.kind 와 같은 값). */
enum class ERadarRevisionKind : uint8
{
	Scm,      // 엔진 소스 컨트롤 프로바이더 리비전
	Commit,   // git CLI 커밋
	Head,     // git HEAD (작업 사본이 더러울 때만 후보)
	Working,  // 현재 에디터에 로드된 그 에셋
	File,     // 사용자가 지정한 파일
};

RADARUMGDIFFEDITOR_API const TCHAR* LexToString(ERadarRevisionKind Kind);

struct RADARUMGDIFFEDITOR_API FRadarRevision
{
	ERadarRevisionKind Kind = ERadarRevisionKind::Working;
	FString Id;              // sha(전체) 또는 프로바이더 리비전 문자열
	FString ShortId;         // sha7, 리비전 문자열 그대로
	int32 Changelist = -1;
	FDateTime Date;
	FString User;
	FString Description;
	FString LocalFile;       // Kind == File 일 때 원본 경로
	FString RepoPath;        // Kind == Commit/Head 일 때 그 커밋에서의 저장소 상대 경로 (--follow 로 이름이 바뀐 경우 대비)
	FString ProviderName;    // Kind == Scm 일 때 "Perforce" 등
	bool bDirty = false;     // Kind == Working 일 때 마지막 체크인과 다른지

	/** 피커·창 제목에 쓰는 한 줄 이름. */
	FText GetLabel() const;
};

/** 옛 리비전 파일을 얻는 백엔드 (dev 문서 §8.0). 순서: 프로바이더 → git CLI → 파일 두 개. */
class RADARUMGDIFFEDITOR_API IRadarRevisionSource
{
public:
	virtual ~IRadarRevisionSource() = default;

	virtual FName GetId() const = 0;
	virtual FText GetDisplayName() const = 0;
	/** AssetFile = 에셋의 절대 디스크 경로. */
	virtual bool IsAvailableFor(const FString& AssetFile) = 0;
	/** 최신 → 과거. 작업 사본·HEAD 항목은 넣지 않는다(피커가 붙인다). */
	virtual TArray<FRadarRevision> GetHistory(const FString& AssetFile, FString& OutError) = 0;
	/** 작업 사본이 마지막 체크인과 다른지. */
	virtual bool IsWorkingCopyDirty(const FString& AssetFile) = 0;
	/** 리비전을 OutTempFile(Saved/Diff 아래, 우리 이름 규칙) 에 받는다. 이미 있으면 재사용. */
	virtual bool Fetch(const FRadarRevision& Revision, const FString& AssetFile, FString& OutTempFile, FString& OutError) = 0;
};

namespace RadarRevisionSources
{
	/** §8.0 순서로 백엔드 셋을 만든다. */
	RADARUMGDIFFEDITOR_API TArray<TSharedRef<IRadarRevisionSource>> MakeAll();

	RADARUMGDIFFEDITOR_API TSharedRef<IRadarRevisionSource> MakeProvider();
	RADARUMGDIFFEDITOR_API TSharedRef<IRadarRevisionSource> MakeGit();
	RADARUMGDIFFEDITOR_API TSharedRef<IRadarRevisionSource> MakeFiles();

	/** git HEAD 리비전 (작업 사본이 더러울 때 피커가 목록 맨 위에 붙인다). 저장소가 아니면 false. */
	RADARUMGDIFFEDITOR_API bool MakeGitHead(const FString& AssetFile, FRadarRevision& OutHead);

	/** 리비전 지정 문자열(working | head | git:<sha> | scm:<리비전> | file:<경로>) → 리비전 + 그것을 받을 백엔드. 콘솔 명령·커맨드릿 공용. */
	RADARUMGDIFFEDITOR_API bool ResolveSpec(const FString& Spec, const FString& AssetFile, FRadarRevision& OutRev, TSharedPtr<IRadarRevisionSource>& OutSource, FString& OutError);

	/** Saved/Diff/temp-<Id>-<AssetName>.uasset. Id 의 파일명 금지 문자는 '_' 로 바꾼다. */
	RADARUMGDIFFEDITOR_API FString MakeTempFilePath(const FString& Id, const FString& AssetFile);
}

/**
 * 비교 한 번의 수명 (dev 문서 §8.6). 두 패키지·두 블루프린트·두 미리보기를 GC 로부터 붙잡고,
 * 창이 닫히면 놓는다. 작업 사본 쪽 패키지는 null (그 에셋은 에디터 소유).
 */
class RADARUMGDIFFEDITOR_API FRadarDiffSession : public FGCObject, public TSharedFromThis<FRadarDiffSession>
{
public:
	FRadarDiffSession();
	virtual ~FRadarDiffSession();

	FRadarRevision OldRevision;
	FRadarRevision NewRevision;
	FString AssetFile;            // 원본 에셋 절대 경로
	FString AssetPackageName;     // /Game/...
	FString OldFile;              // 각 쪽의 디스크 파일 (임시 파일, 작업 사본이면 AssetFile) — Python 분석기로 다시 만들기(§10.3)
	FString NewFile;

	TObjectPtr<UPackage> OldPackage = nullptr;
	TObjectPtr<UPackage> NewPackage = nullptr;
	TObjectPtr<UWidgetBlueprint> OldBlueprint = nullptr;
	TObjectPtr<UWidgetBlueprint> NewBlueprint = nullptr;
	TObjectPtr<UUserWidget> OldPreview = nullptr;
	TObjectPtr<UUserWidget> NewPreview = nullptr;

	/** 두 미리보기가 같이 쓰는 월드. */
	TUniquePtr<FPreviewScene> PreviewScene;

	/** 세션을 붙잡고 있는 창들. 모두 닫혀야 Release 한다. */
	TArray<TWeakPtr<SWindow>> Windows;

	/** 두 미리보기 UUserWidget 을 만든다 (스파이크 2). */
	bool CreatePreviews(FString& OutError);

	/** 미리보기 파기, 임시 패키지 놓기, 설정에 따라 임시 파일 삭제. 여러 번 불러도 된다. */
	void Release();

	/** 창 하나를 세션에 묶는다. 묶인 창이 모두 닫히면 Release + 레지스트리에서 빠진다. */
	void AttachWindow(const TSharedRef<SWindow>& Window);

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FRadarDiffSession"); }

	/** 열려 있는 세션들 (창이 닫힐 때까지 살려 둔다). */
	static TArray<TSharedRef<FRadarDiffSession>>& Registry();

private:
	void OnWindowClosed(const TSharedRef<SWindow>& Window);
	bool IsPackageHeldByOtherSession(const UPackage* Package) const;
	TArray<FString> TempFiles;
	bool bReleased = false;

	friend struct FRadarRevisionLoader;
};

/** 리비전 둘 → 임시 파일 → LoadPackageForDiff → 세션 (dev 문서 §8.5). */
struct RADARUMGDIFFEDITOR_API FRadarRevisionLoader
{
	/**
	 * Source 가 null 이면 Working/File 리비전만 처리할 수 있다.
	 * 실패하면 null 과 OutError.
	 */
	static TSharedPtr<FRadarDiffSession> Load(
		const TSharedPtr<IRadarRevisionSource>& OldSource, const FRadarRevision& OldRevision,
		const TSharedPtr<IRadarRevisionSource>& NewSource, const FRadarRevision& NewRevision,
		UWidgetBlueprint* WorkingBlueprint, const FString& AssetFile, FString& OutError);

private:
	static UWidgetBlueprint* LoadOne(const TSharedPtr<IRadarRevisionSource>& Source, const FRadarRevision& Revision,
		UWidgetBlueprint* WorkingBlueprint, const FString& AssetFile, FRadarDiffSession& Session,
		TObjectPtr<UPackage>& OutPackage, FString& OutError);
};
