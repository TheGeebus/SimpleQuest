// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class SimpleCoreEditor : ModuleRules
{
	public SimpleCoreEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SimpleCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"EditorStyle",
			"EditorFramework",
			"UnrealEd",
			"GameplayTags",
			"ToolMenus",
			"InputCore",
			"WorkspaceMenuStructure",
			"Projects",
			"ApplicationCore"
		});
	}
}
