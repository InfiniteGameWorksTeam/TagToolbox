// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxRenameFixup.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "ISourceControlModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "STagToolboxResaveDialog.h"
#include "SourceControlHelpers.h"
#include "TagToolboxNotifications.h"
#include "TagToolboxSettings.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "TagToolboxRenameFixup"

namespace TagToolboxRenameInternal
{
	static void Notify(const FText& Message)
	{
		TagToolboxNotifications::Show(Message);
	}

	/** The config path a list persists to (settings uses its default file). */
	static FString ResolveListConfigPath(UGameplayTagsList* List)
	{
		if (!List)
		{
			return FString();
		}
		if (List == GetMutableDefault<UGameplayTagsSettings>() || List->ConfigFileName.IsEmpty())
		{
			return List->GetDefaultConfigFilename();
		}
		return List->ConfigFileName;
	}

	/** Best-effort writability: checkout under a provider, clear RO without one. */
	static bool EnsureConfigWritable(const FString& ConfigPath, FText& OutError)
	{
		if (ConfigPath.IsEmpty())
		{
			OutError = LOCTEXT("NoConfigPath", "The tag list has no config file path.");
			return false;
		}
		if (!IFileManager::Get().FileExists(*ConfigPath))
		{
			return true; // Will be created by the write.
		}
		if (!IFileManager::Get().IsReadOnly(*ConfigPath))
		{
			return true;
		}
		if (ISourceControlModule::Get().IsEnabled())
		{
			if (USourceControlHelpers::CheckOutOrAddFile(ConfigPath))
			{
				return true;
			}
			OutError = FText::Format(LOCTEXT("CheckoutFailed", "Could not check out '{0}' from source control."), FText::AsCultureInvariant(ConfigPath));
			return false;
		}
		if (FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ConfigPath, false))
		{
			return true;
		}
		OutError = FText::Format(LOCTEXT("ReadOnlyConfig", "'{0}' is read-only and could not be made writable."), FText::AsCultureInvariant(ConfigPath));
		return false;
	}

	/** Persist one list, verifying the write actually happened. */
	static bool PersistListChecked(UGameplayTagsList* List, FText& OutError)
	{
		const FString ConfigPath = ResolveListConfigPath(List);
		if (!EnsureConfigWritable(ConfigPath, OutError))
		{
			return false;
		}
		bool bPersisted = false;
		if (List == GetMutableDefault<UGameplayTagsSettings>() || List->ConfigFileName.IsEmpty())
		{
			bPersisted = List->TryUpdateDefaultConfigFile();
		}
		else
		{
			bPersisted = List->TryUpdateDefaultConfigFile(List->ConfigFileName);
		}
		if (!bPersisted)
		{
			// The engine's own shape ignores this return — silently diverging
			// memory from disk so the change resurrects next session. We don't.
			OutError = FText::Format(LOCTEXT("PersistFailed", "Failed to write '{0}'."), FText::AsCultureInvariant(ConfigPath));
			return false;
		}
		GConfig->LoadFile(ConfigPath);
		return true;
	}
}

FName FTagToolboxRenameFixup::DeriveRenamedName(FName SubtreeOldName, FName OldName, FName NewName)
{
	if (SubtreeOldName == OldName)
	{
		return NewName;
	}
	const FString OldPrefix = OldName.ToString() + TEXT(".");
	const FString SubtreeString = SubtreeOldName.ToString();
	if (SubtreeString.StartsWith(OldPrefix, ESearchCase::IgnoreCase))
	{
		return FName(NewName.ToString() + TEXT(".") + SubtreeString.RightChop(OldPrefix.Len()));
	}
	return SubtreeOldName; // Not a subtree member — identity.
}

