// Copyright Infinite Game Works. All Rights Reserved.

#include "STagToolboxTagPicker.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameplayTagsManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
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
				SNew(SSearchBox)
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

	RebuildTree();
	RebuildRecentsStrip();
}

STagToolboxTagPicker::~STagToolboxTagPicker()
{
	UGameplayTagsManager::OnEditorRefreshGameplayTagTree.Remove(TagTreeChangedHandle);
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
