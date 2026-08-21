// Copyright Infinite Game Works. All Rights Reserved.

#include "STagToolboxTagPicker.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "STagToolboxRenameDialog.h"
#include "TagToolboxTagClipboard.h"
#include "Styling/AppStyle.h"
#include "TagToolboxColorBridge.h"
#include "TagToolboxSettings.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STagToolboxTagPicker"

namespace TagToolboxPicker
{
	constexpr int32 MaxRecents = 8;
	const TCHAR* ConfigSection = TEXT("TagToolbox");
	const TCHAR* FavoritesKey = TEXT("BrowserFavorites");
	const TCHAR* RecentsKey = TEXT("BrowserRecents");

	static FString JoinNames(const TArray<FName>& Names)
	{
		TArray<FString> Strings;
		Strings.Reserve(Names.Num());
		for (const FName& Name : Names)
		{
			Strings.Add(Name.ToString());
		}
		return FString::Join(Strings, TEXT(","));
	}

	static TArray<FName> SplitNames(const FString& Joined)
	{
		TArray<FString> Strings;
		Joined.ParseIntoArray(Strings, TEXT(","), true);
		TArray<FName> Names;
		Names.Reserve(Strings.Num());
		for (const FString& String : Strings)
		{
			Names.Add(FName(*String.TrimStartAndEnd()));
		}
		return Names;
	}
}

void STagToolboxTagPicker::Construct(const FArguments& InArgs)
{
	RootFilter = InArgs._Filter;
	OnTagSelected = InArgs._OnTagSelected;
	CurrentTag = InArgs._CurrentTag;
	bMenuHosted = InArgs._MenuHosted;
	CanCreateTags = InArgs._CanCreateTags;

	LoadPersistedState();

	TSharedRef<SWidget> TreeHost = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SAssignNew(TagTree, STreeView<FTagNodePtr>)
			.TreeItemsSource(&VisibleRootNodes)
			.OnGenerateRow(this, &STagToolboxTagPicker::GenerateRow)
			.OnGetChildren(this, &STagToolboxTagPicker::GetChildrenForNode)
			.OnSelectionChanged(this, &STagToolboxTagPicker::HandleSelectionChanged)
			.OnContextMenuOpening(bMenuHosted ? nullptr : FOnContextMenuOpening::CreateSP(this, &STagToolboxTagPicker::BuildRowContextMenu))
			.SelectionMode(ESelectionMode::Single)
		];

	if (InArgs._MaxHeight > 0.0f)
	{
		TreeHost = SNew(SBox)
			.MaxDesiredHeight(InArgs._MaxHeight)
			[
				TreeHost
			];
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// Search + favorites lens + (selection mode) Clear
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search tags..."))
				.OnTextChanged(this, &STagToolboxTagPicker::HandleSearchChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bFavoritesOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bFavoritesOnly = (NewState == ECheckBoxState::Checked);
					RebuildVisibility();
				})
				.ToolTipText(LOCTEXT("FavoritesOnlyToolTip", "Show only favorite tags (and the branches that lead to them)."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FavoritesOnly", "Favorites"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Visibility(IsSelectionMode() ? EVisibility::Collapsed : EVisibility::Visible)
				.IsEnabled_Lambda([]() { return FTagToolboxTagScanService::Get().GetState() != ETagToolboxScanState::NeverScanned; })
				.IsChecked_Lambda([this]() { return bSortByUsage ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bSortByUsage = (NewState == ECheckBoxState::Checked);
					RebuildVisibility();
				})
				.ToolTipText(LOCTEXT("SortByUsageToolTip", "Order siblings by usage (subtree-aggregate count, so a heavily-used leaf lifts its parents). Disabled until a usage scan has run."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SortByUsage", "Sort: Usage"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.Visibility(IsSelectionMode() ? EVisibility::Collapsed : EVisibility::Visible)
				.ToolTipText(LOCTEXT("CountUsageToolTip", "Scan the Asset Registry's saved tag references and show per-tag usage counts. Explicit — the scan never runs on its own, and counts go stale (never wrong) when tags or content change."))
				.OnClicked_Lambda([this]()
				{
					FTagToolboxTagScanService::Get().RunScan(/*bAllowDialog=*/true);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([]()
					{
						return FTagToolboxTagScanService::Get().GetState() == ETagToolboxScanState::Stale
							? LOCTEXT("RecountUsage", "Recount (stale)")
							: LOCTEXT("CountUsage", "Count usage");
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.Visibility(IsSelectionMode() ? EVisibility::Visible : EVisibility::Collapsed)
				.ToolTipText(LOCTEXT("ClearToolTip", "Clear the assigned tag."))
				.OnClicked_Lambda([this]()
				{
					CommitSelectedTag(FGameplayTag());
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Clear", "Clear"))
				]
			]
		]

		// Inline create/reveal row (mirrors the engine picker's add-row slot):
		// drives entirely off CreateRowPlan, whose modes are pure-derived.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Visibility_Lambda([this]()
			{
				return CreateRowPlan.Mode == ETagToolboxCreateRowMode::Hidden ? EVisibility::Collapsed : EVisibility::Visible;
			})
			.Padding(FMargin(4.0f, 2.0f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.IsEnabled_Lambda([this]()
				{
					return CreateRowPlan.Mode == ETagToolboxCreateRowMode::Offer || CreateRowPlan.Mode == ETagToolboxCreateRowMode::ExistsHidden;
				})
				.ToolTipText_Lambda([this]() { return CreateRowPlan.Reason; })
				.OnClicked(this, &STagToolboxTagPicker::ExecuteCreateRowAction)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.Text_Lambda([this]()
					{
						switch (CreateRowPlan.Mode)
						{
						case ETagToolboxCreateRowMode::Offer:
							return FText::Format(LOCTEXT("CreateRowOffer", "+ Create tag '{0}'"), FText::AsCultureInvariant(CreateRowPlan.FinalTagString));
						case ETagToolboxCreateRowMode::ExistsHidden:
							return FText::Format(LOCTEXT("CreateRowReveal", "'{0}' exists — show it"), FText::AsCultureInvariant(CreateRowPlan.FinalTagString));
						case ETagToolboxCreateRowMode::ExistsBlockedByFilter:
						case ETagToolboxCreateRowMode::InvalidInput:
							return CreateRowPlan.Reason;
						default:
							return FText::GetEmpty();
						}
					})
					.ColorAndOpacity_Lambda([this]()
					{
						const bool bActionable = CreateRowPlan.Mode == ETagToolboxCreateRowMode::Offer || CreateRowPlan.Mode == ETagToolboxCreateRowMode::ExistsHidden;
						return bActionable ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
					})
				]
			]
		]

		// Recents strip — in selection mode these are one-click commits.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			.ScrollBarVisibility(EVisibility::Collapsed)
			+ SScrollBox::Slot()
			[
				SAssignNew(RecentsStrip, SHorizontalBox)
			]
		]

		// Tag tree
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			TreeHost
		]

		// References pane (browse mode only): every asset whose saved data
		// references the selected tag — registry metadata, no asset loads.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(SVerticalBox)
			.Visibility(IsSelectionMode() ? EVisibility::Collapsed : EVisibility::Visible)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.Text(this, &STagToolboxTagPicker::GetReferencesSummary)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bIncludeChildReferences ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bIncludeChildReferences = (NewState == ECheckBoxState::Checked);
						RefreshReferences();
					})
					.ToolTipText(LOCTEXT("IncludeChildrenToolTip", "Also list assets referencing any CHILD of the selected tag (the registry stores exact names, so parents never match children implicitly)."))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("IncludeChildren", "Include child tags"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(150.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SAssignNew(ReferencesList, SListView<TSharedPtr<FAssetData>>)
						.ListItemsSource(&ReferenceRows)
						.OnGenerateRow(this, &STagToolboxTagPicker::GenerateReferenceRow)
						.OnMouseButtonDoubleClick(this, &STagToolboxTagPicker::HandleReferenceDoubleClick)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]
		]
	];

	TagTreeChangedHandle = UGameplayTagsManager::OnEditorRefreshGameplayTagTree.AddSP(this, &STagToolboxTagPicker::HandleTagTreeChanged);
	if (!IsSelectionMode())
	{
		ScanStateChangedHandle = FTagToolboxTagScanService::Get().OnScanStateChanged.AddSP(this, &STagToolboxTagPicker::HandleScanStateChanged);
		RebuildUsageAggregates();
	}

	RebuildTree();
	RebuildRecentsStrip();
}

