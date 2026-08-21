// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxResaveService.h"

#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "TagToolboxResaveService"

int32 FTagToolboxResavePlan::CountIncluded() const
{
	int32 Count = 0;
	for (const FTagToolboxResavePlanEntry& Entry : Entries)
	{
		Count += Entry.bIncluded ? 1 : 0;
	}
	return Count;
}

TArray<FName> FTagToolboxResavePlan::IncludedPackageNames() const
{
	TArray<FName> Names;
	for (const FTagToolboxResavePlanEntry& Entry : Entries)
	{
		if (Entry.bIncluded)
		{
			Names.Add(Entry.PackageName);
		}
	}
	return Names;
}

FTagToolboxResavePlan FTagToolboxResaveService::BuildPlan(const TArray<FTagToolboxPackageFacts>& Facts)
{
	FTagToolboxResavePlan Plan;
	for (const FTagToolboxPackageFacts& Fact : Facts)
	{
		FTagToolboxResavePlanEntry& Entry = Plan.Entries.AddDefaulted_GetRef();
		Entry.PackageName = Fact.PackageName;

		if (Fact.bReadOnlyOnDisk && !Fact.bSourceControlActive)
		{
			// Nothing can check it out; surfacing it beats failing mid-batch.
			Entry.Disposition = ETagToolboxPackageDisposition::ReadOnlyNoProvider;
			Entry.bIncluded = false;
		}
		else if (Fact.bLoaded && Fact.bDirty)
		{
			Entry.Disposition = ETagToolboxPackageDisposition::LoadedDirty;
			Entry.bIncluded = false; // explicit opt-in only
		}
		else if (Fact.bLoaded)
		{
			Entry.Disposition = ETagToolboxPackageDisposition::LoadedClean;
		}
		else
		{
			Entry.Disposition = ETagToolboxPackageDisposition::NotLoaded;
		}
	}
	return Plan;
}

void FTagToolboxResaveService::ApplyDirtyConsent(FTagToolboxResavePlan& Plan, const TSet<FName>& OptedInDirtyPackages)
{
	for (FTagToolboxResavePlanEntry& Entry : Plan.Entries)
	{
		if (Entry.Disposition == ETagToolboxPackageDisposition::LoadedDirty && OptedInDirtyPackages.Contains(Entry.PackageName))
		{
			Entry.bIncluded = true;
		}
	}
}

FTagToolboxResaveReport FTagToolboxResaveService::BuildReport(
	const FTagToolboxResavePlan& Plan,
	const TSet<FName>& SavedPackages,
	const TSet<FName>& FailedPackages)
{
	FTagToolboxResaveReport Report;
	for (const FTagToolboxResavePlanEntry& Entry : Plan.Entries)
	{
		if (!Entry.bIncluded)
		{
			Report.Skipped.Add(Entry.PackageName);
		}
		else if (SavedPackages.Contains(Entry.PackageName))
		{
			Report.Saved.Add(Entry.PackageName);
		}
		else if (FailedPackages.Contains(Entry.PackageName))
		{
			Report.Failed.Add(Entry.PackageName);
		}
		else
		{
			Report.Unattempted.Add(Entry.PackageName);
		}
	}
	Report.Saved.Sort(FNameLexicalLess());
	Report.Failed.Sort(FNameLexicalLess());
	Report.Unattempted.Sort(FNameLexicalLess());
	Report.Skipped.Sort(FNameLexicalLess());
	return Report;
}

TArray<FTagToolboxPackageFacts> FTagToolboxResaveService::GatherFacts(const TArray<FName>& PackageNames)
{
	const bool bSourceControlActive = ISourceControlModule::Get().IsEnabled();

	TArray<FTagToolboxPackageFacts> Facts;
	for (const FName& PackageName : PackageNames)
	{
		FTagToolboxPackageFacts& Fact = Facts.AddDefaulted_GetRef();
		Fact.PackageName = PackageName;
		Fact.bSourceControlActive = bSourceControlActive;

		if (const UPackage* Package = FindPackage(nullptr, *PackageName.ToString()))
		{
			Fact.bLoaded = true;
			Fact.bDirty = Package->IsDirty();
		}

		FString FileName;
		if (FPackageName::DoesPackageExist(PackageName.ToString(), &FileName))
		{
			Fact.bReadOnlyOnDisk = IFileManager::Get().IsReadOnly(*FileName);
		}
	}
	return Facts;
}

