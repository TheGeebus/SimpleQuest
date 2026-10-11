// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.Collections.Generic;

public class SimpleQuestDemoEditorTarget : TargetRules
{
	public SimpleQuestDemoEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		
		// Latest for the reasons in SimpleQuestDemo.Target.cs - no fixed value is correct on 5.6 through 5.9 at once.
		// AND ONE REASON THAT IS THIS TARGET'S ALONE: an editor target built against an INSTALLED engine shares its build
		// environment with the prebuilt UnrealEditor. Settings that diverge from what that binary was compiled with force a
		// unique build environment, which an installed engine refuses rather than quietly rebuilding. Matching whatever the
		// compiling engine calls current is what keeps this target using the engine it was installed with.
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SimpleQuestDemo");
	}
}
