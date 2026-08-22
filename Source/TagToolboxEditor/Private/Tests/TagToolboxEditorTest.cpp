// Copyright Infinite Game Works. All Rights Reserved.

#include "GameplayTagContainer.h"
#include "GameplayTagsSettings.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "STagToolboxTagPicker.h"
#include "TagToolboxAudit.h"
#include "TagToolboxRenameFixup.h"
#include "TagToolboxResaveService.h"
#include "TagToolboxTagAuditCommandlet.h"
#include "TagToolboxCommentTint.h"
#include "TagToolboxSettings.h"
#include "TagToolboxTagClipboard.h"
#include "TagToolboxTagScanService.h"
#include "TagToolboxUndo.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "TagToolboxVariableFilterCustomization.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// Unity-build rule: every helper in this file carries the TagToolboxTest_ prefix.

struct FTagToolboxTestTagHierarchy
{
	FNativeGameplayTag AnimationNative {
		UE_PLUGIN_NAME, FName(TEXT("TagToolbox")), TEXT("TagToolbox.Test.Animation"),
		TEXT("TagToolbox automation fixture"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD };
	FNativeGameplayTag CombatNative {
		UE_PLUGIN_NAME, FName(TEXT("TagToolbox")), TEXT("TagToolbox.Test.Animation.Combat"),
		TEXT("TagToolbox automation fixture"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD };
	FNativeGameplayTag HeavyNative {
		UE_PLUGIN_NAME, FName(TEXT("TagToolbox")), TEXT("TagToolbox.Test.Animation.Combat.Heavy"),
		TEXT("TagToolbox automation fixture"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD };

	FGameplayTag Animation() const { return AnimationNative.GetTag(); }
	FGameplayTag Combat() const { return CombatNative.GetTag(); }
	FGameplayTag Heavy() const { return HeavyNative.GetTag(); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxStyleResolveFallUpTest,
	"TagToolbox.Styles.ResolveFallUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxStyleResolveFallUpTest::RunTest(const FString& Parameters)
{
	// FNativeGameplayTag unregisters on scope exit, so this test neither depends on host vocabulary nor
	// leaves automation-only tags in a long-lived editor process.
	const FTagToolboxTestTagHierarchy Tags;
	const FGameplayTag Animation = Tags.Animation();
	const FGameplayTag Combat = Tags.Combat();
	const FGameplayTag Heavy = Tags.Heavy();
	if (!TestTrue(TEXT("automation-local tag hierarchy registered successfully"),
		Animation.IsValid() && Combat.IsValid() && Heavy.IsValid()))
	{
		return false;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxVariableFilterUndoTest,
	"TagToolbox.Undo.VariableFilterRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxVariableFilterUndoTest::RunTest(const FString& Parameters)
{
	// Undo is the caller-visible contract: the engine metadata setter records nothing on its own,
	// so this test fails if SetVariableCategories stops transacting the Blueprint.
	if (!TestTrue(TEXT("editor transaction buffer required"), GEditor != nullptr && GEditor->Trans != nullptr))
	{
		return false;
	}

	UPackage* Package = GetTransientPackage();
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("TagToolboxTest_UndoBP")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("transient Blueprint created"), Blueprint))
	{
		return false;
	}

	FEdGraphPinType TagPinType;
	TagPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	TagPinType.PinSubCategoryObject = FGameplayTag::StaticStruct();
	const FName VarName(TEXT("TestTag"));
	if (!TestTrue(TEXT("member variable added"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, TagPinType)))
	{
		return false;
	}

	auto ReadCategories = [&]() -> FString
	{
		FString Value;
		FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VarName, nullptr, TEXT("Categories"), Value);
		return Value;
	};

	// Two committed edits: the undo stack must peel them back one at a time.
	TagToolboxUndo::SetVariableCategories(Blueprint, VarName, nullptr, TEXT("TagToolbox.Test.Animation"));
	TestEqual(TEXT("first filter written"), ReadCategories(), FString(TEXT("TagToolbox.Test.Animation")));

	TagToolboxUndo::SetVariableCategories(Blueprint, VarName, nullptr, TEXT("TagToolbox.Test.Animation.Combat"));
	TestEqual(TEXT("second filter written"), ReadCategories(), FString(TEXT("TagToolbox.Test.Animation.Combat")));

	TestTrue(TEXT("undo #1 succeeds"), GEditor->UndoTransaction());
	TestEqual(TEXT("undo restores the first filter"), ReadCategories(), FString(TEXT("TagToolbox.Test.Animation")));

	TestTrue(TEXT("undo #2 succeeds"), GEditor->UndoTransaction());
	TestEqual(TEXT("undo restores no filter"), ReadCategories(), FString());

	TestTrue(TEXT("redo succeeds"), GEditor->RedoTransaction());
	TestEqual(TEXT("redo reapplies the first filter"), ReadCategories(), FString(TEXT("TagToolbox.Test.Animation")));

	// Clearing is its own undoable step.
	TagToolboxUndo::SetVariableCategories(Blueprint, VarName, nullptr, FString());
	TestEqual(TEXT("clear removes the filter"), ReadCategories(), FString());
	TestTrue(TEXT("undo of clear succeeds"), GEditor->UndoTransaction());
	TestEqual(TEXT("undo of clear restores the filter"), ReadCategories(), FString(TEXT("TagToolbox.Test.Animation")));

	// The undo client keys on exact contexts so unrelated transactions never trigger a skeleton regen.
	TestTrue(TEXT("filter context recognized"), TagToolboxUndo::IsVariableFilterTransaction(TEXT("TagToolbox.VariableFilter")));
	TestFalse(TEXT("style context is not a filter context"), TagToolboxUndo::IsVariableFilterTransaction(TEXT("TagToolbox.TagStyle")));
	TestFalse(TEXT("foreign context ignored"), TagToolboxUndo::IsTagStyleTransaction(TEXT("")));

	// Leave the transaction buffer as found for whoever runs next.
	GEditor->Trans->Reset(FText::FromString(TEXT("TagToolbox undo test cleanup")));
	Blueprint->MarkAsGarbage();
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

namespace
{
	// Convenience wrapper: flags default to the common "tag does not exist,
	// creation available" case so each scenario names only what it varies.
	FTagToolboxCreateRowPlan TagToolboxTest_Plan(
		const FString& SearchText,
		const FString& RootFilter = FString(),
		bool bTagExists = false,
		bool bExistingVisibleInView = false,
		bool bExistingAllowedByFilter = false,
		bool bCanCreateTags = true,
		bool bTagSourcesWritable = true,
		bool bCreateInFlight = false,
		const FString& ExistingResolvedName = FString())
	{
		return STagToolboxTagPicker::BuildCreateRowPlan(SearchText, RootFilter, bTagExists, bExistingVisibleInView,
			bExistingAllowedByFilter, bCanCreateTags, bTagSourcesWritable, bCreateInFlight, ExistingResolvedName);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxCreateRowPlanTest,
	"TagToolbox.Picker.CreateRowPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxCreateRowPlanTest::RunTest(const FString& Parameters)
{
	// Nothing typed — no row, no explanation.
	TestEqual(TEXT("Empty search hides the row"), static_cast<int32>(TagToolboxTest_Plan(TEXT("")).Mode), static_cast<int32>(ETagToolboxCreateRowMode::Hidden));
	TestEqual(TEXT("Whitespace-only search hides the row"), static_cast<int32>(TagToolboxTest_Plan(TEXT("   ")).Mode), static_cast<int32>(ETagToolboxCreateRowMode::Hidden));

	// Existing-tag states.
	TestEqual(TEXT("Visible duplicate hides the row"),
		static_cast<int32>(TagToolboxTest_Plan(TEXT("Combat.Slam"), TEXT(""), /*exists*/true, /*visible*/true, /*allowed*/true).Mode),
		static_cast<int32>(ETagToolboxCreateRowMode::Hidden));
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Combat.Slam"), TEXT(""), /*exists*/true, /*visible*/false, /*allowed*/true, true, true, false, TEXT("Combat.Slam"));
		TestEqual(TEXT("Hidden-but-allowed duplicate offers reveal"), static_cast<int32>(Plan.Mode), static_cast<int32>(ETagToolboxCreateRowMode::ExistsHidden));
		TestEqual(TEXT("Reveal targets the resolved name"), Plan.FinalTagString, FString(TEXT("Combat.Slam")));
	}
	{
		// A typed OLD name resolving through a redirect reveals the TARGET.
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Combat.OldName"), TEXT(""), true, false, true, true, true, false, TEXT("Combat.NewName"));
		TestEqual(TEXT("Redirected duplicate reveals the target name"), Plan.FinalTagString, FString(TEXT("Combat.NewName")));
	}
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Movement.Dash"), TEXT("Combat"), /*exists*/true, /*visible*/false, /*allowed*/false, true, true, false, TEXT("Movement.Dash"));
		TestEqual(TEXT("Filter-blocked duplicate is non-actionable"), static_cast<int32>(Plan.Mode), static_cast<int32>(ETagToolboxCreateRowMode::ExistsBlockedByFilter));
		TestFalse(TEXT("Blocked state carries a reason"), Plan.Reason.IsEmpty());
	}

	// Creation gates.
	TestEqual(TEXT("Read-only property hides the offer"),
		static_cast<int32>(TagToolboxTest_Plan(TEXT("Combat.New"), TEXT(""), false, false, false, /*bCanCreateTags=*/false).Mode),
		static_cast<int32>(ETagToolboxCreateRowMode::Hidden));
	TestEqual(TEXT("Unwritable tag sources hide the offer"),
		static_cast<int32>(TagToolboxTest_Plan(TEXT("Combat.New"), TEXT(""), false, false, false, true, /*bTagSourcesWritable=*/false).Mode),
		static_cast<int32>(ETagToolboxCreateRowMode::Hidden));
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Combat.New"), TEXT(""), false, false, false, true, true, /*bCreateInFlight=*/true);
		TestEqual(TEXT("In-flight create refuses a second plan"), static_cast<int32>(Plan.Mode), static_cast<int32>(ETagToolboxCreateRowMode::InvalidInput));
	}

	// Plain offer.
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Combat.New"));
		TestEqual(TEXT("Valid missing tag offers creation"), static_cast<int32>(Plan.Mode), static_cast<int32>(ETagToolboxCreateRowMode::Offer));
		TestEqual(TEXT("Offer carries the typed name"), Plan.FinalTagString, FString(TEXT("Combat.New")));
	}

	// Filter-root prefixing: out-of-filter input becomes a prefixed offer.
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Dash"), TEXT("Combat"));
		TestEqual(TEXT("Out-of-filter input offers prefixed name"), static_cast<int32>(Plan.Mode), static_cast<int32>(ETagToolboxCreateRowMode::Offer));
		TestEqual(TEXT("Prefix uses the first filter root"), Plan.FinalTagString, FString(TEXT("Combat.Dash")));
	}
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Dash"), TEXT("Movement, Combat"));
		TestEqual(TEXT("Multi-root filter prefixes with its first root"), Plan.FinalTagString, FString(TEXT("Movement.Dash")));
	}
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Combat.Uppercut"), TEXT("Combat"));
		TestEqual(TEXT("In-filter input is offered unprefixed"), Plan.FinalTagString, FString(TEXT("Combat.Uppercut")));
	}

