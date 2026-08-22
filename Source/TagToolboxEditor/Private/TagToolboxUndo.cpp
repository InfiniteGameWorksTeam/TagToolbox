// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxUndo.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "TagToolboxSettings.h"

#define LOCTEXT_NAMESPACE "TagToolboxUndo"

namespace TagToolboxUndoInternal
{
	static TUniquePtr<FScopedTransaction> GInteractiveStyleTransaction;
}

bool TagToolboxUndo::IsVariableFilterTransaction(const FString& Context)
{
	return Context == VariableFilterContext;
}

bool TagToolboxUndo::IsTagStyleTransaction(const FString& Context)
{
	return Context == TagStyleContext;
}

void TagToolboxUndo::SetVariableCategories(UBlueprint* Blueprint, FName VarName, const UStruct* LocalScope, const FString& CategoriesOrEmpty)
{
	if (!Blueprint)
	{
		return;
	}

	FScopedTransaction Transaction(VariableFilterContext,
		CategoriesOrEmpty.IsEmpty() ? LOCTEXT("ClearTagFilter", "Clear Tag Filter") : LOCTEXT("SetTagFilter", "Set Tag Filter"),
		Blueprint);

	// The engine setter mutates NewVariables (members) or the function-entry
	// node's LocalVariables (locals) without Modify(); snapshot both owners.
	Blueprint->Modify();
	if (LocalScope)
	{
		UK2Node_FunctionEntry* EntryNode = nullptr;
		FBlueprintEditorUtils::FindLocalVariable(Blueprint, LocalScope, VarName, &EntryNode);
		if (EntryNode)
		{
			EntryNode->Modify();
		}
	}

	if (CategoriesOrEmpty.IsEmpty())
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VarName, LocalScope, TEXT("Categories"));
	}
	else
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarName, LocalScope, TEXT("Categories"), CategoriesOrEmpty);
	}
}

void TagToolboxUndo::SetTagStyleTransacted(const FGameplayTag& InTag, const FLinearColor& Color)
{
	UTagToolboxSettings* Settings = GetMutableDefault<UTagToolboxSettings>();
	if (IsInteractiveStyleEditActive())
	{
		// Already inside the drag-spanning transaction opened by BeginInteractiveStyleEdit.
		Settings->SetTagColor(InTag, Color);
		return;
	}
	FScopedTransaction Transaction(TagStyleContext, LOCTEXT("SetTagColor", "Set Tag Color"), Settings);
	Settings->Modify();
	Settings->SetTagColor(InTag, Color);
}

void TagToolboxUndo::ClearTagStyleTransacted(const FGameplayTag& InTag)
{
	UTagToolboxSettings* Settings = GetMutableDefault<UTagToolboxSettings>();
	FScopedTransaction Transaction(TagStyleContext, LOCTEXT("ClearTagColor", "Clear Tag Color"), Settings);
	Settings->Modify();
	Settings->ClearTagColor(InTag);
}

void TagToolboxUndo::BeginInteractiveStyleEdit(const FGameplayTag& InTag)
{
	if (!TagToolboxUndoInternal::GInteractiveStyleTransaction)
	{
		UTagToolboxSettings* Settings = GetMutableDefault<UTagToolboxSettings>();
		TagToolboxUndoInternal::GInteractiveStyleTransaction = MakeUnique<FScopedTransaction>(TagStyleContext,
			FText::Format(LOCTEXT("EditTagColor", "Edit Color of {0}"), FText::FromName(InTag.GetTagName())), Settings);
		Settings->Modify();
	}
}

void TagToolboxUndo::EndInteractiveStyleEdit()
{
	TagToolboxUndoInternal::GInteractiveStyleTransaction.Reset();
}

bool TagToolboxUndo::IsInteractiveStyleEditActive()
{
	return TagToolboxUndoInternal::GInteractiveStyleTransaction.IsValid();
}

void FTagToolboxUndoClient::Register()
{
	if (!bRegistered && GEditor)
	{
		GEditor->RegisterForUndo(this);
		// Config CDOs are not transactional by default; the styles array must
		// be recordable for SetTagColor/ClearTagColor undo.
		GetMutableDefault<UTagToolboxSettings>()->SetFlags(RF_Transactional);
		bRegistered = true;
	}
}

void FTagToolboxUndoClient::Unregister()
{
	TagToolboxUndo::EndInteractiveStyleEdit();
	if (bRegistered && GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	bRegistered = false;
}

void FTagToolboxUndoClient::PostUndo(bool bSuccess)
{
	if (bSuccess && GEditor && GEditor->Trans)
	{
		// The transaction that was just undone (mirrors FBlueprintEditor::PostUndo).
		HandleTransaction(GEditor->Trans->GetTransaction(GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount()));
	}
}

void FTagToolboxUndoClient::PostRedo(bool bSuccess)
{
	if (bSuccess && GEditor && GEditor->Trans)
	{
		HandleTransaction(GEditor->Trans->GetTransaction(GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount() - 1));
	}
}

void FTagToolboxUndoClient::HandleTransaction(const FTransaction* Transaction)
{
	if (!Transaction)
	{
		return;
	}
	const FString Context = Transaction->GetContext().Context;

	if (TagToolboxUndo::IsVariableFilterTransaction(Context))
	{
		// Stored metadata reverted; re-stamp the compiled properties the
		// pickers actually read by regenerating the skeleton.
		TArray<UObject*> Objects;
		Transaction->GetTransactionObjects(Objects);
		for (UObject* Object : Objects)
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
		}
	}
	else if (TagToolboxUndo::IsTagStyleTransaction(Context))
	{
		// Memory reverted; keep the default config and every listener in sync.
		UTagToolboxSettings* Settings = GetMutableDefault<UTagToolboxSettings>();
		Settings->TryUpdateDefaultConfigFile();
		Settings->OnTagStylesChanged.Broadcast();
	}
}

#undef LOCTEXT_NAMESPACE