STagToolboxTagPicker::~STagToolboxTagPicker()
{
	UGameplayTagsManager::OnEditorRefreshGameplayTagTree.Remove(TagTreeChangedHandle);
	if (ScanStateChangedHandle.IsValid())
	{
		FTagToolboxTagScanService::Get().OnScanStateChanged.Remove(ScanStateChangedHandle);
	}
}

void STagToolboxTagPicker::HandleScanStateChanged()
{
	RebuildUsageAggregates();

	// Resorting defers one tick: the state change can arrive from inside a
	// scan or save path, and StableSort keeps the scroll position meaningful.
	if (bSortByUsage)
	{
		TWeakPtr<STagToolboxTagPicker> WeakSelf = SharedThis(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSelf](float)
		{
			if (const TSharedPtr<STagToolboxTagPicker> Self = WeakSelf.Pin())
			{
				Self->RebuildVisibility();
			}
			return false;
		}));
	}
	else
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void STagToolboxTagPicker::RebuildUsageAggregates()
{
	const FTagToolboxTagScanService& Service = FTagToolboxTagScanService::Get();
	UsageAggregates = (Service.GetState() == ETagToolboxScanState::NeverScanned)
		? TMap<FName, int32>()
		: BuildUsageAggregates(Service.GetReferencedTagToPackages());
}

FText STagToolboxTagPicker::GetCountBadgeText(FName CompleteTagName) const
{
	const FTagToolboxTagScanService& Service = FTagToolboxTagScanService::Get();
	switch (Service.GetState())
	{
	case ETagToolboxScanState::NeverScanned:
		return FText::AsCultureInvariant(TEXT("—")); // em-dash: no data, never a confident zero
	case ETagToolboxScanState::Stale:
		return FText::Format(LOCTEXT("CountStale", "{0} (stale)"), FText::AsNumber(Service.GetExactUsageCount(CompleteTagName)));
	default:
		return FText::AsNumber(Service.GetExactUsageCount(CompleteTagName));
	}
}

