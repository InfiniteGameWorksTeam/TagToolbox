// Copyright Infinite Game Works. All Rights Reserved.

#include "STagToolboxAuditPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "STagToolboxRenameDialog.h"
#include "STagToolboxResaveDialog.h"
#include "STagToolboxTagPicker.h"
#include "Styling/AppStyle.h"
#include "TagToolboxNotifications.h"
#include "TagToolboxRenameFixup.h"
#include "TagToolboxResaveService.h"
#include "TagToolboxTagScanService.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STagToolboxAuditPanel"

namespace TagToolboxAuditPanel
{
	const FName ColumnCategory(TEXT("Category"));
	const FName ColumnTag(TEXT("Tag"));
	const FName ColumnDetail(TEXT("Detail"));

	static void Notify(const FText& Message)
	{
		TagToolboxNotifications::Show(Message, 6.0f);
	}

	static FString JoinFirstNames(const TArray<FName>& Names, int32 MaxShown)
	{
		TArray<FString> Shown;
		for (int32 Index = 0; Index < Names.Num() && Index < MaxShown; ++Index)
		{
			Shown.Add(Names[Index].ToString());
		}
		FString Joined = FString::Join(Shown, TEXT(", "));
		if (Names.Num() > MaxShown)
		{
			Joined += FString::Printf(TEXT(" (+%d more)"), Names.Num() - MaxShown);
		}
		return Joined;
	}

	/** Modal target picker for Create Redirect… — empty pick = cancel. */
	static FName PickRedirectTargetModal(FName OldName)
	{
		// Headless-safe like the sibling dialogs: refuse (cancel), never open.
		if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized())
		{
			return NAME_None;
		}

		FName Picked = NAME_None;
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(FText::Format(LOCTEXT("PickTargetTitle", "Redirect '{0}' to..."), FText::FromName(OldName)))
			.SizingRule(ESizingRule::UserSized)
			.ClientSize(FVector2D(420.0f, 540.0f))
			.SupportsMaximize(false)
			.SupportsMinimize(false);
		TWeakPtr<SWindow> WeakWindow = Window;

		Window->SetContent(
			SNew(SBox)
			.Padding(8.0f)
			[
				SNew(STagToolboxTagPicker)
				.OnTagSelected(STagToolboxTagPicker::FOnTagSelected::CreateLambda(
					[&Picked, WeakWindow](const FGameplayTag& SelectedTag)
					{
						if (SelectedTag.IsValid())
						{
							Picked = SelectedTag.GetTagName();
						}
						if (const TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
						{
							PinnedWindow->RequestDestroyWindow();
						}
					}))
			]);

		FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());
		return Picked;
	}
}

/** One audit finding as a three-column row. */
class STagToolboxAuditResultRow : public SMultiColumnTableRow<TSharedPtr<FTagToolboxAuditRow>>
{
public:
	SLATE_BEGIN_ARGS(STagToolboxAuditResultRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedPtr<FTagToolboxAuditRow> InRow)
	{
		Row = InRow;
		SMultiColumnTableRow<TSharedPtr<FTagToolboxAuditRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Row.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		if (ColumnName == TagToolboxAuditPanel::ColumnCategory)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0f, 2.0f))
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FTagToolboxAudit::GetCategoryDisplayText(Row->Category))
					.ColorAndOpacity(FSlateColor(FTagToolboxAudit::GetCategoryColor(Row->Category)))
				];
		}
		if (ColumnName == TagToolboxAuditPanel::ColumnTag)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0f, 2.0f))
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromName(Row->Tag))
				];
		}
		return SNew(SBox)
			.Padding(FMargin(4.0f, 2.0f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::AsCultureInvariant(Row->Detail))
				.ToolTipText(FText::AsCultureInvariant(Row->Detail))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

private:
	TSharedPtr<FTagToolboxAuditRow> Row;
};

