// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BindToQuestEvent.generated.h"

/**
 * Dedicated K2 node for the UQuestEventSubscription async action. Inherits all pin generation + expansion +
 * latent-node visual framing from UK2Node_AsyncAction. Two responsibilities on top of the base:
 *   1. Swap the title-bar icon for the Questline icon — same visual signifier UK2Node_CompleteObjectiveWithOutcome
 *      uses — so every SimpleQuest K2 node reads as part of the same system at a glance.
 *   2. Per-instance pin exposure: designers toggle individual lifecycle event pins on/off via the Details panel.
 *      The corresponding event's subscription is gated on the proxy side by an exposure bitmask — unexposed
 *      events incur zero subscription cost.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UK2Node_BindToQuestEvent : public UK2Node_AsyncAction
{
	GENERATED_BODY()

public:
	/** Execution reached a giver-gated quest. Carries prereq evaluation in the lifecycle context. */
	UPROPERTY(EditAnywhere, Category = "Pins|Offer Phase", meta = (DisplayName = "On Activated"))
	bool bExposeOnActivated = true;

	/** Quest is now accept-ready (Activated AND prereqs satisfy). */
	UPROPERTY(EditAnywhere, Category = "Pins|Offer Phase", meta = (DisplayName = "On Enabled"))
	bool bExposeOnEnabled = true;

	/** Quest was accept-ready but prereqs flipped back unsatisfied. Rare; primarily NOT-prereq cases. */
	UPROPERTY(EditAnywhere, Category = "Pins|Offer Phase", meta = (DisplayName = "On Disabled"))
	bool bExposeOnDisabled = false;

	/** A give attempt was refused by the manager. Carries blocker array for contextual UI. */
	UPROPERTY(EditAnywhere, Category = "Pins|Offer Phase", meta = (DisplayName = "On Give Blocked"))
	bool bExposeOnGiveBlocked = false;

	/** The subscribed quest entered the Live state — its objectives are bound. */
	UPROPERTY(EditAnywhere, Category = "Pins|Run Phase", meta = (DisplayName = "On Started"))
	bool bExposeOnStarted = true;

	/** Objective progress tick during the Live phase. Context.CompletionContext carries CurrentCount / RequiredCount. */
	UPROPERTY(EditAnywhere, Category = "Pins|Run Phase", meta = (DisplayName = "On Progress"))
	bool bExposeOnProgress = false;

	/** The subscribed quest resolved with an outcome. */
	UPROPERTY(EditAnywhere, Category = "Pins|End Phase", meta = (DisplayName = "On Completed"))
	bool bExposeOnCompleted = true;

	/** The subscribed quest was deactivated before completing (interrupt, abandon). */
	UPROPERTY(EditAnywhere, Category = "Pins|End Phase", meta = (DisplayName = "On Deactivated"))
	bool bExposeOnDeactivated = false;

	/** The subscribed quest entered Blocked state via a SetBlocked utility node. Co-fires with On Deactivated when
	 *  both pins are exposed; useful when the distinction between Blocked and other deactivations matters. */
	UPROPERTY(EditAnywhere, Category = "Pins|End Phase", meta = (DisplayName = "On Blocked"))
	bool bExposeOnBlocked = false;

	virtual void AllocateDefaultPins() override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PostPlacedNewNode() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

private:
	/** Bitmask combining all enabled bExpose* flags. Stuffed into the factory call's ExposedEvents parameter
	 *  during ExpandNode, then read by the proxy in Activate to gate subscriptions. */
	int32 ComputeExposureMask() const;
};