// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

struct FGameplayTagNode;
class SHorizontalBox;
class SSearchBox;
class STextBlock;

/** What the picker's inline create row shows for the current search text. */
enum class ETagToolboxCreateRowMode : uint8
{
	/** No row (empty search, tag visible, or creation unavailable). */
	Hidden,
	/** Clickable: create FinalTagString in place. */
	Offer,
	/** Clickable: the tag exists but is hidden (lens/search/redirect target) — reveal it. */
	ExistsHidden,
	/** Non-actionable: the tag exists but the property's hard Categories filter excludes it. */
	ExistsBlockedByFilter,
	/** Non-actionable: the input cannot become a tag; Reason says why. */
	InvalidInput,
};

/** One evaluated create-row decision. Built pure so every state is testable. */
struct FTagToolboxCreateRowPlan
{
	ETagToolboxCreateRowMode Mode = ETagToolboxCreateRowMode::Hidden;
	/** Offer: the (possibly fixed / filter-prefixed) name to create — also the
	 *  post-create search text. ExistsHidden: the resolved name to reveal. */
	FString FinalTagString;
	/** Human-readable cause for the non-actionable modes. */
	FText Reason;
};

/**
 * The Tag Toolbox tag tree, built from scratch over UGameplayTagsManager's
 * public node API (the engine picker's row generation is private, so colored
 * rows and favorites cannot be added to it from outside).
 *
 * Two modes share one widget:
 * - BROWSE (default): the dockable Tag Browser — full tree, favorites,
 *   recents, right-click color authoring straight into the style registry.
 * - SELECTION (OnTagSelected bound): the dropdown for tag properties —
 *   clicking a row (or a recents chip) commits that tag through the delegate;
 *   a Clear action commits the empty tag. Set bMenuHosted when hosting inside
 *   an auto-dismissing menu: row context menus are disabled there (pushing a
 *   menu from a dying menu host is the documented editor-fatal crash).
 *
 * Colors resolve through the style registry with ancestor fall-up (plus the
 * optional Paper2DPlus registry fallback); favorites/recents persist per user
 * per project in the editor ini (never flushed manually).
 */
class STagToolboxTagPicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnTagSelected, const FGameplayTag& /*NewTag; empty = cleared*/)

	SLATE_BEGIN_ARGS(STagToolboxTagPicker)
		: _Filter()
		, _MenuHosted(false)
		, _MaxHeight(0.0f)
		, _CanCreateTags(true)
	{}
		/** Comma-delimited root-tag names to restrict the tree to (Categories format). */
		SLATE_ARGUMENT(FString, Filter)

		/** Binding this switches the widget into selection mode. */
		SLATE_EVENT(FOnTagSelected, OnTagSelected)

		/** The currently assigned tag, marked in the tree (selection mode). */
		SLATE_ATTRIBUTE(FGameplayTag, CurrentTag)

		/** True when hosted inside an auto-dismissing menu: selection-only, no row context menus. */
		SLATE_ARGUMENT(bool, MenuHosted)

		/** Caps the tree height (0 = uncapped). Set when hosting in a menu. */
		SLATE_ARGUMENT(float, MaxHeight)

		/**
		 * Gate for the inline create row's Offer mode (the pill passes the
		 * property's editability; browse mode leaves it true). The row is
		 * additionally gated on ShouldImportTagsFromINI().
		 */
		SLATE_ATTRIBUTE(bool, CanCreateTags)
	SLATE_END_ARGS()

	/**
	 * Pure usage-sort comparator (U9): descending by aggregate count with a
	 * lexical name tiebreak. Names absent from the map count zero.
	 */
	static void SortNamesByAggregateUsage(TArray<FName>& Names, const TMap<FName, int32>& AggregateCounts);

	/**
	 * Pure aggregate builder (U9): each exact-count entry max-updates itself
	 * and every ancestor name, so a high-usage leaf under a quiet parent still
	 * surfaces when siblings sort by usage. Badges keep showing OWN exact
	 * counts; aggregates only order the tree.
	 */
	static TMap<FName, int32> BuildUsageAggregates(const TMap<FName, TArray<FName>>& ReferencedTagToPackages);

	/**
	 * The pure create-row decision (U4): every visual state of the inline
	 * create row derives from this one function so tests can drive it without
	 * Slate. Validation runs the engine's tag-string check (with its fixed
	 * suggestion adopted when valid); out-of-filter input is offered as a
	 * filter-root-prefixed name, never as a bare tag the property could not
	 * hold. Malformed non-empty input always yields a visible InvalidInput row
	 * carrying the specific reason (the "never silently dropped" rule);
	 * whitespace-only input hides the row (nothing was typed).
	 */
	static FTagToolboxCreateRowPlan BuildCreateRowPlan(
		const FString& SearchText,
		const FString& RootFilter,
		bool bTagExists,
		bool bExistingVisibleInView,
		bool bExistingAllowedByFilter,
		bool bCanCreateTags,
		bool bTagSourcesWritable,
		bool bCreateInFlight,
		const FString& ExistingResolvedName);

	void Construct(const FArguments& InArgs);
	virtual ~STagToolboxTagPicker() override;

