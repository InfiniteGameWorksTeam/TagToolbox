// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxTagContainerPillCustomization.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "DetailWidgetRow.h"
#include "GameplayTagsManager.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "SGameplayTagPicker.h"
#include "STagToolboxMenuHostedPickerGuard.h"
#include "Styling/AppStyle.h"
#include "TagToolboxColorBridge.h"
#include "TagToolboxSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TagToolboxTagContainerPillCustomization"

namespace TagToolboxContainerPill
{
	static FLinearColor ChipColor(const FGameplayTag& Tag)
	{
		FLinearColor Color;
		if (TagToolboxColorBridge::ResolveTagColor(Tag, Color))
		{
			return Color;
		}
		return FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
	}

	static FLinearColor ChipTextColor(const FLinearColor& Background)
	{
		return Background.GetLuminance() > 0.5f ? FLinearColor(0.02f, 0.02f, 0.02f) : FLinearColor(0.95f, 0.95f, 0.95f);
	}
}

TSharedRef<IPropertyTypeCustomization> FTagToolboxTagContainerPillCustomization::MakeInstance()
{
	return MakeShared<FTagToolboxTagContainerPillCustomization>();
}

void FTagToolboxTagContainerPillCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructPropertyHandle = InStructPropertyHandle;

	// Keep the strip honest across picker edits, undo, and external changes.
	InStructPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FTagToolboxTagContainerPillCustomization::RequestRebuildChipStrip));

	HeaderRow
	.FilterString(FText::AsCultureInvariant(GetCurrentContainer().ToStringSimple()))
	.NameContent()
	[
		InStructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(240.0f)
	.MaxDesiredWidth(600.0f)
	[
		// The whole chip strip IS the dropdown trigger — click any tag (or the
		// empty row) to open the picker, matching the engine widget's feel.
		// Inner buttons (per-chip remove, the +N more/Show less toggle) consume
		// their own clicks, so only strip-body clicks reach the combo.
		SAssignNew(ComboButton, SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.ContentPadding(FMargin(0.0f))
		.HasDownArrow(true)
		.VAlign(VAlign_Center)
		.ToolTipText(LOCTEXT("EditContainerToolTip", "Click to add or remove tags."))
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &FTagToolboxTagContainerPillCustomization::BuildPickerMenuContent))
		.ButtonContent()
		[
			SAssignNew(ChipStrip, SWrapBox)
			.UseAllottedSize(true)
		]
	];

	RebuildChipStrip();
}

void FTagToolboxTagContainerPillCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	RootsRow.AddIfBlueprintVariable(InStructPropertyHandle, ChildBuilder, StructCustomizationUtils);
}

FGameplayTagContainer FTagToolboxTagContainerPillCustomization::GetCurrentContainer() const
{
	FGameplayTagContainer Result;
	if (StructPropertyHandle.IsValid() && StructPropertyHandle->IsValidHandle())
	{
		StructPropertyHandle->EnumerateConstRawData([&Result](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (RawData)
			{
				Result = *static_cast<const FGameplayTagContainer*>(RawData);
			}
			return false;
		});
	}
	return Result;
}

void FTagToolboxTagContainerPillCustomization::RequestRebuildChipStrip()
{
	if (!ChipStrip.IsValid())
	{
		return;
	}

	TWeakPtr<IPropertyTypeCustomization> WeakSelf = AsShared();
	ChipStrip->RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[WeakSelf](double /*CurrentTime*/, float /*DeltaTime*/) -> EActiveTimerReturnType
		{
			if (const TSharedPtr<IPropertyTypeCustomization> Self = WeakSelf.Pin())
			{
				StaticCastSharedPtr<FTagToolboxTagContainerPillCustomization>(Self)->RebuildChipStrip();
			}
			return EActiveTimerReturnType::Stop;
		}));
}

