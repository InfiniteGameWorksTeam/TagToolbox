// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "GameplayTagContainer.h"

class UBlueprint;
class FTransaction;

/**
 * Undo/redo for the plugin's two non-property writes.
 *
 * Variable tag filters: the engine's SetBlueprintVariableMetaData never calls
 * Modify() (so even the stock 5.7+ Roots row is not undoable). The helper
 * wraps the write in one transaction that Modify()s the Blueprint (and the
 * function-entry node for locals). Undo then restores the STORED metadata,
 * but the compiled property the pickers read stays stamped until the skeleton
 * recompiles, so the undo client re-marks the Blueprint structurally modified
 * on undo/redo of exactly these transactions and the filter visibly reverts.
 *
 * Tag styles: the settings CDO is made RF_Transactional so Modify() records
 * the styles array; undo restores memory, and the client re-persists the
 * default config so disk never diverges from what the editor shows.
 */
namespace TagToolboxUndo
{
	/** Transaction contexts the undo client keys on. */
	inline const TCHAR* VariableFilterContext = TEXT("TagToolbox.VariableFilter");
	inline const TCHAR* TagStyleContext = TEXT("TagToolbox.TagStyle");

	/**
	 * Sets (non-empty) or removes (empty) a Blueprint variable's Categories
	 * metadata inside one undoable transaction. LocalScope is the owning
	 * function for a local variable, null for a member.
	 */
	void SetVariableCategories(UBlueprint* Blueprint, FName VarName, const UStruct* LocalScope, const FString& CategoriesOrEmpty);

	/** Transactional style writes (each its own undo step unless an interactive edit is open). */
	void SetTagStyleTransacted(const FGameplayTag& InTag, const FLinearColor& Color);
	void ClearTagStyleTransacted(const FGameplayTag& InTag);

	/**
	 * Interactive color-pick session: one transaction spanning drag begin/end so
	 * a slider drag is a single undo step instead of one per mouse move.
	 */
	void BeginInteractiveStyleEdit(const FGameplayTag& InTag);
	void EndInteractiveStyleEdit();
	bool IsInteractiveStyleEditActive();

	/** Pure: does this transaction context belong to the plugin, and which kind. */
	bool IsVariableFilterTransaction(const FString& Context);
	bool IsTagStyleTransaction(const FString& Context);
}

class FTagToolboxUndoClient : public FEditorUndoClient
{
public:
	void Register();
	void Unregister();

	//~ FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:
	void HandleTransaction(const FTransaction* Transaction);
	bool bRegistered = false;
};
