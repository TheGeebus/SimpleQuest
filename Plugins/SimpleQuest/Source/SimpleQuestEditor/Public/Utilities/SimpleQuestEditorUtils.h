// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#define SQ_ED_GREEN FLinearColor(0.05f, 0.75f, 0.15f)
#define SQ_ED_RED FLinearColor(0.85f, 0.08f, 0.08f)

struct FConnectionParams;
struct FGraphPanelPinConnectionFactory;

namespace SimpleQuestEditorUtilities
{
	/**
	 * Sanitizes a designer-entered label into a valid Gameplay Tag segment. Trims whitespace, replaces any character that is not
	 * alphanumeric or underscore with an underscore.
	 */
	inline FString SanitizeQuestlineTagSegment(const FString& InLabel)
	{
		FString Result = InLabel.TrimStartAndEnd();
		for (TCHAR& Ch : Result)
		{
			if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
				Ch = TEXT('_');
		}
		return Result;
	}
}