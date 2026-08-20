// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxSettings.h"

UTagToolboxSettings::UTagToolboxSettings()
{
}

bool UTagToolboxSettings::ResolveTagColorFromArray(const TArray<FTagToolboxTagStyle>& Styles, const FGameplayTag& Tag, FLinearColor& OutColor)
{
	// Exact match first, then walk up the ancestor chain; first entry wins at each level.
	for (FGameplayTag Current = Tag; Current.IsValid(); Current = Current.RequestDirectParent())
	{
		for (const FTagToolboxTagStyle& Style : Styles)
		{
			if (Style.Tag == Current)
			{
				OutColor = Style.Color;
				return true;
			}
		}
	}
	return false;
}

bool UTagToolboxSettings::ResolveTagColor(const FGameplayTag& Tag, FLinearColor& OutColor) const
{
	return ResolveTagColorFromArray(TagStyles, Tag, OutColor);
}

void UTagToolboxSettings::SetTagColor(const FGameplayTag& Tag, const FLinearColor& Color)
{
	if (!Tag.IsValid())
	{
		return;
	}

	FTagToolboxTagStyle* Existing = TagStyles.FindByPredicate([&Tag](const FTagToolboxTagStyle& Style)
	{
		return Style.Tag == Tag;
	});
	if (Existing)
	{
		Existing->Color = Color;
	}
	else
	{
		FTagToolboxTagStyle& NewStyle = TagStyles.AddDefaulted_GetRef();
		NewStyle.Tag = Tag;
		NewStyle.Color = Color;
	}

#if WITH_EDITOR
	TryUpdateDefaultConfigFile();
#endif
	OnTagStylesChanged.Broadcast();
}

void UTagToolboxSettings::ClearTagColor(const FGameplayTag& Tag)
{
	const int32 Removed = TagStyles.RemoveAll([&Tag](const FTagToolboxTagStyle& Style)
	{
		return Style.Tag == Tag;
	});
	if (Removed == 0)
	{
		return;
	}

#if WITH_EDITOR
	TryUpdateDefaultConfigFile();
#endif
	OnTagStylesChanged.Broadcast();
}

#if WITH_EDITOR
void UTagToolboxSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTagToolboxSettings, TagStyles)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UTagToolboxSettings, TagStyles))
	{
		OnTagStylesChanged.Broadcast();
	}
}
#endif
