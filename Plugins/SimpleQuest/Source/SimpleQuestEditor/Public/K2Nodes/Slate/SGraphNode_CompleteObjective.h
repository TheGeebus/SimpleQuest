// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetNodes/SGraphNodeK2Default.h"

class UK2Node_CompleteObjectiveWithOutcome;

/**
 * Custom Slate widget for UK2Node_CompleteObjectiveWithOutcome. Adds an inline text input field for PathName
 * between the exec pins and the data pins. PathName authoring lives on the node body so designers see the
 * dynamic-placement authoring surface without having to dig for it — UE's SKismetInspector doesn't surface
 * K2 node UPROPERTYs in the BP editor's Details panel by default, so a node-body widget is the reliable
 * path for author-time string fields like this one.
 *
 * OutcomeTag authoring uses the standard pin DefaultValue picker (UE built-in for FGameplayTag struct pins);
 * no Slate customization needed for that. PathName has no equivalent built-in pin widget (FName isn't a
 * pin-friendly type), so this customization fills the gap.
 */
class SGraphNode_CompleteObjective : public SGraphNodeK2Default
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_CompleteObjective) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node_CompleteObjectiveWithOutcome* InNode);

protected:
	virtual void CreatePinWidgets() override;

private:
	void OnPathNameCommitted(const FText& NewText, ETextCommit::Type CommitType);
};