// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#define SQ_ED_UNSET FLinearColor(0.3f, 0.3f, 0.3f)
#define SQ_ED_OUTCOME FLinearColor(0.9f, 0.7f, 0.1f)
#define SQ_ED_ABANDON FLinearColor(0.5f, 0.5f, 0.6f)

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