// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class IDetailChildrenBuilder;
class IPropertyHandle;
class IPropertyTypeCustomizationUtils;
class UBlueprint;

/**
 * The "Gameplay Tag Roots" child row shared by the tag pill and the tag
 * container customizations: for Blueprint-created variables it edits the same
 * "Categories" metadata C++ uses, reproducing the engine's 5.7+ native row
 * (which disappears whenever a plugin replaces the type customization).
 *
 * Owned as a member by the hosting IPropertyTypeCustomization; add the row
 * from CustomizeChildren. State (creator Blueprint, current categories) lives
 * here so both hosts stay identical.
 */
class FTagToolboxTagRootsChildRow
{
public:
	/** Adds the row when the property is a Blueprint-created variable; no-ops otherwise. */
	void AddIfBlueprintVariable(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils);

private:
	FGameplayTagContainer GetMetaDataCategories() const { return MetaDataCategories; }
	void OnCategoriesComboClosed(const FGameplayTagContainer& NewTagContainer);
	void OnCategoriesCleared();

	TSharedPtr<IPropertyHandle> PropertyHandle;
	FGameplayTagContainer MetaDataCategories;
	TWeakObjectPtr<UBlueprint> CreatorBP;
};