void STagToolboxAuditPanel::Construct(const FArguments& InArgs)
{
	TagTreeChangedHandle = UGameplayTagsManager::OnEditorRefreshGameplayTagTree.AddSP(this, &STagToolboxAuditPanel::HandleTagTreeChanged);
	// Package saves also stale the results (the scan service already tracks
	// exactly that) — a tag-tree-only banner would let rows rot after saves.
	ScanStateChangedHandle = FTagToolboxTagScanService::Get().OnScanStateChanged.AddSP(this, &STagToolboxAuditPanel::HandleScanStateChanged);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("RunAuditToolTip", "Scan the Asset Registry's saved tag references (no assets are loaded) and report unused, undefined, near-duplicate, and redirect issues."))
				.OnClicked(this, &STagToolboxAuditPanel::HandleRunClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RunAudit", "Run Audit"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.IsEnabled(this, &STagToolboxAuditPanel::CanDeleteUnused)
				.ToolTipText(this, &STagToolboxAuditPanel::GetDeleteUnusedToolTip)
				.OnClicked(this, &STagToolboxAuditPanel::HandleDeleteUnusedClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DeleteUnused", "Delete Unused..."))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.IsEnabled(this, &STagToolboxAuditPanel::CanCreateRedirect)
				.ToolTipText(this, &STagToolboxAuditPanel::GetCreateRedirectToolTip)
				.OnClicked(this, &STagToolboxAuditPanel::HandleCreateRedirectClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CreateRedirect", "Create Redirect..."))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.IsEnabled(this, &STagToolboxAuditPanel::CanResaveReferencers)
				.ToolTipText(this, &STagToolboxAuditPanel::GetResaveToolTip)
				.OnClicked(this, &STagToolboxAuditPanel::HandleResaveReferencersClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ResaveReferencers", "Resave Referencers..."))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &STagToolboxAuditPanel::GetSummaryText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]

		// Stale banner: armed by any tag-table change, cleared only by re-run.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.6f, 0.45f, 0.1f)))
			.Visibility_Lambda([this]() { return bResultsStale ? EVisibility::Visible : EVisibility::Collapsed; })
			.Padding(FMargin(8.0f, 4.0f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StaleBanner", "Results are stale — the tag table changed since this audit ran. Re-run the audit before acting on these rows."))
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)

			+ SSplitter::Slot()
			.Value(0.7f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(ResultsList, SListView<TSharedPtr<FTagToolboxAuditRow>>)
					.ListItemsSource(&Rows)
					.OnGenerateRow(this, &STagToolboxAuditPanel::GenerateResultRow)
					.OnSelectionChanged(this, &STagToolboxAuditPanel::HandleResultSelectionChanged)
					.OnContextMenuOpening(this, &STagToolboxAuditPanel::BuildResultContextMenu)
					.SelectionMode(ESelectionMode::Multi)
					.HeaderRow
					(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(TagToolboxAuditPanel::ColumnCategory)
						.DefaultLabel(LOCTEXT("ColumnCategory", "Category"))
						.ManualWidth(130.0f)
						+ SHeaderRow::Column(TagToolboxAuditPanel::ColumnTag)
						.DefaultLabel(LOCTEXT("ColumnTag", "Tag"))
						.FillWidth(0.35f)
						+ SHeaderRow::Column(TagToolboxAuditPanel::ColumnDetail)
						.DefaultLabel(LOCTEXT("ColumnDetail", "Detail"))
						.FillWidth(0.65f)
					)
				]
			]

			+ SSplitter::Slot()
			.Value(0.3f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ReferencersLabel", "Referencing packages"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SAssignNew(ReferencersList, SListView<TSharedPtr<FName>>)
						.ListItemsSource(&SelectedReferencers)
						.OnGenerateRow(this, &STagToolboxAuditPanel::GenerateReferencerRow)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]
		]
	];
}

STagToolboxAuditPanel::~STagToolboxAuditPanel()
{
	UGameplayTagsManager::OnEditorRefreshGameplayTagTree.Remove(TagTreeChangedHandle);
	if (FTagToolboxTagScanService* ScanService = FTagToolboxTagScanService::TryGet())
	{
		ScanService->OnScanStateChanged.Remove(ScanStateChangedHandle);
	}
}

void STagToolboxAuditPanel::HandleTagTreeChanged()
{
	if (bHasRun)
	{
		bResultsStale = true;
	}
}

void STagToolboxAuditPanel::HandleScanStateChanged()
{
	if (bHasRun && FTagToolboxTagScanService::Get().GetState() == ETagToolboxScanState::Stale)
	{
		bResultsStale = true;
	}
}

FReply STagToolboxAuditPanel::HandleRunClicked()
{
	Rows = FTagToolboxAudit::RunAudit();
	bHasRun = true;
	bResultsStale = false;

	SelectedReferencers.Reset();
	if (ResultsList.IsValid())
	{
		ResultsList->ClearSelection();
		ResultsList->RequestListRefresh();
	}
	if (ReferencersList.IsValid())
	{
		ReferencersList->RequestListRefresh();
	}
	return FReply::Handled();
}

FText STagToolboxAuditPanel::GetSummaryText() const
{
	if (!bHasRun)
	{
		return LOCTEXT("SummaryNotRun", "Not run yet. The audit reads Asset Registry metadata only; no assets are loaded.");
	}

	int32 Unused = 0, Undefined = 0, NearDuplicates = 0, RedirectIssues = 0;
	for (const TSharedPtr<FTagToolboxAuditRow>& Row : Rows)
	{
		if (!Row.IsValid())
		{
			continue;
		}
		switch (Row->Category)
		{
		case ETagToolboxAuditCategory::UnusedDefined:        ++Unused; break;
		case ETagToolboxAuditCategory::ReferencedUndefined:  ++Undefined; break;
		case ETagToolboxAuditCategory::NearDuplicate:        ++NearDuplicates; break;
		default:                                             ++RedirectIssues; break;
		}
	}
	return FText::Format(
		LOCTEXT("SummaryFormat", "{0} unused, {1} undefined, {2} near-duplicate, {3} redirect issue(s)"),
		FText::AsNumber(Unused), FText::AsNumber(Undefined), FText::AsNumber(NearDuplicates), FText::AsNumber(RedirectIssues));
}

TSharedRef<ITableRow> STagToolboxAuditPanel::GenerateResultRow(TSharedPtr<FTagToolboxAuditRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STagToolboxAuditResultRow, OwnerTable, Row);
}

