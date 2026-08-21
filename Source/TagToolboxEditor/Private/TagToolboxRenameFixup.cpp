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
#include "TagToolboxSettings.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "TagToolboxRenameFixup"

namespace TagToolboxRenameInternal
{
	static void Notify(const FText& Message)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
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
				Plan.SubtreeOldNames.Add(OldName);
				Plan.OldToNewNames.Add(OldName, NewName);
				Plan.RetirementVerificationNames.Add(OldName);
				// Chain-reachable old names still verify before retirement.
				for (const FTagToolboxRedirectRecord& Chain : AllRedirects)
				{
					if (Chain.NewTagName == OldName)
					{
						Plan.ChainsToCollapse.Add(Chain);
						Plan.RetirementVerificationNames.AddUnique(Chain.OldTagName);
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
	for (const FTagToolboxRedirectRecord& Redirect : AllRedirects)
	{
		if (Plan.OldToNewNames.Contains(Redirect.NewTagName))
		{
			Plan.ChainsToCollapse.Add(Redirect);
			Plan.RetirementVerificationNames.AddUnique(Redirect.OldTagName);
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
	// Group by owning list so each ini writes (and can revert) once.
	TMap<UGameplayTagsList*, TArray<FGameplayTagRedirect>> RemovedPerList;
	for (const FTagToolboxRedirectRecord& Row : Rows)
	{
		UGameplayTagsList* List = Row.OwningList.Get();
		if (!List)
		{
			OutError = FText::Format(LOCTEXT("ListGone", "The tag source list owning redirect '{0}' no longer exists."), FText::FromName(Row.OldTagName));
			return false;
		}
		for (int32 Index = List->GameplayTagRedirects.Num() - 1; Index >= 0; --Index)
		{
			const FGameplayTagRedirect& Redirect = List->GameplayTagRedirects[Index];
			if (Redirect.OldTagName == Row.OldTagName && Redirect.NewTagName == Row.NewTagName)
			{
				RemovedPerList.FindOrAdd(List).Add(Redirect);
				List->GameplayTagRedirects.RemoveAt(Index);
			}
		}
	}

	for (const TPair<UGameplayTagsList*, TArray<FGameplayTagRedirect>>& Pair : RemovedPerList)
	{
		FText PersistError;
		if (!TagToolboxRenameInternal::PersistListChecked(Pair.Key, PersistError))
		{
			// Revert every in-memory mutation: a half-persisted retirement
			// resurrects rows next session while this session believes them gone.
			for (const TPair<UGameplayTagsList*, TArray<FGameplayTagRedirect>>& RevertPair : RemovedPerList)
			{
				for (const FGameplayTagRedirect& Removed : RevertPair.Value)
				{
					RevertPair.Key->GameplayTagRedirects.AddUnique(Removed);
				}
			}
			OutError = PersistError;
			return false;
		}
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

	// Preview referencers, live (feeds the stale-delta check after resave).
	const TMap<FName, TArray<FName>> PreviewReferencers = QueryLiveReferencers(Plan.SubtreeOldNames);
	TSet<FName> PreviewPackages;
	for (const TPair<FName, TArray<FName>>& Pair : PreviewReferencers)
	{
		PreviewPackages.Append(Pair.Value);
	}

	if (Plan.Verdict != ETagToolboxRenameVerdict::SkipToResave)
	{
		// --- Snapshot every ini the rename can touch (rollback window).
		TMap<FString, TArray<uint8>> IniSnapshots;
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
			TArray<uint8>& Bytes = IniSnapshots.Add(Path);
			FFileHelper::LoadFileToArray(Bytes, *Path); // Missing file = empty snapshot.
		}

		// --- Rename. Children included; the engine writes one redirect per
		// descendant into each old source list.
		const bool bRenamed = IGameplayTagsEditorModule::Get().RenameTagInINI(Plan.OldName.ToString(), Plan.NewName.ToString(), /*bRenameChildren=*/true);
		if (!bRenamed)
		{
			// --- Rollback: reachable only here, before any package saves.
			for (const TPair<FString, TArray<uint8>>& Snapshot : IniSnapshots)
			{
				if (Snapshot.Value.Num() > 0)
				{
					FFileHelper::SaveArrayToFile(Snapshot.Value, *Snapshot.Key);
				}
			}
			const FString SettingsPath = GetMutableDefault<UGameplayTagsSettings>()->GetDefaultConfigFilename();
			GConfig->LoadFile(SettingsPath);
			GetMutableDefault<UGameplayTagsSettings>()->ReloadConfig();
			Manager.EditorRefreshGameplayTagTree();

			// The U2 probe IS the reconciliation check: loose lists reload from
			// disk on refresh; settings-owned state may not (finding 6).
			const FGameplayTag Probe = FGameplayTag::RequestGameplayTag(Plan.OldName, /*ErrorIfNotFound=*/false);
			const bool bReconciled = Probe.IsValid() && Probe.GetTagName() == Plan.OldName;
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

		// --- Chain collapse: A→Old becomes A→New in A's own list.
		for (const FTagToolboxRedirectRecord& Chain : Plan.ChainsToCollapse)
		{
			const FName* CollapsedTarget = Plan.OldToNewNames.Find(Chain.NewTagName);
			if (!CollapsedTarget)
			{
				continue;
			}
			FText ChainError;
			if (!RemoveRedirectRowsChecked({ Chain }, ChainError)
				|| !AddRedirectRowChecked(Chain.OldTagName, *CollapsedTarget, Chain.OwningList.Get(), ChainError))
			{
				Notify(FText::Format(LOCTEXT("ChainCollapseFailed", "Could not collapse redirect chain '{0}': {1} The chained redirect will not resolve until fixed."),
					FText::FromName(Chain.OldTagName), ChainError));
			}
		}

		// --- Plugin stores move in the same apply.
		FixupPluginStores(Plan);
	}

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
