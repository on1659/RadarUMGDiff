// Copyright Radar. All Rights Reserved.

#include "Viewer/SRadarRevisionPicker.h"
#include "RadarUMGDiffEditorModule.h"
#include "RadarUMGDiffLog.h"

#include "WidgetBlueprint.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "RadarUMGDiff"

namespace RadarPickerColumns
{
	static const FName Rev(TEXT("Rev"));
	static const FName CL(TEXT("CL"));
	static const FName Date(TEXT("Date"));
	static const FName User(TEXT("User"));
	static const FName Desc(TEXT("Desc"));
}

namespace
{
	class SRadarRevisionRow : public SMultiColumnTableRow<TSharedPtr<FRadarRevision>>
	{
	public:
		SLATE_BEGIN_ARGS(SRadarRevisionRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& Owner, TSharedPtr<FRadarRevision> InItem)
		{
			Item = InItem;
			SMultiColumnTableRow<TSharedPtr<FRadarRevision>>::Construct(FSuperRowType::FArguments().Padding(FMargin(2.f, 1.f)), Owner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			const FRadarRevision& R = *Item;
			FText Text;
			if (Column == RadarPickerColumns::Rev)
			{
				switch (R.Kind)
				{
				case ERadarRevisionKind::Working: Text = R.GetLabel(); break;
				case ERadarRevisionKind::Head:    Text = FText::Format(LOCTEXT("RowHead", "HEAD {0}"), FText::FromString(R.ShortId)); break;
				case ERadarRevisionKind::Scm:     Text = FText::Format(LOCTEXT("RowScm", "#{0}"), FText::FromString(R.ShortId)); break;
				default:                          Text = FText::FromString(R.ShortId); break;
				}
			}
			else if (Column == RadarPickerColumns::CL)
			{
				Text = R.Changelist >= 0 ? FText::AsNumber(R.Changelist, &FNumberFormattingOptions::DefaultNoGrouping()) : FText::GetEmpty();
			}
			else if (Column == RadarPickerColumns::Date)
			{
				// 백엔드는 UTC 로 준다(git ParseIso8601, 프로바이더 유닉스 시각) → 이 PC 의 로컬 시각으로 보인다.
				const FDateTime Local = R.Date + (FDateTime::Now() - FDateTime::UtcNow());
				Text = R.Date.GetTicks() > 0 ? FText::FromString(Local.ToString(TEXT("%Y-%m-%d %H:%M"))) : FText::GetEmpty();
			}
			else if (Column == RadarPickerColumns::User)
			{
				Text = FText::FromString(R.User);
			}
			else if (Column == RadarPickerColumns::Desc)
			{
				Text = FText::FromString(R.Description.Replace(TEXT("\n"), TEXT(" ")));
			}
			return SNew(SBox).Padding(FMargin(4.f, 2.f))
				[
					SNew(STextBlock).Text(Text).ToolTipText(Text)
				];
		}

	private:
		TSharedPtr<FRadarRevision> Item;
	};
}

void SRadarRevisionPicker::Construct(const FArguments& InArgs, UWidgetBlueprint* InBlueprint)
{
	Blueprint = InBlueprint;
	ParentWindow = InArgs._ParentWindow;

	FString Filename;
	if (InBlueprint && FPackageName::TryConvertLongPackageNameToFilename(InBlueprint->GetOutermost()->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		AssetFile = FPaths::ConvertRelativePathToFull(Filename);
	}

	{
		FScopedSlowTask Task(3.f, LOCTEXT("ProbeSources", "리비전 백엔드 확인 중"));
		Task.MakeDialogDelayed(0.5f);
		for (const TSharedRef<IRadarRevisionSource>& Source : RadarRevisionSources::MakeAll())
		{
			Task.EnterProgressFrame(1.f);
			if (Source->IsAvailableFor(AssetFile))
			{
				Sources.Add(Source);
			}
		}
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			.Text(FText::Format(LOCTEXT("PickerTitle", "{0} — 비교할 두 리비전"), FText::FromString(InBlueprint ? InBlueprint->GetName() : FString())))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f)
		[
			MakeSourceTabs()
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(8.f, 4.f)
		[
			SAssignNew(BodyBox, SBox)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(this, &SRadarRevisionPicker::GetFooterText)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FLinearColor(1.f, 0.45f, 0.35f))
			.AutoWrapText(true)
			.Text_Lambda([this]() { return StatusText; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 4.f, 8.f, 8.f).HAlign(HAlign_Fill)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenJson", "diff.json 열기..."))
				.ToolTipText(LOCTEXT("OpenJsonTip", "다른 생산자(Python 분석기·커맨드릿)가 만든 diff.json 을 data-only 로 엽니다"))
				.OnClicked(this, &SRadarRevisionPicker::OnOpenJson)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)[ SNullWidget::NullWidget ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CompareWithPrevious", "이전과 비교"))
				.ToolTipText(LOCTEXT("CompareWithPreviousTip", "작업 사본을 그 이전 리비전과 비교합니다 (작업 사본이 수정됐으면 최신 체크인과)"))
				.IsEnabled_Lambda([this]() { return Sources.IsValidIndex(ActiveSource) && Sources[ActiveSource]->GetId() != FName(TEXT("file")); })
				.OnClicked(this, &SRadarRevisionPicker::OnCompareWithPrevious)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CompareWithWorking", "작업 사본과 비교"))
				.IsEnabled_Lambda([this]() { return List.IsValid() && List->GetNumItemsSelected() == 1; })
				.OnClicked(this, &SRadarRevisionPicker::OnCompareWithWorking)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CompareSelected", "선택한 둘 비교"))
				.IsEnabled_Lambda([this]() { return List.IsValid() && List->GetNumItemsSelected() == 2; })
				.OnClicked(this, &SRadarRevisionPicker::OnCompareSelected)
			]
		]
	];

	SelectSource(Sources.Num() > 0 ? 0 : INDEX_NONE);
}

TSharedRef<SWidget> SRadarRevisionPicker::MakeSourceTabs()
{
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked_Lambda([this, Index]() { return ActiveSource == Index ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Index](ECheckBoxState) { SelectSource(Index); })
			[
				SNew(SBox).Padding(FMargin(10.f, 4.f))
				[
					SNew(STextBlock).Text(Sources[Index]->GetDisplayName())
				]
			]
		];
	}
	return Box;
}

