// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxVariableFilterCustomization.h"
#include "TagToolboxUndo.h"

#include "BlueprintEditorModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "GameplayTagsManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SGameplayTagContainerCombo.h"
#include "TagToolboxSettings.h"
#include "UObject/PropertyWrapper.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TagToolboxVariableFilterCustomization"

namespace TagToolboxVariableFilter
{
	static bool IsTagStructProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty
			&& (StructProperty->Struct == FGameplayTag::StaticStruct()
				|| StructProperty->Struct == FGameplayTagContainer::StaticStruct());
	}
}

TSharedPtr<IDetailCustomization> FTagToolboxVariableFilterCustomization::MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	// Returning nullptr opts this editor out (the RigVM registration pattern).
	const TArray<UObject*>* Objects = InBlueprintEditor.IsValid() ? InBlueprintEditor->GetObjectsCurrentlyBeingEdited() : nullptr;
	if (Objects && Objects->Num() == 1)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>((*Objects)[0]))
		{
			return MakeShared<FTagToolboxVariableFilterCustomization>(TWeakObjectPtr<UBlueprint>(Blueprint));
		}
	}
	return nullptr;
}

bool FTagToolboxVariableFilterCustomization::IsTagFlavoredVariable(const FProperty* Property)
{
	using namespace TagToolboxVariableFilter;

	if (!Property)
	{
		return false;
	}
	if (IsTagStructProperty(Property))
	{
		return true;
	}
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return IsTagStructProperty(ArrayProperty->Inner);
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		return IsTagStructProperty(SetProperty->ElementProp);
	}
	if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		return IsTagStructProperty(MapProperty->KeyProp) || IsTagStructProperty(MapProperty->ValueProp);
	}
	return false;
}

void FTagToolboxVariableFilterCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	if (!GetDefault<UTagToolboxSettings>()->bEnableVariableTagFilters)
	{
		return;
	}

	UBlueprint* BlueprintPtr = Blueprint.Get();
	if (!BlueprintPtr)
	{
		return;
	}

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	for (const TWeakObjectPtr<UObject>& Object : ObjectsBeingCustomized)
	{
		UPropertyWrapper* PropertyWrapper = Cast<UPropertyWrapper>(Object.Get());
		FProperty* Property = PropertyWrapper ? PropertyWrapper->GetProperty() : nullptr;
		if (!Property || Property->IsNative() || !IsTagFlavoredVariable(Property))
		{
			continue;
		}

		// Local variables carry their scope function as owner; member variables
		// must actually be declared by this Blueprint (inherited/native/SCS
		// variables have no FBPVariableDescription for the metadata to land on).
		UFunction* LocalVarScope = Property->GetOwner<UFunction>();
		if (!LocalVarScope && !FBlueprintEditorUtils::IsVariableCreatedByBlueprint(BlueprintPtr, Property))
		{
			continue;
		}

		const FName VarName = Property->GetFName();

		// Seed the row from the stored variable metadata (the compiled property
		// can lag one compile behind what the designer just authored).
		TSharedRef<FGameplayTagContainer> CurrentFilter = MakeShared<FGameplayTagContainer>();
		FString ExistingCategories;
		if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(BlueprintPtr, VarName, LocalVarScope, TEXT("Categories"), ExistingCategories) && !ExistingCategories.IsEmpty())
		{
			TArray<FString> CategoriesList;
			if (ExistingCategories.ParseIntoArray(CategoriesList, TEXT(","), true) > 0)
			{
				UGameplayTagsManager::Get().RequestGameplayTagContainer(CategoriesList, *CurrentFilter, /*bErrorIfNotFound=*/false);
			}
		}

		TWeakObjectPtr<UBlueprint> WeakBlueprint = Blueprint;
		TWeakObjectPtr<UFunction> WeakScope = LocalVarScope;
		auto CommitFilter = [WeakBlueprint, WeakScope, VarName, CurrentFilter](const FGameplayTagContainer& NewFilter)
		{
			*CurrentFilter = NewFilter;

			UBlueprint* CommitBlueprint = WeakBlueprint.Get();
			if (!CommitBlueprint)
			{
				return;
			}

			FString ContainerString = CurrentFilter->ToStringSimple();
			ContainerString.ReplaceInline(TEXT(" "), TEXT(""));

			// Undoable (the engine setter alone records nothing).
			TagToolboxUndo::SetVariableCategories(CommitBlueprint, VarName, WeakScope.Get(),
				CurrentFilter->IsEmpty() ? FString() : ContainerString);
		};

		const FText FilterToolTip = LOCTEXT("TagFilterToolTip",
			"Restricts this variable's tag picker to the chosen root tags (writes the same 'Categories' metadata C++ uses). Filters guide the picker only — Blueprint logic can still assign any tag.");

		DetailLayout.EditCategory("Variable")
		.AddCustomRow(LOCTEXT("TagFilterFilterString", "Tag Filter"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(DetailLayout.GetDetailFont())
			.Text(LOCTEXT("TagFilterLabel", "Tag Filter"))
			.ToolTipText(FilterToolTip)
		]
		.ValueContent()
		.VAlign(VAlign_Center)
		[
			SNew(SGameplayTagContainerCombo)
			.ToolTipText(FilterToolTip)
			.TagContainer_Lambda([CurrentFilter]() { return *CurrentFilter; })
			.OnTagContainerChanged_Lambda([CurrentFilter](const FGameplayTagContainer& NewFilter)
			{
				*CurrentFilter = NewFilter;
			})
			.OnTagContainerComboClosed_Lambda(CommitFilter)
			.OnTagCleared_Lambda([CommitFilter, CurrentFilter]()
			{
				CommitFilter(*CurrentFilter);
			})
		];
	}
}

#undef LOCTEXT_NAMESPACE
