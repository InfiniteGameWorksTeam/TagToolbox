// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"

#include "TagToolboxSettings.generated.h"

/**
 * One project-wide style entry for a gameplay tag.
 *
 * Stored as an array rather than a TMap keyed by FGameplayTag: config-serialized
 * TMap<FGameplayTag, ...> corrupts on load when the tag table changes (UE-230676).
 */
USTRUCT()
struct FTagToolboxTagStyle
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = "Tag Style")
	FGameplayTag Tag;

	UPROPERTY(config, EditAnywhere, Category = "Tag Style")
	FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
};

/**
 * Project settings for Tag Toolbox.
 *
 * The style registry is an OVERRIDE list: empty by default, and every consumer keeps
 * its own fallback presentation, so an unstyled tag renders exactly as stock Unreal.
 * Styles resolve exact-match first, then fall up through ancestor tags, so styling
 * "Combat" also tints "Combat.Heavy" unless the child has its own entry.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Tag Toolbox"))
class TAGTOOLBOX_API UTagToolboxSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTagToolboxSettings();

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Project-wide tag colors. Exact tag wins; otherwise the nearest styled ancestor. */
	UPROPERTY(config, EditAnywhere, Category = "Tag Styles")
	TArray<FTagToolboxTagStyle> TagStyles;

	/**
	 * Replace the editor-wide FGameplayTag property widget with Tag Toolbox's colored
	 * pill. Turn off if another plugin (e.g. Paper2DPlus) already provides one.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Editor Integration")
	bool bColorizeGameplayTagPickers = true;

	/** Show the Tag Filter editing section on Blueprint-defined gameplay tag variables. */
	UPROPERTY(config, EditAnywhere, Category = "Editor Integration")
	bool bEnableVariableTagFilters = true;

	/**
	 * When Paper2DPlus is installed and a tag has no Tag Toolbox style, fall
	 * back to Paper2DPlus's Tag Colors registry (read reflectively) so existing
	 * authored colors keep showing. Tag Toolbox's own registry always wins.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Editor Integration")
	bool bUsePaper2DPlusColorsAsFallback = true;

	/**
	 * Tag container properties collapse their chip list past this many tags
	 * (a "+N more" chip expands them). 0 = never collapse.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Editor Integration", meta = (ClampMin = "0", UIMax = "20"))
	int32 MaxVisibleTagChips = 5;

	/**
	 * Tint Blueprint graph comment boxes whose text carries a "#Some.Tag"
	 * token with that tag's registry color, so comment groups share one
	 * project-wide color. Applied when a graph builds its widgets; never
	 * dirties the asset by itself.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Editor Integration")
	bool bColorizeTaggedGraphComments = true;

	/** Resolve a tag's color: exact entry first, then nearest styled ancestor. */
	bool ResolveTagColor(const FGameplayTag& Tag, FLinearColor& OutColor) const;

	/** Pure resolve over an arbitrary style array (testable without the CDO). */
	static bool ResolveTagColorFromArray(const TArray<FTagToolboxTagStyle>& Styles, const FGameplayTag& Tag, FLinearColor& OutColor);

	/** Add or update the exact entry for Tag and persist to the default config. */
	void SetTagColor(const FGameplayTag& Tag, const FLinearColor& Color);

	/** Remove the exact entry for Tag (ancestor styles keep applying) and persist. */
	void ClearTagColor(const FGameplayTag& Tag);

	/** Fires after any style mutation (settings edit, SetTagColor, ClearTagColor). */
	FSimpleMulticastDelegate OnTagStylesChanged;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
