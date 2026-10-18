// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarSummaryStrip.h"
#include "Viewer/RadarViewModel.h"
#include "RadarDiffJson.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

void SRadarSummaryStrip::Construct(const FArguments& InArgs, TSharedRef<FRadarViewModel> InModel)
{
	Model = InModel;
	TMap<FString, int32> ByKind, ByArea;
	RadarDiffJson::CountSummary(*Model->Diff, ByKind, ByArea);
	ByKind.FindOrAdd(TEXT("text-key-only")) = Model->Diff->TextKeyCount;

	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
	// 종류 칩. text-key-only 는 Python 생산자만 낸다 → 선언 안 한 생산자에서는 회색 (§5 capabilities)
	const TPair<const TCHAR*, FText> Kinds[] = {
		{ TEXT("added"), LOCTEXT("ChipAdded", "추가") }, { TEXT("removed"), LOCTEXT("ChipRemoved", "삭제") },
		{ TEXT("changed"), LOCTEXT("ChipChanged", "변경") }, { TEXT("moved"), LOCTEXT("ChipMoved", "이동") },
		{ TEXT("renamed-probable"), LOCTEXT("ChipRenamed", "이름 변경(추정)") }, { TEXT("text-key-only"), LOCTEXT("ChipTextKey", "텍스트 키만") } };
	for (const TPair<const TCHAR*, FText>& K : Kinds)
	{
		const TCHAR* Cap = FCString::Strcmp(K.Key, TEXT("text-key-only")) == 0 ? TEXT("text-key-only") : nullptr;
		Chips->AddSlot()[ MakeChip(K.Key, K.Value, ByKind.FindRef(K.Key), false, Cap) ];
	}
	Chips->AddSlot()[ SNew(SBox).WidthOverride(12) ];
	const TPair<const TCHAR*, FText> Areas[] = {
		{ TEXT("designer"), LOCTEXT("ChipDesigner", "디자이너") }, { TEXT("slot"), LOCTEXT("ChipSlot", "슬롯") },
		{ TEXT("binding"), LOCTEXT("ChipBinding", "바인딩") }, { TEXT("cdo"), LOCTEXT("ChipCdo", "CDO") },
		{ TEXT("anim"), LOCTEXT("ChipAnim", "애니메이션") }, { TEXT("graph-logic"), LOCTEXT("ChipGraphLogic", "그래프 로직") },
		{ TEXT("graph-layout"), LOCTEXT("ChipGraphLayout", "그래프 배치") } };
	for (const TPair<const TCHAR*, FText>& A : Areas)
	{
		const bool bGraph = FCString::Strncmp(A.Key, TEXT("graph"), 5) == 0;
		Chips->AddSlot()[ MakeChip(A.Key, A.Value, ByArea.FindRef(A.Key), true, bGraph ? TEXT("graph") : nullptr) ];
	}

	auto Check = [this](FText Label, FText Tip, bool* Flag) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.ToolTipText(Tip)
			.IsChecked_Lambda([Flag]() { return *Flag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Flag](ECheckBoxState S)
			{
				*Flag = S == ECheckBoxState::Checked;
				Model->Refresh();
				Model->OnFilterChanged.Broadcast();
			})
			[
				SNew(STextBlock).Text(Label)
			];
	};

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ Chips ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
		[
			SNew(SBox).WidthOverride(220)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "이름·라벨·속성·값 검색"))
				.OnTextChanged_Lambda([this](const FText& T)
				{
					Model->SearchText = T.ToString();
					Model->Refresh();
					Model->OnFilterChanged.Broadcast();
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
		[ Check(LOCTEXT("ChangedOnly", "변경만"), LOCTEXT("ChangedOnlyTip", "바뀌지 않았고 바뀐 자손도 없는 행을 숨깁니다"), &Model->bChangedOnly) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
		[ Check(LOCTEXT("DimUnchanged", "변경 없음 흐리게"), LOCTEXT("DimUnchangedTip", "미리보기에서 바뀐 상자 밖을 어둡게 합니다"), &Model->bDimUnchanged) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
		[ Check(LOCTEXT("HideAuto", "자동 값 숨기기"), LOCTEXT("HideAutoTip", "디자이너가 자동으로 바꾸기도 하는 값(ini AutoProperties: bIsVariable 등)의 변경을 가립니다"), &Model->bHideAuto) ]
	];
}

void SRadarSummaryStrip::Toggle(TSet<FString>& Set, const FString& Key)
{
	if (Set.Contains(Key)) { Set.Remove(Key); } else { Set.Add(Key); }
	Model->Refresh();
	Model->OnFilterChanged.Broadcast();
}

TSharedRef<SWidget> SRadarSummaryStrip::MakeChip(const FString& Key, const FText& Label, int32 Count, bool bArea, const TCHAR* Capability)
{
	const bool bSupported = !Capability || Model->HasCapability(Capability);
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
		.IsEnabled(bSupported)
		.ToolTipText(bSupported ? FText::GetEmpty() : LOCTEXT("ChipUnsupported", "이 생산자는 이 종류를 내지 않습니다"))
		.IsChecked_Lambda([this, Key, bArea]() { return (bArea ? Model->AreaFilter : Model->KindFilter).Contains(Key) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Key, bArea](ECheckBoxState) { Toggle(bArea ? Model->AreaFilter : Model->KindFilter, Key); })
		[
			SNew(SBox).Padding(FMargin(8, 2))
			[
				SNew(STextBlock)
				.Text(bSupported ? FText::Format(LOCTEXT("ChipFmt", "{0} {1}"), Label, FText::AsNumber(Count)) : FText::Format(LOCTEXT("ChipFmtNA", "{0} —"), Label))
				.ColorAndOpacity(bSupported ? FSlateColor::UseForeground() : FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			]
		];
}

#undef LOCTEXT_NAMESPACE