TSharedRef<ITableRow> STagToolboxAuditPanel::GenerateReferencerRow(TSharedPtr<FName> PackageName, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
	[
		SNew(STextBlock)
		.Text(PackageName.IsValid() ? FText::FromName(*PackageName) : FText::GetEmpty())
	];
}

void STagToolboxAuditPanel::HandleResultSelectionChanged(TSharedPtr<FTagToolboxAuditRow> Row, ESelectInfo::Type SelectInfo)
{
	SelectedReferencers.Reset();
	if (Row.IsValid())
	{
		for (const FName& PackageName : Row->ReferencerPackages)
		{
			SelectedReferencers.Add(MakeShared<FName>(PackageName));
		}
	}
	if (ReferencersList.IsValid())
	{
		ReferencersList->RequestListRefresh();
	}
}

TSharedPtr<SWidget> STagToolboxAuditPanel::BuildResultContextMenu()
{
	const TArray<TSharedPtr<FTagToolboxAuditRow>> Selected = ResultsList.IsValid() ? ResultsList->GetSelectedItems() : TArray<TSharedPtr<FTagToolboxAuditRow>>();
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FTagToolboxAuditRow> Row = Selected[0];
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyTagName", "Copy Tag Name"),
		FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateLambda([Row]()
		{
			FPlatformApplicationMisc::ClipboardCopy(*Row->Tag.ToString());
		})));

	// Redirect rows open the rename dialog's recovery path: the old name
	// already redirects, so applying resumes at the referencer resave.
	if (Selected.Num() == 1
		&& (Row->Category == ETagToolboxAuditCategory::LingeringRedirect || Row->Category == ETagToolboxAuditCategory::BrokenRedirect)
		&& !Row->RedirectTarget.IsNone())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ResumeFixup", "Resume rename fix-up..."),
			LOCTEXT("ResumeFixupToolTip", "Open the rename dialog for this redirect: resave the referencing packages, verify, and retire the redirect."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Rename"),
			FUIAction(FExecuteAction::CreateLambda([OldName = Row->Tag, Target = Row->RedirectTarget]()
			{
				STagToolboxRenameDialog::ShowForTag(OldName, Target);
			})));
	}
	return MenuBuilder.MakeWidget();
}

TArray<TSharedPtr<FTagToolboxAuditRow>> STagToolboxAuditPanel::GetSelectedRowsOfCategory(ETagToolboxAuditCategory Category) const
{
	TArray<TSharedPtr<FTagToolboxAuditRow>> Result;
	if (ResultsList.IsValid())
	{
		for (const TSharedPtr<FTagToolboxAuditRow>& Row : ResultsList->GetSelectedItems())
		{
			if (Row.IsValid() && Row->Category == Category)
			{
				Result.Add(Row);
			}
		}
	}
	return Result;
}

bool STagToolboxAuditPanel::CanDeleteUnused() const
{
	return GetSelectedRowsOfCategory(ETagToolboxAuditCategory::UnusedDefined).Num() > 0;
}

