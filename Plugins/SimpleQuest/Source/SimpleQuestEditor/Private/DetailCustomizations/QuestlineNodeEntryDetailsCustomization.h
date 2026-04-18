// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UQuestlineNode_Entry;
class IDetailLayoutBuilder;
class IPropertyUtilities;
class FReply;

/**
 * Custom Details panel layout for UQuestlineNode_Entry. Replaces the default raw-array editor on IncomingSignals with a tree
 * view grouped by source content node. Each group header shows the source's display label, with an asset qualifier appended
 * when two sources share a label across different parent assets. Each leaf row shows an outcome label ("Any Outcome" for
 * any-outcome-from-source specs, otherwise the tag leaf) and a checkbox bound to the spec's bExposed field via the property-
 * handle system — so toggling fires PostEditChangeProperty with MemberProperty=IncomingSignals, which the node's existing
 * handler converts into a RefreshOutcomePins call for clean pin sync.
 *
 * Header row offers a Refresh from Sources button that invokes ImportOutcomePinsFromParent (same action as the context menu)
 * then forces a panel re-layout via PropertyUtilities so newly-imported specs show up immediately.
 */
class FQuestlineNodeEntryDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<UQuestlineNode_Entry> CustomizedNode;
	TWeakPtr<IPropertyUtilities> PropertyUtilities;

	FReply OnRefreshClicked();
};