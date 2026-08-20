// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "IPropertyTypeCustomization.h"
#include "TagToolboxTagRootsChildRow.h"

class IPropertyHandle;
class SComboButton;
class SWrapBox;

/**
 * Editor-wide replacement for the stock FGameplayTagContainer property widget:
 * a wrap strip of colored chips (style registry + Paper2DPlus fallback, each
 * with a remove button) that COLLAPSES once the container holds more than
 * UTagToolboxSettings::MaxVisibleTagChips — a "+N more" chip expands it, "Show
 * less" folds it back — plus an Edit dropdown hosting the engine multi-select
 * picker bound to the property handle (full add/remove/search/add-new-tag).
 *
 * Multi-object edits display the first object's container, matching the tag
 * pill; removals apply to every selected object (engine-mirrored
 * SetPerObjectValues). CustomizeChildren adds the shared "Gameplay Tag Roots"
 * row for Blueprint-created variables.
 */
class FTagToolboxTagContainerPillCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	/** First-object value of the edited property (multi-edit shows the first). */
	FGameplayTagContainer GetCurrentContainer() const;

	void RebuildChipStrip();
	/**
	 * Deferred rebuild for paths triggered from inside a chip's own click
	 * handler (toggle, per-chip remove) or a property-change notify: rebuilding
	 * synchronously would ClearChildren the very widget whose handler is still
	 * on the stack.
	 */
	void RequestRebuildChipStrip();
	TSharedRef<SWidget> MakeTagChip(const FGameplayTag& Tag);
	TSharedRef<SWidget> MakeToggleChip(const FText& Label, const FText& ToolTip);

	/** Removes one tag from every selected object (engine-mirrored commit). */
	void HandleRemoveTag(FGameplayTag TagToRemove);

	TSharedRef<SWidget> BuildPickerMenuContent();

	TSharedPtr<IPropertyHandle> StructPropertyHandle;
	TSharedPtr<SComboButton> ComboButton;
	TSharedPtr<SWrapBox> ChipStrip;

	/** True while the full chip list is shown despite exceeding the collapse limit. */
	bool bChipsExpanded = false;

	/** Shared "Gameplay Tag Roots" child row (Blueprint-created variables). */
	FTagToolboxTagRootsChildRow RootsRow;
};
