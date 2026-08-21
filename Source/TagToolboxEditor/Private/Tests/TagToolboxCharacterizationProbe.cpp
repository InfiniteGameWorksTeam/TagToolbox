// Copyright Infinite Game Works. All Rights Reserved.

// SCRATCH U2 characterization probes. Runs ONLY under the "TagDebtProbe"
// automation filter (deliberately outside the "TagToolbox" prefix so the
// normal suite never mutates config). Mutates throwaway tag sources plus one
// probe asset, restores what it touched, and logs greppable
// "TAGDEBTPROBE|" findings. Deleted once findings land in architecture.md.

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "TagToolboxProbeTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Unity-build rule: file-unique helper prefix TagDebtProbe_.

	static const TCHAR* TagDebtProbe_SourceIni = TEXT("TagDebtProbe.ini");
	static const TCHAR* TagDebtProbe_PackagePath = TEXT("/Game/TagDebtProbe/ProbeAsset");

	FString TagDebtProbe_Log(FAutomationTestBase& Test, const FString& Line)
	{
		const FString Full = FString::Printf(TEXT("TAGDEBTPROBE|%s"), *Line);
		Test.AddInfo(Full);
		return Full;
	}

	TArray<FName> TagDebtProbe_GetSearchableTagDeps(const FName PackageName)
	{
		TArray<FName> Result;
		TArray<FAssetIdentifier> Dependencies;
		IAssetRegistry::GetChecked().GetDependencies(FAssetIdentifier(PackageName), Dependencies, UE::AssetRegistry::EDependencyCategory::SearchableName);
		for (const FAssetIdentifier& Dependency : Dependencies)
		{
			if (Dependency.PackageName == FName(TEXT("/Script/GameplayTags")) && Dependency.ObjectName == FName(TEXT("GameplayTag")) && !Dependency.ValueName.IsNone())
			{
				Result.Add(Dependency.ValueName);
			}
		}
		Result.Sort(FNameLexicalLess());
		return Result;
	}

	void TagDebtProbe_Rescan()
	{
		IAssetRegistry::GetChecked().ScanPathsSynchronous({ TEXT("/Game/TagDebtProbe") }, /*bForceRescan=*/true);
	}

	bool TagDebtProbe_SaveProbeAsset(UPackage* Package, UObject* Asset)
	{
		const FString FileName = FPackageName::LongPackageNameToFilename(TagDebtProbe_PackagePath, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), /*Tree=*/true);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		return UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
	}

	int32 TagDebtProbe_CountRedirects(const UGameplayTagsList* List, FName OldName)
	{
		int32 Count = 0;
		if (List)
		{
			for (const FGameplayTagRedirect& Redirect : List->GameplayTagRedirects)
			{
				if (Redirect.OldTagName == OldName)
				{
					++Count;
				}
			}
		}
		return Count;
	}

	FString TagDebtProbe_DescribeTag(FName Requested)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Requested, /*ErrorIfNotFound=*/false);
		if (!Tag.IsValid())
		{
			return FString::Printf(TEXT("%s -> INVALID"), *Requested.ToString());
		}
		return FString::Printf(TEXT("%s -> %s%s"), *Requested.ToString(), *Tag.GetTagName().ToString(),
			(Tag.GetTagName() == Requested) ? TEXT(" (direct)") : TEXT(" (REDIRECTED)"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagDebtProbeCharacterizationTest,
	"TagDebtProbe.Characterization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagDebtProbeCharacterizationTest::RunTest(const FString& Parameters)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	IGameplayTagsEditorModule& TagsEditor = IGameplayTagsEditorModule::Get();
	UGameplayTagsSettings* Settings = GetMutableDefault<UGameplayTagsSettings>();

	if (!Manager.ShouldImportTagsFromINI())
	{
		TagDebtProbe_Log(*this, TEXT("SKIP: ShouldImportTagsFromINI is false in this host."));
		return true;
	}

	// ---- Pre-run snapshots for cleanup.
	const FString SettingsIniPath = Settings->GetDefaultConfigFilename();
	TArray<uint8> OriginalSettingsBytes;
	FFileHelper::LoadFileToArray(OriginalSettingsBytes, *SettingsIniPath);
	const FString ProbeContentDir = FPaths::ProjectContentDir() / TEXT("TagDebtProbe");

	// =====================================================================
	// P1 (confirm): AddNewGameplayTagToINI duplicate + creation
	// =====================================================================
	{
		const bool bFirstAdd = TagsEditor.AddNewGameplayTagToINI(TEXT("TagDebtProbe.Alpha"), TEXT(""), FName(TagDebtProbe_SourceIni));
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P1|first add Alpha -> %d"), bFirstAdd ? 1 : 0));
		const bool bDuplicateAdd = TagsEditor.AddNewGameplayTagToINI(TEXT("TagDebtProbe.Alpha"), TEXT(""), FName(TagDebtProbe_SourceIni));
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P1|duplicate add Alpha -> %d (expected 0, notification 'already exists')"), bDuplicateAdd ? 1 : 0));
	}

	const FGameplayTagSource* ProbeSource = Manager.FindTagSource(FName(TagDebtProbe_SourceIni));
	UGameplayTagsList* ProbeList = ProbeSource ? ProbeSource->SourceTagList.Get() : nullptr;
	const FString ProbeIniPath = ProbeList ? ProbeList->ConfigFileName : FString();
	TagDebtProbe_Log(*this, FString::Printf(TEXT("P1|probe source list %s at '%s'"), ProbeList ? TEXT("EXISTS") : TEXT("MISSING"), *ProbeIniPath));
	if (!ProbeList)
	{
		AddError(TEXT("Probe tag source was not created; aborting."));
		return false;
	}

	// =====================================================================
	// P4/P7: referencer save/rename/reload + query searchable names
	// =====================================================================
	{
		TagsEditor.AddNewGameplayTagToINI(TEXT("TagDebtProbe.Refd"), TEXT(""), FName(TagDebtProbe_SourceIni));
		TagsEditor.AddNewGameplayTagToINI(TEXT("TagDebtProbe.ContainerOnly"), TEXT(""), FName(TagDebtProbe_SourceIni));
		TagsEditor.AddNewGameplayTagToINI(TEXT("TagDebtProbe.QueryOnly"), TEXT(""), FName(TagDebtProbe_SourceIni));

		UPackage* Package = CreatePackage(TagDebtProbe_PackagePath);
		UTagToolboxProbeAsset* Asset = NewObject<UTagToolboxProbeAsset>(Package, TEXT("ProbeAsset"), RF_Public | RF_Standalone);
		Asset->ProbeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.Refd")));
		Asset->ProbeContainer.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.ContainerOnly"))));
		FGameplayTagQueryExpression QueryExpression;
		QueryExpression.AllTagsMatch().AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.QueryOnly"))));
		Asset->ProbeQuery = FGameplayTagQuery::BuildQuery(QueryExpression);

		const bool bSaved = TagDebtProbe_SaveProbeAsset(Package, Asset);
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|initial save -> %d"), bSaved ? 1 : 0));
		TagDebtProbe_Rescan();

		{
			const TArray<FName> Deps = TagDebtProbe_GetSearchableTagDeps(FName(TagDebtProbe_PackagePath));
			FString Joined;
			for (const FName& Dep : Deps) { Joined += Dep.ToString() + TEXT(";"); }
			TagDebtProbe_Log(*this, FString::Printf(TEXT("P7|searchable deps after save: %s (QueryOnly present answers the FGameplayTagQuery question)"), *Joined));
		}

		// Rename while the referencer package is LOADED.
		const bool bRenamed = TagsEditor.RenameTagInINI(TEXT("TagDebtProbe.Refd"), TEXT("TagDebtProbe.RefdNew"), /*bRenameChildren=*/true);
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|rename Refd->RefdNew -> %d"), bRenamed ? 1 : 0));
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|in-memory ProbeTag after rename: %s"), *Asset->ProbeTag.GetTagName().ToString()));

		// Resave WITHOUT reload.
		TagDebtProbe_SaveProbeAsset(Package, Asset);
		TagDebtProbe_Rescan();
		{
			const TArray<FName> Deps = TagDebtProbe_GetSearchableTagDeps(FName(TagDebtProbe_PackagePath));
			FString Joined;
			for (const FName& Dep : Deps) { Joined += Dep.ToString() + TEXT(";"); }
			TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|deps after resave WITHOUT reload: %s"), *Joined));
		}

		// Reload (non-interactive), then resave.
		{
			TArray<UPackage*> ToReload = { Package };
			FText ReloadError;
			const bool bReloaded = UPackageTools::ReloadPackages(ToReload, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
			TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|clean reload (AssumePositive) -> %d err='%s'"), bReloaded ? 1 : 0, *ReloadError.ToString()));
		}
		UPackage* ReloadedPackage = FindPackage(nullptr, TagDebtProbe_PackagePath);
		UTagToolboxProbeAsset* ReloadedAsset = ReloadedPackage ? FindObject<UTagToolboxProbeAsset>(ReloadedPackage, TEXT("ProbeAsset")) : nullptr;
		if (!ReloadedAsset)
		{
			TagDebtProbe_Log(*this, TEXT("P4|reloaded asset NOT FOUND after reload"));
		}
		else
		{
			TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|in-memory ProbeTag after reload: %s"), *ReloadedAsset->ProbeTag.GetTagName().ToString()));
			TagDebtProbe_SaveProbeAsset(ReloadedPackage, ReloadedAsset);
			TagDebtProbe_Rescan();
			const TArray<FName> Deps = TagDebtProbe_GetSearchableTagDeps(FName(TagDebtProbe_PackagePath));
			FString Joined;
			for (const FName& Dep : Deps) { Joined += Dep.ToString() + TEXT(";"); }
			TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|deps after reload+resave: %s"), *Joined));

			// Dirty-package reload characterization.
			ReloadedAsset->Modify();
			ReloadedAsset->ProbeContainer.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.Alpha"))));
			ReloadedPackage->MarkPackageDirty();

			{
				TArray<UPackage*> ToReload = { ReloadedPackage };
				FText ReloadError;
				const bool bNegative = UPackageTools::ReloadPackages(ToReload, ReloadError, EReloadPackagesInteractionMode::AssumeNegative);
				UPackage* AfterNeg = FindPackage(nullptr, TagDebtProbe_PackagePath);
				UTagToolboxProbeAsset* AfterNegAsset = AfterNeg ? FindObject<UTagToolboxProbeAsset>(AfterNeg, TEXT("ProbeAsset")) : nullptr;
				TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|dirty reload AssumeNegative -> %d, still dirty=%d, edit intact=%d"),
					bNegative ? 1 : 0,
					(AfterNeg && AfterNeg->IsDirty()) ? 1 : 0,
					(AfterNegAsset && AfterNegAsset->ProbeContainer.HasTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.Alpha")), false))) ? 1 : 0));
			}
			{
				UPackage* StillPackage = FindPackage(nullptr, TagDebtProbe_PackagePath);
				TArray<UPackage*> ToReload = { StillPackage };
				FText ReloadError;
				const bool bPositive = UPackageTools::ReloadPackages(ToReload, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
				UPackage* AfterPos = FindPackage(nullptr, TagDebtProbe_PackagePath);
				UTagToolboxProbeAsset* AfterPosAsset = AfterPos ? FindObject<UTagToolboxProbeAsset>(AfterPos, TEXT("ProbeAsset")) : nullptr;
				TagDebtProbe_Log(*this, FString::Printf(TEXT("P4|dirty reload AssumePositive -> %d, still dirty=%d, edit survived=%d (0 = unsaved edits DISCARDED)"),
					bPositive ? 1 : 0,
					(AfterPos && AfterPos->IsDirty()) ? 1 : 0,
					(AfterPosAsset && AfterPosAsset->ProbeContainer.HasTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.Alpha")), false))) ? 1 : 0));
			}
		}
	}

	// =====================================================================
	// P5 (confirm): re-run rename duplicates the redirect into settings
	// =====================================================================
	{
		const FName OldName(TEXT("TagDebtProbe.Refd"));
		const int32 ProbeListBefore = TagDebtProbe_CountRedirects(ProbeList, OldName);
		const int32 SettingsBefore = TagDebtProbe_CountRedirects(Settings, OldName);
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P5|redirect rows before re-run: probeList=%d settings=%d"), ProbeListBefore, SettingsBefore));

		const bool bRerun = TagsEditor.RenameTagInINI(TEXT("TagDebtProbe.Refd"), TEXT("TagDebtProbe.RefdNew"), true);
		const int32 ProbeListAfter = TagDebtProbe_CountRedirects(ProbeList, OldName);
		const int32 SettingsAfter = TagDebtProbe_CountRedirects(Settings, OldName);
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P5|re-run rename -> %d; redirect rows after: probeList=%d settings=%d (settings>0 = DUPLICATE confirmed)"), bRerun ? 1 : 0, ProbeListAfter, SettingsAfter));
	}

	// =====================================================================
	// P6: rollback probes — loose-list case and settings case
	// =====================================================================
	{
		// Loose-list case: redirect lands in the probe ini.
		TagsEditor.AddNewGameplayTagToINI(TEXT("TagDebtProbe.Roll"), TEXT(""), FName(TagDebtProbe_SourceIni));
		TArray<uint8> ProbeIniSnapshot;
		FFileHelper::LoadFileToArray(ProbeIniSnapshot, *ProbeIniPath);
		TArray<uint8> SettingsSnapshot;
		FFileHelper::LoadFileToArray(SettingsSnapshot, *SettingsIniPath);

		TagsEditor.RenameTagInINI(TEXT("TagDebtProbe.Roll"), TEXT("TagDebtProbe.Rolled"), true);
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P6|loose pre-rollback: %s"), *TagDebtProbe_DescribeTag(FName(TEXT("TagDebtProbe.Roll")))));

		// Stage (a): restore bytes + tree refresh only.
		FFileHelper::SaveArrayToFile(ProbeIniSnapshot, *ProbeIniPath);
		FFileHelper::SaveArrayToFile(SettingsSnapshot, *SettingsIniPath);
		Manager.EditorRefreshGameplayTagTree();
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P6|loose after restore+refresh only: %s ; Rolled defined=%d"),
			*TagDebtProbe_DescribeTag(FName(TEXT("TagDebtProbe.Roll"))),
			FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.Rolled")), false).IsValid() ? 1 : 0));

		// Settings case: a tag whose source is DefaultGameplayTags.ini.
		TagsEditor.AddNewGameplayTagToINI(TEXT("TagDebtProbe.SettingsRoll"), TEXT(""), NAME_None);
		TArray<uint8> SettingsSnapshot2;
		FFileHelper::LoadFileToArray(SettingsSnapshot2, *SettingsIniPath);
		TArray<uint8> ProbeIniSnapshot2;
		FFileHelper::LoadFileToArray(ProbeIniSnapshot2, *ProbeIniPath);

		TagsEditor.RenameTagInINI(TEXT("TagDebtProbe.SettingsRoll"), TEXT("TagDebtProbe.SettingsRolled"), true);
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P6|settings redirect rows for SettingsRoll: settings=%d probeList=%d"),
			TagDebtProbe_CountRedirects(Settings, FName(TEXT("TagDebtProbe.SettingsRoll"))),
			TagDebtProbe_CountRedirects(ProbeList, FName(TEXT("TagDebtProbe.SettingsRoll")))));

		// Stage (a): restore + refresh only.
		FFileHelper::SaveArrayToFile(SettingsSnapshot2, *SettingsIniPath);
		FFileHelper::SaveArrayToFile(ProbeIniSnapshot2, *ProbeIniPath);
		Manager.EditorRefreshGameplayTagTree();
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P6|settings after restore+refresh only: %s (REDIRECTED = settings CDO not reloaded from disk)"),
			*TagDebtProbe_DescribeTag(FName(TEXT("TagDebtProbe.SettingsRoll")))));

		// Stage (b): + GConfig->LoadFile + ReloadConfig on the settings CDO.
		GConfig->LoadFile(SettingsIniPath);
		Settings->ReloadConfig();
		Manager.EditorRefreshGameplayTagTree();
		TagDebtProbe_Log(*this, FString::Printf(TEXT("P6|settings after LoadFile+ReloadConfig+refresh: %s ; SettingsRolled defined=%d"),
			*TagDebtProbe_DescribeTag(FName(TEXT("TagDebtProbe.SettingsRoll"))),
			FGameplayTag::RequestGameplayTag(FName(TEXT("TagDebtProbe.SettingsRolled")), false).IsValid() ? 1 : 0));
	}

	// =====================================================================
	// Cleanup: restore settings ini, delete the probe tag ini + content.
	// =====================================================================
	{
		FFileHelper::SaveArrayToFile(OriginalSettingsBytes, *SettingsIniPath);
		GConfig->LoadFile(SettingsIniPath);
		Settings->ReloadConfig();

		if (!ProbeIniPath.IsEmpty())
		{
			IFileManager::Get().Delete(*ProbeIniPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
		}
		IFileManager::Get().DeleteDirectory(*ProbeContentDir, /*RequireExists=*/false, /*Tree=*/true);
		Manager.EditorRefreshGameplayTagTree();
		TagDebtProbe_Log(*this, TEXT("CLEANUP|settings ini restored, probe ini + content deleted"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
