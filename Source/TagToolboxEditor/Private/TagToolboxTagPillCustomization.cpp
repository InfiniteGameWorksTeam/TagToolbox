// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxTagPillCustomization.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameplayTagsManager.h"
#include "IDetailChildrenBuilder.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "SGameplayTagContainerCombo.h"
#include "STagToolboxMenuHostedPickerGuard.h"
#include "STagToolboxTagPicker.h"
#include "Styling/AppStyle.h"
#include "TagToolboxColorBridge.h"
#include "TagToolboxSettings.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TagToolboxTagPillCustomization"

TSharedRef<IPropertyTypeCustomization> FTagToolboxTagPillCustomization::MakeInstance()
{
	return MakeShared<FTagToolboxTagPillCustomization>();
}

void FTagToolboxTagPillCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructPropertyHandle = InStructPropertyHandle;

	// One shared rounded brush; each pill tints it through BorderBackgroundColor.
	static const FSlateRoundedBoxBrush PillBrush(FLinearColor::White, 3.0f);

	HeaderRow
	.FilterString(FText::AsCultureInvariant(GetCurrentTag().ToString()))
	.NameContent()
	[
		InStructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.0f)
	[
		SAssignNew(ComboButton, SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.ContentPadding(FMargin(2.0f, 0.0f))
		.HasDownArrow(true)
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &FTagToolboxTagPillCustomization::BuildPickerMenuContent))
		.ButtonContent()
		[
			SNew(SBorder)
			.BorderImage(&PillBrush)
			.BorderBackgroundColor(TAttribute<FSlateColor>::CreateSP(this, &FTagToolboxTagPillCustomization::GetPillColor))
			.Padding(FMargin(6.0f, 2.0f))
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				.Text(TAttribute<FText>::CreateSP(this, &FTagToolboxTagPillCustomization::GetPillText))
				.ColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &FTagToolboxTagPillCustomization::GetPillTextColor))
			]
		]
	];
}

void FTagToolboxTagPillCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	RootsRow.AddIfBlueprintVariable(InStructPropertyHandle, ChildBuilder, StructCustomizationUtils);
}

FGameplayTag FTagToolboxTagPillCustomization::GetCurrentTag() const
{
	FGameplayTag Result;
	if (StructPropertyHandle.IsValid() && StructPropertyHandle->IsValidHandle())
	{
		// Multi-edit shows the first object's value, matching the engine chip.
		StructPropertyHandle->EnumerateConstRawData([&Result](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (RawData)
			{
				Result = *static_cast<const FGameplayTag*>(RawData);
			}
			return false;
		});
	}
	return Result;
}

FSlateColor FTagToolboxTagPillCustomization::GetPillColor() const
{
	const FGameplayTag Tag = GetCurrentTag();
	FLinearColor Color;
	if (TagToolboxColorBridge::ResolveTagColor(Tag, Color))
	{
		return Color;
	}
	// Unset or unstyled tags keep a neutral dark chip, never a color.
	return FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
}

FSlateColor FTagToolboxTagPillCustomization::GetPillTextColor() const
{
	const FSlateColor Background = GetPillColor();
	const float Luminance = Background.GetSpecifiedColor().GetLuminance();
	return Luminance > 0.5f ? FLinearColor(0.02f, 0.02f, 0.02f) : FLinearColor(0.95f, 0.95f, 0.95f);
}

FText FTagToolboxTagPillCustomization::GetPillText() const
{
	const FGameplayTag Tag = GetCurrentTag();
	return Tag.IsValid() ? FText::AsCultureInvariant(Tag.ToString()) : LOCTEXT("PillNone", "None");
}

TSharedRef<SWidget> FTagToolboxTagPillCustomization::BuildPickerMenuContent()
{
	if (!StructPropertyHandle.IsValid() || !StructPropertyHandle->IsValidHandle())
	{
		return SNullWidget::NullWidget;
	}

	// The manager helper walks parent handles, owner functions, and tag-typed
	// struct fields; the bare GetMetaData("Categories") silently unfilters
	// pickers whose filter comes from a parent handle or function parameter.
	const FString Filter = UGameplayTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);

	// The dropdown is Tag Toolbox's own picker: colored rows, favorites, and
	// one-click recents, all honoring the resolved Categories filter.
	return SNew(SBox)
		.MinDesiredWidth(300.0f)
		[
			SNew(STagToolboxMenuHostedPickerGuard)
			[
				SNew(STagToolboxTagPicker)
				.Filter(Filter)
				.MenuHosted(true)
				.MaxHeight(380.0f)
				.CurrentTag(TAttribute<FGameplayTag>::CreateSP(this, &FTagToolboxTagPillCustomization::GetCurrentTag))
				.OnTagSelected(STagToolboxTagPicker::FOnTagSelected::CreateSP(this, &FTagToolboxTagPillCustomization::HandlePickerTagSelected))
			]
		];
}

void FTagToolboxTagPillCustomization::HandlePickerTagSelected(const FGameplayTag& NewTag)
{
	if (StructPropertyHandle.IsValid() && StructPropertyHandle->IsValidHandle())
	{
		// Mirrors the engine picker's single-tag commit exactly: one transaction,
		// one formatted-string write ("None" clears through normal import).
		FScopedTransaction Transaction(LOCTEXT("SetTagTransaction", "Select Gameplay Tag"));
		const FString FormattedString = FString::Printf(TEXT("(TagName=\"%s\")"), *NewTag.GetTagName().ToString());
		StructPropertyHandle->SetValueFromFormattedString(FormattedString);
	}

	if (const TSharedPtr<SComboButton> Combo = ComboButton)
	{
		Combo->SetIsOpen(false);
	}
}

#undef LOCTEXT_NAMESPACE