void SRadarRevisionPicker::SelectSource(int32 Index)
{
	ActiveSource = Index;
	StatusText = FText::GetEmpty();
	Items.Reset();

	if (!Sources.IsValidIndex(ActiveSource))
	{
		RebuildBody();
		return;
	}
	IRadarRevisionSource& Source = *Sources[ActiveSource];
	if (Source.GetId() == FName(TEXT("file")))
	{
		RebuildBody();
		return;
	}

	FScopedSlowTask Task(2.f, LOCTEXT("FetchHistory", "히스토리 받는 중"));
	Task.MakeDialogDelayed(0.3f);
	Task.EnterProgressFrame(1.f);

	TSharedPtr<FRadarRevision> Working = MakeShared<FRadarRevision>();
	Working->Kind = ERadarRevisionKind::Working;
	Working->bDirty = Source.IsWorkingCopyDirty(AssetFile);
	Items.Add(Working);

	if (Working->bDirty && Source.GetId() == FName(TEXT("git")))
	{
		FRadarRevision Head;
		if (RadarRevisionSources::MakeGitHead(AssetFile, Head))
		{
			Items.Add(MakeShared<FRadarRevision>(Head));
		}
	}

	Task.EnterProgressFrame(1.f);
	FString Error;
	for (FRadarRevision& R : Source.GetHistory(AssetFile, Error))
	{
		Items.Add(MakeShared<FRadarRevision>(MoveTemp(R)));
	}
	if (!Error.IsEmpty())
	{
		StatusText = FText::FromString(Error);
	}
	RebuildBody();
}

