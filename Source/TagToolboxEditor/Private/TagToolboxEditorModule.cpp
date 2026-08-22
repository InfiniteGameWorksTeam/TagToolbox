// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxEditorModule.h"
#include "TagToolboxUndo.h"

#include "BlueprintEditorModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "STagToolboxAuditPanel.h"
#include "STagToolboxTagPicker.h"
#include "Styling/AppStyle.h"
#include "TagToolboxCommentTint.h"
#include "TagToolboxEditorLog.h"
#include "TagToolboxSettings.h"
#include "TagToolboxTagContainerPillCustomization.h"
#include "TagToolboxTagPillCustomization.h"
#include "TagToolboxTagScanService.h"
#include "TagToolboxVariableFilterCustomization.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "TagToolboxEditorModule"

DEFINE_LOG_CATEGORY(LogTagToolbox);

namespace TagToolboxTabs
{
	static const FName TagBrowserTabId(TEXT("TagToolboxTagBrowser"));
	static const FName TagAuditTabId(TEXT("TagToolboxTagAudit"));

	static TSharedRef<SDockTab> SpawnTagBrowserTab(const FSpawnTabArgs& /*Args*/)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(STagToolboxTagPicker)
			];
	}

	static TSharedRef<SDockTab> SpawnTagAuditTab(const FSpawnTabArgs& /*Args*/)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(STagToolboxAuditPanel)
			];
	}
}

// Console seams for scripted/ECA verification (Live Coding resets file-scope
// statics; reopen the editor after header-touching patches).
static FAutoConsoleCommand GTagToolboxOpenTagBrowserCommand(
	TEXT("TagToolbox.OpenTagBrowser"),
	TEXT("Opens the Tag Toolbox Tag Browser tab."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(TagToolboxTabs::TagBrowserTabId);
	}));

static FAutoConsoleCommand GTagToolboxOpenTagAuditCommand(
	TEXT("TagToolbox.OpenTagAudit"),
	TEXT("Opens the Tag Toolbox Tag Audit tab."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(TagToolboxTabs::TagAuditTabId);
	}));

void FTagToolboxEditorModule::StartupModule()
{
	if (GEngine)
	{
		// Hot reload / late plugin load: the engine (and GameplayTagsEditor's
		// own customization) is already up, so registering now sticks.
		PerformDeferredStartup();
		RegisterTagPillCustomization();
	}
	else
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FTagToolboxEditorModule::HandlePostEngineInit);
#else
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FTagToolboxEditorModule::HandlePostEngineInit);
#endif
	}
}

void FTagToolboxEditorModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
#else
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
#endif
		PostEngineInitHandle.Reset();
	}

	UnregisterVariableCustomizations();
	UnregisterTagPillCustomization();
	UnregisterCommentTintFactory();
	UnregisterTabs();
	if (UndoClient)
	{
		UndoClient->Unregister();
		UndoClient.Reset();
	}
	FTagToolboxTagScanService::Shutdown();
}

void FTagToolboxEditorModule::HandlePostEngineInit()
{
	PerformDeferredStartup();

	// GameplayTagsEditor registers its "GameplayTag" customization synchronously
	// inside this same broadcast, and identifier-less registration is last-wins.
	// Deferring one tick lands ours strictly after the engine's regardless of
	// handler order within the broadcast.
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateRaw(this, &FTagToolboxEditorModule::RegisterTagPillCustomization));
	}
	else
	{
		RegisterTagPillCustomization();
	}
}

void FTagToolboxEditorModule::PerformDeferredStartup()
{
	if (bDeferredStartupDone)
	{
		return;
	}
	bDeferredStartupDone = true;

	UndoClient = MakeUnique<FTagToolboxUndoClient>();
	UndoClient->Register();
	RegisterVariableCustomizations();
	RegisterCommentTintFactory();
	RegisterTabs();
}

void FTagToolboxEditorModule::RegisterCommentTintFactory()
{
	if (!CommentTintFactory.IsValid())
	{
		CommentTintFactory = MakeShared<FTagToolboxCommentTintFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(CommentTintFactory);
	}
}

void FTagToolboxEditorModule::UnregisterCommentTintFactory()
{
	if (CommentTintFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(CommentTintFactory);
		CommentTintFactory.Reset();
	}
}

