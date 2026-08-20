// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxAudit.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "TagToolboxAudit"

namespace TagToolboxAuditInternal
{
	static const FName GameplayTagsScriptPackage(TEXT("/Script/GameplayTags"));
	static const FName GameplayTagStructName(TEXT("GameplayTag"));

	static void SortNames(TArray<FName>& Names)
	{
		Names.Sort(FNameLexicalLess());
	}
}

void FTagToolboxAudit::ClassifyReferencedTags(
	const TSet<FName>& DefinedTags,
	const TSet<FName>& RedirectOldNames,
	const TSet<FName>& ReferencedTags,
	TArray<FName>& OutUndefined,
	TArray<FName>& OutLingeringRedirects)
{
	OutUndefined.Reset();
	OutLingeringRedirects.Reset();

	for (const FName& Referenced : ReferencedTags)
	{
		if (DefinedTags.Contains(Referenced))
		{
			continue;
		}
		if (RedirectOldNames.Contains(Referenced))
		{
			OutLingeringRedirects.Add(Referenced);
		}
		else
		{
			OutUndefined.Add(Referenced);
		}
	}

	TagToolboxAuditInternal::SortNames(OutUndefined);
	TagToolboxAuditInternal::SortNames(OutLingeringRedirects);
}

bool FTagToolboxAudit::AreNamesNearDuplicate(const FString& A, const FString& B)
{
	const FString LowerA = A.ToLower();
	const FString LowerB = B.ToLower();
	if (LowerA == LowerB)
	{
		// Distinct sibling FNames can never be case-only variants; equal
		// normalized names mean the same tag, not a near-duplicate.
		return false;
	}

	const int32 LenA = LowerA.Len();
	const int32 LenB = LowerB.Len();
	if (FMath::Abs(LenA - LenB) > 1)
	{
		return false;
	}

	if (LenA == LenB)
	{
		// Exactly one substitution.
		int32 Differences = 0;
		for (int32 Index = 0; Index < LenA; ++Index)
		{
			if (LowerA[Index] != LowerB[Index] && ++Differences > 1)
			{
				return false;
			}
		}
		return Differences == 1;
	}

	// Exactly one insertion/deletion.
	const FString& Shorter = (LenA < LenB) ? LowerA : LowerB;
	const FString& Longer = (LenA < LenB) ? LowerB : LowerA;
	int32 ShortIndex = 0;
	int32 LongIndex = 0;
	bool bSkippedOne = false;
	while (ShortIndex < Shorter.Len() && LongIndex < Longer.Len())
	{
		if (Shorter[ShortIndex] == Longer[LongIndex])
		{
			++ShortIndex;
			++LongIndex;
		}
		else
		{
			if (bSkippedOne)
			{
				return false;
			}
			bSkippedOne = true;
			++LongIndex;
		}
	}
	return true;
}

