// Copyright Infinite Game Works. All Rights Reserved.

#include "STagToolboxResaveDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STagToolboxResaveDialog"

namespace TagToolboxResaveDialogInternal
{
	// One modal at a time; nested requests refuse rather than stack.
	static bool bDialogOpen = false;
}

bool STagToolboxResaveDialog::ShowDialog(FTagToolboxResavePlan& Plan, const FText& HeaderText)
{
	// Headless / nested: refuse (cancel) — never fabricate consent.
	if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized() || TagToolboxResaveDialogInternal::bDialogOpen)
	{
		return false;
	}

	TagToolboxResaveDialogInternal::bDialogOpen = true;
	ON_SCOPE_EXIT { TagToolboxResaveDialogInternal::bDialogOpen = false; };

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DialogTitle", "Resave Referencing Packages"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(620.0f, 420.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<STagToolboxResaveDialog> Dialog = SNew(STagToolboxResaveDialog, &Plan).HeaderText(HeaderText);
	Dialog->HostWindow = Window;
	Window->SetContent(Dialog);

	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	return Dialog->bConfirmed;
}

void STagToolboxResaveDialog::Construct(const FArguments& InArgs, FTagToolboxResavePlan* InPlan)
{
	Plan = InPlan;

	Rows.Reset();
	for (int32 Index = 0; Index < Plan->Entries.Num(); ++Index)
	{
		TSharedPtr<FRowItem> Item = MakeShared<FRowItem>();
		Item->EntryIndex = Index;
		Rows.Add(Item);
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 12.0f, 12.0f, 4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(InArgs._HeaderText)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 0.0f, 12.0f, 8.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("DirtyExplainer", "Packages with unsaved edits are included only when you tick them. A ticked package is SAVED FIRST (keeping your edits), then reloaded and resaved with the updated tag names."))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(12.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SListView<TSharedPtr<FRowItem>>)
				.ListItemsSource(&Rows)
				.OnGenerateRow(this, &STagToolboxResaveDialog::GenerateRow)
				.SelectionMode(ESelectionMode::None)
			]
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
				.IsEnabled_Lambda([this]() { return Plan && Plan->CountIncluded() > 0; })
				.OnClicked_Lambda([this]()
				{
					bConfirmed = true;
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
						return FText::Format(LOCTEXT("ConfirmButton", "Resave {0} package(s)"), FText::AsNumber(Plan ? Plan->CountIncluded() : 0));
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
					bConfirmed = false;
					if (const TSharedPtr<SWindow> Window = HostWindow.Pin())
					{
						Window->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CancelButton", "Cancel"))
				]
			]
		]
	];
}

FText STagToolboxResaveDialog::GetDispositionText(const FTagToolboxResavePlanEntry& Entry) const
{
	switch (Entry.Disposition)
	{
	case ETagToolboxPackageDisposition::NotLoaded:
		return LOCTEXT("DispositionNotLoaded", "will load and save");
	case ETagToolboxPackageDisposition::LoadedClean:
		return LOCTEXT("DispositionLoadedClean", "loaded — will reload and save");
	case ETagToolboxPackageDisposition::LoadedDirty:
		return LOCTEXT("DispositionLoadedDirty", "UNSAVED EDITS — tick to save-first, reload, resave");
	case ETagToolboxPackageDisposition::ReadOnlyNoProvider:
		return LOCTEXT("DispositionReadOnly", "read-only on disk, no source control — excluded");
	default:
		return FText::GetEmpty();
	}
}

TSharedRef<ITableRow> STagToolboxResaveDialog::GenerateRow(TSharedPtr<FRowItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 EntryIndex = Item.IsValid() ? Item->EntryIndex : INDEX_NONE;
	const bool bValid = Plan && Plan->Entries.IsValidIndex(EntryIndex);
	const FTagToolboxResavePlanEntry Entry = bValid ? Plan->Entries[EntryIndex] : FTagToolboxResavePlanEntry();

	return SNew(STableRow<TSharedPtr<FRowItem>>, OwnerTable)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 2.0f)
		[
			SNew(SCheckBox)
			.Visibility(Entry.Disposition == ETagToolboxPackageDisposition::LoadedDirty ? EVisibility::Visible : EVisibility::Collapsed)
			.IsChecked_Lambda([this, EntryIndex]()
			{
				return (Plan && Plan->Entries.IsValidIndex(EntryIndex) && Plan->Entries[EntryIndex].bIncluded)
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, EntryIndex](ECheckBoxState NewState)
			{
				if (Plan && Plan->Entries.IsValidIndex(EntryIndex))
				{
					Plan->Entries[EntryIndex].bIncluded = (NewState == ECheckBoxState::Checked);
				}
			})
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(FText::AsCultureInvariant(Entry.PackageName.ToString()))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.ColorAndOpacity(Entry.Disposition == ETagToolboxPackageDisposition::LoadedDirty
				? FSlateColor(FLinearColor(0.95f, 0.65f, 0.2f))
				: FSlateColor::UseSubduedForeground())
			.Text(GetDispositionText(Entry))
		]
	];
}

#undef LOCTEXT_NAMESPACE