void FTagToolboxTagContainerPillCustomization::RebuildChipStrip()
{
	if (!ChipStrip.IsValid())
	{
		return;
	}

	ChipStrip->ClearChildren();

	const FGameplayTagContainer Container = GetCurrentContainer();
	if (Container.IsEmpty())
	{
		ChipStrip->AddSlot()
		.Padding(2.0f, 2.0f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(LOCTEXT("EmptyContainer", "Empty — click to add tags"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	// 0 = never collapse.
	const int32 MaxVisible = FMath::Max(0, GetDefault<UTagToolboxSettings>()->MaxVisibleTagChips);
	const bool bCollapsible = MaxVisible > 0 && Container.Num() > MaxVisible;
	const int32 NumToShow = (bCollapsible && !bChipsExpanded) ? MaxVisible : Container.Num();

	int32 Shown = 0;
	for (const FGameplayTag& Tag : Container)
	{
		if (Shown >= NumToShow)
		{
			break;
		}
		++Shown;
		ChipStrip->AddSlot()
		.Padding(0.0f, 1.0f, 4.0f, 1.0f)
		[
			MakeTagChip(Tag)
		];
	}

	if (bCollapsible)
	{
		if (!bChipsExpanded)
		{
			ChipStrip->AddSlot()
			.Padding(0.0f, 1.0f, 0.0f, 1.0f)
			[
				MakeToggleChip(
					FText::Format(LOCTEXT("MoreChipsFormat", "+{0} more"), FText::AsNumber(Container.Num() - NumToShow)),
					LOCTEXT("MoreChipsToolTip", "Show every tag in this container."))
			];
		}
		else
		{
			ChipStrip->AddSlot()
			.Padding(0.0f, 1.0f, 0.0f, 1.0f)
			[
				MakeToggleChip(
					LOCTEXT("ShowLess", "Show less"),
					LOCTEXT("ShowLessToolTip", "Collapse the tag list."))
			];
		}
	}
}

TSharedRef<SWidget> FTagToolboxTagContainerPillCustomization::MakeTagChip(const FGameplayTag& Tag)
{
	static const FSlateRoundedBoxBrush ChipBrush(FLinearColor::White, 3.0f);

	const FLinearColor Background = TagToolboxContainerPill::ChipColor(Tag);
	const FLinearColor TextColor = TagToolboxContainerPill::ChipTextColor(Background);

	return SNew(SBorder)
		.BorderImage(&ChipBrush)
		.BorderBackgroundColor(Background)
		.Padding(FMargin(6.0f, 2.0f, 2.0f, 2.0f))
		.ToolTipText(FText::Format(LOCTEXT("ChipToolTipFormat", "{0}\nClick to edit tags; ✕ removes this one."), FText::AsCultureInvariant(Tag.ToString())))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				.Text(FText::AsCultureInvariant(Tag.ToString()))
				.ColorAndOpacity(TextColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(2.0f, 0.0f))
				.ToolTipText(LOCTEXT("RemoveTagToolTip", "Remove this tag."))
				.OnClicked_Lambda([this, Tag]()
				{
					HandleRemoveTag(Tag);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.Text(FText::AsCultureInvariant(TEXT("✕")))
					.ColorAndOpacity(TextColor)
				]
			]
		];
}

TSharedRef<SWidget> FTagToolboxTagContainerPillCustomization::MakeToggleChip(const FText& Label, const FText& ToolTip)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(4.0f, 2.0f))
		.ToolTipText(ToolTip)
		.OnClicked_Lambda([this]()
		{
			bChipsExpanded = !bChipsExpanded;
			RequestRebuildChipStrip();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(Label)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

void FTagToolboxTagContainerPillCustomization::HandleRemoveTag(FGameplayTag TagToRemove)
{
	if (!StructPropertyHandle.IsValid() || !StructPropertyHandle->IsValidHandle())
	{
		return;
	}

	// Engine-mirrored removal: per-object container rewrite in one transaction.
	TArray<FGameplayTagContainer> Containers;
	if (SGameplayTagPicker::GetEditableTagContainersFromPropertyHandle(StructPropertyHandle.ToSharedRef(), Containers))
	{
		TArray<FString> NewValues;
		NewValues.Reserve(Containers.Num());
		for (FGameplayTagContainer& Container : Containers)
		{
			Container.RemoveTag(TagToRemove);
			NewValues.Add(Container.ToString());
		}

		FScopedTransaction Transaction(LOCTEXT("RemoveTagTransaction", "Remove Gameplay Tag"));
		StructPropertyHandle->SetPerObjectValues(NewValues);
	}

	// Deferred: this runs from the chip's own ✕ handler.
	RequestRebuildChipStrip();
}

TSharedRef<SWidget> FTagToolboxTagContainerPillCustomization::BuildPickerMenuContent()
{
	if (!StructPropertyHandle.IsValid() || !StructPropertyHandle->IsValidHandle())
	{
		return SNullWidget::NullWidget;
	}

	const FString Filter = UGameplayTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);

	// Multi-select editing stays on the engine picker (property-handle-bound,
	// menu stays open across toggles); the guard keeps its row context menus
	// from fatal-asserting inside this auto-dismissing host.
	return SNew(SBox)
		.MinDesiredWidth(300.0f)
		[
			SNew(STagToolboxMenuHostedPickerGuard)
			[
				SNew(SGameplayTagPicker)
				.Filter(Filter)
				.MultiSelect(true)
				.PropertyHandle(StructPropertyHandle)
				.OnTagChanged_Lambda([this](const TArray<FGameplayTagContainer>& /*TagContainers*/)
				{
					RequestRebuildChipStrip();
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
