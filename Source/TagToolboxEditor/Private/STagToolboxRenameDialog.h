// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TagToolboxRenameFixup.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SEditableTextBox;

/**
 * The rename-with-fix-up dialog (U7): old name fixed, new name typed, and a
 * live preview rebuilt on every edit — verdict (ready / merge warning / cycle
 * or unwritable-source refusal / skip-to-resave recovery), every referencing
 * package of the whole subtree with its load/dirty/read-only status, and the
 * un-fixable stores named explicitly. Apply closes the dialog and runs
 * FTagToolboxRenameFixup::ExecuteRename (which owns the resave consent dialog,
 * the retirement offer, and every outcome notification).
 *
 * Reachable from the Tag Browser row context menu and the audit's
 * lingering-redirect rows. Headless-safe: refuses to open without Slate.
 */
class STagToolboxRenameDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STagToolboxRenameDialog) {}
	SLATE_END_ARGS()

	/** Opens the modal dialog for OldTagName; SuggestedNewName may be NAME_None. */
	static void ShowForTag(FName OldTagName, FName SuggestedNewName = NAME_None);

	void Construct(const FArguments& InArgs, FName InOldTagName, FName InSuggestedNewName);

private:
	struct FReferencerRow
	{
		FName PackageName;
		FText Status;
	};

	/** Rebuilds Plan + referencer preview from the current new-name text. */
	void RefreshPlan();
	FTagToolboxRenamePlan BuildPlanFromLiveState(FName NewName) const;

	TSharedRef<ITableRow> GenerateReferencerRow(TSharedPtr<FReferencerRow> Row, const TSharedRef<STableViewBase>& OwnerTable);

	FName OldTagName;
	FTagToolboxRenamePlan Plan;
	TArray<TSharedPtr<FReferencerRow>> ReferencerRows;

	TSharedPtr<SEditableTextBox> NewNameBox;
	TSharedPtr<SListView<TSharedPtr<FReferencerRow>>> ReferencerList;
	TWeakPtr<class SWindow> HostWindow;
	bool bApplyRequested = false;
};
