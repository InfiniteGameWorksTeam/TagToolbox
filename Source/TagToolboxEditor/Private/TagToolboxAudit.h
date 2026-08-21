// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class ETagToolboxAuditCategory : uint8
{
	/** Defined in the tag table but never referenced by saved content or config. */
	UnusedDefined,
	/** Referenced by saved content but no longer defined (and not redirected). */
	ReferencedUndefined,
	/** Two sibling tags whose leaf names differ by a single edit — likely a typo. */
	NearDuplicate,
	/** A GameplayTagRedirect whose new tag name is not defined. */
	BrokenRedirect,
	/** A redirect whose OLD name is still referenced by saved content. */
	LingeringRedirect,
};

struct FTagToolboxAuditRow
{
	ETagToolboxAuditCategory Category = ETagToolboxAuditCategory::UnusedDefined;
	FName Tag;
	FString Detail;
	/** Packages whose saved data references Tag, when the category has any. */
	TArray<FName> ReferencerPackages;
};

/**
 * The Tag Audit engine. The full audit reads only Asset Registry metadata:
 * FGameplayTag/FGameplayTagContainer serialization marks each saved tag as a
 * SearchableName dependency, so usage questions never require loading assets.
 *
 * Known blind spots, by design: C++ RequestGameplayTag call sites, tags stored
 * as plain FName/FString properties, and unsaved/dirty edits are invisible to
 * the registry. The unused list comes from the engine's own scan, which also
 * covers config/INI references.
 */
struct FTagToolboxAudit
{
	/**
	 * Pure classification of referenced tag names (testable without engine
	 * state): names not defined split into truly-undefined vs old names that a
	 * redirect still covers. Outputs are sorted for determinism.
	 */
	static void ClassifyReferencedTags(
		const TSet<FName>& DefinedTags,
		const TSet<FName>& RedirectOldNames,
		const TSet<FName>& ReferencedTags,
		TArray<FName>& OutUndefined,
		TArray<FName>& OutLingeringRedirects);

	/**
	 * Pure near-duplicate test: case-insensitive edit distance of exactly 1
	 * (two sibling FNames can never differ only by case, so distance 0 never
	 * occurs between distinct siblings).
	 */
	static bool AreNamesNearDuplicate(const FString& A, const FString& B);

	/**
	 * Runs the complete audit against the editor Asset Registry. Never loads
	 * assets. The referencer walk runs through the shared scan service (so an
	 * explicit audit run also refreshes Browser usage counts), and redirects
	 * aggregate across EVERY tag source list, not just the default settings
	 * list. bAllowDialog=false suppresses the slow-task dialog (commandlet).
	 */
	static TArray<TSharedPtr<FTagToolboxAuditRow>> RunAudit(bool bAllowDialog = true);

	static FText GetCategoryDisplayText(ETagToolboxAuditCategory Category);
	static FLinearColor GetCategoryColor(ETagToolboxAuditCategory Category);
};
