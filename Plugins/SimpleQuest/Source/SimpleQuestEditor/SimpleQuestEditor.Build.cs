// Copyright 2026, Greg Bussell, All Rights Reserved.

using UnrealBuildTool;

public class SimpleQuestEditor: ModuleRules
{
	public SimpleQuestEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] { "GameplayTags", "SimpleQuest" });
		
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", 
			"CoreUObject", 
			"Engine", 
			"UnrealEd", 
			"Settings", 
			"SimpleQuest",
			"SimpleCore",
			"AssetTools", 
			"GraphEditor",
			"Slate",
			"SlateCore",
			"InputCore",
			"BlueprintGraph",
			"ToolMenus",
			"AssetRegistry", 
			"MessageLog",
			"KismetCompiler", 
			"GameplayTagsEditor",
			"PropertyEditor",
			"Projects",
			"ApplicationCore", 
			"WorkspaceMenuStructure",
			"WorldPartitionEditor"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("SettingsEditor");
		}
	}
}