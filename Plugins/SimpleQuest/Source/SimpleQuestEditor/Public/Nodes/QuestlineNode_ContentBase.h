// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNodeBase.h"
#include "QuestlineNode_ContentBase.generated.h"

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_ContentBase : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	UQuestlineNode_ContentBase();
	virtual bool IsContentNode() const override { return true; }
	
	virtual void AllocateDefaultPins() override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PostPlacedNewNode() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual void EnsureDeactivationPinsForAutowire() override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// Display name set by the designer in the graph
	UPROPERTY(EditAnywhere, Category = "Quest")
	FText NodeLabel;

	// Stable identity for this node. Used to derive QuestID at compile time. Generated once on placement and regenerated
	// on duplication. Never hand-edited.
	UPROPERTY(VisibleAnywhere, Category = "Quest")
	FGuid QuestGuid;

	UPROPERTY(EditAnywhere, Category = "Quest")
	bool bShowDeactivationPins = false;

	/** Persistent expanded/collapsed state for the "Givers" list shown by content-node Slate widgets. Lifted to
	the base so every content-node widget that surfaces givers shares one storage location — avoids duplicating
	the flag on every subclass that ends up with a giver affordance. */
	UPROPERTY(Transient)
	bool bGiversExpanded = false;

protected:
	virtual FString GetDefaultNodeBaseName() const { return TEXT("Node"); }

	/**
	 * Override to insert custom output pins between Prerequisites and Any Outcome. Do NOT override AllocateDefaultPins — the
	 * base class controls pin ordering to guarantee Deactivate always appears at the bottom.
	 */
	virtual void AllocateOutcomePins() {}

};