	// Fixed-string adoption: the offer names exactly what will be created.
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("Combat.Dash."));
		TestEqual(TEXT("Fixable input still offers"), static_cast<int32>(Plan.Mode), static_cast<int32>(ETagToolboxCreateRowMode::Offer));
		TestEqual(TEXT("Offer carries the engine-fixed name"), Plan.FinalTagString, FString(TEXT("Combat.Dash")));
	}

	// Unfixable input: a visible disabled row with the reason, never absence.
	// ("..." trims to nothing — the engine's fixed suggestion is empty. Inputs
	// whose fix is non-empty, like ",,", correctly become fixed-name offers.)
	{
		const FTagToolboxCreateRowPlan Plan = TagToolboxTest_Plan(TEXT("..."));
		TestEqual(TEXT("Unfixable input renders InvalidInput"), static_cast<int32>(Plan.Mode), static_cast<int32>(ETagToolboxCreateRowMode::InvalidInput));
		TestFalse(TEXT("InvalidInput carries the specific reason"), Plan.Reason.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxClipboardTest,
	"TagToolbox.Clipboard.ClassifyAndFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxClipboardTest::RunTest(const FString& Parameters)
{
	// Export shape: the engine chip's plain string; an unset tag is "None".
	TestEqual(TEXT("Empty tag exports as None"), FTagToolboxTagClipboard::ExportTagString(FGameplayTag()), FString(TEXT("None")));

	// Container recognition runs before import so refusals can name the cause.
	TestTrue(TEXT("Container export text recognized"), FTagToolboxTagClipboard::LooksLikeContainerText(TEXT("(GameplayTags=((TagName=\"A.B\"),(TagName=\"C\")))")));
	TestTrue(TEXT("Empty container export recognized"), FTagToolboxTagClipboard::LooksLikeContainerText(TEXT("(GameplayTags=)")));
	TestTrue(TEXT("Two TagName entries recognized as container"), FTagToolboxTagClipboard::LooksLikeContainerText(TEXT("(TagName=\"A\"),(TagName=\"B\")")));
	TestFalse(TEXT("Single-tag export is not a container"), FTagToolboxTagClipboard::LooksLikeContainerText(TEXT("(TagName=\"A.B\")")));
	TestFalse(TEXT("Plain name is not a container"), FTagToolboxTagClipboard::LooksLikeContainerText(TEXT("A.B")));

	FGameplayTag Parsed;
	TestEqual(TEXT("Empty text classifies Empty"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("  "), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::Empty));
	TestEqual(TEXT("Container text classifies Container"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("(GameplayTags=((TagName=\"A\")))"), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::Container));
	TestEqual(TEXT("Garbage classifies Invalid"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("!!not a tag!!"), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::Invalid));
	TestEqual(TEXT("Unregistered name classifies Invalid"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("Zzz.NotARealTag.Ever"), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::Invalid));
	TestEqual(TEXT("Bare None classifies Invalid (paste never clears implicitly)"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("None"), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::Invalid));

	const FTagToolboxTestTagHierarchy Tags;
	const FGameplayTag AnimationTag = Tags.Animation();
	if (!TestTrue(TEXT("clipboard fixture tag registered on demand"), AnimationTag.IsValid()))
	{
		return false;
	}
	// Registered-name parsing uses the automation-local fixture, not host project vocabulary.
	TestEqual(TEXT("Plain registered name classifies SingleTag"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("TagToolbox.Test.Animation"), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::SingleTag));
	TestEqual(TEXT("Plain name parses to the tag"), Parsed, AnimationTag);
	TestEqual(TEXT("Export form classifies SingleTag"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("(TagName=\"TagToolbox.Test.Animation\")"), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::SingleTag));
	TestEqual(TEXT("Export form parses to the tag"), Parsed, AnimationTag);
	TestEqual(TEXT("Surrounding whitespace tolerated"), static_cast<int32>(FTagToolboxTagClipboard::ClassifyClipboardText(TEXT("  TagToolbox.Test.Animation  "), Parsed)), static_cast<int32>(ETagToolboxClipboardContent::SingleTag));

	// Filter matching (the shared create/paste policy).
	TestTrue(TEXT("Empty filter allows everything"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("Anything.At.All"), TEXT("")));
	TestTrue(TEXT("Exact root matches"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("Combat"), TEXT("Combat")));
	TestTrue(TEXT("Child matches"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("Combat.Heavy"), TEXT("Combat")));
	TestTrue(TEXT("Deep child matches"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("Combat.Heavy.Spin"), TEXT("Combat")));
	TestFalse(TEXT("Prefix-similar sibling does not match"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("CombatArena"), TEXT("Combat")));
	TestFalse(TEXT("Out-of-filter does not match"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("Movement.Dash"), TEXT("Combat")));
	TestTrue(TEXT("Multi-root filter with spaces matches second root"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("Movement.Dash"), TEXT("Combat, Movement")));
	TestTrue(TEXT("Filter matching is case-insensitive"), FTagToolboxTagClipboard::NameMatchesFilter(TEXT("combat.heavy"), TEXT("Combat")));

	return true;
}

namespace
{
	FTagToolboxPackageFacts TagToolboxTest_Facts(const TCHAR* Package, bool bLoaded, bool bDirty, bool bReadOnly, bool bSourceControl)
	{
		FTagToolboxPackageFacts Facts;
		Facts.PackageName = FName(Package);
		Facts.bLoaded = bLoaded;
		Facts.bDirty = bDirty;
		Facts.bReadOnlyOnDisk = bReadOnly;
		Facts.bSourceControlActive = bSourceControl;
		return Facts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxResavePlanTest,
	"TagToolbox.Resave.PlanConsentAndReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxResavePlanTest::RunTest(const FString& Parameters)
{
	// --- Classification of the four package states from supplied facts.
	TArray<FTagToolboxPackageFacts> Facts;
	Facts.Add(TagToolboxTest_Facts(TEXT("/Game/NotLoaded"), false, false, false, false));
	Facts.Add(TagToolboxTest_Facts(TEXT("/Game/LoadedClean"), true, false, false, false));
	Facts.Add(TagToolboxTest_Facts(TEXT("/Game/LoadedDirty"), true, true, false, false));
	Facts.Add(TagToolboxTest_Facts(TEXT("/Game/ReadOnly"), false, false, true, false));
	Facts.Add(TagToolboxTest_Facts(TEXT("/Game/ReadOnlyWithSCC"), false, false, true, true));

	FTagToolboxResavePlan Plan = FTagToolboxResaveService::BuildPlan(Facts);
	TestEqual(TEXT("Every fact classifies"), Plan.Entries.Num(), 5);
	if (Plan.Entries.Num() == 5)
	{
		TestEqual(TEXT("Not loaded"), static_cast<int32>(Plan.Entries[0].Disposition), static_cast<int32>(ETagToolboxPackageDisposition::NotLoaded));
		TestTrue(TEXT("Not loaded included"), Plan.Entries[0].bIncluded);
		TestEqual(TEXT("Loaded clean"), static_cast<int32>(Plan.Entries[1].Disposition), static_cast<int32>(ETagToolboxPackageDisposition::LoadedClean));
		TestTrue(TEXT("Loaded clean included"), Plan.Entries[1].bIncluded);
		TestEqual(TEXT("Loaded dirty"), static_cast<int32>(Plan.Entries[2].Disposition), static_cast<int32>(ETagToolboxPackageDisposition::LoadedDirty));
		TestFalse(TEXT("Dirty excluded by default"), Plan.Entries[2].bIncluded);
		TestEqual(TEXT("Read-only without provider"), static_cast<int32>(Plan.Entries[3].Disposition), static_cast<int32>(ETagToolboxPackageDisposition::ReadOnlyNoProvider));
		TestFalse(TEXT("Read-only excluded"), Plan.Entries[3].bIncluded);
		TestEqual(TEXT("Read-only WITH provider stays checkout-able"), static_cast<int32>(Plan.Entries[4].Disposition), static_cast<int32>(ETagToolboxPackageDisposition::NotLoaded));
		TestTrue(TEXT("Checkout-able included"), Plan.Entries[4].bIncluded);
	}

	// --- Dirty consent: only named dirty entries flip; others untouched.
	FTagToolboxResaveService::ApplyDirtyConsent(Plan, { FName(TEXT("/Game/LoadedDirty")), FName(TEXT("/Game/ReadOnly")) });
	if (Plan.Entries.Num() == 5)
	{
		TestTrue(TEXT("Opted-in dirty becomes included"), Plan.Entries[2].bIncluded);
		TestFalse(TEXT("Consent never includes read-only entries"), Plan.Entries[3].bIncluded);
	}
	TestEqual(TEXT("Included count after consent"), Plan.CountIncluded(), 4);

	// --- Report aggregation, including the cancelled-mid-batch shape: an
	// included package that neither saved nor failed is UNATTEMPTED.
	{
		const TSet<FName> Saved = { FName(TEXT("/Game/NotLoaded")), FName(TEXT("/Game/LoadedClean")) };
		const TSet<FName> Failed = { FName(TEXT("/Game/LoadedDirty")) };
		const FTagToolboxResaveReport Report = FTagToolboxResaveService::BuildReport(Plan, Saved, Failed);

		TestEqual(TEXT("Saved set exact"), Report.Saved.Num(), 2);
		TestEqual(TEXT("Failed set exact"), Report.Failed.Num(), 1);
		TestEqual(TEXT("Cancelled remainder is unattempted, not failed"), Report.Unattempted.Num(), 1);
		if (Report.Unattempted.Num() == 1)
		{
			TestEqual(TEXT("Unattempted names the never-tried package"), Report.Unattempted[0], FName(TEXT("/Game/ReadOnlyWithSCC")));
		}
		TestEqual(TEXT("Skipped names the excluded package"), Report.Skipped.Num(), 1);
		TestFalse(TEXT("Partial outcome reports not-all-saved"), Report.AllIncludedSaved());
	}

	// --- Clean outcome and the empty plan.
	{
		const TSet<FName> AllSaved = { FName(TEXT("/Game/NotLoaded")), FName(TEXT("/Game/LoadedClean")), FName(TEXT("/Game/LoadedDirty")), FName(TEXT("/Game/ReadOnlyWithSCC")) };
		const FTagToolboxResaveReport Report = FTagToolboxResaveService::BuildReport(Plan, AllSaved, TSet<FName>());
		TestTrue(TEXT("All-saved outcome reports clean"), Report.AllIncludedSaved());
	}

	// --- Precedence pin: a package present in BOTH sets (saved on a retry
	// after an earlier failure) classifies Saved — the save event is ground
	// truth, the failed list is history.
	{
		const TSet<FName> Saved = { FName(TEXT("/Game/NotLoaded")) };
		const TSet<FName> Failed = { FName(TEXT("/Game/NotLoaded")) };
		const FTagToolboxResaveReport Report = FTagToolboxResaveService::BuildReport(Plan, Saved, Failed);
		TestTrue(TEXT("Saved-and-failed package classifies Saved"), Report.Saved.Contains(FName(TEXT("/Game/NotLoaded"))));
		TestFalse(TEXT("Saved-and-failed package not doubled into Failed"), Report.Failed.Contains(FName(TEXT("/Game/NotLoaded"))));
	}
	{
		const FTagToolboxResavePlan EmptyPlan = FTagToolboxResaveService::BuildPlan(TArray<FTagToolboxPackageFacts>());
		TestEqual(TEXT("No facts produce an empty plan"), EmptyPlan.Entries.Num(), 0);
		TestEqual(TEXT("Empty plan includes nothing"), EmptyPlan.CountIncluded(), 0);
	}

	return true;
}

namespace
{
	struct FTagToolboxTest_RenamePlanArgs
	{
		FName OldName;
		FName NewName;
		bool bNewNameValid = true;
		bool bTagSourcesWritable = true;
		TSet<FName> Snapshot;
		TArray<FTagToolboxRedirectRecord> Redirects;
		TMap<FName, FName> NameToSource;
		TSet<FName> UnwritableSources;
		TArray<FGameplayTag> Styles;
		TArray<FName> Favorites;
		TArray<FName> Recents;
		int32 MergeRefs = 0;
		int32 MergeChildren = 0;
	};

	FTagToolboxRenamePlan TagToolboxTest_BuildRenamePlan(const FTagToolboxTest_RenamePlanArgs& Args)
	{
		return FTagToolboxRenameFixup::BuildPlan(Args.OldName, Args.NewName, Args.bNewNameValid, Args.bTagSourcesWritable,
			Args.Snapshot, Args.Redirects, Args.NameToSource, Args.UnwritableSources, Args.Styles, Args.Favorites, Args.Recents,
			Args.MergeRefs, Args.MergeChildren);
	}

	FTagToolboxRedirectRecord TagToolboxTest_Redirect(const TCHAR* OldName, const TCHAR* NewName)
	{
		FTagToolboxRedirectRecord Record;
		Record.OldTagName = FName(OldName);
		Record.NewTagName = FName(NewName);
		Record.OwningSourceName = FName(TEXT("TestSource.ini"));
		return Record;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxRenamePlanBuildTest,
	"TagToolbox.Rename.PlanVerdictsAndPayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxRenamePlanBuildTest::RunTest(const FString& Parameters)
{
	// DeriveRenamedName: parent, child, deep child, non-member identity.
	TestEqual(TEXT("Parent renames to the target"), FTagToolboxRenameFixup::DeriveRenamedName(FName(TEXT("A.B")), FName(TEXT("A.B")), FName(TEXT("C.D"))), FName(TEXT("C.D")));
	TestEqual(TEXT("Child re-roots under the target"), FTagToolboxRenameFixup::DeriveRenamedName(FName(TEXT("A.B.X")), FName(TEXT("A.B")), FName(TEXT("C.D"))), FName(TEXT("C.D.X")));
	TestEqual(TEXT("Deep child keeps its suffix"), FTagToolboxRenameFixup::DeriveRenamedName(FName(TEXT("A.B.X.Y")), FName(TEXT("A.B")), FName(TEXT("C.D"))), FName(TEXT("C.D.X.Y")));
	TestEqual(TEXT("Non-member is identity"), FTagToolboxRenameFixup::DeriveRenamedName(FName(TEXT("A.Bc")), FName(TEXT("A.B")), FName(TEXT("C.D"))), FName(TEXT("A.Bc")));

	FTagToolboxTest_RenamePlanArgs Base;
	Base.OldName = FName(TEXT("Combat.Heavy"));
	Base.NewName = FName(TEXT("Combat.Strong"));
	Base.Snapshot = { FName(TEXT("Combat")), FName(TEXT("Combat.Heavy")), FName(TEXT("Combat.Heavy.Spin")), FName(TEXT("Combat.Light")) };
	Base.NameToSource.Add(FName(TEXT("Combat.Heavy")), FName(TEXT("MainTags.ini")));
	Base.NameToSource.Add(FName(TEXT("Combat.Heavy.Spin")), FName(TEXT("SpinTags.ini")));

	// Ordinary rename: subtree from the snapshot, mapping derived.
	{
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Base);
		TestEqual(TEXT("Ordinary rename is Ready"), static_cast<int32>(Plan.Verdict), static_cast<int32>(ETagToolboxRenameVerdict::Ready));
		TestEqual(TEXT("Subtree expands from the snapshot"), Plan.SubtreeOldNames.Num(), 2);
		TestEqual(TEXT("Child maps to the re-rooted name"), Plan.OldToNewNames.FindRef(FName(TEXT("Combat.Heavy.Spin"))), FName(TEXT("Combat.Strong.Spin")));
		TestEqual(TEXT("Verification covers the subtree"), Plan.RetirementVerificationNames.Num(), 2);
	}

	// Merge target detected with the confirm payload.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.NewName = FName(TEXT("Combat.Light"));
		Args.MergeRefs = 7;
		Args.MergeChildren = 2;
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Existing target yields ReadyMerge"), static_cast<int32>(Plan.Verdict), static_cast<int32>(ETagToolboxRenameVerdict::ReadyMerge));
		TestEqual(TEXT("Merge confirm carries referencer count"), Plan.MergeTargetReferencerCount, 7);
		TestEqual(TEXT("Merge confirm carries child count"), Plan.MergeTargetChildCount, 2);
		TestFalse(TEXT("Merge warning text present"), Plan.VerdictReason.IsEmpty());
	}

	// Cycles refuse in both directions, naming the relationship.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.NewName = FName(TEXT("Combat.Heavy.Spin.New"));
		TestEqual(TEXT("Rename into own subtree refused"), static_cast<int32>(TagToolboxTest_BuildRenamePlan(Args).Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedCycle));
		Args.NewName = FName(TEXT("Combat"));
		TestEqual(TEXT("Rename onto own ancestor refused"), static_cast<int32>(TagToolboxTest_BuildRenamePlan(Args).Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedCycle));
	}

	// Chain A→B plus rename of B: the chain collapses and A joins verification.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.OldHeavy"), TEXT("Combat.Heavy")));
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Chain into the subtree collapses"), Plan.ChainsToCollapse.Num(), 1);
		TestTrue(TEXT("Chain-reachable old name verifies before retirement"), Plan.RetirementVerificationNames.Contains(FName(TEXT("Combat.OldHeavy"))));
		TestEqual(TEXT("Verification set = subtree + chain"), Plan.RetirementVerificationNames.Num(), 3);
	}

	// Any read-only descendant source refuses the WHOLE rename, named.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.UnwritableSources.Add(FName(TEXT("SpinTags.ini")));
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Unwritable descendant source refuses whole subtree"), static_cast<int32>(Plan.Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedUnwritableSource));
		TestTrue(TEXT("Refusal names the blocking source"), Plan.UnwritableSources.Contains(FName(TEXT("SpinTags.ini"))));
	}

	// Recovery: old name gone but already redirecting to the target.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.Snapshot.Remove(FName(TEXT("Combat.Heavy")));
		Args.Snapshot.Remove(FName(TEXT("Combat.Heavy.Spin")));
		Args.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.Heavy"), TEXT("Combat.Strong")));
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Recovery input skips to resave"), static_cast<int32>(Plan.Verdict), static_cast<int32>(ETagToolboxRenameVerdict::SkipToResave));
		// A redirecting-elsewhere old name is NOT recovery.
		FTagToolboxTest_RenamePlanArgs Wrong = Args;
		Wrong.Redirects.Reset();
		Wrong.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.Heavy"), TEXT("Somewhere.Else")));
		TestEqual(TEXT("Old name redirecting elsewhere refuses"), static_cast<int32>(TagToolboxTest_BuildRenamePlan(Wrong).Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedInvalid));
	}

	// Plugin-store fixup lists exactly the affected entries.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.Favorites = { FName(TEXT("Combat.Heavy")), FName(TEXT("Combat.Light")) };
		Args.Recents = { FName(TEXT("Combat.Heavy.Spin")), FName(TEXT("Movement.Run")) };
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Affected favorites exact"), Plan.AffectedFavorites.Num(), 1);
		TestEqual(TEXT("Affected recents exact"), Plan.AffectedRecents.Num(), 1);
		TestTrue(TEXT("Unaffected favorite untouched"), !Plan.AffectedFavorites.Contains(FName(TEXT("Combat.Light"))));
	}

	// Same-name and invalid-name refusals.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.NewName = FName(TEXT("combat.heavy"));
		TestEqual(TEXT("Case-only rename refused"), static_cast<int32>(TagToolboxTest_BuildRenamePlan(Args).Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedInvalid));
		Args.NewName = FName(TEXT("Combat.Strong"));
		Args.bNewNameValid = false;
		TestEqual(TEXT("Invalid new name refused"), static_cast<int32>(TagToolboxTest_BuildRenamePlan(Args).Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedInvalid));
	}

	// Unwritable tag sources (ShouldImportTagsFromINI false) refuse up front.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.bTagSourcesWritable = false;
		TestEqual(TEXT("Ini-import-off project refuses rename"), static_cast<int32>(TagToolboxTest_BuildRenamePlan(Args).Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedInvalid));
	}

	// Rename-back refusal: the target being an existing redirect OLD name
	// would destroy the identity (engine resolves through the redirect and
	// never re-creates it; collapsing writes a self-redirect).
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.Strong"), TEXT("Combat.Heavy")));
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Rename onto a redirect old-name refused"), static_cast<int32>(Plan.Verdict), static_cast<int32>(ETagToolboxRenameVerdict::RefusedInvalid));
		TestFalse(TEXT("Rename-back refusal names the redirect"), Plan.VerdictReason.IsEmpty());
	}

	// Self-collapse exclusion: a chain whose old name IS the renamed form of
	// its target never enters the retirement re-query (it is the LIVE tag).
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.Strong.Spin"), TEXT("Combat.Heavy.Spin")));
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Self-collapse chain still collected for removal"), Plan.ChainsToCollapse.Num(), 1);
		TestFalse(TEXT("Live tag name excluded from retirement re-query"), Plan.RetirementVerificationNames.Contains(FName(TEXT("Combat.Strong.Spin"))));
	}

	// Recovery reconstructs the WHOLE subtree from the redirect records the
	// original rename wrote — children resume too, not just the parent.
	{
		FTagToolboxTest_RenamePlanArgs Args = Base;
		Args.Snapshot.Remove(FName(TEXT("Combat.Heavy")));
		Args.Snapshot.Remove(FName(TEXT("Combat.Heavy.Spin")));
		Args.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.Heavy"), TEXT("Combat.Strong")));
		Args.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.Heavy.Spin"), TEXT("Combat.Strong.Spin")));
		Args.Redirects.Add(TagToolboxTest_Redirect(TEXT("Combat.Heavy.Other"), TEXT("Somewhere.Unrelated"))); // NOT part of this rename
		const FTagToolboxRenamePlan Plan = TagToolboxTest_BuildRenamePlan(Args);
		TestEqual(TEXT("Recovery is skip-to-resave"), static_cast<int32>(Plan.Verdict), static_cast<int32>(ETagToolboxRenameVerdict::SkipToResave));
		TestEqual(TEXT("Recovery subtree includes the child"), Plan.SubtreeOldNames.Num(), 2);
		TestTrue(TEXT("Child old name resumes"), Plan.SubtreeOldNames.Contains(FName(TEXT("Combat.Heavy.Spin"))));
		TestTrue(TEXT("Child verification resumes"), Plan.RetirementVerificationNames.Contains(FName(TEXT("Combat.Heavy.Spin"))));
		TestFalse(TEXT("Unrelated redirect under the old root not adopted"), Plan.SubtreeOldNames.Contains(FName(TEXT("Combat.Heavy.Other"))));
	}

	// DeriveRenamedName is case-insensitive on the prefix.
	TestEqual(TEXT("Casing-divergent subtree member still re-roots"),
		FTagToolboxRenameFixup::DeriveRenamedName(FName(TEXT("combat.heavy.spin")), FName(TEXT("Combat.Heavy")), FName(TEXT("Combat.Strong"))),
		FName(TEXT("Combat.Strong.spin")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxAuditActionsTest,
	"TagToolbox.Audit.DeleteDiffAndRedirectValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxAuditActionsTest::RunTest(const FString& Parameters)
{
	// --- Post-delete refusal diffing: requested-vs-still-explicit → exact sets.
	{
		const TArray<FName> Requested = { FName(TEXT("A.One")), FName(TEXT("A.Two")), FName(TEXT("A.Three")) };
		const TSet<FName> StillExplicit = { FName(TEXT("A.Two")) };
		TArray<FName> Deleted;
		TArray<FName> Refused;
		FTagToolboxAudit::DiffRefusedDeletions(Requested, StillExplicit, Deleted, Refused);
		TestEqual(TEXT("Deleted set exact"), Deleted.Num(), 2);
		TestEqual(TEXT("Refused set exact"), Refused.Num(), 1);
		if (Refused.Num() == 1)
		{
			TestEqual(TEXT("Refused names the surviving tag"), Refused[0], FName(TEXT("A.Two")));
		}
		// A parent surviving only implicitly is NOT in the still-explicit set,
		// so it correctly lands in Deleted.
		TestTrue(TEXT("Implicit survivor counts deleted"), Deleted.Contains(FName(TEXT("A.One"))));
	}

	// --- Create-redirect validation matrix (R8).
	{
		const TSet<FName> Defined = { FName(TEXT("Combat.Heavy")), FName(TEXT("Combat.Light")) };
		const TSet<FName> RedirectOldNames = { FName(TEXT("Combat.Old")), FName(TEXT("Combat.Ancient")) };
		const FName OldName(TEXT("Combat.Missing"));

		TestEqual(TEXT("Defined target accepted"),
			static_cast<int32>(FTagToolboxAudit::ValidateRedirectTarget(OldName, FName(TEXT("Combat.Heavy")), Defined, RedirectOldNames)),
			static_cast<int32>(ETagToolboxRedirectTargetVerdict::Ok));
		TestEqual(TEXT("Undefined target refused"),
			static_cast<int32>(FTagToolboxAudit::ValidateRedirectTarget(OldName, FName(TEXT("Combat.Nope")), Defined, RedirectOldNames)),
			static_cast<int32>(ETagToolboxRedirectTargetVerdict::TargetUndefined));
		TestEqual(TEXT("Redirect-old-name target refused"),
			static_cast<int32>(FTagToolboxAudit::ValidateRedirectTarget(OldName, FName(TEXT("Combat.Old")), Defined, RedirectOldNames)),
			static_cast<int32>(ETagToolboxRedirectTargetVerdict::TargetIsRedirectOldName));
		TestEqual(TEXT("Self target refused"),
			static_cast<int32>(FTagToolboxAudit::ValidateRedirectTarget(OldName, OldName, Defined, RedirectOldNames)),
			static_cast<int32>(ETagToolboxRedirectTargetVerdict::SelfRedirect));
		TestEqual(TEXT("None target refused as self"),
			static_cast<int32>(FTagToolboxAudit::ValidateRedirectTarget(OldName, NAME_None, Defined, RedirectOldNames)),
			static_cast<int32>(ETagToolboxRedirectTargetVerdict::SelfRedirect));
		TestEqual(TEXT("Already-redirected old name refused across aggregated sources"),
			static_cast<int32>(FTagToolboxAudit::ValidateRedirectTarget(FName(TEXT("Combat.Ancient")), FName(TEXT("Combat.Heavy")), Defined, RedirectOldNames)),
			static_cast<int32>(ETagToolboxRedirectTargetVerdict::DuplicateOldName));

		TestTrue(TEXT("Refusals carry reason text"), !FTagToolboxAudit::GetRedirectTargetVerdictText(ETagToolboxRedirectTargetVerdict::TargetUndefined).IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxUsageSortTest,
	"TagToolbox.Picker.UsageAggregatesAndSort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxUsageSortTest::RunTest(const FString& Parameters)
{
	// Aggregates: each exact count max-updates itself and every ancestor.
	TMap<FName, TArray<FName>> Referenced;
	Referenced.Add(FName(TEXT("Combat.Heavy.Spin")), { FName(TEXT("/Game/A")), FName(TEXT("/Game/B")), FName(TEXT("/Game/C")) });
	Referenced.Add(FName(TEXT("Combat.Light")), { FName(TEXT("/Game/A")) });
	Referenced.Add(FName(TEXT("Movement")), { FName(TEXT("/Game/A")), FName(TEXT("/Game/B")) });

	const TMap<FName, int32> Aggregates = STagToolboxTagPicker::BuildUsageAggregates(Referenced);
	TestEqual(TEXT("Leaf keeps its exact count"), Aggregates.FindRef(FName(TEXT("Combat.Heavy.Spin"))), 3);
	TestEqual(TEXT("Parent lifts to the hot leaf"), Aggregates.FindRef(FName(TEXT("Combat.Heavy"))), 3);
	TestEqual(TEXT("Root lifts to the hot leaf"), Aggregates.FindRef(FName(TEXT("Combat"))), 3);
	TestEqual(TEXT("Sibling keeps its own count"), Aggregates.FindRef(FName(TEXT("Combat.Light"))), 1);
	TestEqual(TEXT("Unrelated root unaffected"), Aggregates.FindRef(FName(TEXT("Movement"))), 2);
	TestEqual(TEXT("Absent name reads zero"), Aggregates.FindRef(FName(TEXT("Zzz"))), 0);

	// Comparator: descending by aggregate, lexical tiebreak, absent-last.
	TArray<FName> Names = { FName(TEXT("Movement")), FName(TEXT("Combat")), FName(TEXT("Interface")), FName(TEXT("Audio")) };
	TMap<FName, int32> SortCounts;
	SortCounts.Add(FName(TEXT("Combat")), 3);
	SortCounts.Add(FName(TEXT("Movement")), 2);
	SortCounts.Add(FName(TEXT("Audio")), 2);
	STagToolboxTagPicker::SortNamesByAggregateUsage(Names, SortCounts);
	TestEqual(TEXT("Highest aggregate first"), Names[0], FName(TEXT("Combat")));
	TestEqual(TEXT("Tie breaks lexically"), Names[1], FName(TEXT("Audio")));
	TestEqual(TEXT("Tie breaks lexically (second)"), Names[2], FName(TEXT("Movement")));
	TestEqual(TEXT("Uncounted name sorts last"), Names[3], FName(TEXT("Interface")));

	return true;
}

namespace
{
	TSharedPtr<FTagToolboxAuditRow> TagToolboxTest_AuditRow(ETagToolboxAuditCategory Category, const TCHAR* InTagName, std::initializer_list<const TCHAR*> Referencers = {})
	{
		TSharedPtr<FTagToolboxAuditRow> Row = MakeShared<FTagToolboxAuditRow>();
		Row->Category = Category;
		Row->Tag = FName(InTagName);
		Row->Detail = FString::Printf(TEXT("detail for %s"), InTagName);
		for (const TCHAR* Referencer : Referencers)
		{
			Row->ReferencerPackages.Add(FName(Referencer));
		}
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTagToolboxAuditReportTest,
	"TagToolbox.Commandlet.ReportScopeAndSeverity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTagToolboxAuditReportTest::RunTest(const FString& Parameters)
{
	TArray<TSharedPtr<FTagToolboxAuditRow>> Rows;
	Rows.Add(TagToolboxTest_AuditRow(ETagToolboxAuditCategory::ReferencedUndefined, TEXT("Combat.Gone"), { TEXT("/Game/A"), TEXT("/Engine/E"), TEXT("/SomePlugin/P") }));
	Rows.Add(TagToolboxTest_AuditRow(ETagToolboxAuditCategory::ReferencedUndefined, TEXT("Combat.PluginOnly"), { TEXT("/SomePlugin/P") }));
	Rows.Add(TagToolboxTest_AuditRow(ETagToolboxAuditCategory::LingeringRedirect, TEXT("Combat.Old"), { TEXT("/Game/B") }));
	Rows.Add(TagToolboxTest_AuditRow(ETagToolboxAuditCategory::UnusedDefined, TEXT("Combat.Idle")));
	Rows.Add(TagToolboxTest_AuditRow(ETagToolboxAuditCategory::BrokenRedirect, TEXT("Combat.Broken")));

	// --- Scope: default (game only) drops out-of-scope referencers and whole
	// rows whose referencers all fall out; definition-side rows pass through.
	FTagToolboxAuditReportOptions DefaultOptions;
	const TArray<TSharedPtr<FTagToolboxAuditRow>> Scoped = FTagToolboxAuditReportBuilder::ApplyScope(Rows, DefaultOptions);
	TestEqual(TEXT("Plugin-only referencer row dropped in default scope"), Scoped.Num(), 4);
	for (const TSharedPtr<FTagToolboxAuditRow>& Row : Scoped)
	{
		if (Row->Tag == FName(TEXT("Combat.Gone")))
		{
			TestEqual(TEXT("Out-of-scope referencers filtered"), Row->ReferencerPackages.Num(), 1);
			TestEqual(TEXT("Game referencer kept"), Row->ReferencerPackages[0], FName(TEXT("/Game/A")));
		}
		TestTrue(TEXT("Plugin-only row absent"), Row->Tag != FName(TEXT("Combat.PluginOnly")));
	}
	{
		FTagToolboxAuditReportOptions Widened;
		Widened.bIncludePluginContent = true;
		TestEqual(TEXT("Widened scope keeps the plugin-only row"), FTagToolboxAuditReportBuilder::ApplyScope(Rows, Widened).Num(), 5);
	}

	// --- Severity: default fails ONLY referenced-but-undefined.
	TestTrue(TEXT("Undefined always fails"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::ReferencedUndefined, DefaultOptions));
	TestFalse(TEXT("Unused passes by default"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::UnusedDefined, DefaultOptions));
	TestFalse(TEXT("Lingering passes by default"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::LingeringRedirect, DefaultOptions));
	TestFalse(TEXT("Broken passes by default"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::BrokenRedirect, DefaultOptions));
	TestFalse(TEXT("Near-duplicate passes by default"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::NearDuplicate, DefaultOptions));
	{
		FTagToolboxAuditReportOptions Escalated;
		Escalated.bFailOnUnused = true;
		Escalated.bFailOnBrokenRedirects = true;
		Escalated.bFailOnLingeringRedirects = true;
		Escalated.bFailOnNearDuplicates = true;
		TestTrue(TEXT("Unused escalates"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::UnusedDefined, Escalated));
		TestTrue(TEXT("Broken escalates"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::BrokenRedirect, Escalated));
		TestTrue(TEXT("Lingering escalates"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::LingeringRedirect, Escalated));
		TestTrue(TEXT("Near-duplicate escalates"), FTagToolboxAuditReportBuilder::CategoryFails(ETagToolboxAuditCategory::NearDuplicate, Escalated));
	}

	// --- Report: schema present, verdict from severity, deterministic text.
	const FString StartedUtc = TEXT("2026-08-20T12:00:00Z");
	const FString FinishedUtc = TEXT("2026-08-20T12:00:05Z");
	const FString JsonA = FTagToolboxAuditReportBuilder::GenerateJson(Scoped, DefaultOptions, StartedUtc, FinishedUtc);
	const FString JsonB = FTagToolboxAuditReportBuilder::GenerateJson(Scoped, DefaultOptions, StartedUtc, FinishedUtc);
	TestEqual(TEXT("Report is deterministic across runs"), JsonA, JsonB);
	TestTrue(TEXT("Schema version present"), JsonA.Contains(TEXT("\"schema_version\": 1")));
	TestTrue(TEXT("Default severity fails on undefined"), JsonA.Contains(TEXT("\"failed\": true")));
	{
		// Without any undefined rows the default severity passes.
		TArray<TSharedPtr<FTagToolboxAuditRow>> PassingRows;
		PassingRows.Add(TagToolboxTest_AuditRow(ETagToolboxAuditCategory::UnusedDefined, TEXT("Combat.Idle")));
		const FString PassingJson = FTagToolboxAuditReportBuilder::GenerateJson(PassingRows, DefaultOptions, StartedUtc, FinishedUtc);
		TestTrue(TEXT("Unused-only report passes by default"), PassingJson.Contains(TEXT("\"failed\": false")));
	}
	{
		// The common clean-CI shape: no findings at all.
		const FString EmptyJson = FTagToolboxAuditReportBuilder::GenerateJson(TArray<TSharedPtr<FTagToolboxAuditRow>>(), DefaultOptions, StartedUtc, FinishedUtc);
		TestTrue(TEXT("Empty report carries schema"), EmptyJson.Contains(TEXT("\"schema_version\": 1")));
		TestTrue(TEXT("Empty report passes"), EmptyJson.Contains(TEXT("\"failed\": false")));
		TestTrue(TEXT("Empty report has an empty findings array"), EmptyJson.Contains(TEXT("\"findings\": []")));
	}
	{
		// Definition-side rows pass through scope untouched even when they
		// carry referencer lists (broken redirect with plugin referencers).
		TArray<TSharedPtr<FTagToolboxAuditRow>> BrokenRows;
		BrokenRows.Add(TagToolboxTest_AuditRow(ETagToolboxAuditCategory::BrokenRedirect, TEXT("Combat.Dead"), { TEXT("/SomePlugin/P") }));
		const TArray<TSharedPtr<FTagToolboxAuditRow>> ScopedBroken = FTagToolboxAuditReportBuilder::ApplyScope(BrokenRows, DefaultOptions);
		TestEqual(TEXT("BrokenRedirect row passes scope regardless of referencers"), ScopedBroken.Num(), 1);
		if (ScopedBroken.Num() == 1)
		{
			TestEqual(TEXT("Definition-side referencer list untouched by scope"), ScopedBroken[0]->ReferencerPackages.Num(), 1);
		}
	}
	{
		// PackageInScope edges: script packages and bare roots.
		TestFalse(TEXT("Script package out of default scope"), FTagToolboxAuditReportBuilder::PackageInScope(FName(TEXT("/Script/Engine")), DefaultOptions));
		TestFalse(TEXT("Bare /Game root (no trailing content) out of scope"), FTagToolboxAuditReportBuilder::PackageInScope(FName(TEXT("/Game")), DefaultOptions));
		TestTrue(TEXT("Game content in scope"), FTagToolboxAuditReportBuilder::PackageInScope(FName(TEXT("/Game/Maps/Map")), DefaultOptions));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
