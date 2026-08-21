// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TagToolboxResaveService.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * Modal consent dialog over a resave plan: one row per package with its
 * disposition, a checkbox on dirty rows (excluded unless explicitly opted in
 * — the copy states the save-first ordering that preserves the user's edits),
 * and read-only rows named as excluded. Returns true only when the user
 * confirms; the (possibly consent-mutated) plan is then ready for
 * FTagToolboxResaveService::ExecutePlan.
 *
 * Headless-safe: returns false (cancel) without opening anything when Slate
 * is unavailable, and a static re-entrancy guard refuses nested dialogs
 * (mirrors the host's SReimportConflictDialog pattern).
 */
class STagToolboxResaveDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STagToolboxResaveDialog) {}
		SLATE_ARGUMENT(FText, HeaderText)
	SLATE_END_ARGS()

	/** Shows the modal dialog for Plan; mutates Plan's dirty consent in place. */
	static bool ShowDialog(FTagToolboxResavePlan& Plan, const FText& HeaderText);

	void Construct(const FArguments& InArgs, FTagToolboxResavePlan* InPlan);

private:
	struct FRowItem
	{
		int32 EntryIndex = INDEX_NONE;
	};

	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FRowItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	FText GetDispositionText(const FTagToolboxResavePlanEntry& Entry) const;

	FTagToolboxResavePlan* Plan = nullptr;
	TArray<TSharedPtr<FRowItem>> Rows;
	TWeakPtr<class SWindow> HostWindow;

public:
	/** Read after the modal loop returns. */
	bool bConfirmed = false;
};
