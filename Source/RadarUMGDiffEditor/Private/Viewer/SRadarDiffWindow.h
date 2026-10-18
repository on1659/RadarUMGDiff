// Copyright Radar. All Rights Reserved.

#pragma once

// 뷰어 창 루트 (dev 문서 §9.1): 제목 띠 · 요약 띠 · 탭/모드 · [계층 | 미리보기·탭 | 상세].

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Viewer/RadarViewModel.h"

class SBox;
class SRadarHierarchy;
class SRadarDetailsGrid;
class SRadarPreviewPane;
class SRadarAnimationTab;
class SRadarGraphTab;
class SWidgetSwitcher;

class SRadarDiffWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRadarDiffWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel);
	virtual ~SRadarDiffWindow();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	/** 터널 단계: 포커스된 위젯(체크박스 등)이 Space 를 먹기 전에 받는다. 텍스트 입력 중이면 넘긴다. */
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** 세션 (미리보기 있음) 으로 창을 연다. */
	static TSharedRef<SWindow> Open(TSharedRef<FRadarViewModel> Model);

private:
	TSharedPtr<FRadarViewModel> Model;
	TSharedPtr<SRadarHierarchy> Hierarchy;
	TSharedPtr<SRadarDetailsGrid> Details;
	TSharedPtr<SRadarAnimationTab> AnimTab;
	TSharedPtr<SRadarGraphTab> GraphTab;
	TSharedPtr<SBox> PreviewArea;
	TSharedPtr<SWidgetSwitcher> CenterSwitcher;
	TArray<TSharedPtr<SRadarPreviewPane>> Panes;
	FDelegateHandle SelectionHandle;
	FDelegateHandle FilterHandle;

	void RebuildPreviews();
	void OnSelectionChanged();
	void OnFilterChanged();
	void Step(int32 Delta);
	void FocusSelected();
	void SetMode(ERadarPreviewMode Mode);
	void ToggleFlip();
	TSharedRef<SWidget> MakeTitleStrip();
	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> PaneWithLabel(TSharedRef<SRadarPreviewPane> Pane, const TAttribute<FText>& Label);
	FText SideLabel(bool bOld) const;
};