FTagToolboxRenamePlan FTagToolboxRenameFixup::BuildPlan(
	FName OldName,
	FName NewName,
	bool bNewNameValidTagString,
	bool bTagSourcesWritable,
	const TSet<FName>& TagTableSnapshot,
	const TArray<FTagToolboxRedirectRecord>& AllRedirects,
	const TMap<FName, FName>& SubtreeNameToSource,
	const TSet<FName>& UnwritableSourceNames,
	const TArray<FGameplayTag>& StyleTags,
	const TArray<FName>& Favorites,
	const TArray<FName>& Recents,
	int32 MergeTargetReferencerCount,
	int32 MergeTargetChildCount)
{
	FTagToolboxRenamePlan Plan;
	Plan.OldName = OldName;
	Plan.NewName = NewName;

	const FString OldString = OldName.ToString();
	const FString NewString = NewName.ToString();

	if (OldName.IsNone() || NewName.IsNone() || !bNewNameValidTagString)
	{
		Plan.Verdict = ETagToolboxRenameVerdict::RefusedInvalid;
		Plan.VerdictReason = LOCTEXT("InvalidNames", "The new tag name is not a valid tag string.");
		return Plan;
	}
	if (!bTagSourcesWritable)
	{
		Plan.Verdict = ETagToolboxRenameVerdict::RefusedInvalid;
		Plan.VerdictReason = LOCTEXT("IniImportOff", "This project does not import tags from ini files (ShouldImportTagsFromINI is false) — the engine rename cannot write anything.");
		return Plan;
	}
	if (OldString.Equals(NewString, ESearchCase::IgnoreCase))
	{
		Plan.Verdict = ETagToolboxRenameVerdict::RefusedInvalid;
		Plan.VerdictReason = LOCTEXT("SameName", "The new name is the same as the old name.");
		return Plan;
	}

	// Cycle detection before anything else: renaming into (or out of) the own
	// subtree explodes the engine's child recursion. Name the relationship.
	if (NewString.StartsWith(OldString + TEXT("."), ESearchCase::IgnoreCase))
	{
		Plan.Verdict = ETagToolboxRenameVerdict::RefusedCycle;
		Plan.VerdictReason = FText::Format(LOCTEXT("CycleDescendant", "'{0}' is a descendant of '{1}' — a tag cannot be renamed into its own subtree."),
			FText::AsCultureInvariant(NewString), FText::AsCultureInvariant(OldString));
		return Plan;
	}
	if (OldString.StartsWith(NewString + TEXT("."), ESearchCase::IgnoreCase))
	{
		Plan.Verdict = ETagToolboxRenameVerdict::RefusedCycle;
		Plan.VerdictReason = FText::Format(LOCTEXT("CycleAncestor", "'{0}' is an ancestor of '{1}' — a tag cannot be renamed onto its own ancestor."),
			FText::AsCultureInvariant(NewString), FText::AsCultureInvariant(OldString));
		return Plan;
	}

	// Renaming ONTO a name that currently redirects elsewhere destroys the
	// identity: the engine resolves the target THROUGH the surviving redirect
	// (so it never re-creates it), and collapsing the reverse chain would
	// write a self-redirect. Same rule Create Redirect enforces for targets.
	for (const FTagToolboxRedirectRecord& Redirect : AllRedirects)
	{
		if (Redirect.NewTagName == NewName && Redirect.OldTagName == OldName)
		{
			continue; // The recovery pair itself — handled below.
		}
		if (Redirect.OldTagName == NewName)
		{
			Plan.Verdict = ETagToolboxRenameVerdict::RefusedInvalid;
			Plan.VerdictReason = FText::Format(LOCTEXT("TargetIsRedirectOldName", "'{0}' is currently a redirected OLD tag name (it points at '{1}'). Retire or collapse that redirect first — renaming onto it would leave an unresolvable identity."),
				FText::AsCultureInvariant(NewString), FText::FromName(Redirect.NewTagName));
			return Plan;
		}
	}

	if (!TagTableSnapshot.Contains(OldName))
	{
		// Recovery entry: a prior partial run left the redirect in place. The
		// apply must skip the Rename state — a re-run engine rename would walk
		// redirected children into destructive X→X self-renames (U2/3).
		for (const FTagToolboxRedirectRecord& Redirect : AllRedirects)
		{
			if (Redirect.OldTagName == OldName && Redirect.NewTagName == NewName)
			{
				Plan.Verdict = ETagToolboxRenameVerdict::SkipToResave;
				Plan.VerdictReason = LOCTEXT("SkipToResave", "This name already redirects to the requested target (a previous rename did not finish its fix-up). Applying resumes at the referencer resave.");

				// Reconstruct the WHOLE subtree the original rename touched
				// from the redirect records it wrote — resuming only the
				// parent would leave child referencers unfixed and child
				// redirects unretired despite the "re-running resumes the
				// fix-up" promise.
				Plan.SubtreeOldNames.Add(OldName);
				Plan.OldToNewNames.Add(OldName, NewName);
				Plan.RetirementVerificationNames.Add(OldName);
				const FString ChildPrefix = OldString + TEXT(".");
				for (const FTagToolboxRedirectRecord& ChildRedirect : AllRedirects)
				{
					if (ChildRedirect.OldTagName.ToString().StartsWith(ChildPrefix, ESearchCase::IgnoreCase)
						&& ChildRedirect.NewTagName == DeriveRenamedName(ChildRedirect.OldTagName, OldName, NewName))
					{
						Plan.SubtreeOldNames.AddUnique(ChildRedirect.OldTagName);
						Plan.OldToNewNames.Add(ChildRedirect.OldTagName, ChildRedirect.NewTagName);
						Plan.RetirementVerificationNames.AddUnique(ChildRedirect.OldTagName);
					}
				}
				Plan.SubtreeOldNames.Sort(FNameLexicalLess());

				// Chain-reachable old names still verify before retirement,
				// and the resumed apply collapses them (same as a fresh run).
				for (const FTagToolboxRedirectRecord& Chain : AllRedirects)
				{
					if (Plan.OldToNewNames.Contains(Chain.NewTagName))
					{
						Plan.ChainsToCollapse.Add(Chain);
						if (Chain.OldTagName != Plan.OldToNewNames.FindRef(Chain.NewTagName))
						{
							Plan.RetirementVerificationNames.AddUnique(Chain.OldTagName);
						}
					}
				}
				return Plan;
			}
		}
		Plan.Verdict = ETagToolboxRenameVerdict::RefusedInvalid;
		Plan.VerdictReason = FText::Format(LOCTEXT("OldUndefined", "'{0}' is not a defined tag."), FText::AsCultureInvariant(OldString));
		return Plan;
	}

	// Subtree expansion runs on the PRE-rename snapshot only.
	Plan.SubtreeOldNames = FTagToolboxTagScanService::ExpandSubtreeNames(TagTableSnapshot, OldName);
	for (const FName& SubtreeName : Plan.SubtreeOldNames)
	{
		Plan.OldToNewNames.Add(SubtreeName, DeriveRenamedName(SubtreeName, OldName, NewName));
		Plan.RetirementVerificationNames.AddUnique(SubtreeName);
	}

	// Whole-subtree writability, or refusal naming the blockers: a partial
	// subtree would orphan excluded children behind the parent's redirect.
	for (const FName& SubtreeName : Plan.SubtreeOldNames)
	{
		if (const FName* Source = SubtreeNameToSource.Find(SubtreeName))
		{
			if (UnwritableSourceNames.Contains(*Source))
			{
				Plan.UnwritableSources.AddUnique(*Source);
			}
		}
	}
	if (Plan.UnwritableSources.Num() > 0)
	{
		Plan.Verdict = ETagToolboxRenameVerdict::RefusedUnwritableSource;
		TArray<FString> SourceStrings;
		for (const FName& Source : Plan.UnwritableSources)
		{
			SourceStrings.Add(Source.ToString());
		}
		Plan.VerdictReason = FText::Format(LOCTEXT("UnwritableSources", "Cannot rename: these tag sources are not writable: {0}. A partial rename would orphan the excluded children behind the parent's redirect."),
			FText::AsCultureInvariant(FString::Join(SourceStrings, TEXT(", "))));
		return Plan;
	}

	// Chains touching the subtree collapse to point at the renamed names —
	// never left chained (single-hop redirect resolution cannot follow A→B→C).
	// A chain whose old name IS the renamed form of its target would collapse
	// into a self-redirect: it is removed without replacement at apply time
	// and its (live) name never enters the retirement re-query.
	for (const FTagToolboxRedirectRecord& Redirect : AllRedirects)
	{
		if (Plan.OldToNewNames.Contains(Redirect.NewTagName))
		{
			Plan.ChainsToCollapse.Add(Redirect);
			if (Redirect.OldTagName != Plan.OldToNewNames.FindRef(Redirect.NewTagName))
			{
				Plan.RetirementVerificationNames.AddUnique(Redirect.OldTagName);
			}
		}
	}

	// Plugin-owned stores that carry subtree names (fixed in the same apply).
	for (const FGameplayTag& StyleTag : StyleTags)
	{
		if (Plan.OldToNewNames.Contains(StyleTag.GetTagName()))
		{
			Plan.AffectedStyleTags.Add(StyleTag);
		}
	}
	for (const FName& Favorite : Favorites)
	{
		if (Plan.OldToNewNames.Contains(Favorite))
		{
			Plan.AffectedFavorites.Add(Favorite);
		}
	}
	for (const FName& Recent : Recents)
	{
		if (Plan.OldToNewNames.Contains(Recent))
		{
			Plan.AffectedRecents.Add(Recent);
		}
	}

	if (TagTableSnapshot.Contains(NewName))
	{
		Plan.Verdict = ETagToolboxRenameVerdict::ReadyMerge;
		Plan.MergeTargetReferencerCount = MergeTargetReferencerCount;
		Plan.MergeTargetChildCount = MergeTargetChildCount;
		Plan.VerdictReason = FText::Format(LOCTEXT("MergeWarning", "'{0}' already exists ({1} referencing package(s), {2} child tag(s)). Renaming MERGES the two identities — after this they cannot be told apart."),
			FText::AsCultureInvariant(NewString), FText::AsNumber(MergeTargetReferencerCount), FText::AsNumber(MergeTargetChildCount));
	}
	else
	{
		Plan.Verdict = ETagToolboxRenameVerdict::Ready;
		Plan.VerdictReason = FText::GetEmpty();
	}
	return Plan;
}

