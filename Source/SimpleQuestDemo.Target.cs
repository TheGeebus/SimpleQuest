// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class SimpleQuestDemoTarget : TargetRules
{
	public SimpleQuestDemoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		
		// Latest auto-resolves: V5 under 5.6, V6 under 5.7+. Pins to whichever defaults the compiling
		// engine considers current — keeps the editor target's build environment compatible with the
		// parent UnrealEditor settings regardless of which engine version is in use.
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("SimpleQuestDemo");
	}
}
