// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** How one target package stands before a resave (U2 findings 5/6 drive this). */
enum class ETagToolboxPackageDisposition : uint8
{
	/** Not in memory: load, then save. */
	NotLoaded,
	/** Loaded and clean: reload (re-resolves redirected tag names), then save. */
	LoadedClean,
	/**
	 * Loaded with unsaved edits. Excluded unless the user explicitly opts in;
	 * opted-in packages are SAVED FIRST (preserving the edits — the old tag
	 * name they serialize is covered by the redirect), then reloaded, then
	 * resaved. Never reload a dirty package that was not just saved: a
	 * non-interactive positive reload silently discards the user's edits.
	 */
	LoadedDirty,
	/** Read-only on disk with no source-control provider to check it out: excluded, named in the plan. */
	ReadOnlyNoProvider,
};

/** Caller-supplied facts about one package (pure inputs — testable). */
struct FTagToolboxPackageFacts
{
	FName PackageName;
	bool bLoaded = false;
	bool bDirty = false;
	bool bReadOnlyOnDisk = false;
	bool bSourceControlActive = false;
};

/** One planned package with its disposition and inclusion state. */
struct FTagToolboxResavePlanEntry
{
	FName PackageName;
	ETagToolboxPackageDisposition Disposition = ETagToolboxPackageDisposition::NotLoaded;
	/** Dirty and read-only entries start excluded; dirty flips on explicit opt-in. */
	bool bIncluded = true;
};

struct FTagToolboxResavePlan
{
	TArray<FTagToolboxResavePlanEntry> Entries;

	int32 CountIncluded() const;
	TArray<FName> IncludedPackageNames() const;
};

/**
 * Per-package outcome report. "Saved" comes from a batch-scoped
 * PackageSavedWithContextEvent subscription — never from OutFailedPackages
 * alone, which cannot distinguish unattempted packages after a mid-batch
 * Cancel of the engine's save-failure dialog.
 */
struct FTagToolboxResaveReport
{
	TArray<FName> Saved;
	/** Attempted and failed (engine save-failure dialog's Continue, or hard failure). */
	TArray<FName> Failed;
	/** Included but never attempted (the engine dialog's mid-batch Cancel). */
	TArray<FName> Unattempted;
	/** Excluded by the plan or by declined dirty consent. */
	TArray<FName> Skipped;

	bool AllIncludedSaved() const { return Failed.Num() == 0 && Unattempted.Num() == 0; }
};

/**
 * The one resave engine shared by rename fix-up and the audit's "Resave
 * referencers" action: one dirty-consent rule, one failure-report shape.
 * The pure plan/consent/report seams carry every decision; the execute path
 * stays thin over the UnrealEd file helpers.
 */
struct FTagToolboxResaveService
{
	/** Pure: classify each package from supplied facts. */
	static FTagToolboxResavePlan BuildPlan(const TArray<FTagToolboxPackageFacts>& Facts);

	/** Pure: flip opted-in dirty entries to included. Unknown names ignored. */
	static void ApplyDirtyConsent(FTagToolboxResavePlan& Plan, const TSet<FName>& OptedInDirtyPackages);

	/**
	 * Pure: aggregate the outcome sets. Included = the plan's included names;
	 * SavedPackages = names observed through the save event; FailedPackages =
	 * the engine's failed list. Unattempted = included − saved − failed.
	 */
	static FTagToolboxResaveReport BuildReport(
		const FTagToolboxResavePlan& Plan,
		const TSet<FName>& SavedPackages,
		const TSet<FName>& FailedPackages);

	/** Gathers live facts for the named packages (editor state + disk). */
	static TArray<FTagToolboxPackageFacts> GatherFacts(const TArray<FName>& PackageNames);

	/**
	 * Executes an already-consented plan: save opted-in dirty first, reload
	 * loaded packages (non-interactive — no engine modal mid-apply), load the
	 * rest, then one checkout-and-save batch. The engine's own save-failure
	 * dialog (Cancel/Retry/Continue) can still appear per failed package in
	 * interactive sessions; Cancel lands as Unattempted entries, never Failed.
	 */
	static FTagToolboxResaveReport ExecutePlan(const FTagToolboxResavePlan& Plan);
};