TMap<FName, TArray<FName>> FTagToolboxRenameFixup::QueryLiveReferencers(const TArray<FName>& TagNames)
{
	TMap<FName, TArray<FName>> Result;
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	for (const FName& TagName : TagNames)
	{
		TArray<FAssetIdentifier> Referencers;
		AssetRegistry.GetReferencers(FAssetIdentifier(FGameplayTag::StaticStruct(), TagName), Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);
		TArray<FName>& Packages = Result.Add(TagName);
		for (const FAssetIdentifier& Referencer : Referencers)
		{
			if (!Referencer.PackageName.IsNone())
			{
				Packages.AddUnique(Referencer.PackageName);
			}
		}
		Packages.Sort(FNameLexicalLess());
	}
	return Result;
}

bool FTagToolboxRenameFixup::RemoveRedirectRowsChecked(const TArray<FTagToolboxRedirectRecord>& Rows, FText& OutError)
{
	// Group by owning list first — nothing mutates if any list is gone.
	TMap<UGameplayTagsList*, TArray<FTagToolboxRedirectRecord>> RowsPerList;
	for (const FTagToolboxRedirectRecord& Row : Rows)
	{
		UGameplayTagsList* List = Row.OwningList.Get();
		if (!List)
		{
			OutError = FText::Format(LOCTEXT("ListGone", "The tag source list owning redirect '{0}' no longer exists."), FText::FromName(Row.OldTagName));
			return false;
		}
		RowsPerList.FindOrAdd(List).Add(Row);
	}

	// Per-list transactionality: remove → persist one list at a time. On a
	// persist failure only THIS list's rows revert; already-persisted lists
	// stay retired (their disk and memory agree — re-adding their rows in
	// memory would be the divergence). The error names what remains.
	int32 ListsPersisted = 0;
	for (const TPair<UGameplayTagsList*, TArray<FTagToolboxRedirectRecord>>& Pair : RowsPerList)
	{
		UGameplayTagsList* List = Pair.Key;
		TArray<FGameplayTagRedirect> Removed;
		for (const FTagToolboxRedirectRecord& Row : Pair.Value)
		{
			for (int32 Index = List->GameplayTagRedirects.Num() - 1; Index >= 0; --Index)
			{
				const FGameplayTagRedirect& Redirect = List->GameplayTagRedirects[Index];
				if (Redirect.OldTagName == Row.OldTagName && Redirect.NewTagName == Row.NewTagName)
				{
					Removed.Add(Redirect);
					List->GameplayTagRedirects.RemoveAt(Index);
				}
			}
		}
		if (Removed.Num() == 0)
		{
			continue; // Rows already gone (idempotent re-run).
		}

		FText PersistError;
		if (!TagToolboxRenameInternal::PersistListChecked(List, PersistError))
		{
			for (const FGameplayTagRedirect& RemovedRow : Removed)
			{
				List->GameplayTagRedirects.AddUnique(RemovedRow);
			}
			OutError = FText::Format(LOCTEXT("PartialRetirement", "{0} of {1} list(s) persisted before '{2}' failed: {3} That list's rows were kept — re-run retirement from the audit for the remainder."),
				FText::AsNumber(ListsPersisted), FText::AsNumber(RowsPerList.Num()),
				FText::AsCultureInvariant(TagToolboxRenameInternal::ResolveListConfigPath(List)), PersistError);
			return false;
		}
		++ListsPersisted;
	}
	return true;
}

