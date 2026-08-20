// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "IDetailCustomization.h"

class IBlueprintEditor;
class UBlueprint;

/**
 * Adds a "Tag Filter" row to the Variable category of the Blueprint editor's
 * variable Details for gameplay-tag-flavored variables: FGameplayTag,
 * FGameplayTagContainer, and arrays/sets/maps whose element/key/value is one.
 *
 * The row edits the same "Categories" metadata C++ uses (via
 * FBlueprintEditorUtils::SetBlueprintVariableMetaData), so the stock picker —
 * and every other Categories-aware surface — honors it after the write. The
 * engine's own 5.7+ "Gameplay Tag Roots" row covers only plain tag/container
 * variables from inside the value editor; this row also covers container
 * variables, whose per-element customizations never qualify.
 */
class FTagToolboxVariableFilterCustomization : public IDetailCustomization
{
public:
	/** Factory registered through FBlueprintEditorModule::Register(Local)VariableCustomization. */
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	explicit FTagToolboxVariableFilterCustomization(TWeakObjectPtr<UBlueprint> InBlueprint)
		: Blueprint(InBlueprint)
	{
	}

	//~ IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

	/**
	 * True when the variable property is gameplay-tag flavored: a tag or tag
	 * container directly, or an array/set/map carrying one. Pure; testable.
	 */
	static bool IsTagFlavoredVariable(const FProperty* Property);

private:
	TWeakObjectPtr<UBlueprint> Blueprint;
};
