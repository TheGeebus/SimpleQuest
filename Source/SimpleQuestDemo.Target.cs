// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.Collections.Generic;

public class SimpleQuestDemoTarget : TargetRules
{
	public SimpleQuestDemoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		
		// BOTH OF THESE TRACK THE COMPILING ENGINE INSTEAD OF NAMING A VERSION, because this project is built against 5.6,
		// 5.7, and 5.8, and every fixed value is wrong on at least one of them. It fails from both ends: BuildSettingsVersion.V6
		// does not EXIST before 5.7, so naming it stops project generation dead on 5.6, while EngineIncludeOrderVersion.Unreal5_6
		// is deprecated on 5.8 and unsupported from 5.9, so naming that one rots instead. Latest is the only spelling that is
		// correct on all three, and it stays correct when the next engine lands.
		// THE COST: Latest means this code must compile under whichever include order the compiling engine considers current, 
        // and each release drops implicit includes that used to arrive for free. That is the same bargain the README's three-engine 
        // claim already makes - pinning here would not have avoided the bill, only deferred it to an upgrade, and hidden it in 
        // the meantime.
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SimpleQuestDemo");
	}
}