FTagToolboxResaveReport FTagToolboxResaveService::ExecutePlan(const FTagToolboxResavePlan& Plan)
{
	TArray<UPackage*> DirtyToSaveFirst;
	TArray<UPackage*> LoadedToReload;
	TArray<FName> ToLoad;

	for (const FTagToolboxResavePlanEntry& Entry : Plan.Entries)
	{
		if (!Entry.bIncluded)
		{
			continue;
		}
		switch (Entry.Disposition)
		{
		case ETagToolboxPackageDisposition::LoadedDirty:
			if (UPackage* Package = FindPackage(nullptr, *Entry.PackageName.ToString()))
			{
				DirtyToSaveFirst.Add(Package);
				LoadedToReload.Add(Package);
			}
			break;
		case ETagToolboxPackageDisposition::LoadedClean:
			if (UPackage* Package = FindPackage(nullptr, *Entry.PackageName.ToString()))
			{
				LoadedToReload.Add(Package);
			}
			break;
		default:
			ToLoad.Add(Entry.PackageName);
			break;
		}
	}

	// Every save in the batch is attributed through the save event — the API's
	// OutFailedPackages alone cannot name unattempted packages after a
	// mid-batch Cancel (U2/KTD).
	TSet<FName> SavedPackages;
	const FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
		[&SavedPackages](const FString& /*FileName*/, UPackage* Package, FObjectPostSaveContext /*Context*/)
		{
			if (Package)
			{
				SavedPackages.Add(Package->GetFName());
			}
		});

	TSet<FName> FailedPackages;
	{
		FScopedSlowTask SlowTask(4.0f, LOCTEXT("ResaveTask", "Tag Toolbox: resaving referencing packages..."));
		SlowTask.MakeDialog(/*bShowCancelButton=*/false);

		// 1) Opted-in dirty packages save FIRST: the user's edits persist, and
		//    the old tag names they serialize stay covered by the redirect.
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("ResaveDirtyFirst", "Saving opted-in unsaved edits..."));
		if (DirtyToSaveFirst.Num() > 0)
		{
			TArray<UPackage*> FailedDirty;
			FEditorFileUtils::FPromptForCheckoutAndSaveParams SaveParams;
			SaveParams.bCheckDirty = false;
			SaveParams.bPromptToSave = false;
			SaveParams.OutFailedPackages = &FailedDirty;
			FEditorFileUtils::PromptForCheckoutAndSave(DirtyToSaveFirst, SaveParams);

			// A dirty package whose save-first did NOT complete (failure, or
			// the engine dialog's mid-batch Cancel) must never be reloaded —
			// a non-interactive positive reload would silently discard the
			// user's edits. Ground truth is the save event, not the failed
			// list: Cancel leaves unattempted packages out of BOTH.
			for (const UPackage* Failed : FailedDirty)
			{
				if (Failed)
				{
					FailedPackages.Add(Failed->GetFName());
				}
			}
			for (int32 Index = LoadedToReload.Num() - 1; Index >= 0; --Index)
			{
				const UPackage* Package = LoadedToReload[Index];
				if (DirtyToSaveFirst.Contains(Package) && (!Package || !SavedPackages.Contains(Package->GetFName())))
				{
					if (Package)
					{
						FailedPackages.Add(Package->GetFName());
					}
					LoadedToReload.RemoveAt(Index);
				}
			}
		}

		// 2) Reload every loaded target non-interactively so in-memory tag
		//    names re-resolve through the redirects — no engine modal here.
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("ResaveReload", "Reloading referencers..."));
		if (LoadedToReload.Num() > 0)
		{
			FText ReloadError;
			const bool bReloaded = UPackageTools::ReloadPackages(LoadedToReload, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
			if (!bReloaded)
			{
				// A failed reload means these packages may still hold the OLD
				// tag names in memory; saving them would write the old names
				// back while the report claims they were fixed. Attribute the
				// whole reload batch as Failed rather than lie.
				for (const UPackage* Package : LoadedToReload)
				{
					if (Package)
					{
						FailedPackages.Add(Package->GetFName());
					}
				}
			}
		}

		// 3) Load the rest.
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("ResaveLoad", "Loading referencers..."));
		TArray<UPackage*> AllToSave;
		for (const FTagToolboxResavePlanEntry& Entry : Plan.Entries)
		{
			if (!Entry.bIncluded || FailedPackages.Contains(Entry.PackageName))
			{
				continue;
			}
			// Reload replaced loaded package objects; re-resolve everything.
			UPackage* Package = FindPackage(nullptr, *Entry.PackageName.ToString());
			if (!Package)
			{
				Package = LoadPackage(nullptr, *Entry.PackageName.ToString(), LOAD_None);
			}
			if (Package)
			{
				AllToSave.Add(Package);
			}
			else
			{
				FailedPackages.Add(Entry.PackageName);
			}
		}

		// 4) One checkout-and-save batch. The engine's per-package
		//    Cancel/Retry/Continue dialog can appear on failure in interactive
		//    sessions; a mid-batch Cancel leaves the remainder unattempted.
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("ResaveSave", "Saving referencers..."));

		// Reset saved tracking for the final attribution UNCONDITIONALLY: the
		// dirty-first saves wrote the OLD names and must never count as
		// fixed-up saves, even when nothing survives to the final batch.
		SavedPackages.Reset();

		if (AllToSave.Num() > 0)
		{
			TArray<UPackage*> FailedSaves;
			FEditorFileUtils::FPromptForCheckoutAndSaveParams SaveParams;
			SaveParams.bCheckDirty = false;
			SaveParams.bPromptToSave = false;
			SaveParams.OutFailedPackages = &FailedSaves;
			FEditorFileUtils::PromptForCheckoutAndSave(AllToSave, SaveParams);
			for (const UPackage* Failed : FailedSaves)
			{
				if (Failed)
				{
					FailedPackages.Add(Failed->GetFName());
				}
			}
		}
	}

	UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);

	return BuildReport(Plan, SavedPackages, FailedPackages);
}

#undef LOCTEXT_NAMESPACE
