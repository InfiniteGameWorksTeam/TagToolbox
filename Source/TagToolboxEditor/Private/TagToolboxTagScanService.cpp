// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxTagScanService.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "Misc/EnumRange.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "TagToolboxTagScanService"

namespace TagToolboxScanInternal
{
	static const FName GameplayTagsScriptPackage(TEXT("/Script/GameplayTags"));
	static const FName GameplayTagStructName(TEXT("GameplayTag"));

	static TUniquePtr<FTagToolboxTagScanService> GInstance;
}

FTagToolboxTagScanService& FTagToolboxTagScanService::Get()
{
	if (!TagToolboxScanInternal::GInstance)
	{
		TagToolboxScanInternal::GInstance = TUniquePtr<FTagToolboxTagScanService>(new FTagToolboxTagScanService());
	}
	return *TagToolboxScanInternal::GInstance;
}

void FTagToolboxTagScanService::Shutdown()
{
	TagToolboxScanInternal::GInstance.Reset();
}

FTagToolboxTagScanService::FTagToolboxTagScanService()
{
	TagTreeChangedHandle = UGameplayTagsManager::OnEditorRefreshGameplayTagTree.AddRaw(this, &FTagToolboxTagScanService::HandleTagTreeChanged);
	PackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FTagToolboxTagScanService::HandlePackageSaved);
}

FTagToolboxTagScanService::~FTagToolboxTagScanService()
{
	UGameplayTagsManager::OnEditorRefreshGameplayTagTree.Remove(TagTreeChangedHandle);
	UPackage::PackageSavedWithContextEvent.Remove(PackageSavedHandle);
}

void FTagToolboxTagScanService::HandleTagTreeChanged()
{
	MarkStale();
}

void FTagToolboxTagScanService::HandlePackageSaved(const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext Context)
{
	// Procedural/no-op saves still change what the Asset Registry will report,
	// so any package save marks the usage picture stale.
	MarkStale();
}

void FTagToolboxTagScanService::MarkStale()
{
	if (State == ETagToolboxScanState::Fresh)
	{
		State = ETagToolboxScanState::Stale;
		OnScanStateChanged.Broadcast();
	}
}

void FTagToolboxTagScanService::RunScan(bool bAllowDialog)
{
	using namespace TagToolboxScanInternal;

	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

	TSet<FName> AllPackages;
	AssetRegistry.EnumerateAllAssets([&AllPackages](const FAssetData& AssetData)
	{
		AllPackages.Add(AssetData.PackageName);
		return true;
	});

	TMap<FName, TArray<FName>> NewMap;
	{
		FScopedSlowTask SlowTask(static_cast<float>(AllPackages.Num()), LOCTEXT("ScanningPackages", "Tag Toolbox: scanning Asset Registry tag references..."));
		if (bAllowDialog)
		{
			SlowTask.MakeDialog(/*bShowCancelButton=*/false);
		}

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
					NewMap.FindOrAdd(Dependency.ValueName).Add(PackageName);
				}
			}
		}
	}

	for (TPair<FName, TArray<FName>>& Pair : NewMap)
	{
		Pair.Value.Sort(FNameLexicalLess());
	}

	ReferencedTagToPackages = MoveTemp(NewMap);
	State = ETagToolboxScanState::Fresh;
	OnScanStateChanged.Broadcast();
}

TArray<FName> FTagToolboxTagScanService::GetReferencersForExactTag(FName TagName) const
{
	return ReferencedTagToPackages.FindRef(TagName);
}

int32 FTagToolboxTagScanService::GetExactUsageCount(FName TagName) const
{
	const TArray<FName>* Found = ReferencedTagToPackages.Find(TagName);
	return Found ? Found->Num() : 0;
}

void FTagToolboxTagScanService::SetCacheForTesting(TMap<FName, TArray<FName>> InReferencedTagToPackages)
{
	ReferencedTagToPackages = MoveTemp(InReferencedTagToPackages);
	State = ETagToolboxScanState::Fresh;
	OnScanStateChanged.Broadcast();
}

void FTagToolboxTagScanService::CollectRedirectsFromList(FName SourceName, UGameplayTagsList* List, TArray<FTagToolboxRedirectRecord>& Out)
{
	if (!List)
	{
		return;
	}
	for (const FGameplayTagRedirect& Redirect : List->GameplayTagRedirects)
	{
		FTagToolboxRedirectRecord& Record = Out.AddDefaulted_GetRef();
		Record.OldTagName = Redirect.OldTagName;
		Record.NewTagName = Redirect.NewTagName;
		Record.OwningSourceName = SourceName;
		Record.OwningList = List;
	}
}

TArray<FTagToolboxRedirectRecord> FTagToolboxTagScanService::CollectAllRedirects()
{
	TArray<FTagToolboxRedirectRecord> Records;
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	// The engine writes rename redirects into the OLD tag's owning source list,
	// so aggregation must cover every list-backed source — the default settings
	// list is just one of them. (Reading only UGameplayTagsSettings was the
	// v0.1 blind spot.)
	TArray<const FGameplayTagSource*> Sources;
	Manager.FindTagSourcesWithType(EGameplayTagSourceType::DefaultTagList, Sources);
	Manager.FindTagSourcesWithType(EGameplayTagSourceType::TagList, Sources);

	TSet<UGameplayTagsList*> SeenLists;
	for (const FGameplayTagSource* Source : Sources)
	{
		if (Source && Source->SourceTagList && !SeenLists.Contains(Source->SourceTagList))
		{
			SeenLists.Add(Source->SourceTagList);
			CollectRedirectsFromList(Source->SourceName, Source->SourceTagList, Records);
		}
	}

	// The default settings object exists even before the manager registers a
	// DefaultTagList source (e.g. no tags authored yet) — make sure it is
	// always represented.
	UGameplayTagsSettings* MutableSettings = GetMutableDefault<UGameplayTagsSettings>();
	if (!SeenLists.Contains(MutableSettings))
	{
		CollectRedirectsFromList(FGameplayTagSource::GetDefaultName(), MutableSettings, Records);
	}

	return Records;
}

TArray<FName> FTagToolboxTagScanService::ExpandSubtreeNames(const TSet<FName>& TagTableSnapshot, FName ParentTag)
{
	TArray<FName> Result;
	if (ParentTag.IsNone())
	{
		return Result;
	}

	const FString ParentString = ParentTag.ToString();
	const FString ChildPrefix = ParentString + TEXT(".");
	for (const FName& Name : TagTableSnapshot)
	{
		const FString NameString = Name.ToString();
		if (NameString.Equals(ParentString, ESearchCase::IgnoreCase) || NameString.StartsWith(ChildPrefix, ESearchCase::IgnoreCase))
		{
			Result.Add(Name);
		}
	}
	Result.Sort(FNameLexicalLess());
	return Result;
}

#undef LOCTEXT_NAMESPACE