FText STagToolboxAuditPanel::GetDeleteUnusedToolTip() const
{
	const int32 Applicable = GetSelectedRowsOfCategory(ETagToolboxAuditCategory::UnusedDefined).Num();
	const int32 Total = ResultsList.IsValid() ? ResultsList->GetSelectedItems().Num() : 0;
	if (Applicable == 0)
	{
		return LOCTEXT("DeleteUnusedDisabled", "Select one or more Unused rows to delete those tags from their ini lists.");
	}
	return FText::Format(LOCTEXT("DeleteUnusedToolTip", "Delete {0} unused tag(s) from their ini lists (acts on {0} of {1} selected rows). The engine refuses tags that are still referenced; the confirm lists everything first."),
		FText::AsNumber(Applicable), FText::AsNumber(Total));
}

FReply STagToolboxAuditPanel::HandleDeleteUnusedClicked()
{
	const TArray<TSharedPtr<FTagToolboxAuditRow>> Applicable = GetSelectedRowsOfCategory(ETagToolboxAuditCategory::UnusedDefined);
	if (Applicable.Num() == 0)
	{
		return FReply::Handled();
	}

	TArray<FName> RequestedNames;
	for (const TSharedPtr<FTagToolboxAuditRow>& Row : Applicable)
	{
		RequestedNames.AddUnique(Row->Tag);
	}

	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
		LOCTEXT("DeleteConfirm", "Delete {0} unused tag(s) from their ini lists?\n\n{1}\n\nThe audit sees SAVED content only: a tag used solely by unsaved edits looks unused here. The engine will refuse any tag that still has saved referencers."),
		FText::AsNumber(RequestedNames.Num()),
		FText::AsCultureInvariant(TagToolboxAuditPanel::JoinFirstNames(RequestedNames, 10))));
	if (Answer != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	TArray<TSharedPtr<FGameplayTagNode>> NodesToDelete;
	for (const FName& RequestedName : RequestedNames)
	{
		if (const TSharedPtr<FGameplayTagNode> Node = Manager.FindTagNode(RequestedName))
		{
			NodesToDelete.Add(Node);
		}
	}

	// Bulk form: logs instead of notifying per tag, refreshes the tree ONCE.
	IGameplayTagsEditorModule::Get().DeleteTagsFromINI(NodesToDelete);

	// Deleted-vs-refused via the explicit diff: a parent surviving only
	// implicitly through children counts as deleted.
	TSet<FName> StillExplicit;
	for (const FName& RequestedName : RequestedNames)
	{
		const TSharedPtr<FGameplayTagNode> Node = Manager.FindTagNode(RequestedName);
		if (Node.IsValid() && Node->IsExplicitTag())
		{
			StillExplicit.Add(RequestedName);
		}
	}
	TArray<FName> Deleted;
	TArray<FName> Refused;
	FTagToolboxAudit::DiffRefusedDeletions(RequestedNames, StillExplicit, Deleted, Refused);

	if (Refused.Num() == 0)
	{
		TagToolboxAuditPanel::Notify(FText::Format(LOCTEXT("DeleteAllOk", "Deleted {0} tag(s): {1}"),
			FText::AsNumber(Deleted.Num()), FText::AsCultureInvariant(TagToolboxAuditPanel::JoinFirstNames(Deleted, 6))));
	}
	else
	{
		TagToolboxAuditPanel::Notify(FText::Format(LOCTEXT("DeletePartial", "Deleted {0} tag(s); {1} refused (still referenced or implicit): {2}. See the log for the engine's per-tag reasons."),
			FText::AsNumber(Deleted.Num()), FText::AsNumber(Refused.Num()), FText::AsCultureInvariant(TagToolboxAuditPanel::JoinFirstNames(Refused, 6))));
	}
	return FReply::Handled();
}

bool STagToolboxAuditPanel::CanCreateRedirect() const
{
	// Single-row by design: one target per old name.
	return GetSelectedRowsOfCategory(ETagToolboxAuditCategory::ReferencedUndefined).Num() == 1
		&& ResultsList.IsValid() && ResultsList->GetSelectedItems().Num() == 1;
}

FText STagToolboxAuditPanel::GetCreateRedirectToolTip() const
{
	if (!CanCreateRedirect())
	{
		return LOCTEXT("CreateRedirectDisabled", "Select exactly ONE Undefined row — a redirect needs one target per old name.");
	}
	return LOCTEXT("CreateRedirectToolTip", "Pick a defined target tag; a GameplayTagRedirect is written so saved references to the old name resolve again.");
}

