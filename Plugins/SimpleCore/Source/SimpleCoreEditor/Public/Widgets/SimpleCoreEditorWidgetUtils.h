// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Styling/SlateColor.h"

namespace FSimpleCoreEditorWidgetUtils
{
	/**
	 * Standard zebra-stripe tint for editor table-view rows. Even rows get a subtle dark overlay; odd rows are
	 * transparent. Pair with FCoreStyle::Get().GetBrush("GenericWhiteBox") on an SBorder wrapping each cell, with
	 * BorderBackgroundColor bound via a delegate (NOT a static value) so the attribute fires per paint, by which
	 * time STableRow::IndexInList has been populated.
	 */
	inline FSlateColor GetTableRowStripeColor(int32 RowIndex)
	{
		return (RowIndex % 2 == 0)
			? FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.15f))
			: FSlateColor(FLinearColor::Transparent);
	}
}