FText STagToolboxTagPicker::GetCountToolTipText() const
{
	switch (FTagToolboxTagScanService::Get().GetState())
	{
	case ETagToolboxScanState::NeverScanned:
		return LOCTEXT("CountToolTipNever", "No usage scan has run this session — press 'Count usage'. Counts are exact-name (children counted separately; the References pane below can include children). Unsaved edits are invisible to any scan.");
	case ETagToolboxScanState::Stale:
		return LOCTEXT("CountToolTipStale", "Counts are from the last scan and the tag table or saved content has changed since — press 'Recount'. Counts are exact-name (children counted separately; the References pane below can include children). Unsaved edits are invisible to any scan.");
	default:
		return LOCTEXT("CountToolTipFresh", "Saved packages referencing exactly this tag name (children counted separately; the References pane below can include children). Unsaved edits are invisible to any scan.");
	}
}

void STagToolboxTagPicker::SortNamesByAggregateUsage(TArray<FName>& Names, const TMap<FName, int32>& AggregateCounts)
{
	Names.StableSort([&AggregateCounts](const FName& A, const FName& B)
	{
		const int32 CountA = AggregateCounts.FindRef(A);
		const int32 CountB = AggregateCounts.FindRef(B);
		if (CountA != CountB)
		{
			return CountA > CountB;
		}
		return A.LexicalLess(B);
	});
}

TMap<FName, int32> STagToolboxTagPicker::BuildUsageAggregates(const TMap<FName, TArray<FName>>& ReferencedTagToPackages)
{
	TMap<FName, int32> Aggregates;
	for (const TPair<FName, TArray<FName>>& Pair : ReferencedTagToPackages)
	{
		const int32 ExactCount = Pair.Value.Num();
		FString NameString = Pair.Key.ToString();
		while (!NameString.IsEmpty())
		{
			int32& Aggregate = Aggregates.FindOrAdd(FName(*NameString));
			Aggregate = FMath::Max(Aggregate, ExactCount);
			int32 LastDot = INDEX_NONE;
			if (!NameString.FindLastChar(TEXT('.'), LastDot))
			{
				break;
			}
			NameString.LeftInline(LastDot);
		}
	}
	return Aggregates;
}

void STagToolboxTagPicker::RebuildTree()
{
	RootNodes.Reset();
	UGameplayTagsManager::Get().GetFilteredGameplayRootTags(RootFilter, RootNodes);
	RebuildVisibility();
}

void STagToolboxTagPicker::RebuildVisibility()
{
	VisibleNodes.Reset();
	VisibleRootNodes.Reset();

	for (const FTagNodePtr& Root : RootNodes)
	{
		if (BuildVisibilityRecursive(Root))
		{
			VisibleRootNodes.Add(Root);
		}
	}

	// U9: the usage lens also orders the ROOTS by subtree-aggregate count.
	if (bSortByUsage && VisibleRootNodes.Num() > 1)
	{
		VisibleRootNodes.StableSort([this](const FTagNodePtr& A, const FTagNodePtr& B)
		{
			const int32 CountA = UsageAggregates.FindRef(A->GetCompleteTagName());
			const int32 CountB = UsageAggregates.FindRef(B->GetCompleteTagName());
			if (CountA != CountB)
			{
				return CountA > CountB;
			}
			return A->GetCompleteTagName().LexicalLess(B->GetCompleteTagName());
		});
	}

	if (TagTree.IsValid())
	{
		TagTree->RequestTreeRefresh();

		// While a filter narrows the tree, expand everything that survived so
		// matches are visible without hunting; leave expansion alone otherwise.
		if (!SearchString.IsEmpty() || bFavoritesOnly)
		{
			TArray<FTagNodePtr> Pending = VisibleRootNodes;
			while (Pending.Num() > 0)
			{
				const FTagNodePtr Node = Pending.Pop();
				TagTree->SetItemExpansion(Node, true);
				for (const FTagNodePtr& Child : Node->GetChildTagNodes())
				{
					if (Child.IsValid() && VisibleNodes.Contains(Child.Get()))
					{
						Pending.Add(Child);
					}
				}
			}
		}
	}

	RefreshCreateRowPlan();
}

