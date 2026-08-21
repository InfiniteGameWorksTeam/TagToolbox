// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/** What a piece of clipboard text holds, from the single-tag pill's view. */
enum class ETagToolboxClipboardContent : uint8
{
	/** Nothing usable (empty/whitespace). */
	Empty,
	/** Exactly one registered tag (plain name or engine export form; redirected old names resolve to their target). */
	SingleTag,
	/** Container-shaped export text — a single-tag property refuses it, by name. */
	Container,
	/** Text that parses to no registered tag. */
	Invalid,
};

/**
 * Pure clipboard helpers for tag properties. The engine's own import/export
 * wrappers (UE::GameplayTags::EditorUtilities) live in a Private header the
 * plugin cannot include, so the thin StaticStruct ImportText/ExportText
 * wrappers are reimplemented here; parsing therefore accepts exactly what the
 * engine chip accepts (plain names AND (TagName="X") export text), and
 * redirects apply during import just as they do on load.
 */
struct FTagToolboxTagClipboard
{
	/** What Copy writes: the plain tag string (the engine chip's shape). */
	static FString ExportTagString(const FGameplayTag& InTag);

	/** Local equivalent of the engine's Private GameplayTagTryImportText. */
	static FGameplayTag TryImportTag(const FString& Text);

	/**
	 * Classifies Text and parses the single-tag case. OutTag is set only for
	 * SingleTag. Container-shaped text is recognized BEFORE import so the pill
	 * can name the actual refusal cause instead of a generic failure.
	 */
	static ETagToolboxClipboardContent ClassifyClipboardText(const FString& Text, FGameplayTag& OutTag);

	/** True when Text looks like FGameplayTagContainer export text. */
	static bool LooksLikeContainerText(const FString& Text);

	/**
	 * True when TagName sits inside the comma-delimited Categories filter
	 * (equal to a root or underneath one). An empty filter allows everything.
	 * Case-insensitive; whitespace around roots is tolerated.
	 */
	static bool NameMatchesFilter(const FString& TagName, const FString& Filter);
};