bool FTagToolboxRenameFixup::AddRedirectRowChecked(FName OldName, FName NewName, UGameplayTagsList* OwningList, FText& OutError)
{
	if (!OwningList)
	{
		OutError = LOCTEXT("NoList", "No owning tag source list.");
		return false;
	}
	if (OldName == NewName)
	{
		// A self-redirect makes the name resolve to itself through the
		// redirector and can never be verified/retired — refuse at the writer
		// so no caller can produce one.
		OutError = FText::Format(LOCTEXT("SelfRedirectRefused", "Refusing to write a self-redirect for '{0}'."), FText::FromName(OldName));
		return false;
	}
	FGameplayTagRedirect Redirect;
	Redirect.OldTagName = OldName;
	Redirect.NewTagName = NewName;
	const int32 CountBefore = OwningList->GameplayTagRedirects.Num();
	OwningList->GameplayTagRedirects.AddUnique(Redirect);

	FText PersistError;
	if (!TagToolboxRenameInternal::PersistListChecked(OwningList, PersistError))
	{
		if (OwningList->GameplayTagRedirects.Num() > CountBefore)
		{
			OwningList->GameplayTagRedirects.RemoveAt(OwningList->GameplayTagRedirects.Num() - 1);
		}
		OutError = PersistError;
		return false;
	}
	return true;
}