FTagToolboxCreateRowPlan STagToolboxTagPicker::BuildCreateRowPlan(
	const FString& SearchText,
	const FString& RootFilter,
	bool bTagExists,
	bool bExistingVisibleInView,
	bool bExistingAllowedByFilter,
	bool bCanCreateTags,
	bool bTagSourcesWritable,
	bool bCreateInFlight,
	const FString& ExistingResolvedName)
{
	FTagToolboxCreateRowPlan Plan;

	const FString Trimmed = SearchText.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return Plan; // Nothing was typed — nothing to offer or explain.
	}

	if (bTagExists)
	{
		if (bExistingVisibleInView)
		{
			return Plan; // The user can already see and click it.
		}
		if (bExistingAllowedByFilter)
		{
			// Hidden by the soft favorites lens, a stale search string, or a
			// redirect whose target the search text no longer matches — all
			// revealable through the existing SelectAndRevealTag primitive.
			Plan.Mode = ETagToolboxCreateRowMode::ExistsHidden;
			Plan.FinalTagString = ExistingResolvedName.IsEmpty() ? Trimmed : ExistingResolvedName;
			Plan.Reason = LOCTEXT("CreateRowRevealTip", "The tag exists but is hidden by the current view. Click to reveal it.");
			return Plan;
		}
		// Blocked by the property's hard Categories filter: correctly
		// unreachable from this picker — say so, offer nothing.
		Plan.Mode = ETagToolboxCreateRowMode::ExistsBlockedByFilter;
		Plan.FinalTagString = ExistingResolvedName.IsEmpty() ? Trimmed : ExistingResolvedName;
		Plan.Reason = FText::Format(LOCTEXT("CreateRowBlocked", "'{0}' exists but is outside this property's filter ({1})."),
			FText::AsCultureInvariant(Plan.FinalTagString), FText::AsCultureInvariant(RootFilter));
		return Plan;
	}

	if (!bTagSourcesWritable || !bCanCreateTags)
	{
		return Plan; // Creation unavailable: mirror the engine (no add row at all).
	}

	if (bCreateInFlight)
	{
		Plan.Mode = ETagToolboxCreateRowMode::InvalidInput;
		Plan.Reason = LOCTEXT("CreateRowInFlight", "A tag is being created...");
		return Plan;
	}

	// Out-of-filter input becomes a filter-root-prefixed offer: creating a
	// value the property can never hold, from the property's own picker, is a
	// footgun rather than a feature.
	FString Candidate = Trimmed;
	if (!RootFilter.IsEmpty() && !FTagToolboxTagClipboard::NameMatchesFilter(Candidate, RootFilter))
	{
		TArray<FString> Roots;
		RootFilter.ParseIntoArray(Roots, TEXT(","), /*bCullEmpty=*/true);
		if (Roots.Num() > 0)
		{
			Candidate = Roots[0].TrimStartAndEnd() + TEXT(".") + Trimmed;
		}
	}

	FText ValidationError;
	FString FixedString;
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	if (Manager.IsValidGameplayTagString(Candidate, &ValidationError, &FixedString))
	{
		Plan.Mode = ETagToolboxCreateRowMode::Offer;
		Plan.FinalTagString = Candidate;
		Plan.Reason = LOCTEXT("CreateRowOfferTip", "Create this tag in the project's tag list and select it.");
		return Plan;
	}

	// Adopt the engine's fixed-string suggestion when it is itself valid —
	// the offer then names exactly what will be created.
	if (!FixedString.IsEmpty() && FixedString != Candidate && Manager.IsValidGameplayTagString(FixedString, nullptr, nullptr))
	{
		Plan.Mode = ETagToolboxCreateRowMode::Offer;
		Plan.FinalTagString = FixedString;
		Plan.Reason = FText::Format(LOCTEXT("CreateRowFixedTip", "'{0}' is not a valid tag name; this creates '{1}' instead."),
			FText::AsCultureInvariant(Candidate), FText::AsCultureInvariant(FixedString));
		return Plan;
	}

	// Malformed input renders a visible disabled row carrying the specific
	// reason — never a bare absence (the R3 "never silently dropped" rule).
	Plan.Mode = ETagToolboxCreateRowMode::InvalidInput;
	Plan.Reason = FText::Format(LOCTEXT("CreateRowInvalid", "Cannot create '{0}': {1}"),
		FText::AsCultureInvariant(Candidate), ValidationError);
	return Plan;
}

void STagToolboxTagPicker::RefreshCreateRowPlan()
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	const FString Trimmed = SearchString.TrimStartAndEnd();
	bool bTagExists = false;
	bool bExistingVisibleInView = false;
	bool bExistingAllowedByFilter = false;
	FString ExistingResolvedName;

	if (!Trimmed.IsEmpty())
	{
		// RequestGameplayTag applies redirects before dictionary lookup, so a
		// typed OLD name resolves to its target — the row must never offer to
		// re-create a redirected name.
		const FGameplayTag Existing = FGameplayTag::RequestGameplayTag(FName(*Trimmed), /*ErrorIfNotFound=*/false);
		if (Existing.IsValid())
		{
			bTagExists = true;
			ExistingResolvedName = Existing.GetTagName().ToString();
			bExistingAllowedByFilter = FTagToolboxTagClipboard::NameMatchesFilter(ExistingResolvedName, RootFilter);
			if (const TSharedPtr<FGameplayTagNode> Node = Manager.FindTagNode(Existing.GetTagName()))
			{
				bExistingVisibleInView = VisibleNodes.Contains(Node.Get());
			}
		}
	}

	CreateRowPlan = BuildCreateRowPlan(
		SearchString,
		RootFilter,
		bTagExists,
		bExistingVisibleInView,
		bExistingAllowedByFilter,
		CanCreateTags.Get(true),
		Manager.ShouldImportTagsFromINI(),
		bCreateInFlight,
		ExistingResolvedName);
}

