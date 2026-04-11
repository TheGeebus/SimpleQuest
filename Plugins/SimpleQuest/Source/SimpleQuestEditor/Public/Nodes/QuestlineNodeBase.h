// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "QuestlineNodeBase.generated.h"

/**
 * Abstract base for all questline graph editor nodes. Provides the self-describing classification interface that
 * FQuestlineGraphTraversalPolicy dispatches to.
 *
 * Override the classification virtuals in concrete node subclasses to participate in schema validation and graph traversal
 * without modifying the policy. Extension plugins add new node types by subclassing this and overriding the relevant method.
 *
 * @see FQuestlineGraphTraversalPolicy
 */
UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNodeBase : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/** Returns true if this node is a questline exit terminal (success or failure). Default: false. */
	virtual bool IsExitNode() const { return false; }

	/** Returns true if this node is a questline content node (Quest, Leaf, LinkedQuestline). Default: false. */
	virtual bool IsContentNode() const { return false; }

	/** Returns true if this node should be traversed through transparently (i.e., a reroute/knot). Default: false. */
	virtual bool IsPassThroughNode() const { return false; }

	/** Returns true if this node is a utility/flow-control node (SetBlocked, ClearBlocked, GroupSignal, etc.). Default: false. */
	virtual bool IsUtilityNode() const { return false; }

	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;

protected:
	/**
	 * Surgically synchronizes output or input pins of a given category to match a desired set of pin names. Removes pins
	 * that are no longer desired (breaking their links), adds pins that are missing. Existing pins whose names still appear
	 * in the desired set are left untouched — their wiring is preserved.
	 *
	 * Calls Modify() and NotifyGraphChanged() only when something actually changed. No-ops cleanly.
	 *
	 * @param Direction              Pin direction to operate on.
	 * @param PinCategory            Only pins matching this category (and Direction) are considered.
	 * @param DesiredPinNames        The pin names that should exist after the sync.
	 * @param InsertBeforeCategories New pins are inserted before the first existing pin whose category appears in this set.
	 *								 Pass empty to append at the end. Order of DesiredPinNames array determines the order that the
	 *								 new pins are added relative to themselves.
	 */
	void SyncPinsByCategory(EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories = {});

	/** Returns the leaf segment of a dotted tag name (everything after the last '.'). */
	static FText GetTagLeafLabel(FName TagName);
};
