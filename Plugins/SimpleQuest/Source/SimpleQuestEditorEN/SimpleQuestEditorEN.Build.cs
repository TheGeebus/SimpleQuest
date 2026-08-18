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
            "Core", 
            "CoreUObject", 
            "Slate",
            "Engine",
            "SlateCore",
            "UnrealEd",
            "GraphEditor", 
            "BlueprintGraph",
            "SimpleQuestEditor"
        });
    
        // EN integration requires UE 5.7+ - the MakeDrawSpline virtual hook needed for the prereq-wire dash effect
        // was introduced in the Electronic Nodes release for UE 5.7. Stock 5.6 EN doesn't expose hooks at the
        // abstraction layer the integration needs, so on pre-5.7 engines, or wherever Electronic Nodes simply isn't
        // installed, the integration is skipped: WITH_ELECTRONIC_NODES is defined to 0 and the guarded source files
        // compile to empty translation units. The rest of the SimpleQuest plugin is unaffected either way.
        // The definition is UNCONDITIONAL and only its value varies - an undefined macro under #if is a hard error
        // on newer engines, so leaving it undefined on the skip path would break the build rather than disable a feature.
        bool bEngineSupportsENIntegration =
            Target.Version.MajorVersion > 5 ||
            (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 7);

        string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
        string ENSourceDir = "";

        bool bWithElectronicNodes = bEngineSupportsENIntegration &&
            FindElectronicNodes(EngineDir, ModuleDirectory, Target, out ENSourceDir);

        if (bWithElectronicNodes)
        {
            PrivateDependencyModuleNames.Add("ElectronicNodes");
            PrivateIncludePaths.Add(Path.Combine(ENSourceDir, "Private"));
        }

        PrivateDefinitions.Add("WITH_ELECTRONIC_NODES=" + (bWithElectronicNodes ? "1" : "0"));
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
