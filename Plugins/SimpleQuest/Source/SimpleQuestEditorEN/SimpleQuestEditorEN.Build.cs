using System;
using UnrealBuildTool;
using System.IO;

public class SimpleQuestEditorEN : ModuleRules
{
    public SimpleQuestEditorEN(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Slate", "SlateCore",
            "UnrealEd", "GraphEditor", "BlueprintGraph",
            "SimpleQuestEditor"
        });

        string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

        if (FindElectronicNodes(EngineDir, ModuleDirectory, Target, out string ENSourceDir))
        {
            PrivateDependencyModuleNames.Add("ElectronicNodes");
            PrivateIncludePaths.Add(Path.Combine(ENSourceDir, "Private"));
            PrivateDefinitions.Add("WITH_ELECTRONIC_NODES=1");
        }
    }

    static bool FindElectronicNodes(string EngineDir, string ModuleDir, ReadOnlyTargetRules Target, out string ENSourceDir)
    {
        ENSourceDir = "";

        string ProjectPluginsDir = Path.GetFullPath(Path.Combine(ModuleDir, "../../.."));

        string[] Roots =
        {
            Path.Combine(EngineDir, "Plugins", "Marketplace"),
            Path.Combine(EngineDir, "Plugins"),
            ProjectPluginsDir
        };

        foreach (string Root in Roots)
        {
            if (!Directory.Exists(Root)) continue;

            foreach (string Dir in Directory.GetDirectories(Root))
            {
                string Candidate = Path.Combine(Dir, "Source", "ElectronicNodes", "ElectronicNodes.Build.cs");
                if (File.Exists(Candidate))
                {
                    // EN is installed — check it isn't explicitly disabled in the .uproject
                    if (Target.ProjectFile != null && File.Exists(Target.ProjectFile.FullName))
                    {
                        string uproject = File.ReadAllText(Target.ProjectFile.FullName);
                        int nameIdx = uproject.IndexOf("\"ElectronicNodes\"", StringComparison.Ordinal);
                        if (nameIdx >= 0)
                        {
                            int braceEnd = uproject.IndexOf('}', nameIdx);
                            if (braceEnd > nameIdx)
                            {
                                string entry = uproject.Substring(nameIdx, braceEnd - nameIdx);
                                if (entry.Contains("\"Enabled\": false") || entry.Contains("\"Enabled\":false"))
                                    return false;
                            }
                        }
                    }

                    ENSourceDir = Path.Combine(Dir, "Source", "ElectronicNodes");
                    return true;
                }
            }
        }

        return false;
    }
}