FReply STagToolboxTagPicker::ExecuteCreateRowAction()
{
	if (CreateRowPlan.Mode == ETagToolboxCreateRowMode::ExistsHidden)
	{
		SelectAndRevealTag(FName(*CreateRowPlan.FinalTagString));
		return FReply::Handled();
	}

	if (CreateRowPlan.Mode != ETagToolboxCreateRowMode::Offer || bCreateInFlight)
	{
		return FReply::Handled();
	}

	// Capture everything the post-create commit needs BEFORE the engine call:
	// the ini checkout can steal focus and dismiss a menu host mid-create, and
	// the commit must not depend on this widget surviving. The delegate copy
	// is SP-bound to the property customization, so a dead owner is a no-op.
	const FString FinalName = CreateRowPlan.FinalTagString;
	const FOnTagSelected CommitCopy = OnTagSelected;
	const bool bSelectionMode = IsSelectionMode();
	TWeakPtr<STagToolboxTagPicker> WeakSelf = SharedThis(this);

	bCreateInFlight = true;
	RefreshCreateRowPlan();

	const bool bAdded = IGameplayTagsEditorModule::Get().AddNewGameplayTagToINI(FinalName);
	// The engine call is synchronous (its tag-tree refresh already ran and
	// HandleTagTreeChanged cleared the flag); a failed create clears it here
	// so the row never latches dead. The engine has already shown the reason.
	bCreateInFlight = false;

	if (!bAdded)
	{
		RefreshCreateRowPlan();
		return FReply::Handled();
	}

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[CommitCopy, FinalName, bSelectionMode, WeakSelf](float)
		{
			const FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(FName(*FinalName), /*ErrorIfNotFound=*/false);
			if (bSelectionMode && NewTag.IsValid())
			{
				CommitCopy.ExecuteIfBound(NewTag);
			}
			if (const TSharedPtr<STagToolboxTagPicker> Self = WeakSelf.Pin())
			{
				// Search resets to the created name so the row never re-offers
				// a tag that now exists; browse mode also reveals it.
				if (Self->SearchBox.IsValid())
				{
					Self->SearchBox->SetText(FText::AsCultureInvariant(FinalName));
				}
				else
				{
					Self->SearchString = FinalName;
					Self->RebuildVisibility();
				}
				if (!bSelectionMode && NewTag.IsValid())
				{
					Self->SelectAndRevealTag(NewTag.GetTagName());
				}
			}
			return false; // one-shot
		}));

	return FReply::Handled();
}

bool STagToolboxTagPicker::BuildVisibilityRecursive(const FTagNodePtr& Node)
{
	if (!Node.IsValid())
	{
		return false;
	}

	bool bVisible = NodePassesFilter(Node);
	for (const FTagNodePtr& Child : Node->GetChildTagNodes())
	{
		bVisible |= BuildVisibilityRecursive(Child);
	}

	if (bVisible)
	{
		VisibleNodes.Add(Node.Get());
	}
	return bVisible;
}

bool STagToolboxTagPicker::NodePassesFilter(const FTagNodePtr& Node) const
{
	if (bFavoritesOnly && !Favorites.Contains(Node->GetCompleteTagName()))
	{
		return false;
	}
	if (!SearchString.IsEmpty() && !Node->GetCompleteTagString().Contains(SearchString))
	{
		return false;
	}
	return true;
}

TSharedRef<ITableRow> STagToolboxTagPicker::GenerateRow(FTagNodePtr Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	static const FSlateRoundedBoxBrush SwatchBrush(FLinearColor::White, 2.0f);

	const FName CompleteName = Node->GetCompleteTagName();
	const FGameplayTag NodeCompleteTag = Node->GetCompleteTag();

	FText ToolTip;
	{
		FString ToolTipString = Node->GetCompleteTagString();
#if WITH_EDITORONLY_DATA
		const FString DevComment = Node->GetDevComment();
		if (!DevComment.IsEmpty())
		{
			ToolTipString += TEXT("\n") + DevComment;
		}
		const FName SourceName = Node->GetFirstSourceName();
		if (!SourceName.IsNone())
		{
			ToolTipString += FString::Printf(TEXT("\nSource: %s"), *SourceName.ToString());
		}
#endif
		ToolTip = FText::AsCultureInvariant(ToolTipString);
	}

	return SNew(STableRow<FTagNodePtr>, OwnerTable)
	.ToolTipText(ToolTip)
	[
		SNew(SHorizontalBox)

		// Assigned-tag check (selection mode only)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(IsSelectionMode() ? 16.0f : 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Check"))
				.Visibility_Lambda([this, NodeCompleteTag]()
				{
					return (IsSelectionMode() && CurrentTag.Get(FGameplayTag()) == NodeCompleteTag) ? EVisibility::Visible : EVisibility::Hidden;
				})
			]
		]

		// Favorite star
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.0f, 0.0f))
			.ToolTipText(LOCTEXT("FavoriteToolTip", "Favorite / unfavorite this tag"))
			.OnClicked_Lambda([this, Node]()
			{
				ToggleFavorite(Node);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Node]() { return IsFavorite(Node) ? FText::AsCultureInvariant(TEXT("★")) : FText::AsCultureInvariant(TEXT("☆")); })
				.ColorAndOpacity_Lambda([this, Node]()
				{
					return IsFavorite(Node) ? FSlateColor(FLinearColor(1.0f, 0.78f, 0.15f)) : FSlateColor::UseSubduedForeground();
				})
			]
		]

		// Registry color swatch
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(10.0f)
			.HeightOverride(10.0f)
			[
				SNew(SBorder)
				.BorderImage(&SwatchBrush)
				.BorderBackgroundColor_Lambda([this, CompleteName]() { return FSlateColor(ResolveSwatchColor(CompleteName)); })
			]
		]

		// Leaf name
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromName(Node->GetSimpleTagName()))
		]

		// U9: exact-name usage badge (browse mode; paint-time cache lookup,
		// never a registry query). Em-dash = not scanned; "(stale)" = honest.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(IsSelectionMode() ? EVisibility::Collapsed : EVisibility::Visible)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.ColorAndOpacity_Lambda([]()
			{
				return FTagToolboxTagScanService::Get().GetState() == ETagToolboxScanState::Stale
					? FSlateColor(FLinearColor(0.95f, 0.65f, 0.2f))
					: FSlateColor::UseSubduedForeground();
			})
			.Text_Lambda([this, CompleteName]() { return GetCountBadgeText(CompleteName); })
			.ToolTipText_Lambda([this]() { return GetCountToolTipText(); })
		]
	];
}

