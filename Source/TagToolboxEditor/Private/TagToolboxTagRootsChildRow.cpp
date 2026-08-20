// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxTagRootsChildRow.h"

#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameplayTagsManager.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "SGameplayTagContainerCombo.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TagToolboxTagRootsChildRow"

void FTagToolboxTagRootsChildRow::AddIfBlueprintVariable(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;

	FProperty* Property = InPropertyHandle->GetProperty();
	if (!Property || Property->IsNative())
	{
		return;
	}

	if (const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(InPropertyHandle->GetOuterBaseClass()))
	{
		CreatorBP = Cast<UBlueprint>(GeneratedClass->ClassGeneratedBy);
		if (!CreatorBP.IsValid() || !FBlueprintEditorUtils::IsVariableCreatedByBlueprint(CreatorBP.Get(), Property))
		{
			return;
		}
	}

	MetaDataCategories.Reset();
	const FString CategoriesString = UGameplayTagsManager::Get().GetCategoriesMetaFromPropertyHandle(InPropertyHandle);
	if (!CategoriesString.IsEmpty())
	{
		TArray<FString> CategoriesStringList;
		if (CategoriesString.ParseIntoArray(CategoriesStringList, TEXT(","), true) > 0)
		{
			UGameplayTagsManager::Get().RequestGameplayTagContainer(CategoriesStringList, MetaDataCategories, /*bErrorIfNotFound=*/false);
		}
	}

	const FText RootsToolTip = LOCTEXT("TagRootsToolTip", "Selects the allowed root tags this variable's picker is filtered to.");
	ChildBuilder.AddCustomRow(LOCTEXT("TagRootsFilterString", "Gameplay Tag Roots"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("TagRootsLabel", "Gameplay Tag Roots"))
		.ToolTipText(RootsToolTip)
		.Font(CustomizationUtils.GetRegularFont())
	]
	.ValueContent()
	.VAlign(VAlign_Center)
	[
		SNew(SGameplayTagContainerCombo)
		.TagContainer_Lambda([this]() { return MetaDataCategories; })
		.ToolTipText(RootsToolTip)
		.OnTagContainerChanged_Lambda([this](const FGameplayTagContainer& NewTagContainer)
		{
			MetaDataCategories = NewTagContainer;
		})
		.OnTagCleared_Lambda([this]() { OnCategoriesCleared(); })
		.OnTagContainerComboClosed_Lambda([this](const FGameplayTagContainer& NewTagContainer) { OnCategoriesComboClosed(NewTagContainer); })
	];
}

void FTagToolboxTagRootsChildRow::OnCategoriesComboClosed(const FGameplayTagContainer& NewTagContainer)
{
	MetaDataCategories = NewTagContainer;
	FString ContainerString = MetaDataCategories.ToStringSimple();
	ContainerString.ReplaceInline(TEXT(" "), TEXT(""));

	FProperty* Property = PropertyHandle.IsValid() ? PropertyHandle->GetProperty() : nullptr;
	if (!Property)
	{
		return;
	}

	if (UBlueprint* CreatorBPPtr = CreatorBP.Get())
	{
		if (!MetaDataCategories.IsEmpty())
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(CreatorBPPtr, Property->GetFName(), Property->GetOwner<UFunction>(), TEXT("Categories"), ContainerString);
		}
		else
		{
			FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(CreatorBPPtr, Property->GetFName(), Property->GetOwner<UFunction>(), TEXT("Categories"));
		}
	}
	else
	{
		if (!MetaDataCategories.IsEmpty())
		{
			Property->SetMetaData(TEXT("Categories"), *ContainerString);
		}
		else
		{
			Property->RemoveMetaData(TEXT("Categories"));
		}
	}
}

void FTagToolboxTagRootsChildRow::OnCategoriesCleared()
{
	OnCategoriesComboClosed(MetaDataCategories);
}

#undef LOCTEXT_NAMESPACE
