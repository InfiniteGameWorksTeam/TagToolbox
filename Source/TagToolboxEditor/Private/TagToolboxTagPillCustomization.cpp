// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxTagPillCustomization.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailChildrenBuilder.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "SGameplayTagContainerCombo.h"
#include "STagToolboxMenuHostedPickerGuard.h"
#include "STagToolboxTagPicker.h"
#include "Styling/AppStyle.h"
#include "TagToolboxColorBridge.h"
#include "TagToolboxNotifications.h"
#include "TagToolboxSettings.h"
#include "TagToolboxTagClipboard.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TagToolboxTagPillCustomization"

namespace TagToolboxPillInternal
{
	// Refusals surface as notifications, never silence — and they name the
	// actual cause so the user knows what to fix.
	static void ShowRefusal(const FText& Message)
	{
		TagToolboxNotifications::Show(Message, 4.0f);
	}
}

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
	.CopyAction(FUIAction(
		FExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::HandleCopyTag),
		FCanExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::CanCopyTag)))
	.PasteAction(FUIAction(
		FExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::HandlePasteTag),
		FCanExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::CanPasteTag)))
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
			.OnMouseButtonUp(this, &FTagToolboxTagPillCustomization::HandlePillMouseButtonUp)
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

	const FString Filter = GetResolvedFilter();

	// The dropdown is Tag Toolbox's own picker: colored rows, favorites, and
	// one-click recents, all honoring the resolved Categories filter. The
	// create row's Offer mode gates on the property's editability.
	return SNew(SBox)
		.MinDesiredWidth(300.0f)
		[
			SNew(STagToolboxMenuHostedPickerGuard)
			[
				SNew(STagToolboxTagPicker)
				.Filter(Filter)
				.MenuHosted(true)
				.MaxHeight(380.0f)
				.CanCreateTags(!StructPropertyHandle->IsEditConst())
				.CurrentTag(TAttribute<FGameplayTag>::CreateSP(this, &FTagToolboxTagPillCustomization::GetCurrentTag))
				.OnTagSelected(STagToolboxTagPicker::FOnTagSelected::CreateSP(this, &FTagToolboxTagPillCustomization::HandlePickerTagSelected))
			]
		];
}

FString FTagToolboxTagPillCustomization::GetResolvedFilter() const
{
	// The manager helper walks parent handles, owner functions, and tag-typed
	// struct fields; the bare GetMetaData("Categories") silently unfilters
	// pickers whose filter comes from a parent handle or function parameter.
	if (StructPropertyHandle.IsValid() && StructPropertyHandle->IsValidHandle())
	{
		return UGameplayTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);
	}
	return FString();
}

void FTagToolboxTagPillCustomization::HandleCopyTag() const
{
	FPlatformApplicationMisc::ClipboardCopy(*FTagToolboxTagClipboard::ExportTagString(GetCurrentTag()));
}

bool FTagToolboxTagPillCustomization::CanCopyTag() const
{
	return GetCurrentTag().IsValid();
}

void FTagToolboxTagPillCustomization::HandlePasteTag()
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	FGameplayTag ParsedTag;
	switch (FTagToolboxTagClipboard::ClassifyClipboardText(PastedText, ParsedTag))
	{
	case ETagToolboxClipboardContent::Empty:
		TagToolboxPillInternal::ShowRefusal(LOCTEXT("PasteEmpty", "Paste failed: the clipboard is empty."));
		return;

	case ETagToolboxClipboardContent::Container:
		TagToolboxPillInternal::ShowRefusal(LOCTEXT("PasteContainer", "Paste failed: the clipboard holds a tag CONTAINER, and this property takes a single tag."));
		return;

	case ETagToolboxClipboardContent::Invalid:
		TagToolboxPillInternal::ShowRefusal(FText::Format(LOCTEXT("PasteInvalid", "Paste failed: '{0}' is not a registered gameplay tag."),
			FText::AsCultureInvariant(PastedText.TrimStartAndEnd().Left(64))));
		return;

	case ETagToolboxClipboardContent::SingleTag:
	{
		const FString Filter = GetResolvedFilter();
		if (!FTagToolboxTagClipboard::NameMatchesFilter(ParsedTag.ToString(), Filter))
		{
			// Mirrors the create policy: a value the property can never hold
			// is refused with the reason, not silently written.
			TagToolboxPillInternal::ShowRefusal(FText::Format(LOCTEXT("PasteOutsideFilter", "Paste failed: '{0}' is outside this property's filter ({1})."),
				FText::AsCultureInvariant(ParsedTag.ToString()), FText::AsCultureInvariant(Filter)));
			return;
		}
		// The one commit funnel: engine-identical transaction + write shape,
		// multi-object edits included.
		HandlePickerTagSelected(ParsedTag);
		return;
	}
	}
}

bool FTagToolboxTagPillCustomization::CanPasteTag() const
{
	if (!StructPropertyHandle.IsValid() || !StructPropertyHandle->IsValidHandle() || StructPropertyHandle->IsEditConst())
	{
		return false;
	}

	// Probe silently — no notifications, no logging (the row menu calls this
	// every time it opens).
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	FGameplayTag ParsedTag;
	if (FTagToolboxTagClipboard::ClassifyClipboardText(PastedText, ParsedTag) != ETagToolboxClipboardContent::SingleTag)
	{
		return false;
	}
	return FTagToolboxTagClipboard::NameMatchesFilter(ParsedTag.ToString(), GetResolvedFilter());
}

void FTagToolboxTagPillCustomization::HandleClearTag()
{
	HandlePickerTagSelected(FGameplayTag());
}

FReply FTagToolboxTagPillCustomization::HandlePillMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}

	// The pill lives in a details row (not an auto-dismissing menu), so
	// pushing a context menu here is safe. Mirrors the engine chip's RMB menu.
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, /*InCommandList=*/nullptr);
	MenuBuilder.AddMenuEntry(
		NSLOCTEXT("PropertyView", "CopyProperty", "Copy"),
		LOCTEXT("CopyTagTooltip", "Copy the tag as a plain string."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::HandleCopyTag),
			FCanExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::CanCopyTag)));
	MenuBuilder.AddMenuEntry(
		NSLOCTEXT("PropertyView", "PasteProperty", "Paste"),
		LOCTEXT("PasteTagTooltip", "Paste a tag from the clipboard (plain name or export text)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Paste"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::HandlePasteTag),
			FCanExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::CanPasteTag)));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ClearTag", "Clear Gameplay Tag"),
		FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::HandleClearTag),
			FCanExecuteAction::CreateSP(this, &FTagToolboxTagPillCustomization::CanCopyTag)));

	const FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(ComboButton.ToSharedRef(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	return FReply::Handled();
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