namespace TagToolboxRenameInternal
{
	/** Fix the plugin-owned tag-name stores in the same apply (R7). */
	static void FixupPluginStores(const FTagToolboxRenamePlan& Plan)
	{
		// Style registry: move each affected entry's color to the new name.
		UTagToolboxSettings* Settings = GetMutableDefault<UTagToolboxSettings>();
		for (const FGameplayTag& StyleTag : Plan.AffectedStyleTags)
		{
			FLinearColor Color;
			if (UTagToolboxSettings::ResolveTagColorFromArray(Settings->TagStyles, StyleTag, Color))
			{
				const FName* NewNamePtr = Plan.OldToNewNames.Find(StyleTag.GetTagName());
				const FGameplayTag NewTag = NewNamePtr ? FGameplayTag::RequestGameplayTag(*NewNamePtr, /*ErrorIfNotFound=*/false) : FGameplayTag();
				Settings->ClearTagColor(StyleTag);
				if (NewTag.IsValid())
				{
					Settings->SetTagColor(NewTag, Color);
				}
			}
		}

		// Favorites/recents: persisted per user per project; open pickers
		// reload on their next construct (the tree refresh already repaints).
		const TCHAR* Section = TEXT("TagToolbox");
		for (const TCHAR* Key : { TEXT("BrowserFavorites"), TEXT("BrowserRecents") })
		{
			FString Joined;
			if (GConfig->GetString(Section, Key, Joined, GEditorPerProjectIni))
			{
				TArray<FString> Names;
				Joined.ParseIntoArray(Names, TEXT(","), true);
				bool bChanged = false;
				for (FString& NameString : Names)
				{
					const FName* NewNamePtr = Plan.OldToNewNames.Find(FName(*NameString.TrimStartAndEnd()));
					if (NewNamePtr)
					{
						NameString = NewNamePtr->ToString();
						bChanged = true;
					}
				}
				if (bChanged)
				{
					GConfig->SetString(Section, Key, *FString::Join(Names, TEXT(",")), GEditorPerProjectIni);
				}
			}
		}
	}
}

