// Copyright Infinite Game Works. All Rights Reserved.

using UnrealBuildTool;

public class TagToolbox : ModuleRules
{
	public TagToolbox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DeveloperSettings",
		});
	}
}
