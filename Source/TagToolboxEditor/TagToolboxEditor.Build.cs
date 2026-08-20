// Copyright Infinite Game Works. All Rights Reserved.

using UnrealBuildTool;

public class TagToolboxEditor : ModuleRules
{
	public TagToolboxEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"TagToolbox",
			"GameplayTags",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AppFramework",
			"ApplicationCore",
			"AssetRegistry",
			"BlueprintGraph",
			"GameplayTagsEditor",
			"InputCore",
			"Kismet",
			"Projects",
			"PropertyEditor",
			"Settings",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd",
			"WorkspaceMenuStructure",
		});
	}
}