FTagToolboxRenameResult FTagToolboxRenameFixup::ExecuteRename(const FTagToolboxRenamePlan& Plan)
{
	using namespace TagToolboxRenameInternal;

	FTagToolboxRenameResult Result;

	const bool bApplicable = Plan.Verdict == ETagToolboxRenameVerdict::Ready
		|| Plan.Verdict == ETagToolboxRenameVerdict::ReadyMerge
		|| Plan.Verdict == ETagToolboxRenameVerdict::SkipToResave;
	if (!bApplicable)
	{
		return Result; // Aborted — refusals never mutate.
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	// Preview referencers, live, over the FULL verification set (subtree plus
	// chain-reachable old names): retirement re-queries exactly this set, so
	// any name in it must also be in the resave scope — otherwise a
	// pre-existing chain's referencers block retirement forever while being
	// mislabeled "appeared since the preview".
	const TMap<FName, TArray<FName>> PreviewReferencers = QueryLiveReferencers(Plan.RetirementVerificationNames);
	TSet<FName> PreviewPackages;
	for (const TPair<FName, TArray<FName>>& Pair : PreviewReferencers)
	{
		PreviewPackages.Append(Pair.Value);
	}

	if (Plan.Verdict != ETagToolboxRenameVerdict::SkipToResave)
	{
		// --- Snapshot every ini the rename can touch (rollback window).
		TMap<FString, TArray<uint8>> IniSnapshots;
		TSet<FString> PreExistingPaths;
		TSet<FString> TouchedPaths;
		for (const FName& SubtreeName : Plan.SubtreeOldNames)
		{
			FString Comment;
			FName SourceName;
			bool bExplicit, bRestricted, bAllowNonRestricted;
			if (Manager.GetTagEditorData(SubtreeName, Comment, SourceName, bExplicit, bRestricted, bAllowNonRestricted))
			{
				if (const FGameplayTagSource* Source = Manager.FindTagSource(SourceName))
				{
					TouchedPaths.Add(ResolveListConfigPath(Source->SourceTagList));
				}
			}
		}
		for (const FTagToolboxRedirectRecord& Chain : Plan.ChainsToCollapse)
		{
			TouchedPaths.Add(ResolveListConfigPath(Chain.OwningList.Get()));
		}
		// The engine can always fall back to the settings file.
		TouchedPaths.Add(GetMutableDefault<UGameplayTagsSettings>()->GetDefaultConfigFilename());
		TouchedPaths.Remove(FString());
		for (const FString& Path : TouchedPaths)
		{
			if (IFileManager::Get().FileExists(*Path))
			{
				PreExistingPaths.Add(Path);
				TArray<uint8>& Bytes = IniSnapshots.Add(Path);
				FFileHelper::LoadFileToArray(Bytes, *Path);
			}
			else
			{
				IniSnapshots.Add(Path); // Empty snapshot marks "did not exist".
			}
		}

		// --- Rename. Children included; the engine writes one redirect per
		// descendant into each old source list.
		const bool bRenamed = IGameplayTagsEditorModule::Get().RenameTagInINI(Plan.OldName.ToString(), Plan.NewName.ToString(), /*bRenameChildren=*/true);
		if (!bRenamed)
		{
			// --- Rollback: reachable only here, before any package saves.
			// Files that pre-existed restore their bytes; a file the engine
			// CREATED during the failed rename is deleted outright — leaving
			// its partial content behind would survive the rollback.
			for (const TPair<FString, TArray<uint8>>& Snapshot : IniSnapshots)
			{
				if (PreExistingPaths.Contains(Snapshot.Key))
				{
					FFileHelper::SaveArrayToFile(Snapshot.Value, *Snapshot.Key);
				}
				else if (IFileManager::Get().FileExists(*Snapshot.Key))
				{
					IFileManager::Get().Delete(*Snapshot.Key, /*RequireExists=*/false, /*EvenReadOnly=*/true);
				}
			}
			const FString SettingsPath = GetMutableDefault<UGameplayTagsSettings>()->GetDefaultConfigFilename();
			GConfig->LoadFile(SettingsPath);
			GetMutableDefault<UGameplayTagsSettings>()->ReloadConfig();
			Manager.EditorRefreshGameplayTagTree();

			// The U2 probe IS the reconciliation check, run over EVERY subtree
			// old name: a partial child rename diverges at the child even when
			// the parent probes clean. Loose lists reload from disk on
			// refresh; settings-owned state may not (finding 6).
			bool bReconciled = true;
			for (const FName& SubtreeName : Plan.SubtreeOldNames)
			{
				const FGameplayTag Probe = FGameplayTag::RequestGameplayTag(SubtreeName, /*ErrorIfNotFound=*/false);
				if (!Probe.IsValid() || Probe.GetTagName() != SubtreeName)
				{
					bReconciled = false;
					break;
				}
			}
			if (bReconciled)
			{
				Result.Outcome = ETagToolboxRenameOutcome::RolledBack;
				Notify(LOCTEXT("RolledBack", "Rename failed; tag configuration was restored (no packages were touched)."));
			}
			else
			{
				Result.Outcome = ETagToolboxRenameOutcome::RolledBackRestartRequired;
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("RestartRequired",
					"Rename failed and was rolled back on disk, but this editor session still holds the old redirect in memory.\n\nRESTART THE EDITOR BEFORE SAVING ANYTHING: any asset saved now would silently rewrite the old tag name to a name that no longer exists."));
			}
			return Result;
		}

	}

	// --- Chain collapse: A→Old becomes A→New in A's own list. Runs on BOTH
	// entry paths — a resumed (skip-to-resave) rename must still collapse the
	// chains its plan collected, or crash-orphaned A→Old→New chains stay
	// unresolvable. Idempotent: removal no-ops on absent rows.
	for (const FTagToolboxRedirectRecord& Chain : Plan.ChainsToCollapse)
	{
		const FName* CollapsedTarget = Plan.OldToNewNames.Find(Chain.NewTagName);
		if (!CollapsedTarget)
		{
			continue;
		}
		FText ChainError;
		if (Chain.OldTagName == *CollapsedTarget)
		{
			// Collapsing would write a self-redirect (rename-back shape):
			// remove the row without a replacement — the name it carried is
			// the LIVE tag again and was excluded from retirement re-query.
			if (!RemoveRedirectRowsChecked({ Chain }, ChainError))
			{
				Notify(FText::Format(LOCTEXT("SelfChainRemoveFailed", "Could not remove the now-redundant redirect '{0}': {1}"),
					FText::FromName(Chain.OldTagName), ChainError));
			}
			continue;
		}
		if (!RemoveRedirectRowsChecked({ Chain }, ChainError)
			|| !AddRedirectRowChecked(Chain.OldTagName, *CollapsedTarget, Chain.OwningList.Get(), ChainError))
		{
			Notify(FText::Format(LOCTEXT("ChainCollapseFailed", "Could not collapse redirect chain '{0}': {1} The chained redirect will not resolve until fixed."),
				FText::FromName(Chain.OldTagName), ChainError));
		}
	}

	// --- Plugin stores move in the same apply (no-op when the plan carries
	// none, as on the recovery path).
	FixupPluginStores(Plan);

	// --- Resave every referencer of the whole subtree, under consent.
	TArray<FName> AllReferencerNames = PreviewPackages.Array();
	AllReferencerNames.Sort(FNameLexicalLess());
	FTagToolboxResavePlan ResavePlan = FTagToolboxResaveService::BuildPlan(FTagToolboxResaveService::GatherFacts(AllReferencerNames));
	if (ResavePlan.Entries.Num() > 0)
	{
		if (!STagToolboxResaveDialog::ShowDialog(ResavePlan, FText::Format(
			LOCTEXT("ResaveHeader", "'{0}' was renamed to '{1}'. These packages still reference the old name(s) and will be fixed by a resave. Redirects stay in place until every referencer is clean."),
			FText::FromName(Plan.OldName), FText::FromName(Plan.NewName))))
		{
			// Declined: redirects kept; nothing saved. Recovery = re-run
			// rename with the same input (enters through skip-to-resave).
			Result.Outcome = ETagToolboxRenameOutcome::Partial;
			Result.ResaveReport = FTagToolboxResaveService::BuildReport(ResavePlan, TSet<FName>(), TSet<FName>());
			return Result;
		}
		Result.ResaveReport = FTagToolboxResaveService::ExecutePlan(ResavePlan);
	}

	// --- Retirement gate: every plan entry saved AND the live re-query of
	// every old name (chain-reachable included) comes back empty.
	const bool bCleanResave = Result.ResaveReport.Failed.Num() == 0
		&& Result.ResaveReport.Unattempted.Num() == 0
		&& Result.ResaveReport.Skipped.Num() == 0;
	if (!bCleanResave)
	{
		Result.Outcome = ETagToolboxRenameOutcome::Partial;
		Notify(FText::Format(LOCTEXT("PartialOutcome", "Rename fix-up incomplete: {0} failed, {1} unattempted, {2} skipped. Redirects kept — re-running the same rename resumes the fix-up."),
			FText::AsNumber(Result.ResaveReport.Failed.Num()), FText::AsNumber(Result.ResaveReport.Unattempted.Num()), FText::AsNumber(Result.ResaveReport.Skipped.Num())));
		return Result;
	}

	const TMap<FName, TArray<FName>> Requery = QueryLiveReferencers(Plan.RetirementVerificationNames);
	TSet<FName> RemainingPackages;
	for (const TPair<FName, TArray<FName>>& Pair : Requery)
	{
		RemainingPackages.Append(Pair.Value);
	}
	if (RemainingPackages.Num() > 0)
	{
		for (const FName& Package : RemainingPackages)
		{
			if (!PreviewPackages.Contains(Package))
			{
				Result.NewReferencersSincePreview.Add(Package);
			}
		}
		Result.NewReferencersSincePreview.Sort(FNameLexicalLess());
		Result.Outcome = ETagToolboxRenameOutcome::StaleReferencers;
		Notify(FText::Format(LOCTEXT("StaleReferencers", "All previewed packages saved, but {0} referencer(s) appeared since the preview ({1} new). Redirects kept — run the rename again to fix them."),
			FText::AsNumber(RemainingPackages.Num()), FText::AsNumber(Result.NewReferencersSincePreview.Num())));
		return Result;
	}

	Result.Outcome = ETagToolboxRenameOutcome::Verified;

	// --- Retirement offer (strictly optional; redirects are harmless).
	TArray<FTagToolboxRedirectRecord> ToRetire;
	for (const FTagToolboxRedirectRecord& Redirect : FTagToolboxTagScanService::CollectAllRedirects())
	{
		if (Plan.RetirementVerificationNames.Contains(Redirect.OldTagName))
		{
			ToRetire.Add(Redirect);
		}
	}
	if (ToRetire.Num() > 0)
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			LOCTEXT("RetirementOffer", "Every referencer now saves the new name, and re-checking the old name(s) found nothing.\n\nRetire the {0} redirect(s) now? (Keeping them is harmless; retiring keeps the config clean.)"),
			FText::AsNumber(ToRetire.Num())));
		if (Answer == EAppReturnType::Yes)
		{
			FText RetireError;
			if (RemoveRedirectRowsChecked(ToRetire, RetireError))
			{
				Result.bRedirectsRetired = true;
				Manager.EditorRefreshGameplayTagTree();
				Notify(FText::Format(LOCTEXT("Retired", "Retired {0} redirect(s)."), FText::AsNumber(ToRetire.Num())));
			}
			else
			{
				Notify(FText::Format(LOCTEXT("RetireFailed", "Redirect retirement failed and was reverted: {0} Redirects kept — retry from the audit."), RetireError));
			}
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
