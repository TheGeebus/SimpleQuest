// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.IO;
using EpicGames.Core;
using UnrealBuildTool;

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

        bool bWithElectronicNodes = bEngineSupportsENIntegration && FindElectronicNodes(EngineDir, Target, out ENSourceDir);

        if (bWithElectronicNodes)
        {
            PrivateDependencyModuleNames.Add("ElectronicNodes");
            PrivateIncludePaths.Add(Path.Combine(ENSourceDir, "Private"));
        }

        PrivateDefinitions.Add("WITH_ELECTRONIC_NODES=" + (bWithElectronicNodes ? "1" : "0"));
    }

    // WILL Electronic Nodes actually be LOADED in this target? That is the only question worth asking, and UBT already
    // answers it - project entry, Target.cs EnablePlugins, platform and target-type allow lists all accounted for.
    // This used to ask a DIFFERENT question: "is EN on disk, and did the .uproject explicitly disable it." A project
    // with EN INSTALLED but never enabled - the default for every EN owner, since EN declares no EnabledByDefault -
    // answered yes, linked against UnrealEditor-ElectronicNodes.dll, and then failed to load it, which takes the WHOLE
    // SimpleQuest plugin down rather than dropping one integration. *** A BUILD-TIME GUESS ABOUT A RUNTIME DECISION
    // MUST FAIL TOWARD OFF: wrong here now costs a feature, where wrong before cost the plugin. *** Adopters must never
    // have to name a plugin they do not use in order to use this one.
    static bool FindElectronicNodes(string EngineDir, ReadOnlyTargetRules Target, out string ENSourceDir)
    {
        ENSourceDir = "";

        ProjectDescriptor Project = (Target.ProjectFile != null) ? ProjectDescriptor.FromFile(Target.ProjectFile) : null;
        DirectoryReference ProjectDir = (Target.ProjectFile != null) ? Target.ProjectFile.Directory : null;

        // Covers engine, Marketplace and project plugin folders, so the hand-rolled roots list is gone with it.
        List<PluginInfo> Available = Plugins.ReadAvailablePlugins(new DirectoryReference(EngineDir), ProjectDir, null);

        foreach (PluginInfo Plugin in Available)
        {
            if (!Plugin.Name.Equals("ElectronicNodes", StringComparison.Ordinal)) { continue; }
            if (!Plugins.IsPluginEnabledForTarget(Plugin, Project, Target.Platform, Target.Configuration, Target.Type)) { return false; }

            // Enabled, but a binary-only install has no headers to include - treat that as "no integration" rather than
            // adding an include path that does not exist.
            string Candidate = Path.Combine(Plugin.Directory.FullName, "Source", "ElectronicNodes");
            if (!File.Exists(Path.Combine(Candidate, "ElectronicNodes.Build.cs"))) { return false; }

            ENSourceDir = Candidate;
            return true;
        }

        return false;
    }
}
