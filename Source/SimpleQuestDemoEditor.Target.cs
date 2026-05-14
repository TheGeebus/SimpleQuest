// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.Collections.Generic;

public class SimpleQuestDemoEditorTarget : TargetRules
{
	public SimpleQuestDemoEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		
		// Latest auto-resolves: V5 under 5.6, V6 under 5.7+. Pins to whichever defaults the compiling
		// engine considers current — keeps the editor target's build environment compatible with the
		// parent UnrealEditor settings regardless of which engine version is in use.
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("SimpleQuestDemo");
	}
}
