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
class STextBlock;

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
	SLATE_END_ARGS()

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
	TSharedPtr<SListView<TSharedPtr<FAssetData>>> ReferencesList;

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

	/** Complete tag names, persisted per user per project. */
	TSet<FName> Favorites;
	/** Most recent first, capped; persisted per user per project. */
	TArray<FName> Recents;

	FDelegateHandle TagTreeChangedHandle;
};
