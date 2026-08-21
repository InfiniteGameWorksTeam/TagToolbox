// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxTagAuditCommandlet.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogTagToolboxAuditCommandlet, Log, All);

bool FTagToolboxAuditReportBuilder::PackageInScope(FName PackageName, const FTagToolboxAuditReportOptions& Options)
{
	const FString PackageString = PackageName.ToString();
	if (PackageString.StartsWith(TEXT("/Game/")))
	{
		return true;
	}
	if (PackageString.StartsWith(TEXT("/Engine/")))
	{
		return Options.bIncludeEngineContent;
	}
	// Everything else is a plugin (or script) mount point.
	return Options.bIncludePluginContent;
}

TArray<TSharedPtr<FTagToolboxAuditRow>> FTagToolboxAuditReportBuilder::ApplyScope(
	const TArray<TSharedPtr<FTagToolboxAuditRow>>& Rows,
	const FTagToolboxAuditReportOptions& Options)
{
	TArray<TSharedPtr<FTagToolboxAuditRow>> Result;
	for (const TSharedPtr<FTagToolboxAuditRow>& Row : Rows)
	{
		if (!Row.IsValid())
		{
			continue;
		}

		const bool bReferencerBased = Row->Category == ETagToolboxAuditCategory::ReferencedUndefined
			|| Row->Category == ETagToolboxAuditCategory::LingeringRedirect;
		if (!bReferencerBased)
		{
			Result.Add(Row);
			continue;
		}

		TSharedPtr<FTagToolboxAuditRow> Scoped = MakeShared<FTagToolboxAuditRow>(*Row);
		Scoped->ReferencerPackages.RemoveAll([&Options](const FName& PackageName)
		{
			return !PackageInScope(PackageName, Options);
		});
		if (Scoped->ReferencerPackages.Num() > 0)
		{
			Result.Add(Scoped);
		}
	}
	return Result;
}

bool FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory Category, const FTagToolboxAuditReportOptions& Options)
{
	switch (Category)
	{
	case ETagToolboxAuditCategory::ReferencedUndefined: return true; // the shipped-bug class — always fails
	case ETagToolboxAuditCategory::UnusedDefined:       return Options.bFailOnUnused;
	case ETagToolboxAuditCategory::BrokenRedirect:      return Options.bFailOnBrokenRedirects;
	case ETagToolboxAuditCategory::LingeringRedirect:   return Options.bFailOnLingeringRedirects;
	case ETagToolboxAuditCategory::NearDuplicate:       return Options.bFailOnNearDuplicates;
	default:                                            return false;
	}
}

FString FTagToolboxAuditReportBuilder::CategoryToString(ETagToolboxAuditCategory Category)
{
	switch (Category)
	{
	case ETagToolboxAuditCategory::UnusedDefined:       return TEXT("unused");
	case ETagToolboxAuditCategory::ReferencedUndefined: return TEXT("referenced_undefined");
	case ETagToolboxAuditCategory::NearDuplicate:       return TEXT("near_duplicate");
	case ETagToolboxAuditCategory::BrokenRedirect:      return TEXT("broken_redirect");
	case ETagToolboxAuditCategory::LingeringRedirect:   return TEXT("lingering_redirect");
	default:                                            return TEXT("unknown");
	}
}