void SRadarRevisionPicker::RebuildBody()
{
	if (!BodyBox.IsValid())
	{
		return;
	}
	if (!Sources.IsValidIndex(ActiveSource))
	{
		BodyBox->SetContent(SNew(STextBlock).Text(LOCTEXT("NoSource", "쓸 수 있는 리비전 백엔드가 없습니다.")));
		return;
	}
	if (Sources[ActiveSource]->GetId() == FName(TEXT("file")))
	{
		List.Reset();
		BodyBox->SetContent(MakeFilesForm());
		return;
	}

	const bool bScm = Sources[ActiveSource]->GetId() == FName(TEXT("scm"));
	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
		+ SHeaderRow::Column(RadarPickerColumns::Rev).DefaultLabel(bScm ? LOCTEXT("ColRev", "리비전") : LOCTEXT("ColSha", "커밋")).FillWidth(0.14f);
	if (bScm)
	{
		Header->AddColumn(SHeaderRow::Column(RadarPickerColumns::CL).DefaultLabel(LOCTEXT("ColCL", "CL")).FillWidth(0.08f));
	}
	Header->AddColumn(SHeaderRow::Column(RadarPickerColumns::Date).DefaultLabel(LOCTEXT("ColDate", "날짜")).FillWidth(0.16f));
	Header->AddColumn(SHeaderRow::Column(RadarPickerColumns::User).DefaultLabel(bScm ? LOCTEXT("ColUser", "사용자") : LOCTEXT("ColAuthor", "작성자")).FillWidth(0.14f));
	Header->AddColumn(SHeaderRow::Column(RadarPickerColumns::Desc).DefaultLabel(bScm ? LOCTEXT("ColDesc", "설명") : LOCTEXT("ColSubject", "제목")).FillWidth(0.48f));

	BodyBox->SetContent(
		SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(0.f)
		[
			SAssignNew(List, SListView<FItem>)
			.ListItemsSource(&Items)
			.SelectionMode(ESelectionMode::Multi)
			.HeaderRow(Header)
			.OnGenerateRow(this, &SRadarRevisionPicker::OnGenerateRow)
			.OnMouseButtonDoubleClick_Lambda([this](FItem Item)
			{
				if (Item.IsValid() && Item->Kind != ERadarRevisionKind::Working)
				{
					OpenDiff(*Item, *Items[0]);
				}
			})
		]);
}

TSharedRef<ITableRow> SRadarRevisionPicker::OnGenerateRow(FItem Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SRadarRevisionRow, Owner, Item);
}