FReply STagToolboxAuditPanel::HandleCreateRedirectClicked()
{
	const TArray<TSharedPtr<FTagToolboxAuditRow>> Applicable = GetSelectedRowsOfCategory(ETagToolboxAuditCategory::ReferencedUndefined);
	if (Applicable.Num() != 1)
	{
		return FReply::Handled();
	}
	const FName OldName = Applicable[0]->Tag;

	const FName TargetName = TagToolboxAuditPanel::PickRedirectTargetModal(OldName);
	if (TargetName.IsNone())
	{
		return FReply::Handled();
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	const TSet<FName> DefinedTags = FTagToolboxAudit::CollectAllDefinedTagNames();
	TSet<FName> AggregatedOldNames;
	for (const FTagToolboxRedirectRecord& Record : FTagToolboxTagScanService::CollectAllRedirects())
	{
		AggregatedOldNames.Add(Record.OldTagName);
	}

	const ETagToolboxRedirectTargetVerdict Verdict = FTagToolboxAudit::ValidateRedirectTarget(OldName, TargetName, DefinedTags, AggregatedOldNames);
	if (Verdict != ETagToolboxRedirectTargetVerdict::Ok)
	{
		TagToolboxAuditPanel::Notify(FText::Format(LOCTEXT("CreateRedirectRefused", "Redirect refused: {0}"), FTagToolboxAudit::GetRedirectTargetVerdictText(Verdict)));
		return FReply::Handled();
	}

	// A new redirect for an undefined name has no owning source — it homes in
	// the default settings list, through the checked writer (persist verified,
	// in-memory revert on failure).
	FText WriteError;
	if (FTagToolboxRenameFixup::AddRedirectRowChecked(OldName, TargetName, GetMutableDefault<UGameplayTagsSettings>(), WriteError))
	{
		TagToolboxAuditPanel::Notify(FText::Format(LOCTEXT("CreateRedirectOk", "Created redirect {0} -> {1}. Re-run the audit to refresh the rows."),
			FText::FromName(OldName), FText::FromName(TargetName)));
		Manager.EditorRefreshGameplayTagTree(); // arms the stale banner
	}
	else
	{
		TagToolboxAuditPanel::Notify(FText::Format(LOCTEXT("CreateRedirectFailed", "Redirect NOT created (reverted): {0}"), WriteError));
	}
	return FReply::Handled();
}

bool STagToolboxAuditPanel::CanResaveReferencers() const
{
	for (const TSharedPtr<FTagToolboxAuditRow>& Row : GetSelectedRowsOfCategory(ETagToolboxAuditCategory::LingeringRedirect))
	{
		if (Row->ReferencerPackages.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

FText STagToolboxAuditPanel::GetResaveToolTip() const
{
	const int32 Applicable = GetSelectedRowsOfCategory(ETagToolboxAuditCategory::LingeringRedirect).Num();
	const int32 Total = ResultsList.IsValid() ? ResultsList->GetSelectedItems().Num() : 0;
	if (Applicable == 0)
	{
		return LOCTEXT("ResaveDisabled", "Select one or more Lingering Redirect rows to resave their referencing packages (retiring the redirect becomes possible once nothing references the old name).");
	}
	return FText::Format(LOCTEXT("ResaveToolTip", "Resave the packages still referencing {0} redirected old name(s) (acts on {0} of {1} selected rows), under the shared consent dialog."),
		FText::AsNumber(Applicable), FText::AsNumber(Total));
}

FReply STagToolboxAuditPanel::HandleResaveReferencersClicked()
{
	TSet<FName> Packages;
	for (const TSharedPtr<FTagToolboxAuditRow>& Row : GetSelectedRowsOfCategory(ETagToolboxAuditCategory::LingeringRedirect))
	{
		Packages.Append(Row->ReferencerPackages);
	}
	if (Packages.Num() == 0)
	{
		return FReply::Handled();
	}
	TArray<FName> Sorted = Packages.Array();
	Sorted.Sort(FNameLexicalLess());

	FTagToolboxResavePlan Plan = FTagToolboxResaveService::BuildPlan(FTagToolboxResaveService::GatherFacts(Sorted));
	if (!STagToolboxResaveDialog::ShowDialog(Plan, LOCTEXT("ResaveHeader", "These packages still reference redirected old tag names. Resaving rewrites them to the new names; once nothing references an old name, its redirect can be retired.")))
	{
		return FReply::Handled();
	}

	const FTagToolboxResaveReport Report = FTagToolboxResaveService::ExecutePlan(Plan);
	TagToolboxAuditPanel::Notify(FText::Format(LOCTEXT("ResaveOutcome", "Resave finished: {0} saved, {1} failed, {2} unattempted, {3} skipped. Re-run the audit to see the rows clear."),
		FText::AsNumber(Report.Saved.Num()), FText::AsNumber(Report.Failed.Num()), FText::AsNumber(Report.Unattempted.Num()), FText::AsNumber(Report.Skipped.Num())));
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
