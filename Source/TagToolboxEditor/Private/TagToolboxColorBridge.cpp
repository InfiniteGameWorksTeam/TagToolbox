// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxColorBridge.h"

#include "GameplayTagContainer.h"
#include "TagToolboxSettings.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace TagToolboxColorBridge
{

static bool ResolvePaper2DPlusTagColor(const FGameplayTag& Tag, FLinearColor& OutColor)
{
	const UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/Paper2DPlus.Paper2DPlusSettings"));
	if (!SettingsClass)
	{
		return false;
	}
	const UObject* SettingsCDO = SettingsClass->GetDefaultObject();
	const FArrayProperty* TagColorsProperty = FindFProperty<FArrayProperty>(SettingsClass, TEXT("TagColors"));
	if (!SettingsCDO || !TagColorsProperty)
	{
		return false;
	}

	const FStructProperty* EntryStruct = CastField<FStructProperty>(TagColorsProperty->Inner);
	if (!EntryStruct)
	{
		return false;
	}
	const FStructProperty* EntryTagProperty = CastField<FStructProperty>(EntryStruct->Struct->FindPropertyByName(TEXT("Tag")));
	const FStructProperty* EntryColorProperty = CastField<FStructProperty>(EntryStruct->Struct->FindPropertyByName(TEXT("Color")));
	if (!EntryTagProperty || EntryTagProperty->Struct != FGameplayTag::StaticStruct()
		|| !EntryColorProperty || EntryColorProperty->Struct != TBaseStructure<FLinearColor>::Get())
	{
		return false;
	}

	FScriptArrayHelper ArrayHelper(TagColorsProperty, TagColorsProperty->ContainerPtrToValuePtr<void>(SettingsCDO));
	for (FGameplayTag Current = Tag; Current.IsValid(); Current = Current.RequestDirectParent())
	{
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			const void* Entry = ArrayHelper.GetRawPtr(Index);
			const FGameplayTag* EntryTag = EntryTagProperty->ContainerPtrToValuePtr<FGameplayTag>(Entry);
			if (EntryTag && *EntryTag == Current)
			{
				OutColor = *EntryColorProperty->ContainerPtrToValuePtr<FLinearColor>(Entry);
				return true;
			}
		}
	}
	return false;
}

bool ResolveTagColor(const FGameplayTag& Tag, FLinearColor& OutColor)
{
	if (!Tag.IsValid())
	{
		return false;
	}

	const UTagToolboxSettings* Settings = GetDefault<UTagToolboxSettings>();
	if (Settings->ResolveTagColor(Tag, OutColor))
	{
		return true;
	}
	if (!Settings->bUsePaper2DPlusColorsAsFallback)
	{
		return false;
	}
	return ResolvePaper2DPlusTagColor(Tag, OutColor);
}

} // namespace TagToolboxColorBridge
