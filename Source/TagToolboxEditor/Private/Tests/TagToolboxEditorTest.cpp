// Copyright Infinite Game Works. All Rights Reserved.

#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "Misc/AutomationTest.h"
#include "TagToolboxAudit.h"
#include "TagToolboxCommentTint.h"
#include "TagToolboxSettings.h"
#include "TagToolboxTagScanService.h"
#include "TagToolboxVariableFilterCustomization.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// Unity-build rule: every helper in this file carries the TagToolboxTest_ prefix.

namespace
{
	FGameplayTag TagToolboxTest_RequestTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxStyleResolveFallUpTest,
	"TagToolbox.Styles.ResolveFallUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxStyleResolveFallUpTest::RunTest(const FString& Parameters)
{
	// Host-registered tags: the ancestor walk needs a real registered hierarchy.
	const FGameplayTag Animation = TagToolboxTest_RequestTag(TEXT("Paper2DPlus.Animation"));
	const FGameplayTag Combat = TagToolboxTest_RequestTag(TEXT("Paper2DPlus.Animation.Combat"));
	const FGameplayTag Heavy = TagToolboxTest_RequestTag(TEXT("Paper2DPlus.Animation.Combat.Heavy"));
	if (!Animation.IsValid() || !Combat.IsValid() || !Heavy.IsValid())
	{
		AddInfo(TEXT("Skipping: the Paper2DPlus.Animation tag hierarchy is not registered in this host project."));
		return true;
	}

	const FLinearColor Red(1.0f, 0.0f, 0.0f);
	const FLinearColor Green(0.0f, 1.0f, 0.0f);
	const FLinearColor Blue(0.0f, 0.0f, 1.0f);

	TArray<FTagToolboxTagStyle> Styles;
	{
		FTagToolboxTagStyle& AnimationStyle = Styles.AddDefaulted_GetRef();
		AnimationStyle.Tag = Animation;
		AnimationStyle.Color = Red;
	}
	{
		FTagToolboxTagStyle& CombatStyle = Styles.AddDefaulted_GetRef();
		CombatStyle.Tag = Combat;
		CombatStyle.Color = Green;
	}

	FLinearColor Resolved;
	TestTrue(TEXT("Exact entry resolves"), UTagToolboxSettings::ResolveTagColorFromArray(Styles, Combat, Resolved));
	TestEqual(TEXT("Exact entry wins over ancestor"), Resolved, Green);

	TestTrue(TEXT("Child falls up to nearest styled ancestor"), UTagToolboxSettings::ResolveTagColorFromArray(Styles, Heavy, Resolved));
	TestEqual(TEXT("Nearest ancestor beats farther ancestor"), Resolved, Green);

	TestTrue(TEXT("Root entry resolves"), UTagToolboxSettings::ResolveTagColorFromArray(Styles, Animation, Resolved));
	TestEqual(TEXT("Root entry color"), Resolved, Red);

	// First registered entry wins among duplicates at the same level.
	{
		FTagToolboxTagStyle& DuplicateCombat = Styles.AddDefaulted_GetRef();
		DuplicateCombat.Tag = Combat;
		DuplicateCombat.Color = Blue;
	}
	TestTrue(TEXT("Duplicate entries still resolve"), UTagToolboxSettings::ResolveTagColorFromArray(Styles, Combat, Resolved));
	TestEqual(TEXT("First duplicate entry wins"), Resolved, Green);

	TestFalse(TEXT("Empty style list resolves nothing"), UTagToolboxSettings::ResolveTagColorFromArray(TArray<FTagToolboxTagStyle>(), Heavy, Resolved));
	TestFalse(TEXT("Invalid tag resolves nothing"), UTagToolboxSettings::ResolveTagColorFromArray(Styles, FGameplayTag(), Resolved));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxAuditClassifyTest,
	"TagToolbox.Audit.ClassifyReferencedTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxAuditClassifyTest::RunTest(const FString& Parameters)
{
	const TSet<FName> Defined = { FName(TEXT("Combat.Heavy")), FName(TEXT("Combat.Light")) };
	const TSet<FName> RedirectOldNames = { FName(TEXT("Combat.Old")) };
	const TSet<FName> Referenced = {
		FName(TEXT("Combat.Heavy")),      // defined → no finding
		FName(TEXT("Combat.Old")),        // redirected old name → lingering
		FName(TEXT("Zombie.Ghost")),      // gone → undefined
		FName(TEXT("Aardvark.Ghost")),    // gone → undefined (sorts first)
	};

	TArray<FName> Undefined;
	TArray<FName> Lingering;
	FTagToolboxAudit::ClassifyReferencedTags(Defined, RedirectOldNames, Referenced, Undefined, Lingering);

	TestEqual(TEXT("Two undefined tags"), Undefined.Num(), 2);
	if (Undefined.Num() == 2)
	{
		TestEqual(TEXT("Undefined output is sorted"), Undefined[0], FName(TEXT("Aardvark.Ghost")));
		TestEqual(TEXT("Undefined second entry"), Undefined[1], FName(TEXT("Zombie.Ghost")));
	}

	TestEqual(TEXT("One lingering redirect"), Lingering.Num(), 1);
	if (Lingering.Num() == 1)
	{
		TestEqual(TEXT("Lingering entry is the redirected old name"), Lingering[0], FName(TEXT("Combat.Old")));
	}

	// Defined and redirected names never land in undefined.
	TestFalse(TEXT("Defined tag is not undefined"), Undefined.Contains(FName(TEXT("Combat.Heavy"))));
	TestFalse(TEXT("Redirected old name is not undefined"), Undefined.Contains(FName(TEXT("Combat.Old"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxAuditNearDuplicateTest,
	"TagToolbox.Audit.NearDuplicateNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxAuditNearDuplicateTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Single deletion"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Attack"), TEXT("Atack")));
	TestTrue(TEXT("Single insertion at end"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Attack"), TEXT("AttackX")));
	TestTrue(TEXT("Single substitution"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Attack"), TEXT("Uttack")));
	TestTrue(TEXT("Trailing digit variant"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Idle"), TEXT("Idle2")));
	TestTrue(TEXT("Vowel swap"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Run"), TEXT("Ran")));

	TestFalse(TEXT("Identical names are not near-duplicates"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Attack"), TEXT("Attack")));
	TestFalse(TEXT("Case-only variants are the same FName, not near-duplicates"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Attack"), TEXT("attack")));
	TestFalse(TEXT("Unrelated names"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Attack"), TEXT("Block")));
	TestFalse(TEXT("Two edits apart"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("AB"), TEXT("BA")));
	TestFalse(TEXT("Length difference above one"), FTagToolboxAudit::AreNamesNearDuplicate(TEXT("Idle"), TEXT("IdleLoop")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxVariableFlavorTest,
	"TagToolbox.VariableFilter.IsTagFlavoredVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxVariableFlavorTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Null property is not tag-flavored"), FTagToolboxVariableFilterCustomization::IsTagFlavoredVariable(nullptr));

	// FTagToolboxTagStyle::Tag — a plain FGameplayTag struct property.
	const FProperty* TagProperty = FindFProperty<FProperty>(FTagToolboxTagStyle::StaticStruct(), TEXT("Tag"));
	TestNotNull(TEXT("Found FTagToolboxTagStyle::Tag"), TagProperty);
	if (TagProperty)
	{
		TestTrue(TEXT("Plain FGameplayTag is tag-flavored"), FTagToolboxVariableFilterCustomization::IsTagFlavoredVariable(TagProperty));
	}

	// FGameplayTagContainer::GameplayTags — a TArray<FGameplayTag>.
	const FProperty* ArrayOfTags = FindFProperty<FProperty>(FGameplayTagContainer::StaticStruct(), TEXT("GameplayTags"));
	TestNotNull(TEXT("Found FGameplayTagContainer::GameplayTags"), ArrayOfTags);
	if (ArrayOfTags)
	{
		TestTrue(TEXT("Array of FGameplayTag is tag-flavored"), FTagToolboxVariableFilterCustomization::IsTagFlavoredVariable(ArrayOfTags));
	}

	// FTagToolboxTagStyle::Color — an ordinary struct property.
	const FProperty* ColorProperty = FindFProperty<FProperty>(FTagToolboxTagStyle::StaticStruct(), TEXT("Color"));
	TestNotNull(TEXT("Found FTagToolboxTagStyle::Color"), ColorProperty);
	if (ColorProperty)
	{
		TestFalse(TEXT("Non-tag struct is not tag-flavored"), FTagToolboxVariableFilterCustomization::IsTagFlavoredVariable(ColorProperty));
	}

	// UTagToolboxSettings::TagStyles — an array of a NON-tag struct.
	const FProperty* StylesArray = FindFProperty<FProperty>(UTagToolboxSettings::StaticClass(), TEXT("TagStyles"));
	TestNotNull(TEXT("Found UTagToolboxSettings::TagStyles"), StylesArray);
	if (StylesArray)
	{
		TestFalse(TEXT("Array of non-tag struct is not tag-flavored"), FTagToolboxVariableFilterCustomization::IsTagFlavoredVariable(StylesArray));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxCommentTokenTest,
	"TagToolbox.CommentTint.ExtractTagTokenCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxCommentTokenTest::RunTest(const FString& Parameters)
{
	{
		const TArray<FString> Tokens = TagToolboxCommentTint::ExtractTagTokenCandidates(TEXT("Handles #Team here"));
		TestEqual(TEXT("Single token count"), Tokens.Num(), 1);
		if (Tokens.Num() == 1)
		{
			TestEqual(TEXT("Single token value"), Tokens[0], FString(TEXT("Team")));
		}
	}
	{
		const TArray<FString> Tokens = TagToolboxCommentTint::ExtractTagTokenCandidates(TEXT("fix #1 then #Combat.Heavy."));
		TestEqual(TEXT("Two tokens in order"), Tokens.Num(), 2);
		if (Tokens.Num() == 2)
		{
			TestEqual(TEXT("Non-tag token still listed (registration is the caller's check)"), Tokens[0], FString(TEXT("1")));
			TestEqual(TEXT("Trailing dot trimmed"), Tokens[1], FString(TEXT("Combat.Heavy")));
		}
	}
	{
		TestEqual(TEXT("No tokens in plain text"), TagToolboxCommentTint::ExtractTagTokenCandidates(TEXT("no tokens here")).Num(), 0);
		TestEqual(TEXT("Bare hash yields nothing"), TagToolboxCommentTint::ExtractTagTokenCandidates(TEXT("just a # sign")).Num(), 0);
	}
	{
		const TArray<FString> Tokens = TagToolboxCommentTint::ExtractTagTokenCandidates(TEXT("##Double"));
		TestEqual(TEXT("Empty token skipped, next hash read"), Tokens.Num(), 1);
		if (Tokens.Num() == 1)
		{
			TestEqual(TEXT("Double-hash token value"), Tokens[0], FString(TEXT("Double")));
		}
	}
	{
		const TArray<FString> Tokens = TagToolboxCommentTint::ExtractTagTokenCandidates(TEXT("(#A.B) and #C_1"));
		TestEqual(TEXT("Punctuation-delimited tokens"), Tokens.Num(), 2);
		if (Tokens.Num() == 2)
		{
			TestEqual(TEXT("Dotted token"), Tokens[0], FString(TEXT("A.B")));
			TestEqual(TEXT("Underscored token"), Tokens[1], FString(TEXT("C_1")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxScanRedirectAggregationTest,
	"TagToolbox.ScanService.RedirectAggregation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxScanRedirectAggregationTest::RunTest(const FString& Parameters)
{
	// Two transient source lists standing in for DefaultGameplayTags.ini and a
	// Config/Tags list — the aggregation must see BOTH and each record must
	// resolve its owning list (the v0.1 settings-only read was the blind spot).
	UGameplayTagsList* SettingsLikeList = NewObject<UGameplayTagsList>(GetTransientPackage());
	UGameplayTagsList* SecondaryList = NewObject<UGameplayTagsList>(GetTransientPackage());

	{
		FGameplayTagRedirect& Redirect = SettingsLikeList->GameplayTagRedirects.AddDefaulted_GetRef();
		Redirect.OldTagName = FName(TEXT("Combat.OldSettings"));
		Redirect.NewTagName = FName(TEXT("Combat.NewSettings"));
	}
	{
		FGameplayTagRedirect& Redirect = SecondaryList->GameplayTagRedirects.AddDefaulted_GetRef();
		Redirect.OldTagName = FName(TEXT("Combat.OldSecondary"));
		Redirect.NewTagName = FName(TEXT("Combat.NewSecondary"));
	}

	const FName SettingsSourceName(TEXT("DefaultGameplayTags.ini"));
	const FName SecondarySourceName(TEXT("ProbeTags.ini"));

	TArray<FTagToolboxRedirectRecord> Records;
	FTagToolboxTagScanService::CollectRedirectsFromList(SettingsSourceName, SettingsLikeList, Records);
	FTagToolboxTagScanService::CollectRedirectsFromList(SecondarySourceName, SecondaryList, Records);

	TestEqual(TEXT("Both lists contribute records"), Records.Num(), 2);
	if (Records.Num() == 2)
	{
		TestEqual(TEXT("Settings record old name"), Records[0].OldTagName, FName(TEXT("Combat.OldSettings")));
		TestEqual(TEXT("Settings record owning source"), Records[0].OwningSourceName, SettingsSourceName);
		TestTrue(TEXT("Settings record resolves owning list"), Records[0].OwningList.Get() == SettingsLikeList);

		TestEqual(TEXT("Secondary record old name"), Records[1].OldTagName, FName(TEXT("Combat.OldSecondary")));
		TestEqual(TEXT("Secondary record owning source"), Records[1].OwningSourceName, SecondarySourceName);
		TestTrue(TEXT("Secondary record resolves owning list"), Records[1].OwningList.Get() == SecondaryList);
	}

	// Settings-only fixture: aggregated old-name set matches the v0.1 read
	// exactly, so single-source projects classify byte-identically.
	TArray<FTagToolboxRedirectRecord> SettingsOnlyRecords;
	FTagToolboxTagScanService::CollectRedirectsFromList(SettingsSourceName, SettingsLikeList, SettingsOnlyRecords);
	TestEqual(TEXT("Settings-only fixture record count"), SettingsOnlyRecords.Num(), 1);
	if (SettingsOnlyRecords.Num() == 1)
	{
		TestEqual(TEXT("Settings-only old name unchanged"), SettingsOnlyRecords[0].OldTagName, FName(TEXT("Combat.OldSettings")));
	}

	// Null list contributes nothing rather than crashing.
	TArray<FTagToolboxRedirectRecord> NullRecords;
	FTagToolboxTagScanService::CollectRedirectsFromList(SettingsSourceName, nullptr, NullRecords);
	TestEqual(TEXT("Null list contributes nothing"), NullRecords.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxScanExpandSubtreeTest,
	"TagToolbox.ScanService.ExpandSubtreeNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxScanExpandSubtreeTest::RunTest(const FString& Parameters)
{
	TSet<FName> Snapshot = {
		FName(TEXT("Combat")),
		FName(TEXT("Combat.Heavy")),
		FName(TEXT("Combat.Heavy.Spin")),
		FName(TEXT("Combat.Light")),
		FName(TEXT("CombatArena")),   // shares the prefix string but not the subtree
		FName(TEXT("Movement.Run")),
	};

	{
		const TArray<FName> Subtree = FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, FName(TEXT("Combat")));
		TestEqual(TEXT("Subtree has parent plus descendants"), Subtree.Num(), 4);
		if (Subtree.Num() == 4)
		{
			TestEqual(TEXT("Sorted first"), Subtree[0], FName(TEXT("Combat")));
			TestEqual(TEXT("Sorted second"), Subtree[1], FName(TEXT("Combat.Heavy")));
			TestEqual(TEXT("Sorted third"), Subtree[2], FName(TEXT("Combat.Heavy.Spin")));
			TestEqual(TEXT("Sorted fourth"), Subtree[3], FName(TEXT("Combat.Light")));
		}
		TestFalse(TEXT("Prefix-similar sibling excluded"), Subtree.Contains(FName(TEXT("CombatArena"))));
	}

	{
		const TArray<FName> Leaf = FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, FName(TEXT("Movement.Run")));
		TestEqual(TEXT("Leaf expands to itself"), Leaf.Num(), 1);
	}

	{
		const TArray<FName> Missing = FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, FName(TEXT("Absent")));
		TestEqual(TEXT("Absent parent expands to nothing"), Missing.Num(), 0);
		TestEqual(TEXT("None expands to nothing"), FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, NAME_None).Num(), 0);
	}

	// The helper operates on the supplied snapshot only: mutating the table
	// afterwards cannot change an already-computed expansion, and re-running
	// against the ORIGINAL snapshot still yields the original answer.
	const TArray<FName> Before = FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, FName(TEXT("Combat")));
	TSet<FName> MutatedTable = Snapshot;
	MutatedTable.Add(FName(TEXT("Combat.New")));
	const TArray<FName> FromSnapshotAgain = FTagToolboxTagScanService::ExpandSubtreeNames(Snapshot, FName(TEXT("Combat")));
	TestEqual(TEXT("Snapshot expansion unaffected by post-snapshot table changes"), FromSnapshotAgain, Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxScanCacheStateTest,
	"TagToolbox.ScanService.CacheStateTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxScanCacheStateTest::RunTest(const FString& Parameters)
{
	FTagToolboxTagScanService& Service = FTagToolboxTagScanService::Get();

	int32 BroadcastCount = 0;
	FDelegateHandle Handle = Service.OnScanStateChanged.AddLambda([&BroadcastCount]() { ++BroadcastCount; });

	TMap<FName, TArray<FName>> Fake;
	Fake.Add(FName(TEXT("Combat.Heavy")), { FName(TEXT("/Game/A")), FName(TEXT("/Game/B")) });
	Service.SetCacheForTesting(MoveTemp(Fake));

	TestEqual(TEXT("Scan leaves the cache Fresh"), static_cast<int32>(Service.GetState()), static_cast<int32>(ETagToolboxScanState::Fresh));
	TestEqual(TEXT("Fresh count reads from the cache"), Service.GetExactUsageCount(FName(TEXT("Combat.Heavy"))), 2);
	TestEqual(TEXT("Scanned-but-absent tag counts zero"), Service.GetExactUsageCount(FName(TEXT("Combat.Absent"))), 0);
	TestEqual(TEXT("Scan broadcast fired"), BroadcastCount, 1);

	Service.MarkStale();
	TestEqual(TEXT("Invalidation flips Fresh to Stale"), static_cast<int32>(Service.GetState()), static_cast<int32>(ETagToolboxScanState::Stale));
	TestEqual(TEXT("Stale keeps the cached count — no auto-rescan, no zeroing"), Service.GetExactUsageCount(FName(TEXT("Combat.Heavy"))), 2);
	TestEqual(TEXT("Stale broadcast fired"), BroadcastCount, 2);

	Service.MarkStale();
	TestEqual(TEXT("Repeat invalidation is a no-op"), BroadcastCount, 2);
	TestEqual(TEXT("State stays Stale"), static_cast<int32>(Service.GetState()), static_cast<int32>(ETagToolboxScanState::Stale));

	Service.OnScanStateChanged.Remove(Handle);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
