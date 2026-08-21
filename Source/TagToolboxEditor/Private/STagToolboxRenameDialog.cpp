// Copyright Infinite Game Works. All Rights Reserved.

#include "STagToolboxRenameDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/AppStyle.h"
#include "TagToolboxSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STagToolboxRenameDialog"

namespace TagToolboxRenameDialogInternal
{
	static bool bDialogOpen = false;
}

void STagToolboxRenameDialog::ShowForTag(FName OldTagName, FName SuggestedNewName)
{
	if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized() || TagToolboxRenameDialogInternal::bDialogOpen || OldTagName.IsNone())
	{
		return;
	}

	TagToolboxRenameDialogInternal::bDialogOpen = true;
	ON_SCOPE_EXIT { TagToolboxRenameDialogInternal::bDialogOpen = false; };

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("DialogTitle", "Rename Tag '{0}' (with asset fix-up)"), FText::FromName(OldTagName)))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(680.0f, 520.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<STagToolboxRenameDialog> Dialog = SNew(STagToolboxRenameDialog, OldTagName, SuggestedNewName);
	Dialog->HostWindow = Window;
	Window->SetContent(Dialog);

	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	if (Dialog->bApplyRequested)
	{
		// Run OUTSIDE the modal loop: the apply opens its own consent dialog,
		// message boxes, and slow tasks.
		FTagToolboxRenameFixup::ExecuteRename(Dialog->Plan);
	}
}

void STagToolboxRenameDialog::Construct(const FArguments& InArgs, FName InOldTagName, FName InSuggestedNewName)
{
	OldTagName = InOldTagName;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 12.0f, 12.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("RenameLabel", "Rename '{0}' to:"), FText::FromName(OldTagName)))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(NewNameBox, SEditableTextBox)
				.Text(InSuggestedNewName.IsNone() ? FText::GetEmpty() : FText::FromName(InSuggestedNewName))
				.HintText(LOCTEXT("NewNameHint", "New.Tag.Name"))
				.OnTextChanged_Lambda([this](const FText&) { RefreshPlan(); })
			]
		]

		// Verdict line (merge warning, refusal reason, or recovery note).
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]() { return Plan.VerdictReason; })
			.ColorAndOpacity_Lambda([this]()
			{
				switch (Plan.Verdict)
				{
				case ETagToolboxRenameVerdict::ReadyMerge:
					return FSlateColor(FLinearColor(0.95f, 0.65f, 0.2f));
				case ETagToolboxRenameVerdict::SkipToResave:
					return FSlateColor(FLinearColor(0.4f, 0.7f, 0.95f));
				case ETagToolboxRenameVerdict::Ready:
					return FSlateColor::UseSubduedForeground();
				default:
					return FSlateColor(FLinearColor(0.9f, 0.3f, 0.3f));
				}
			})
			.Visibility_Lambda([this]() { return Plan.VerdictReason.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		]

		// Subtree + referencer preview.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 4.0f, 12.0f, 2.0f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.Text_Lambda([this]()
			{
				return FText::Format(LOCTEXT("PreviewSummary", "{0} tag(s) in the subtree; {1} referencing package(s) will be resaved:"),
					FText::AsNumber(Plan.SubtreeOldNames.Num()), FText::AsNumber(ReferencerRows.Num()));
			})
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(12.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SAssignNew(ReferencerList, SListView<TSharedPtr<FReferencerRow>>)
				.ListItemsSource(&ReferencerRows)
				.OnGenerateRow(this, &STagToolboxRenameDialog::GenerateReferencerRow)
				.SelectionMode(ESelectionMode::None)
			]
		]

		// The stores the fix-up cannot reach, named explicitly (R4).
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 6.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("UnfixableStores", "Not fixable by this tool (check manually after the rename): Blueprint variable 'Categories' picker filters, C++ RequestGameplayTag literals, and tag names stored as plain strings in config or code. Saved tag, container, and query properties ARE covered."))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f)
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.IsEnabled_Lambda([this]()
				{
					return Plan.Verdict == ETagToolboxRenameVerdict::Ready
						|| Plan.Verdict == ETagToolboxRenameVerdict::ReadyMerge
						|| Plan.Verdict == ETagToolboxRenameVerdict::SkipToResave;
				})
				.OnClicked_Lambda([this]()
				{
					bApplyRequested = true;
					if (const TSharedPtr<SWindow> Window = HostWindow.Pin())
					{
						Window->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						switch (Plan.Verdict)
						{
						case ETagToolboxRenameVerdict::ReadyMerge:
							return LOCTEXT("ApplyMerge", "Rename and MERGE...");
						case ETagToolboxRenameVerdict::SkipToResave:
							return LOCTEXT("ApplyResume", "Resume fix-up...");
						default:
							return LOCTEXT("Apply", "Rename and fix up...");
						}
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.OnClicked_Lambda([this]()
				{
					if (const TSharedPtr<SWindow> Window = HostWindow.Pin())
					{
						Window->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Cancel", "Cancel"))
				]
			]
		]
	];

	RefreshPlan();
}

FTagToolboxRenamePlan STagToolboxRenameDialog::BuildPlanFromLiveState(FName NewName) const
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	// Pre-rename snapshot: the complete table, implicit parents included.
	TSet<FName> Snapshot;
	{
		FGameplayTagContainer AllTags;
		Manager.RequestAllGameplayTags(AllTags, /*OnlyIncludeDictionaryTags=*/false);
		for (const FGameplayTag& TableTag : AllTags)
		{
			Snapshot.Add(TableTag.GetTagName());
		}
	}

	const TArray<FTagToolboxRedirectRecord> AllRedirects = FTagToolboxTagScanService::CollectAllRedirects();

	// Each subtree member's owning source, plus each source's writability.
	TMap<FName, FName> SubtreeNameToSource;
	TSet<FName> UnwritableSources;
	const bool bSourceControlActive = ISourceControlModule::Get().IsEnabled();
	for (const FName& SubtreeName : FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, OldTagName))
	{
		FString Comment;
		FName SourceName;
		bool bExplicit, bRestricted, bAllowNonRestricted;
		if (Manager.GetTagEditorData(SubtreeName, Comment, SourceName, bExplicit, bRestricted, bAllowNonRestricted))
		{
			SubtreeNameToSource.Add(SubtreeName, SourceName);
			if (const FGameplayTagSource* Source = Manager.FindTagSource(SourceName))
			{
				if (Source->SourceTagList && !Source->SourceTagList->ConfigFileName.IsEmpty())
				{
					const FString& ConfigPath = Source->SourceTagList->ConfigFileName;
					if (!bSourceControlActive && IFileManager::Get().FileExists(*ConfigPath) && IFileManager::Get().IsReadOnly(*ConfigPath))
					{
						UnwritableSources.Add(SourceName);
					}
				}
			}
		}
	}

	// Plugin stores.
	TArray<FGameplayTag> StyleTags;
	for (const FTagToolboxTagStyle& Style : GetDefault<UTagToolboxSettings>()->TagStyles)
	{
		StyleTags.Add(Style.Tag);
	}
	auto ReadNameList = [](const TCHAR* Key)
	{
		TArray<FName> Names;
		FString Joined;
		if (GConfig->GetString(TEXT("TagToolbox"), Key, Joined, GEditorPerProjectIni))
		{
			TArray<FString> Strings;
			Joined.ParseIntoArray(Strings, TEXT(","), true);
			for (const FString& NameString : Strings)
			{
				Names.Add(FName(*NameString.TrimStartAndEnd()));
			}
		}
		return Names;
	};

	// Merge target facts (live, exact-name).
	int32 MergeReferencers = 0;
	int32 MergeChildren = 0;
	if (Snapshot.Contains(NewName))
	{
		const TMap<FName, TArray<FName>> TargetRefs = FTagToolboxRenameFixup::QueryLiveReferencers({ NewName });
		MergeReferencers = TargetRefs.FindRef(NewName).Num();
		MergeChildren = FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, NewName).Num() - 1;
	}

	FText ValidationError;
	FString FixedString;
	const bool bValidNewName = !NewName.IsNone() && UGameplayTagsManager::Get().IsValidGameplayTagString(NewName.ToString(), &ValidationError, &FixedString);

	return FTagToolboxRenameFixup::BuildPlan(
		OldTagName, NewName, bValidNewName, Snapshot, AllRedirects,
		SubtreeNameToSource, UnwritableSources, StyleTags,
		ReadNameList(TEXT("BrowserFavorites")), ReadNameList(TEXT("BrowserRecents")),
		MergeReferencers, MergeChildren);
}

