// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGameplayTag;

namespace TagToolboxColorBridge
{
	/**
	 * The one color-resolve seam for every Tag Toolbox editor surface:
	 * Tag Toolbox's own style registry first (exact, then ancestor fall-up);
	 * when that misses and the fallback setting is on, Paper2DPlus's Tag Colors
	 * registry is consulted with the same fall-up — read reflectively, so the
	 * plugin takes no Paper2DPlus dependency. Tag Toolbox's own registry wins
	 * outright: an ancestor entry here beats an exact entry there, keeping
	 * precedence predictable.
	 */
	bool ResolveTagColor(const FGameplayTag& Tag, FLinearColor& OutColor);
}
