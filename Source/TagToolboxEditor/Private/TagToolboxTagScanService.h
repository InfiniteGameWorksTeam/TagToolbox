// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/WeakObjectPtr.h"

class UGameplayTagsList;

enum class ETagToolboxScanState : uint8
{
	/** No scan has run this session; usage data does not exist. */
	NeverScanned,
	/** Scan data reflects the current tag table and saved content. */
	Fresh,
	/** Scan data exists but the tag table or saved content changed since. */
	Stale,
};

/**
 * One GameplayTagRedirect together with the tag-source list that owns it.
 * The engine writes rename redirects to the OLD tag's source list — not to
 * DefaultGameplayTags.ini — so any consumer that reads only
 * UGameplayTagsSettings misses redirects. Carrying the owning list lets
 * retirement edit (and persist) the right file.
 */
struct FTagToolboxRedirectRecord
{
	FName OldTagName;
	FName NewTagName;
	/** FGameplayTagSource::SourceName of the owning list (its ini file name). */
	FName OwningSourceName;
	/** The list object whose GameplayTagRedirects array holds this row. */
	TWeakObjectPtr<UGameplayTagsList> OwningList;
};

/**
 * The session-cached tag usage scan every Tag Toolbox surface shares: one
 * forward walk over Asset Registry SearchableName data producing
 * tag -> referencer packages, plus redirect collection aggregated across
 * EVERY tag source list. The audit, Browser usage counts, rename fix-up
 * preview, and the CI commandlet all consume this service; the Browser's
 * References pane deliberately keeps its own live per-tag query.
 *
 * The scan never runs implicitly: surfaces trigger it explicitly and the
 * cache only ever moves Fresh -> Stale on invalidation (tag tree refresh,
 * package save) — it never auto-rescans, so a displayed count is never a
 * silently wrong number, only an honestly stale one.
 */
class FTagToolboxTagScanService
{
public:
	/** Module-owned singleton. Created lazily; released by Shutdown(). */
	static FTagToolboxTagScanService& Get();
	/** Unbinds delegates and destroys the instance (module shutdown). */
	static void Shutdown();

	/**
	 * Runs the full referencer scan now (explicit — callers own the gesture).
	 * bAllowDialog shows the slow-task dialog; commandlets pass false.
	 */
	void RunScan(bool bAllowDialog);

	ETagToolboxScanState GetState() const { return State; }

	/** The cached scan. Empty (and meaningless) while NeverScanned. */
	const TMap<FName, TArray<FName>>& GetReferencedTagToPackages() const { return ReferencedTagToPackages; }

	/** Sorted referencer packages for one exact tag name (no child expansion). */
	TArray<FName> GetReferencersForExactTag(FName TagName) const;

	/** Exact-name usage count; 0 when scanned and absent. Check GetState() first. */
	int32 GetExactUsageCount(FName TagName) const;

	/** Fresh -> Stale (or no-op). Never rescans, never clears cached data. */
	void MarkStale();

	/** Broadcast on every state change (scan completed, cache went stale). */
	FSimpleMulticastDelegate OnScanStateChanged;

	/**
	 * Redirects aggregated across every tag source list — the default settings
	 * list AND every Config/Tags list — each record carrying its owning list.
	 * Collected live on each call (cheap); never cached.
	 */
	static TArray<FTagToolboxRedirectRecord> CollectAllRedirects();

	/** Pure seam: collect one list's redirect rows into Out. */
	static void CollectRedirectsFromList(FName SourceName, UGameplayTagsList* List, TArray<FTagToolboxRedirectRecord>& Out);

	/**
	 * Pure seam: every name in the snapshot equal to ParentTag or underneath it
	 * (ParentTag + "."), sorted. Operates only on the supplied snapshot so a
	 * post-snapshot table change cannot alter the result.
	 */
	static TArray<FName> ExpandSubtreeNames(const TSet<FName>& TagTableSnapshot, FName ParentTag);

	/** Test seam: install a fake scan result and mark the cache Fresh. */
	void SetCacheForTesting(TMap<FName, TArray<FName>> InReferencedTagToPackages);

	~FTagToolboxTagScanService();

private:
	FTagToolboxTagScanService();

	void HandleTagTreeChanged();
	void HandlePackageSaved(const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext Context);

	ETagToolboxScanState State = ETagToolboxScanState::NeverScanned;
	TMap<FName, TArray<FName>> ReferencedTagToPackages;

	FDelegateHandle TagTreeChangedHandle;
	FDelegateHandle PackageSavedHandle;
};