void STagToolboxRenameDialog::RefreshPlan()
{
	const FString NewNameString = NewNameBox.IsValid() ? NewNameBox->GetText().ToString().TrimStartAndEnd() : FString();
	Plan = BuildPlanFromLiveState(NewNameString.IsEmpty() ? NAME_None : FName(*NewNameString));

	// Referencer preview: live per-name query over the subtree, with each
	// package's dirty/loaded/read-only status (R4).
	ReferencerRows.Reset();
	if (Plan.Verdict == ETagToolboxRenameVerdict::Ready
		|| Plan.Verdict == ETagToolboxRenameVerdict::ReadyMerge
		|| Plan.Verdict == ETagToolboxRenameVerdict::SkipToResave)
	{
		const TMap<FName, TArray<FName>> Referencers = FTagToolboxRenameFixup::QueryLiveReferencers(Plan.SubtreeOldNames);
		TSet<FName> Packages;
		for (const TPair<FName, TArray<FName>>& Pair : Referencers)
		{
			Packages.Append(Pair.Value);
		}
		TArray<FName> Sorted = Packages.Array();
		Sorted.Sort(FNameLexicalLess());

		const TArray<FTagToolboxPackageFacts> Facts = FTagToolboxResaveService::GatherFacts(Sorted);
		for (const FTagToolboxPackageFacts& Fact : Facts)
		{
			TSharedPtr<FReferencerRow> Row = MakeShared<FReferencerRow>();
			Row->PackageName = Fact.PackageName;
			if (Fact.bLoaded && Fact.bDirty)
			{
				Row->Status = LOCTEXT("StatusDirty", "loaded, UNSAVED EDITS");
			}
			else if (Fact.bLoaded)
			{
				Row->Status = LOCTEXT("StatusLoaded", "loaded");
			}
			else if (Fact.bReadOnlyOnDisk && !Fact.bSourceControlActive)
			{
				Row->Status = LOCTEXT("StatusReadOnly", "read-only, no source control");
			}
			else
			{
				Row->Status = LOCTEXT("StatusOnDisk", "on disk");
			}
			ReferencerRows.Add(Row);
		}
	}

	if (ReferencerList.IsValid())
	{
		ReferencerList->RequestListRefresh();
	}
}

TSharedRef<ITableRow> STagToolboxRenameDialog::GenerateReferencerRow(TSharedPtr<FReferencerRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FReferencerRow>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(Row.IsValid() ? FText::AsCultureInvariant(Row->PackageName.ToString()) : FText::GetEmpty())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(Row.IsValid() ? Row->Status : FText::GetEmpty())
		]
	];
}

#undef LOCTEXT_NAMESPACE