TArray<TSharedPtr<FTagToolboxAuditRow>> FTagToolboxAudit::RunAudit()
{
	using namespace TagToolboxAuditInternal;

	TArray<TSharedPtr<FTagToolboxAuditRow>> Rows;

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

	// --- Defined tags (full set, implicit parents included: a referenced
	// parent that exists only implicitly is not "undefined").
	TSet<FName> DefinedTags;
	{
		FGameplayTagContainer AllTags;
		Manager.RequestAllGameplayTags(AllTags, /*OnlyIncludeDictionaryTags=*/false);
		for (const FGameplayTag& Tag : AllTags)
		{
			DefinedTags.Add(Tag.GetTagName());
		}
	}

	// --- Redirects (default settings list; per-source tag lists with their own
	// redirects are a known v1 gap).
	TMap<FName, FName> RedirectOldToNew;
	for (const FGameplayTagRedirect& Redirect : GetDefault<UGameplayTagsSettings>()->GameplayTagRedirects)
	{
		RedirectOldToNew.Add(Redirect.OldTagName, Redirect.NewTagName);
	}
	TSet<FName> RedirectOldNames;
	RedirectOldToNew.GetKeys(RedirectOldNames);

	// --- Referenced tags from Asset Registry SearchableName dependencies.
	// Metadata only — no asset ever loads.
	TSet<FName> AllPackages;
	AssetRegistry.EnumerateAllAssets([&AllPackages](const FAssetData& AssetData)
	{
		AllPackages.Add(AssetData.PackageName);
		return true;
	});

	TMap<FName, TArray<FName>> ReferencedTagToPackages;
	{
		FScopedSlowTask SlowTask(static_cast<float>(AllPackages.Num()), LOCTEXT("ScanningPackages", "Tag Audit: scanning Asset Registry tag references..."));
		SlowTask.MakeDialog(/*bShowCancelButton=*/false);

		TArray<FAssetIdentifier> Dependencies;
		for (const FName& PackageName : AllPackages)
		{
			SlowTask.EnterProgressFrame(1.0f);

			Dependencies.Reset();
			AssetRegistry.GetDependencies(FAssetIdentifier(PackageName), Dependencies, UE::AssetRegistry::EDependencyCategory::SearchableName);
			for (const FAssetIdentifier& Dependency : Dependencies)
			{
				if (Dependency.PackageName == GameplayTagsScriptPackage && Dependency.ObjectName == GameplayTagStructName && !Dependency.ValueName.IsNone())
				{
					ReferencedTagToPackages.FindOrAdd(Dependency.ValueName).Add(PackageName);
				}
			}
		}
	}

	TSet<FName> ReferencedTags;
	ReferencedTagToPackages.GetKeys(ReferencedTags);

	// --- Unused defined tags: the engine's own scan (also covers config/INI
	// references and reverse redirectors).
	{
		TArray<TSharedPtr<FGameplayTagNode>> UnusedNodes;
		IGameplayTagsEditorModule::Get().GetUnusedGameplayTags(UnusedNodes);
		for (const TSharedPtr<FGameplayTagNode>& Node : UnusedNodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}
			TSharedPtr<FTagToolboxAuditRow> Row = MakeShared<FTagToolboxAuditRow>();
			Row->Category = ETagToolboxAuditCategory::UnusedDefined;
			Row->Tag = Node->GetCompleteTagName();
#if WITH_EDITORONLY_DATA
			Row->Detail = FString::Printf(TEXT("Defined in %s but not referenced by any saved content or config."), *Node->GetFirstSourceName().ToString());
#else
			Row->Detail = TEXT("Defined but not referenced by any saved content or config.");
#endif
			Rows.Add(MoveTemp(Row));
		}
	}

	// --- Referenced-but-undefined and lingering redirects.
	{
		TArray<FName> Undefined;
		TArray<FName> Lingering;
		ClassifyReferencedTags(DefinedTags, RedirectOldNames, ReferencedTags, Undefined, Lingering);

		for (const FName& TagName : Undefined)
		{
			TSharedPtr<FTagToolboxAuditRow> Row = MakeShared<FTagToolboxAuditRow>();
			Row->Category = ETagToolboxAuditCategory::ReferencedUndefined;
			Row->Tag = TagName;
			Row->ReferencerPackages = ReferencedTagToPackages.FindRef(TagName);
			TagToolboxAuditInternal::SortNames(Row->ReferencerPackages);
			Row->Detail = FString::Printf(TEXT("Referenced by %d package(s) but not defined and not redirected. Re-add the tag or author a GameplayTagRedirect."), Row->ReferencerPackages.Num());
			Rows.Add(MoveTemp(Row));
		}

		for (const FName& TagName : Lingering)
		{
			TSharedPtr<FTagToolboxAuditRow> Row = MakeShared<FTagToolboxAuditRow>();
			Row->Category = ETagToolboxAuditCategory::LingeringRedirect;
			Row->Tag = TagName;
			Row->ReferencerPackages = ReferencedTagToPackages.FindRef(TagName);
			TagToolboxAuditInternal::SortNames(Row->ReferencerPackages);
			Row->Detail = FString::Printf(TEXT("Old name still referenced by %d package(s); redirects to %s. Resave those packages to retire the redirect."),
				Row->ReferencerPackages.Num(), *RedirectOldToNew.FindRef(TagName).ToString());
			Rows.Add(MoveTemp(Row));
		}
	}

	// --- Broken redirects: the new name must exist.
	for (const TPair<FName, FName>& Redirect : RedirectOldToNew)
	{
		if (!Redirect.Value.IsNone() && !DefinedTags.Contains(Redirect.Value))
		{
			TSharedPtr<FTagToolboxAuditRow> Row = MakeShared<FTagToolboxAuditRow>();
			Row->Category = ETagToolboxAuditCategory::BrokenRedirect;
			Row->Tag = Redirect.Key;
			Row->Detail = FString::Printf(TEXT("GameplayTagRedirects entry points at undefined tag %s."), *Redirect.Value.ToString());
			Rows.Add(MoveTemp(Row));
		}
	}

	// --- Near-duplicate sibling leaf names (single-edit distance).
	{
		TArray<TSharedPtr<FGameplayTagNode>> RootNodes;
		Manager.GetFilteredGameplayRootTags(FString(), RootNodes);

		TArray<TSharedPtr<FGameplayTagNode>> Pending = RootNodes;
		while (Pending.Num() > 0)
		{
			const TSharedPtr<FGameplayTagNode> Node = Pending.Pop();
			if (!Node.IsValid())
			{
				continue;
			}

			// Compare this node's direct children pairwise, then recurse. Roots
			// themselves are compared as siblings of the implicit root below.
			const TArray<TSharedPtr<FGameplayTagNode>>& Children = Node->GetChildTagNodes();
			for (int32 First = 0; First < Children.Num(); ++First)
			{
				for (int32 Second = First + 1; Second < Children.Num(); ++Second)
				{
					if (Children[First].IsValid() && Children[Second].IsValid()
						&& AreNamesNearDuplicate(Children[First]->GetSimpleTagName().ToString(), Children[Second]->GetSimpleTagName().ToString()))
					{
						TSharedPtr<FTagToolboxAuditRow> Row = MakeShared<FTagToolboxAuditRow>();
						Row->Category = ETagToolboxAuditCategory::NearDuplicate;
						Row->Tag = Children[First]->GetCompleteTagName();
						Row->Detail = FString::Printf(TEXT("Sibling tags %s and %s differ by a single edit — possible typo."),
							*Children[First]->GetCompleteTagString(), *Children[Second]->GetCompleteTagString());
						Rows.Add(MoveTemp(Row));
					}
				}
			}
			Pending.Append(Children);
		}

		for (int32 First = 0; First < RootNodes.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < RootNodes.Num(); ++Second)
			{
				if (RootNodes[First].IsValid() && RootNodes[Second].IsValid()
					&& AreNamesNearDuplicate(RootNodes[First]->GetSimpleTagName().ToString(), RootNodes[Second]->GetSimpleTagName().ToString()))
				{
					TSharedPtr<FTagToolboxAuditRow> Row = MakeShared<FTagToolboxAuditRow>();
					Row->Category = ETagToolboxAuditCategory::NearDuplicate;
					Row->Tag = RootNodes[First]->GetCompleteTagName();
					Row->Detail = FString::Printf(TEXT("Root tags %s and %s differ by a single edit — possible typo."),
						*RootNodes[First]->GetCompleteTagString(), *RootNodes[Second]->GetCompleteTagString());
					Rows.Add(MoveTemp(Row));
				}
			}
		}
	}

	return Rows;
}

