// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BindToQuestEvent.generated.h"

/**
 * Dedicated K2 node for the UQuestEventSubscription async action. Inherits all pin generation + expansion +
 * latent-node visual framing from UK2Node_AsyncAction. The only reason this subclass exists is to swap the
 * title-bar icon for the Questline icon — same visual signifier UK2Node_CompleteObjectiveWithOutcome uses —
 * so every SimpleQuest K2 node reads as part of the same system at a glance.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UK2Node_BindToQuestEvent : public UK2Node_AsyncAction
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PostPlacedNewNode() override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};