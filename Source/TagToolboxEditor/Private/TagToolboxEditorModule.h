// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor module: registers the colored tag pill (editor-wide FGameplayTag
 * customization), the Blueprint variable Tag Filter rows, and the Tag Browser /
 * Tag Audit nomad tabs.
 *
 * Registration timing matters: GameplayTagsEditor registers its own
 * "GameplayTag" customization inside the OnPostEngineInit broadcast, and
 * identifier-less RegisterCustomPropertyTypeLayout is silently last-wins. The
 * pill therefore registers one tick past OnPostEngineInit; on hot reload /
 * late load (engine already initialized) it registers immediately.
 */
class FTagToolboxEditorModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandlePostEngineInit();
	void PerformDeferredStartup();

	void RegisterTagPillCustomization();
	void UnregisterTagPillCustomization();
	/** True when Paper2DPlus already owns the editor-wide tag chip. */
	static bool ShouldYieldPillToPaper2DPlus();

	void RegisterVariableCustomizations();
	void UnregisterVariableCustomizations();

	void RegisterTabs();
	void UnregisterTabs();

	void RegisterCommentTintFactory();
	void UnregisterCommentTintFactory();

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle VariableCustomizationHandle;
	FDelegateHandle LocalVariableCustomizationHandle;
	TSharedPtr<class FTagToolboxCommentTintFactory> CommentTintFactory;
	bool bRegisteredTagPillCustomization = false;
	bool bRegisteredTagContainerCustomization = false;
	bool bRegisteredTabs = false;
	bool bDeferredStartupDone = false;
};
