// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxCommentTint.h"

#include "EdGraphNode_Comment.h"
#include "TagToolboxColorBridge.h"
#include "TagToolboxSettings.h"

TSharedPtr<SGraphNode> FTagToolboxCommentTintFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
	{
		if (GetDefault<UTagToolboxSettings>()->bColorizeTaggedGraphComments)
		{
			TagToolboxCommentTint::ApplyTintToComment(CommentNode);
		}
	}
	// Never claim the widget — the stock comment widget builds with the
	// stamped color, so every graph editor keeps its normal behavior.
	return nullptr;
}

namespace TagToolboxCommentTint
{

static bool IsTagTokenChar(TCHAR Char)
{
	return FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('.');
}

TArray<FString> ExtractTagTokenCandidates(const FString& CommentText)
{
	TArray<FString> Candidates;

	const int32 Length = CommentText.Len();
	int32 Index = 0;
	while (Index < Length)
	{
		if (CommentText[Index] != TEXT('#'))
		{
			++Index;
			continue;
		}

		int32 TokenStart = Index + 1;
		int32 TokenEnd = TokenStart;
		while (TokenEnd < Length && IsTagTokenChar(CommentText[TokenEnd]))
		{
			++TokenEnd;
		}

		FString Token = CommentText.Mid(TokenStart, TokenEnd - TokenStart);
		while (Token.EndsWith(TEXT(".")))
		{
			Token.LeftChopInline(1);
		}
		if (!Token.IsEmpty())
		{
			Candidates.Add(MoveTemp(Token));
		}

		Index = FMath::Max(TokenEnd, Index + 1);
	}

	return Candidates;
}

FGameplayTag ResolveCommentTag(const FString& CommentText)
{
	for (const FString& Candidate : ExtractTagTokenCandidates(CommentText))
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Candidate), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

bool ApplyTintToComment(UEdGraphNode_Comment* CommentNode)
{
	if (!CommentNode)
	{
		return false;
	}

	const FGameplayTag Tag = ResolveCommentTag(CommentNode->NodeComment);
	if (!Tag.IsValid())
	{
		return false;
	}

	FLinearColor Color;
	if (!TagToolboxColorBridge::ResolveTagColor(Tag, Color))
	{
		return false;
	}

	// Direct in-memory write on purpose: no Modify, no dirty. The stock widget
	// derives body, title bar, and bubble tints from CommentColor.
	CommentNode->CommentColor = FLinearColor(Color.R, Color.G, Color.B, CommentNode->CommentColor.A);
	return true;
}

} // namespace TagToolboxCommentTint
