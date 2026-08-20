// Copyright Infinite Game Works. All Rights Reserved.

#include "STagToolboxAuditPanel.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STagToolboxAuditPanel"

namespace TagToolboxAuditPanel
{
	const FName ColumnCategory(TEXT("Category"));
	const FName ColumnTag(TEXT("Tag"));
	const FName ColumnDetail(TEXT("Detail"));
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
			.FillWidth(1.0f)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &STagToolboxAuditPanel::GetSummaryText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
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
					.SelectionMode(ESelectionMode::Single)
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

FReply STagToolboxAuditPanel::HandleRunClicked()
{
	Rows = FTagToolboxAudit::RunAudit();
	bHasRun = true;

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
	if (Selected.Num() != 1 || !Selected[0].IsValid())
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
	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
