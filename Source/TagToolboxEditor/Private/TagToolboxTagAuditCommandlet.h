// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "TagToolboxAudit.h"
#include "TagToolboxTagAuditCommandlet.generated.h"

/** Escalation + scope options parsed from the commandlet line. */
struct FTagToolboxAuditReportOptions
{
	/** Categories beyond referenced-but-undefined that fail the gate. */
	bool bFailOnUnused = false;
	bool bFailOnBrokenRedirects = false;
	bool bFailOnLingeringRedirects = false;
	bool bFailOnNearDuplicates = false;

	/** Referencer scope: game content only unless widened. */
	bool bIncludeEngineContent = false;
	bool bIncludePluginContent = false;
};

/**
 * Pure report core (U10): scope filtering, failure-severity classification,
 * and deterministic JSON generation over an immutable row snapshot — all
 * testable without an Asset Registry or file writes.
 */
struct FTagToolboxAuditReportBuilder
{
	/** True when a package path is inside the configured referencer scope. */
	static bool PackageInScope(FName PackageName, const FTagToolboxAuditReportOptions& Options);

	/**
	 * Scope-filters referencer packages. Referencer-based rows (undefined /
	 * lingering) whose referencers ALL fall out of scope are dropped;
	 * definition-side rows (unused / near-duplicate / broken redirect) pass
	 * through untouched.
	 */
	static TArray<TSharedPtr<FTagToolboxAuditRow>> ApplyScope(
		const TArray<TSharedPtr<FTagToolboxAuditRow>>& Rows,
		const FTagToolboxAuditReportOptions& Options);

	/** Referenced-but-undefined always fails; the switches escalate the rest. */
	static bool CategoryFails(ETagToolboxAuditCategory Category, const FTagToolboxAuditReportOptions& Options);

	static FString CategoryToString(ETagToolboxAuditCategory Category);

	/**
	 * Deterministic JSON (schema_version 1): rows sorted category-then-tag,
	 * per-finding "fails" flags, per-category counts, and the overall
	 * "failed" verdict the wrapper reads. Identical inputs produce identical
	 * text.
	 */
	static FString GenerateJson(
		const TArray<TSharedPtr<FTagToolboxAuditRow>>& Rows,
		const FTagToolboxAuditReportOptions& Options,
		const FString& StartedUtcIso8601,
		const FString& FinishedUtcIso8601);
};

/**
 * The CI gate (U10): -run=TagToolboxTagAudit performs a synchronous Asset
 * Registry scan, runs the SAME shared scan + classifiers as the editor audit
 * (one implementation, two projections), and writes the schema-versioned JSON
 * report via temp-file + atomic rename. Read-only by construction — no
 * ShouldImportTagsFromINI gate, so code-tag-only projects still audit. The
 * wrapper script owns the exit contract; this process's own exit code is not
 * the verdict.
 */
UCLASS()
class UTagToolboxTagAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UTagToolboxTagAuditCommandlet();

	//~ UCommandlet
	virtual int32 Main(const FString& Params) override;
};
