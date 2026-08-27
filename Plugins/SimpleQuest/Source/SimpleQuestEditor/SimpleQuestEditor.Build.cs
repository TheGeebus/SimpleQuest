// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class SimpleQuestEditor: ModuleRules
{
	public SimpleQuestEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] { "GameplayTags", "SimpleQuest", "DataTableEditor" });
		
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", 
			"CoreUObject", 
			"Engine", 
			"UnrealEd", 
			"Settings",
			"DeveloperSettings",
			"SimpleQuest",
			"SimpleCore",
			"AssetTools",
			"DesktopPlatform",
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
			"WorldPartitionEditor",
			"SimpleCoreEditor",
			"DataTableEditor",
			"Json",
			"ToolWidgets",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("SettingsEditor");
		}
	}
}