FString FTagToolboxAuditReportBuilder::GenerateJson(
	const TArray<TSharedPtr<FTagToolboxAuditRow>>& Rows,
	const FTagToolboxAuditReportOptions& Options,
	const FString& StartedUtcIso8601,
	const FString& FinishedUtcIso8601)
{
	// Deterministic ordering: category, then tag name.
	TArray<TSharedPtr<FTagToolboxAuditRow>> Sorted = Rows;
	Sorted.Sort([](const TSharedPtr<FTagToolboxAuditRow>& A, const TSharedPtr<FTagToolboxAuditRow>& B)
	{
		if (A->Category != B->Category)
		{
			return static_cast<uint8>(A->Category) < static_cast<uint8>(B->Category);
		}
		return A->Tag.LexicalLess(B->Tag);
	});

	int32 CountUnused = 0, CountUndefined = 0, CountNearDuplicate = 0, CountBroken = 0, CountLingering = 0;
	bool bFailed = false;

	TArray<TSharedPtr<FJsonValue>> FindingValues;
	for (const TSharedPtr<FTagToolboxAuditRow>& Row : Sorted)
	{
		switch (Row->Category)
		{
		case ETagToolboxAuditCategory::UnusedDefined:       ++CountUnused; break;
		case ETagToolboxAuditCategory::ReferencedUndefined: ++CountUndefined; break;
		case ETagToolboxAuditCategory::NearDuplicate:       ++CountNearDuplicate; break;
		case ETagToolboxAuditCategory::BrokenRedirect:      ++CountBroken; break;
		case ETagToolboxAuditCategory::LingeringRedirect:   ++CountLingering; break;
		default: break;
		}

		const bool bRowFails = CategoryFails(Row->Category, Options);
		bFailed |= bRowFails;

		TSharedPtr<FJsonObject> Finding = MakeShared<FJsonObject>();
		Finding->SetStringField(TEXT("category"), CategoryToString(Row->Category));
		Finding->SetStringField(TEXT("tag"), Row->Tag.ToString());
		Finding->SetStringField(TEXT("detail"), Row->Detail);
		Finding->SetBoolField(TEXT("fails"), bRowFails);
		if (!Row->RedirectTarget.IsNone())
		{
			Finding->SetStringField(TEXT("redirect_target"), Row->RedirectTarget.ToString());
		}
		if (Row->ReferencerPackages.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Referencers;
			for (const FName& PackageName : Row->ReferencerPackages)
			{
				Referencers.Add(MakeShared<FJsonValueString>(PackageName.ToString()));
			}
			Finding->SetArrayField(TEXT("referencers"), Referencers);
		}
		FindingValues.Add(MakeShared<FJsonValueObject>(Finding));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetStringField(TEXT("started_utc"), StartedUtcIso8601);
	Root->SetStringField(TEXT("finished_utc"), FinishedUtcIso8601);

	TSharedPtr<FJsonObject> Scope = MakeShared<FJsonObject>();
	Scope->SetBoolField(TEXT("include_engine"), Options.bIncludeEngineContent);
	Scope->SetBoolField(TEXT("include_plugins"), Options.bIncludePluginContent);
	Root->SetObjectField(TEXT("scope"), Scope);

	TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
	Counts->SetNumberField(TEXT("unused"), CountUnused);
	Counts->SetNumberField(TEXT("referenced_undefined"), CountUndefined);
	Counts->SetNumberField(TEXT("near_duplicate"), CountNearDuplicate);
	Counts->SetNumberField(TEXT("broken_redirect"), CountBroken);
	Counts->SetNumberField(TEXT("lingering_redirect"), CountLingering);
	Root->SetObjectField(TEXT("counts"), Counts);

	Root->SetArrayField(TEXT("findings"), FindingValues);
	Root->SetBoolField(TEXT("failed"), bFailed);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Json;
}

UTagToolboxTagAuditCommandlet::UTagToolboxTagAuditCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UTagToolboxTagAuditCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FTagToolboxAuditReportOptions Options;
	Options.bFailOnUnused = Switches.Contains(TEXT("FailOnUnused"));
	Options.bFailOnBrokenRedirects = Switches.Contains(TEXT("FailOnBrokenRedirects"));
	Options.bFailOnLingeringRedirects = Switches.Contains(TEXT("FailOnLingeringRedirects"));
	Options.bFailOnNearDuplicates = Switches.Contains(TEXT("FailOnNearDuplicates"));
	Options.bIncludeEngineContent = Switches.Contains(TEXT("IncludeEngineContent"));
	Options.bIncludePluginContent = Switches.Contains(TEXT("IncludePluginContent"));

	FString ReportPath = ParamsMap.FindRef(TEXT("ReportPath")).TrimQuotes();
	if (ReportPath.IsEmpty())
	{
		ReportPath = FPaths::ProjectSavedDir() / TEXT("TagToolbox/TagAuditReport.json");
	}

	const FString StartedUtc = FDateTime::UtcNow().ToIso8601();

	// The registry must be complete before the walk — commandlets do not get
	// the editor's background scan for free.
	UE_LOG(LogTagToolboxAuditCommandlet, Display, TEXT("Scanning the Asset Registry (synchronous)..."));
	IAssetRegistry::GetChecked().SearchAllAssets(/*bSynchronousSearch=*/true);

	// One implementation, two projections: the exact editor-audit engine.
	const TArray<TSharedPtr<FTagToolboxAuditRow>> Rows = FTagToolboxAudit::RunAudit(/*bAllowDialog=*/false);
	const TArray<TSharedPtr<FTagToolboxAuditRow>> Scoped = FTagToolboxAuditReportBuilder::ApplyScope(Rows, Options);

	const FString Json = FTagToolboxAuditReportBuilder::GenerateJson(Scoped, Options, StartedUtc, FDateTime::UtcNow().ToIso8601());

	// Temp file + atomic rename: the wrapper's freshness check must never see
	// a half-written report.
	const FString TempPath = ReportPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Json, *TempPath))
	{
		UE_LOG(LogTagToolboxAuditCommandlet, Error, TEXT("Could not write %s"), *TempPath);
		return 1;
	}
	IFileManager::Get().Delete(*ReportPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
	if (!IFileManager::Get().Move(*ReportPath, *TempPath))
	{
		UE_LOG(LogTagToolboxAuditCommandlet, Error, TEXT("Could not move report into place at %s"), *ReportPath);
		return 1;
	}

	UE_LOG(LogTagToolboxAuditCommandlet, Display, TEXT("Tag audit report written: %s (%d finding(s))"), *ReportPath, Scoped.Num());
	return 0;
}
