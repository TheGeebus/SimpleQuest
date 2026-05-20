// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.Collections.Generic;

public class SimpleQuestDemoEditorTarget : TargetRules
{
	public SimpleQuestDemoEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SimpleQuestDemo");
	}
}