void STagToolboxTagPicker::GetChildrenForNode(FTagNodePtr Node, TArray<FTagNodePtr>& OutChildren)
{
	if (!Node.IsValid())
	{
		return;
	}
	for (const FTagNodePtr& Child : Node->GetChildTagNodes())
	{
		if (Child.IsValid() && VisibleNodes.Contains(Child.Get()))
		{
			OutChildren.Add(Child);
		}
	}

	// U9: the usage lens reorders SIBLINGS at each depth by subtree-aggregate
	// count, so a hot leaf under a quiet parent still surfaces.
	if (bSortByUsage && OutChildren.Num() > 1)
	{
		OutChildren.StableSort([this](const FTagNodePtr& A, const FTagNodePtr& B)
		{
			const int32 CountA = UsageAggregates.FindRef(A->GetCompleteTagName());
			const int32 CountB = UsageAggregates.FindRef(B->GetCompleteTagName());
			if (CountA != CountB)
			{
				return CountA > CountB;
			}
			return A->GetCompleteTagName().LexicalLess(B->GetCompleteTagName());
		});
	}
}

void STagToolboxTagPicker::HandleSelectionChanged(FTagNodePtr Node, ESelectInfo::Type SelectInfo)
{
	if (!Node.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	if (IsSelectionMode())
	{
		CommitSelectedTag(Node->GetCompleteTag());
	}
	else
	{
		AddRecent(Node->GetCompleteTagName());
		ReferencesTag = Node->GetCompleteTag();
		RefreshReferences();
	}
}

TSharedPtr<SWidget> STagToolboxTagPicker::BuildRowContextMenu()
{
	// Never open management menus from an auto-dismissing menu host — pushing a
	// menu against a dying menu stack is the documented fatal-assert class.
	if (bMenuHosted)
	{
		return nullptr;
	}

	const TArray<FTagNodePtr> Selected = TagTree.IsValid() ? TagTree->GetSelectedItems() : TArray<FTagNodePtr>();
	if (Selected.Num() != 1 || !Selected[0].IsValid())
	{
		return nullptr;
	}

	const FTagNodePtr Node = Selected[0];
	const FGameplayTag NodeTag = Node->GetCompleteTag();

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	MenuBuilder.BeginSection(NAME_None, FText::FromName(Node->GetCompleteTagName()));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyTagName", "Copy Tag Name"),
		LOCTEXT("CopyTagNameToolTip", "Copy the full tag name to the clipboard."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateLambda([Node]()
		{
			FPlatformApplicationMisc::ClipboardCopy(*Node->GetCompleteTagString());
		})));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameTag", "Rename Tag (fix up assets)..."),
		LOCTEXT("RenameTagToolTip", "Rename this tag (children included): previews every referencing package, writes redirects, resaves referencers under consent, verifies, and offers redirect retirement."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Rename"),
		FUIAction(FExecuteAction::CreateLambda([NodeName = Node->GetCompleteTagName()]()
		{
			STagToolboxRenameDialog::ShowForTag(NodeName);
		})));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetTagColor", "Set Color..."),
		LOCTEXT("SetTagColorToolTip", "Set this tag's project-wide color in the Tag Toolbox style registry. Descendant tags inherit it unless they have their own entry."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ColorPicker.Mode"),
		FUIAction(FExecuteAction::CreateSP(this, &STagToolboxTagPicker::OpenColorPickerForTag, NodeTag)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ClearTagColor", "Clear Color"),
		LOCTEXT("ClearTagColorToolTip", "Remove this tag's own style entry (ancestor styles keep applying)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([NodeTag]()
		{
			GetMutableDefault<UTagToolboxSettings>()->ClearTagColor(NodeTag);
		})));

	MenuBuilder.AddMenuEntry(
		IsFavorite(Node) ? LOCTEXT("Unfavorite", "Remove from Favorites") : LOCTEXT("Favorite", "Add to Favorites"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &STagToolboxTagPicker::ToggleFavorite, Node)));

	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

void STagToolboxTagPicker::HandleSearchChanged(const FText& NewText)
{
	SearchString = NewText.ToString().TrimStartAndEnd();
	RebuildVisibility();
}

void STagToolboxTagPicker::HandleTagTreeChanged()
{
	// A tree refresh means any in-flight create has completed its synchronous
	// engine path — the row must never stay latched disabled past it.
	bCreateInFlight = false;

	// The manager rebuilt its node tree; every cached node pointer is stale —
	// including any the tree view still holds as its selection.
	if (TagTree.IsValid())
	{
		TagTree->ClearSelection();
	}
	RebuildTree();
	RebuildRecentsStrip();
}

bool STagToolboxTagPicker::IsFavorite(const FTagNodePtr& Node) const
{
	return Node.IsValid() && Favorites.Contains(Node->GetCompleteTagName());
}