FText FTagToolboxAudit::GetCategoryDisplayText(ETagToolboxAuditCategory Category)
{
	switch (Category)
	{
	case ETagToolboxAuditCategory::UnusedDefined:        return LOCTEXT("CategoryUnused", "Unused");
	case ETagToolboxAuditCategory::ReferencedUndefined:  return LOCTEXT("CategoryUndefined", "Undefined");
	case ETagToolboxAuditCategory::NearDuplicate:        return LOCTEXT("CategoryNearDuplicate", "Near Duplicate");
	case ETagToolboxAuditCategory::BrokenRedirect:       return LOCTEXT("CategoryBrokenRedirect", "Broken Redirect");
	case ETagToolboxAuditCategory::LingeringRedirect:    return LOCTEXT("CategoryLingeringRedirect", "Lingering Redirect");
	default:                                             return FText::GetEmpty();
	}
}

FLinearColor FTagToolboxAudit::GetCategoryColor(ETagToolboxAuditCategory Category)
{
	switch (Category)
	{
	case ETagToolboxAuditCategory::UnusedDefined:        return FLinearColor(0.55f, 0.55f, 0.55f);
	case ETagToolboxAuditCategory::ReferencedUndefined:  return FLinearColor(0.90f, 0.25f, 0.25f);
	case ETagToolboxAuditCategory::NearDuplicate:        return FLinearColor(0.95f, 0.80f, 0.25f);
	case ETagToolboxAuditCategory::BrokenRedirect:       return FLinearColor(0.90f, 0.25f, 0.25f);
	case ETagToolboxAuditCategory::LingeringRedirect:    return FLinearColor(0.95f, 0.55f, 0.20f);
	default:                                             return FLinearColor::White;
	}
}

#undef LOCTEXT_NAMESPACE