private:
	using FTagNodePtr = TSharedPtr<FGameplayTagNode>;

	bool IsSelectionMode() const { return OnTagSelected.IsBound(); }

	void RebuildTree();
	void RebuildVisibility();
	bool BuildVisibilityRecursive(const FTagNodePtr& Node);
	bool NodePassesFilter(const FTagNodePtr& Node) const;

	TSharedRef<ITableRow> GenerateRow(FTagNodePtr Node, const TSharedRef<STableViewBase>& OwnerTable);
	void GetChildrenForNode(FTagNodePtr Node, TArray<FTagNodePtr>& OutChildren);
	void HandleSelectionChanged(FTagNodePtr Node, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> BuildRowContextMenu();

	void HandleSearchChanged(const FText& NewText);
	void HandleTagTreeChanged();

	/** Re-derives CreateRowPlan from the live tree/search/filter state. */
	void RefreshCreateRowPlan();
	/** Executes the plan's clickable modes (create, or reveal a hidden tag). */
	FReply ExecuteCreateRowAction();

	bool IsFavorite(const FTagNodePtr& Node) const;
	void ToggleFavorite(FTagNodePtr Node);
	void AddRecent(FName CompleteTagName);
	void SelectAndRevealTag(FName CompleteTagName);
	void RebuildRecentsStrip();

	void CommitSelectedTag(const FGameplayTag& NewTag);

	/**
	 * References pane (browse mode only): every asset whose SAVED data
	 * references the selected tag, straight from Asset Registry
	 * searchable-name metadata — instant, no asset loads. Points at the
	 * asset, not the node inside it; unsaved edits and C++ call sites are
	 * invisible by nature of the registry.
	 */
	void RefreshReferences();
	TSharedRef<ITableRow> GenerateReferenceRow(TSharedPtr<FAssetData> Asset, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleReferenceDoubleClick(TSharedPtr<FAssetData> Asset);
	FText GetReferencesSummary() const;

	void OpenColorPickerForTag(FGameplayTag InTag);

	void LoadPersistedState();
	void SavePersistedState() const;

	FLinearColor ResolveSwatchColor(FName CompleteTagName) const;

	TSharedPtr<STreeView<FTagNodePtr>> TagTree;
	TSharedPtr<SHorizontalBox> RecentsStrip;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FAssetData>>> ReferencesList;

	/** The current inline create-row decision (see BuildCreateRowPlan). */
	FTagToolboxCreateRowPlan CreateRowPlan;
	/** Editability gate supplied by the hosting property (browse mode: true). */
	TAttribute<bool> CanCreateTags;
	/**
	 * True from the moment an engine create is invoked until its synchronous
	 * tag-tree refresh lands (or the call returns false). A failed create
	 * clears it immediately so the row never latches dead.
	 */
	bool bCreateInFlight = false;

	/** Assets referencing the selected tag (browse mode). */
	TArray<TSharedPtr<FAssetData>> ReferenceRows;
	FGameplayTag ReferencesTag;
	bool bIncludeChildReferences = false;

	/** Roots from the manager, restricted by RootFilter; rebuilt on tag-tree refresh. */
	TArray<FTagNodePtr> RootNodes;
	/** Roots currently shown (post search/favorites filter). */
	TArray<FTagNodePtr> VisibleRootNodes;
	/** Nodes passing the active filter (self or via a descendant). */
	TSet<FGameplayTagNode*> VisibleNodes;

	FString RootFilter;
	FOnTagSelected OnTagSelected;
	TAttribute<FGameplayTag> CurrentTag;
	bool bMenuHosted = false;

	FString SearchString;
	bool bFavoritesOnly = false;

	/** U9: sort-by-usage lens (browse mode; disabled until a scan exists). */
	bool bSortByUsage = false;
	/** Subtree-aggregate counts rebuilt on scan completion / tree rebuild. */
	TMap<FName, int32> UsageAggregates;
	FDelegateHandle ScanStateChangedHandle;

	void HandleScanStateChanged();
	void RebuildUsageAggregates();
	FText GetCountBadgeText(FName CompleteTagName) const;
	FText GetCountToolTipText() const;

	/** Complete tag names, persisted per user per project. */
	TSet<FName> Favorites;
	/** Most recent first, capped; persisted per user per project. */
	TArray<FName> Recents;

	FDelegateHandle TagTreeChangedHandle;
};