void STagToolboxTagPicker::ToggleFavorite(FTagNodePtr Node)
{
	if (!Node.IsValid())
	{
		return;
	}

	const FName CompleteName = Node->GetCompleteTagName();
	if (Favorites.Contains(CompleteName))
	{
		Favorites.Remove(CompleteName);
	}
	else
	{
		Favorites.Add(CompleteName);
	}
	SavePersistedState();

	if (bFavoritesOnly)
	{
		RebuildVisibility();
	}
}

void STagToolboxTagPicker::AddRecent(FName CompleteTagName)
{
	Recents.Remove(CompleteTagName);
	Recents.Insert(CompleteTagName, 0);
	if (Recents.Num() > TagToolboxPicker::MaxRecents)
	{
		Recents.SetNum(TagToolboxPicker::MaxRecents);
	}
	SavePersistedState();
	RebuildRecentsStrip();
}

void STagToolboxTagPicker::SelectAndRevealTag(FName CompleteTagName)
{
	const TSharedPtr<FGameplayTagNode> Node = UGameplayTagsManager::Get().FindTagNode(CompleteTagName);
	if (!Node.IsValid() || !TagTree.IsValid())
	{
		return;
	}

	// A filtered-out target would select invisibly; reset the lens first.
	if (!VisibleNodes.Contains(Node.Get()))
	{
		bFavoritesOnly = false;
		SearchString.Reset();
		RebuildVisibility();
	}

	for (TSharedPtr<FGameplayTagNode> Parent = Node->GetParentTagNode(); Parent.IsValid(); Parent = Parent->GetParentTagNode())
	{
		TagTree->SetItemExpansion(Parent, true);
	}
	TagTree->SetSelection(Node);
	TagTree->RequestScrollIntoView(Node);

	// Programmatic selection reports ESelectInfo::Direct, which the selection
	// handler ignores — refresh the references pane explicitly.
	ReferencesTag = Node->GetCompleteTag();
	RefreshReferences();
}

void STagToolboxTagPicker::RebuildRecentsStrip()
{
	if (!RecentsStrip.IsValid())
	{
		return;
	}

	static const FSlateRoundedBoxBrush ChipBrush(FLinearColor::White, 3.0f);

	RecentsStrip->ClearChildren();
	if (Recents.Num() == 0)
	{
		return;
	}

	RecentsStrip->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 6.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("RecentsLabel", "Recent:"))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
	];

	for (const FName& RecentName : Recents)
	{
		// In selection mode, skip recents that fall outside the root filter —
		// offering a one-click commit to an out-of-filter tag would defeat it.
		if (IsSelectionMode() && !RootFilter.IsEmpty())
		{
			const FGameplayTag RecentTag = FGameplayTag::RequestGameplayTag(RecentName, /*ErrorIfNotFound=*/false);
			if (!RecentTag.IsValid())
			{
				continue;
			}
			TArray<FString> Roots;
			RootFilter.ParseIntoArray(Roots, TEXT(","), true);
			bool bInsideFilter = false;
			const FString RecentString = RecentName.ToString();
			for (FString& Root : Roots)
			{
				Root.TrimStartAndEndInline();
				if (RecentString == Root || RecentString.StartsWith(Root + TEXT(".")))
				{
					bInsideFilter = true;
					break;
				}
			}
			if (!bInsideFilter)
			{
				continue;
			}
		}

		const FString Leaf = [&RecentName]()
		{
			FString Full = RecentName.ToString();
			int32 LastDot = INDEX_NONE;
			return Full.FindLastChar(TEXT('.'), LastDot) ? Full.Mid(LastDot + 1) : Full;
		}();

		RecentsStrip->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(0.0f))
			.ToolTipText(FText::FromName(RecentName))
			.OnClicked_Lambda([this, RecentName]()
			{
				if (IsSelectionMode())
				{
					const FGameplayTag RecentTag = FGameplayTag::RequestGameplayTag(RecentName, /*ErrorIfNotFound=*/false);
					if (RecentTag.IsValid())
					{
						CommitSelectedTag(RecentTag);
					}
				}
				else
				{
					SelectAndRevealTag(RecentName);
				}
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(&ChipBrush)
				.BorderBackgroundColor_Lambda([this, RecentName]() { return FSlateColor(ResolveSwatchColor(RecentName)); })
				.Padding(FMargin(6.0f, 2.0f))
				[
					SNew(STextBlock)
					.Text(FText::AsCultureInvariant(Leaf))
					.ColorAndOpacity_Lambda([this, RecentName]()
					{
						const float Luminance = ResolveSwatchColor(RecentName).GetLuminance();
						return FSlateColor(Luminance > 0.5f ? FLinearColor(0.02f, 0.02f, 0.02f) : FLinearColor(0.95f, 0.95f, 0.95f));
					})
				]
			]
		];
	}
}

void STagToolboxTagPicker::CommitSelectedTag(const FGameplayTag& NewTag)
{
	if (NewTag.IsValid())
	{
		AddRecent(NewTag.GetTagName());
	}
	OnTagSelected.ExecuteIfBound(NewTag);
}

