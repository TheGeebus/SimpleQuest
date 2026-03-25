using UnrealBuildTool;

public class SimpleQuestEditor: ModuleRules
{
	public SimpleQuestEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "UnrealEd", "Settings", "SimpleQuest" });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("SettingsEditor");
		}
	}
}