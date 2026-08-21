// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class SimpleQuest : ModuleRules
{
	public SimpleQuest(ReadOnlyTargetRules Target) : base(Target)
	{
		// Minimum engine version, enforced here rather than through the .uplugin's EngineVersion field. That field holds
		// a single value, so on a plugin supporting several engine versions it is necessarily wrong about most of them -
		// and on a SOURCE plugin it mostly produces a startup dialog on newer engines while saying nothing useful on
		// older ones. A build-time check fires where the failure actually happens and says why, instead of leaving an
		// adopter to work it out from a cascade of unrelated compile errors.
		if (Target.Version.MajorVersion < 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 6))
		{
			throw new BuildException(
				"SimpleQuest requires Unreal Engine 5.6 or later (detected {0}.{1}). See the Requirements section of the README.",
				Target.Version.MajorVersion, Target.Version.MinorVersion);
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"GameplayTags",
				"SimpleCore"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UMG",
				"DeveloperSettings",
				"Projects",
				"Messaging"
			}
			);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
				
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("AssetRegistry");
		}
	}
}