void STagToolboxTagPicker::RefreshReferences()
{
	ReferenceRows.Reset();

	if (ReferencesTag.IsValid())
	{
		TArray<FName> TagNames;
		TagNames.Add(ReferencesTag.GetTagName());
		if (bIncludeChildReferences)
		{
			const FGameplayTagContainer Children = UGameplayTagsManager::Get().RequestGameplayTagChildren(ReferencesTag);
			for (const FGameplayTag& Child : Children)
			{
				TagNames.Add(Child.GetTagName());
			}
		}

		// SearchableName referencers are registry metadata; the whole query is
		// a map lookup per name — no assets load.
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		TSet<FName> Packages;
		TArray<FAssetIdentifier> Referencers;
		for (const FName& TagName : TagNames)
		{
			Referencers.Reset();
			AssetRegistry.GetReferencers(FAssetIdentifier(FGameplayTag::StaticStruct(), TagName), Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);
			for (const FAssetIdentifier& Referencer : Referencers)
			{
				if (!Referencer.PackageName.IsNone())
				{
					Packages.Add(Referencer.PackageName);
				}
			}
		}

		TArray<FName> SortedPackages = Packages.Array();
		SortedPackages.Sort(FNameLexicalLess());

		TArray<FAssetData> PackageAssets;
		for (const FName& PackageName : SortedPackages)
		{
			PackageAssets.Reset();
			AssetRegistry.GetAssetsByPackageName(PackageName, PackageAssets);
			if (PackageAssets.Num() > 0)
			{
				ReferenceRows.Add(MakeShared<FAssetData>(PackageAssets[0]));
			}
		}
	}

	if (ReferencesList.IsValid())
	{
		ReferencesList->RequestListRefresh();
	}
}

TSharedRef<ITableRow> STagToolboxTagPicker::GenerateReferenceRow(TSharedPtr<FAssetData> Asset, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FText AssetName = Asset.IsValid() ? FText::FromName(Asset->AssetName) : FText::GetEmpty();
	const FText ClassName = Asset.IsValid() ? FText::FromString(Asset->AssetClassPath.GetAssetName().ToString()) : FText::GetEmpty();
	const FText PathToolTip = Asset.IsValid() ? FText::FromName(Asset->PackageName) : FText::GetEmpty();

	return SNew(STableRow<TSharedPtr<FAssetData>>, OwnerTable)
	.ToolTipText(FText::Format(LOCTEXT("ReferenceRowToolTip", "{0}\nDouble-click to open."), PathToolTip))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(AssetName)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(ClassName)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
}

void STagToolboxTagPicker::HandleReferenceDoubleClick(TSharedPtr<FAssetData> Asset)
{
	if (Asset.IsValid() && GEditor)
	{
		if (UObject* AssetObject = Asset->GetAsset())
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(AssetObject);
		}
	}
}

FText STagToolboxTagPicker::GetReferencesSummary() const
{
	if (!ReferencesTag.IsValid())
	{
		return LOCTEXT("ReferencesNoTag", "References — select a tag");
	}
	return FText::Format(LOCTEXT("ReferencesSummaryFormat", "References: {0} asset(s) use {1}"),
		FText::AsNumber(ReferenceRows.Num()), FText::AsCultureInvariant(ReferencesTag.ToString()));
}

void STagToolboxTagPicker::OpenColorPickerForTag(FGameplayTag InTag)
{
	FLinearColor InitialColor(0.5f, 0.5f, 0.5f);
	GetDefault<UTagToolboxSettings>()->ResolveTagColor(InTag, InitialColor);

	FColorPickerArgs PickerArgs(InitialColor, FOnLinearColorValueChanged::CreateLambda([InTag](FLinearColor NewColor)
	{
		GetMutableDefault<UTagToolboxSettings>()->SetTagColor(InTag, NewColor);
	}));
	PickerArgs.bUseAlpha = false;
	OpenColorPicker(PickerArgs);
}

void STagToolboxTagPicker::LoadPersistedState()
{
	FString Joined;
	if (GConfig->GetString(TagToolboxPicker::ConfigSection, TagToolboxPicker::FavoritesKey, Joined, GEditorPerProjectIni))
	{
		Favorites.Append(TagToolboxPicker::SplitNames(Joined));
	}
	Joined.Reset();
	if (GConfig->GetString(TagToolboxPicker::ConfigSection, TagToolboxPicker::RecentsKey, Joined, GEditorPerProjectIni))
	{
		Recents = TagToolboxPicker::SplitNames(Joined);
	}
}

void STagToolboxTagPicker::SavePersistedState() const
{
	// SetString alone is durable; the engine flushes on exit. Never flush
	// manually on an interactive path — a flush rewrites the whole ini.
	GConfig->SetString(TagToolboxPicker::ConfigSection, TagToolboxPicker::FavoritesKey, *TagToolboxPicker::JoinNames(Favorites.Array()), GEditorPerProjectIni);
	GConfig->SetString(TagToolboxPicker::ConfigSection, TagToolboxPicker::RecentsKey, *TagToolboxPicker::JoinNames(Recents), GEditorPerProjectIni);
}

FLinearColor STagToolboxTagPicker::ResolveSwatchColor(FName CompleteTagName) const
{
	const FGameplayTag ResolvedTag = FGameplayTag::RequestGameplayTag(CompleteTagName, /*ErrorIfNotFound=*/false);
	FLinearColor Color;
	if (TagToolboxColorBridge::ResolveTagColor(ResolvedTag, Color))
	{
		return Color;
	}
	return FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
}

#undef LOCTEXT_NAMESPACE
