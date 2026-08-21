// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TagToolboxAudit.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * The Tag Audit tab (U8: findings become one-click, confirmed fixes):
 * - explicit "Run Audit" only — a project-wide registry scan is the
 *   designer's call, and refresh/re-scan never mutates anything;
 * - multi-select results with per-category action enablement (mixed
 *   selections act on the applicable subset and say so);
 * - Delete Unused (confirmed; per-tag deleted-vs-refused summary — the
 *   engine itself refuses tags with referencers),
 *   Create Redirect… (single-row: one target per old name; validated),
 *   Resave Referencers (the shared U6 consent + resave engine);
 * - a "results stale — re-run" banner armed by any tag-tree change; no
 *   action mutates during refresh, and results never silently re-scan.
 */
class STagToolboxAuditPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STagToolboxAuditPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~STagToolboxAuditPanel() override;

private:
	FReply HandleRunClicked();
	FText GetSummaryText() const;

	TSharedRef<ITableRow> GenerateResultRow(TSharedPtr<FTagToolboxAuditRow> Row, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> GenerateReferencerRow(TSharedPtr<FName> PackageName, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleResultSelectionChanged(TSharedPtr<FTagToolboxAuditRow> Row, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> BuildResultContextMenu();

	/** Selected rows of one category (the applicable subset for an action). */
	TArray<TSharedPtr<FTagToolboxAuditRow>> GetSelectedRowsOfCategory(ETagToolboxAuditCategory Category) const;

	/** Delete Unused: confirm → engine bulk delete → deleted/refused summary. */
	FReply HandleDeleteUnusedClicked();
	bool CanDeleteUnused() const;
	FText GetDeleteUnusedToolTip() const;

	/** Create Redirect…: single undefined row → validated target → checked write. */
	FReply HandleCreateRedirectClicked();
	bool CanCreateRedirect() const;
	FText GetCreateRedirectToolTip() const;

	/** Resave Referencers: lingering rows → shared U6 consent + resave. */
	FReply HandleResaveReferencersClicked();
	bool CanResaveReferencers() const;
	FText GetResaveToolTip() const;

	void HandleTagTreeChanged();

	TArray<TSharedPtr<FTagToolboxAuditRow>> Rows;
	TArray<TSharedPtr<FName>> SelectedReferencers;

	TSharedPtr<SListView<TSharedPtr<FTagToolboxAuditRow>>> ResultsList;
	TSharedPtr<SListView<TSharedPtr<FName>>> ReferencersList;

	FDelegateHandle TagTreeChangedHandle;
	bool bHasRun = false;
	/** Armed by any tag-table change after a run; cleared only by re-running. */
	bool bResultsStale = false;
};
