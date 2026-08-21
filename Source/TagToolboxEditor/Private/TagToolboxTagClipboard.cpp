// Copyright Infinite Game Works. All Rights Reserved.

#include "TagToolboxTagClipboard.h"

#include "Misc/OutputDeviceNull.h"

FString FTagToolboxTagClipboard::ExportTagString(const FGameplayTag& InTag)
{
	return InTag.ToString();
}

FGameplayTag FTagToolboxTagClipboard::TryImportTag(const FString& Text)
{
	FOutputDeviceNull NullOut;
	FGameplayTag ImportedTag;
	FGameplayTag::StaticStruct()->ImportText(*Text, &ImportedTag, /*OwnerObject=*/nullptr, /*PortFlags=*/0, &NullOut, FGameplayTag::StaticStruct()->GetName(), /*bAllowNativeOverride=*/true);
	return ImportedTag;
}

bool FTagToolboxTagClipboard::LooksLikeContainerText(const FString& Text)
{
	if (Text.Contains(TEXT("GameplayTags="), ESearchCase::IgnoreCase))
	{
		return true;
	}

	// Two or more TagName entries can only be container export text.
	int32 TagNameCount = 0;
	int32 SearchFrom = 0;
	while (true)
	{
		const int32 Found = Text.Find(TEXT("TagName="), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
		if (Found == INDEX_NONE)
		{
			break;
		}
		++TagNameCount;
		SearchFrom = Found + 8;
	}
	return TagNameCount >= 2;
}

ETagToolboxClipboardContent FTagToolboxTagClipboard::ClassifyClipboardText(const FString& Text, FGameplayTag& OutTag)
{
	OutTag = FGameplayTag();

	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return ETagToolboxClipboardContent::Empty;
	}

	if (LooksLikeContainerText(Trimmed))
	{
		return ETagToolboxClipboardContent::Container;
	}

	const FGameplayTag Imported = TryImportTag(Trimmed);
	if (Imported.IsValid())
	{
		OutTag = Imported;
		return ETagToolboxClipboardContent::SingleTag;
	}

	return ETagToolboxClipboardContent::Invalid;
}

bool FTagToolboxTagClipboard::NameMatchesFilter(const FString& TagName, const FString& Filter)
{
	if (Filter.IsEmpty())
	{
		return true;
	}

	TArray<FString> Roots;
	Filter.ParseIntoArray(Roots, TEXT(","), /*bCullEmpty=*/true);
	for (FString& Root : Roots)
	{
		Root.TrimStartAndEndInline();
		if (Root.IsEmpty())
		{
			continue;
		}
		if (TagName.Equals(Root, ESearchCase::IgnoreCase) || TagName.StartsWith(Root + TEXT("."), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}
