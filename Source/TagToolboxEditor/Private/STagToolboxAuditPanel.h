// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TagToolboxAudit.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * The Tag Audit tab: an explicit "Run Audit" action (never runs on open — a
 * project-wide registry scan is the designer's call), a categorized results
 * list, and a referencing-packages pane for the selected finding.
 */
class STagToolboxAuditPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STagToolboxAuditPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply HandleRunClicked();
	FText GetSummaryText() const;

	TSharedRef<ITableRow> GenerateResultRow(TSharedPtr<FTagToolboxAuditRow> Row, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> GenerateReferencerRow(TSharedPtr<FName> PackageName, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleResultSelectionChanged(TSharedPtr<FTagToolboxAuditRow> Row, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> BuildResultContextMenu();

	TArray<TSharedPtr<FTagToolboxAuditRow>> Rows;
	TArray<TSharedPtr<FName>> SelectedReferencers;

	TSharedPtr<SListView<TSharedPtr<FTagToolboxAuditRow>>> ResultsList;
	TSharedPtr<SListView<TSharedPtr<FName>>> ReferencersList;

	bool bHasRun = false;
};