TSharedRef<SWidget> SRadarRevisionPicker::MakeFilesForm()
{
	auto MakeRow = [this](FText Label, bool bOld) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox).WidthOverride(60.f)[ SNew(STextBlock).Text(Label) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.HintText(bOld ? LOCTEXT("HintOld", "이전 .uasset 경로") : LOCTEXT("HintNew", "비우면 작업 사본"))
				.Text_Lambda([this, bOld]() { return FText::FromString(bOld ? FileOld : FileNew); })
				.OnTextCommitted_Lambda([this, bOld](const FText& T, ETextCommit::Type) { (bOld ? FileOld : FileNew) = T.ToString().TrimQuotes(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("Browse", "찾기...")).OnClicked(this, &SRadarRevisionPicker::OnBrowse, bOld)
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock).AutoWrapText(true)
			.Text(LOCTEXT("FilesHelp", "어떤 방법으로든 얻은 .uasset 두 개를 비교합니다 (P4V \"Get Revision\", SVN export, 백업 사본 등). 파일은 Saved/Diff 아래로 복사해서 엽니다."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[ MakeRow(LOCTEXT("OldLabel", "이전"), true) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[ MakeRow(LOCTEXT("NewLabel", "이후"), false) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f).HAlign(HAlign_Left)
		[
			SNew(SButton).Text(LOCTEXT("CompareFiles", "두 파일 비교")).OnClicked(this, &SRadarRevisionPicker::OnCompareFiles)
		];
}

FReply SRadarRevisionPicker::OnBrowse(bool bOld)
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop)
	{
		return FReply::Handled();
	}
	TArray<FString> Picked;
	const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (Desktop->OpenFileDialog(ParentHandle, TEXT("위젯 블루프린트 .uasset"), FPaths::GetPath(AssetFile), TEXT(""), TEXT("Unreal Asset (*.uasset)|*.uasset"), EFileDialogFlags::None, Picked) && Picked.Num() > 0)
	{
		(bOld ? FileOld : FileNew) = FPaths::ConvertRelativePathToFull(Picked[0]);
	}
	return FReply::Handled();
}

TSharedPtr<IRadarRevisionSource> SRadarRevisionPicker::SourceFor(const FRadarRevision& Revision) const
{
	const FName Want = Revision.Kind == ERadarRevisionKind::Scm ? FName(TEXT("scm"))
		: (Revision.Kind == ERadarRevisionKind::File ? FName(TEXT("file")) : FName(TEXT("git")));
	for (const TSharedRef<IRadarRevisionSource>& Source : Sources)
	{
		if (Source->GetId() == Want)
		{
			return Source;
		}
	}
	return nullptr;
}

FReply SRadarRevisionPicker::OnCompareWithPrevious()
{
	// 작업 사본이 깨끗하면 = 최신 체크인이므로 그 이전 리비전과, 수정됐으면 최신 체크인과 비교한다.
	TArray<FItem> History;
	for (const FItem& Item : Items)
	{
		if (Item->Kind == ERadarRevisionKind::Scm || Item->Kind == ERadarRevisionKind::Commit)
		{
			History.Add(Item);
		}
	}
	if (Items.Num() == 0)
	{
		return FReply::Handled();
	}
	const FRadarRevision& Working = *Items[0];
	const int32 OldIndex = Working.bDirty ? 0 : 1;
	if (!History.IsValidIndex(OldIndex))
	{
		StatusText = LOCTEXT("NoPrevious", "비교할 이전 리비전이 없습니다.");
		return FReply::Handled();
	}
	OpenDiff(*History[OldIndex], Working);
	return FReply::Handled();
}

FReply SRadarRevisionPicker::OnCompareWithWorking()
{
	TArray<FItem> Selected = List.IsValid() ? List->GetSelectedItems() : TArray<FItem>();
	if (Selected.Num() == 1 && Items.Num() > 0)
	{
		OpenDiff(*Selected[0], *Items[0]);
	}
	return FReply::Handled();
}

FReply SRadarRevisionPicker::OnCompareSelected()
{
	TArray<FItem> Selected = List.IsValid() ? List->GetSelectedItems() : TArray<FItem>();
	if (Selected.Num() != 2)
	{
		return FReply::Handled();
	}
	// 목록은 최신 → 과거. 아래쪽(인덱스가 큰 쪽)이 이전.
	const int32 A = Items.IndexOfByKey(Selected[0]);
	const int32 B = Items.IndexOfByKey(Selected[1]);
	const FItem Old = A > B ? Selected[0] : Selected[1];
	const FItem New = A > B ? Selected[1] : Selected[0];
	OpenDiff(*Old, *New);
	return FReply::Handled();
}

FReply SRadarRevisionPicker::OnCompareFiles()
{
	if (FileOld.IsEmpty())
	{
		StatusText = LOCTEXT("NeedOldFile", "이전 파일을 지정하세요.");
		return FReply::Handled();
	}
	FRadarRevision Old;
	Old.Kind = ERadarRevisionKind::File;
	Old.LocalFile = FileOld;
	FRadarRevision New;
	if (FileNew.IsEmpty())
	{
		New.Kind = ERadarRevisionKind::Working;
	}
	else
	{
		New.Kind = ERadarRevisionKind::File;
		New.LocalFile = FileNew;
	}
	OpenDiff(Old, New);
	return FReply::Handled();
}

void SRadarRevisionPicker::OpenDiff(const FRadarRevision& Old, const FRadarRevision& New)
{
	StatusText = FText::GetEmpty();
	UWidgetBlueprint* BP = Blueprint.Get();
	if (!BP)
	{
		StatusText = LOCTEXT("BlueprintGone", "에셋이 언로드됐습니다.");
		return;
	}
	FString Error;
	TSharedPtr<FRadarDiffSession> Session;
	{
		FScopedSlowTask Task(1.f, LOCTEXT("LoadingRevisions", "두 리비전을 받아 로드하는 중"));
		Task.MakeDialog();
		Task.EnterProgressFrame(1.f);
		Session = FRadarRevisionLoader::Load(SourceFor(Old), Old, SourceFor(New), New, BP, AssetFile, Error);
	}
	if (!Session.IsValid())
	{
		StatusText = FText::FromString(Error);
		UE_LOG(LogRadarUMGDiff, Warning, TEXT("비교 열기 실패: %s"), *Error);
		return;
	}
	FRadarUMGDiffEditorModule::OpenDiffWindow(Session.ToSharedRef());
}

FReply SRadarRevisionPicker::OnOpenJson()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	TArray<FString> Picked;
	const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (Desktop && Desktop->OpenFileDialog(ParentHandle, TEXT("diff.json"), FPaths::ProjectSavedDir(), TEXT("diff.json"), TEXT("diff.json (*.json)|*.json"), EFileDialogFlags::None, Picked) && Picked.Num() > 0)
	{
		FRadarUMGDiffEditorModule::OpenDiffJson(FPaths::ConvertRelativePathToFull(Picked[0]));
	}
	return FReply::Handled();
}

FText SRadarRevisionPicker::GetFooterText() const
{
	if (!Sources.IsValidIndex(ActiveSource))
	{
		return FText::GetEmpty();
	}
	return FText::Format(LOCTEXT("Footer", "이 리비전은 {0} 에서 받습니다."), Sources[ActiveSource]->GetDisplayName());
}

#undef LOCTEXT_NAMESPACE
