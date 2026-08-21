// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "IPropertyTypeCustomization.h"
#include "TagToolboxTagRootsChildRow.h"

class IPropertyHandle;
class SComboButton;

/**
 * Editor-wide replacement for the stock FGameplayTag property widget: the closed
 * chip is a colored pill driven by the Tag Toolbox style registry (ancestor
 * fall-up, Paper2DPlus registry fallback; neutral gray when unstyled/unset),
 * and the dropdown is Tag Toolbox's own picker — colored rows, favorites,
 * one-click recents — honoring the property's resolved Categories filter.
 *
 * The filter resolves through
 * UGameplayTagsManager::GetCategoriesMetaFromPropertyHandle — never the bare
 * GetMetaData("Categories") — so filters authored on parent handles and function
 * parameters keep applying.
 *
 * CustomizeChildren adds the shared "Gameplay Tag Roots" row for
 * Blueprint-created variables (identifier-less type registration replaces the
 * whole engine customization, so the native 5.7+ row would otherwise vanish).
 */
class FTagToolboxTagPillCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	/** First-object value of the edited property (multi-edit takes the first). */
	FGameplayTag GetCurrentTag() const;

	FSlateColor GetPillColor() const;
	FSlateColor GetPillTextColor() const;
	FText GetPillText() const;

	TSharedRef<SWidget> BuildPickerMenuContent();

	/** Commits a picker selection to the property (empty tag = clear) and closes the combo. */
	void HandlePickerTagSelected(const FGameplayTag& NewTag);

	/** Copy: the plain tag string (the engine chip's clipboard shape). */
	void HandleCopyTag() const;
	bool CanCopyTag() const;
	/**
	 * Paste: accepts one tag (plain name or export form; redirected old names
	 * resolve to their target) through the one commit funnel. Refusals notify
	 * with the actual cause — container-shaped content, unparseable text, or a
	 * tag outside the property's resolved filter — never silently drop.
	 */
	void HandlePasteTag();
	/** Silent probe for action enablement — no notifications, no logging. */
	bool CanPasteTag() const;

	/** RMB on the pill: Copy / Paste / Clear, mirroring the engine chip. */
	FReply HandlePillMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Clear entry: commits the empty tag through the one funnel. */
	void HandleClearTag();

	FString GetResolvedFilter() const;

	TSharedPtr<IPropertyHandle> StructPropertyHandle;
	TSharedPtr<SComboButton> ComboButton;

	/** Shared "Gameplay Tag Roots" child row (Blueprint-created variables). */
	FTagToolboxTagRootsChildRow RootsRow;
};
