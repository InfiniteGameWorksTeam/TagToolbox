// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "TagToolboxProbeTypes.generated.h"

/**
 * SCRATCH characterization fixture (U2) — a saveable asset carrying one of
 * each tag-flavored property so probes can measure what saved packages record
 * as Asset Registry searchable names and what rename/reload does to them.
 * This file is deleted once the characterization findings land in
 * docs/architecture.md; nothing ships with it.
 */
UCLASS()
class UTagToolboxProbeAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGameplayTag ProbeTag;

	UPROPERTY()
	FGameplayTagContainer ProbeContainer;

	UPROPERTY()
	FGameplayTagQuery ProbeQuery;
};
