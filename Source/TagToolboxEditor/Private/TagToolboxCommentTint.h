// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "GameplayTagContainer.h"

class UEdGraphNode_Comment;

/**
 * Tagged graph comments: a comment whose text carries a "#Some.Tag" token that
 * names a registered gameplay tag is tinted with that tag's registry color
 * (style registry first, Paper2DPlus fallback), so comment groups share one
 * project-wide color that recolors from a single place.
 *
 * Implementation is a visual-node-factory STAMP: when the comment's widget is
 * created (graph opened, node recreated), the resolved color is written onto
 * CommentColor in memory and the STOCK widget builds — no widget fork, no
 * dirtying (nothing is marked modified), and the color renders for teammates
 * without the plugin once the asset is saved for other reasons. Registry
 * changes re-apply the next time the graph rebuilds its widgets.
 */
class FTagToolboxCommentTintFactory : public FGraphPanelNodeFactory
{
public:
	/** Stamps tagged comments and always returns null so the stock widget builds. */
	virtual TSharedPtr<class SGraphNode> CreateNode(class UEdGraphNode* Node) const override;
};

namespace TagToolboxCommentTint
{
	/**
	 * Pure: every "#token" candidate in the text, in order of appearance.
	 * A token is the maximal run of [A-Za-z0-9_.] after '#', with trailing
	 * dots trimmed; empty tokens are skipped. Registration is NOT checked here.
	 */
	TArray<FString> ExtractTagTokenCandidates(const FString& CommentText);

	/** First candidate naming a REGISTERED tag; empty tag when none do. */
	FGameplayTag ResolveCommentTag(const FString& CommentText);

	/** Stamps the resolved registry color (in-memory, never dirties). True if stamped. */
	bool ApplyTintToComment(UEdGraphNode_Comment* CommentNode);
}
