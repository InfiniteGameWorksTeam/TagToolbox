// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TagToolboxResaveService.h"
#include "TagToolboxTagScanService.h"

class UGameplayTagsList;

/** The plan phase's verdict for one requested rename. */
enum class ETagToolboxRenameVerdict : uint8
{
	/** Ordinary rename: target free, sources writable. */
	Ready,
	/** Target already defined: proceeding MERGES the identities — explicit confirm. */
	ReadyMerge,
	/**
	 * Recovery entry: the old name is already undefined and redirects to the
	 * requested target (a prior partial run or crash). The apply skips the
	 * Rename state entirely and enters at Resave — re-running the engine
	 * rename would walk redirected children into destructive X→X self-renames
	 * (U2 finding 3).
	 */
	SkipToResave,
	/** Target is inside the renamed subtree (or vice versa) — refused, naming the relationship. */
	RefusedCycle,
	/**
	 * A descendant's source ini is unwritable: the WHOLE rename refuses. A
	 * partial subtree would leave a redirect on the parent while excluded
	 * children stay defined under the old path, where redirects-take-priority
	 * resolution silently orphans them out of every parent-tag query.
	 */
	RefusedUnwritableSource,
	/** Old name undefined (and not a recovery input), or the new name is invalid. */
	RefusedInvalid,
};

/** Everything the rename dialog previews and the apply consumes. Built pure. */
struct FTagToolboxRenamePlan
{
	FName OldName;
	FName NewName;
	ETagToolboxRenameVerdict Verdict = ETagToolboxRenameVerdict::RefusedInvalid;
	FText VerdictReason;

	/** Snapshot-expanded subtree (includes OldName), sorted. */
	TArray<FName> SubtreeOldNames;
	/** Old subtree name → its post-rename name. */
	TMap<FName, FName> OldToNewNames;

	/** Merge-confirm content: what the user conflates by proceeding. */
	int32 MergeTargetReferencerCount = 0;
	int32 MergeTargetChildCount = 0;

	/** Pre-existing redirects X→(subtree member): collapsed to X→renamed at apply (never chained). */
	TArray<FTagToolboxRedirectRecord> ChainsToCollapse;
	/** Every old name the retirement re-query must find unreferenced: subtree + chain-reachable. */
	TArray<FName> RetirementVerificationNames;

	/** RefusedUnwritableSource payload: the blocking source inis. */
	TArray<FName> UnwritableSources;

	/** Plugin-owned stores fixed in the same apply (R7). */
	TArray<FGameplayTag> AffectedStyleTags;
	TArray<FName> AffectedFavorites;
	TArray<FName> AffectedRecents;
};

/** Outcome of one full rename apply (the state machine's terminal states). */
enum class ETagToolboxRenameOutcome : uint8
{
	/** All referencers saved, re-query empty; retirement was offered. */
	Verified,
	/** Some referencer failed/unattempted/skipped; redirects kept. */
	Partial,
	/** All saved but the re-query found NEW referencers since the preview; redirects kept, no retirement. */
	StaleReferencers,
	/** Rename step failed; inis restored with zero packages saved. */
	RolledBack,
	/** Rollback restored disk state but the in-memory redirect survived — restart before saving. */
	RolledBackRestartRequired,
	/** Preflight/consent aborted; zero mutations. */
	Aborted,
};

struct FTagToolboxRenameResult
{
	ETagToolboxRenameOutcome Outcome = ETagToolboxRenameOutcome::Aborted;
	FTagToolboxResaveReport ResaveReport;
	/** Referencers that appeared between preview and apply (StaleReferencers). */
	TArray<FName> NewReferencersSincePreview;
	bool bRedirectsRetired = false;
};

/**
 * Rename-with-asset-fixup (U7): the pure plan phase every dialog decision and
 * test drives, plus the apply orchestration following the design's one-way
 * state machine. Referencer sets are always queried LIVE from the Asset
 * Registry at plan and re-query time — never from the session scan cache.
 */
struct FTagToolboxRenameFixup
{
	/** Pure: the renamed form of one subtree member. */
	static FName DeriveRenamedName(FName SubtreeOldName, FName OldName, FName NewName);

	/**
	 * Pure planner over supplied snapshots/facts (no engine state). The caller
	 * gathers: the pre-rename tag-table snapshot, all-source redirect records,
	 * each subtree member's owning source, the unwritable-source set, the
	 * plugin stores, and the merge target's counts.
	 */
	static FTagToolboxRenamePlan BuildPlan(
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
		int32 MergeTargetChildCount);

	/** Live per-old-name referencer query (exact names, Asset Registry). */
	static TMap<FName, TArray<FName>> QueryLiveReferencers(const TArray<FName>& TagNames);

	/**
	 * The apply: ini snapshots → engine rename (skipped for SkipToResave) →
	 * chain collapse → plugin-store fixup → consented resave → live re-query →
	 * retirement offer. Rollback (ini restore + refresh + the U2 probe) is
	 * reachable only from a failed rename step, before any package saves.
	 * Interactive: shows the resave consent dialog and the retirement offer.
	 */
	static FTagToolboxRenameResult ExecuteRename(const FTagToolboxRenamePlan& Plan);

	/**
	 * Shared redirect-row writer (retirement + U8's create-redirect): mutates
	 * the owning list only after a writability check, verifies
	 * TryUpdateDefaultConfigFile, and reverts the in-memory rows on persist
	 * failure. Returns false with OutError when nothing was persisted.
	 */
	static bool RemoveRedirectRowsChecked(const TArray<FTagToolboxRedirectRecord>& Rows, FText& OutError);
	static bool AddRedirectRowChecked(FName OldName, FName NewName, UGameplayTagsList* OwningList, FText& OutError);
};