void FTagToolboxEditorModule::RegisterTagPillCustomization()
{
	if (!GetDefault<UTagToolboxSettings>()->bColorizeGameplayTagPickers)
	{
		return;
	}
	if (bRegisteredTagPillCustomization && bRegisteredTagContainerCustomization)
	{
		return;
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	bool bRegisteredAnything = false;

	// The single-tag pill contends with Paper2DPlus's own customization
	// (identifier-less registration is silently last-wins), so it yields.
	if (!bRegisteredTagPillCustomization)
	{
		if (ShouldYieldPillToPaper2DPlus())
		{
			UE_LOG(LogTagToolbox, Display, TEXT("Tag Toolbox: Paper2DPlus already owns the editor-wide GameplayTag chip; yielding. Disable its Colorize Gameplay Tag Pickers setting to let Tag Toolbox take over."));
		}
		else
		{
			PropertyModule.RegisterCustomPropertyTypeLayout(
				TEXT("GameplayTag"),
				FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FTagToolboxTagPillCustomization::MakeInstance));
			bRegisteredTagPillCustomization = true;
			bRegisteredAnything = true;
		}
	}

	// Containers have no Paper2DPlus counterpart — no yield probe needed.
	if (!bRegisteredTagContainerCustomization)
	{
		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("GameplayTagContainer"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FTagToolboxTagContainerPillCustomization::MakeInstance));
		bRegisteredTagContainerCustomization = true;
		bRegisteredAnything = true;
	}

	if (bRegisteredAnything)
	{
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

void FTagToolboxEditorModule::UnregisterTagPillCustomization()
{
	const bool bHadPill = bRegisteredTagPillCustomization;
	const bool bHadContainer = bRegisteredTagContainerCustomization;
	bRegisteredTagPillCustomization = false;
	bRegisteredTagContainerCustomization = false;

	if ((bHadPill || bHadContainer) && FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		if (bHadPill)
		{
			PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("GameplayTag"));
		}
		if (bHadContainer)
		{
			PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("GameplayTagContainer"));
		}
	}
}

bool FTagToolboxEditorModule::ShouldYieldPillToPaper2DPlus()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("Paper2DPlusEditor")))
	{
		return false;
	}

	// Read Paper2DPlus's own colorize switch reflectively — no module dependency.
	const UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/Paper2DPlus.Paper2DPlusSettings"));
	if (!SettingsClass)
	{
		return false;
	}
	const UObject* SettingsCDO = SettingsClass->GetDefaultObject();
	const FBoolProperty* ColorizeProperty = FindFProperty<FBoolProperty>(SettingsClass, TEXT("bColorizeGameplayTagPickers"));
	return SettingsCDO && ColorizeProperty && ColorizeProperty->GetPropertyValue_InContainer(SettingsCDO);
}

void FTagToolboxEditorModule::RegisterVariableCustomizations()
{
	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

	// FProperty (not FStructProperty): container variables surface as
	// FArrayProperty/FSetProperty/FMapProperty; the customization filters to
	// tag-flavored variables itself.
	VariableCustomizationHandle = BlueprintEditorModule.RegisterVariableCustomization(
		FProperty::StaticClass(),
		FOnGetVariableCustomizationInstance::CreateStatic(&FTagToolboxVariableFilterCustomization::MakeInstance));
	LocalVariableCustomizationHandle = BlueprintEditorModule.RegisterLocalVariableCustomization(
		FProperty::StaticClass(),
		FOnGetLocalVariableCustomizationInstance::CreateStatic(&FTagToolboxVariableFilterCustomization::MakeInstance));
}

void FTagToolboxEditorModule::UnregisterVariableCustomizations()
{
	if (FModuleManager::Get().IsModuleLoaded("Kismet"))
	{
		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
		if (VariableCustomizationHandle.IsValid())
		{
			BlueprintEditorModule.UnregisterVariableCustomization(FProperty::StaticClass(), VariableCustomizationHandle);
		}
		if (LocalVariableCustomizationHandle.IsValid())
		{
			BlueprintEditorModule.UnregisterLocalVariableCustomization(FProperty::StaticClass(), LocalVariableCustomizationHandle);
		}
	}
	VariableCustomizationHandle.Reset();
	LocalVariableCustomizationHandle.Reset();
}

void FTagToolboxEditorModule::RegisterTabs()
{
	if (bRegisteredTabs)
	{
		return;
	}
	bRegisteredTabs = true;

	const TSharedRef<FWorkspaceItem> ToolsCategory = WorkspaceMenu::GetMenuStructure().GetToolsCategory();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TagToolboxTabs::TagBrowserTabId, FOnSpawnTab::CreateStatic(&TagToolboxTabs::SpawnTagBrowserTab))
		.SetDisplayName(LOCTEXT("TagBrowserTabTitle", "Tag Browser"))
		.SetTooltipText(LOCTEXT("TagBrowserTabToolTip", "Browse the gameplay tag tree with colors, favorites, and recents (Tag Toolbox)."))
		.SetGroup(ToolsCategory)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TagToolboxTabs::TagAuditTabId, FOnSpawnTab::CreateStatic(&TagToolboxTabs::SpawnTagAuditTab))
		.SetDisplayName(LOCTEXT("TagAuditTabTitle", "Tag Audit"))
		.SetTooltipText(LOCTEXT("TagAuditTabToolTip", "Find unused, undefined, near-duplicate, and redirect-lingering gameplay tags without loading assets (Tag Toolbox)."))
		.SetGroup(ToolsCategory)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Warning"));
}

void FTagToolboxEditorModule::UnregisterTabs()
{
	if (!bRegisteredTabs)
	{
		return;
	}
	bRegisteredTabs = false;

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TagToolboxTabs::TagBrowserTabId);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TagToolboxTabs::TagAuditTabId);
	}
}

IMPLEMENT_MODULE(FTagToolboxEditorModule, TagToolboxEditor)

#undef LOCTEXT_NAMESPACE
