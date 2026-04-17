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
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual void PostEditUndo() override;

	/**
	 * Auto-wire a newly-placed node to the pin the user dragged from. Walks candidate pins on this node in priority order matching
	 * natural semantic pairing — outcome output prefers Activate input, prereq output prefers Prerequisites, deactivated output
	 * prefers Deactivate — and attaches to the first candidate CanCreateConnection accepts. Silent on failure.
	 */
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;

	/**
	 * Called during AutowireNewNode when the drag source is a deactivation-flavored pin (QuestDeactivated output or QuestDeactivate
	 * input). Override on nodes that support optional deactivation pins to ensure those pins exist before the auto-wire candidate
	 * walk runs. Default: no-op.
	 */
	virtual void EnsureDeactivationPinsForAutowire() {}

	/** Returns true if this node has any orphaned (stale) pins. */
	bool HasStalePins() const;

	/** Breaks all links on orphaned pins and removes them from the node. */
	void RemoveStalePins();
	
protected:
	/**
	 * Surgically synchronizes output or input pins of a given category to match a desired set of pin names. Pins no longer
	 * desired are orphaned if they have active connections (preserving wires for the designer to re-route), or removed immediately
	 * if unconnected. Pins that reappear in the desired set after being orphaned are restored with their wiring intact. Existing
	 * active pins whose names still appear in the desired set are left untouched.
	 *
	 * Calls Modify() and NotifyGraphChanged() only when something actually changed. No-ops cleanly.
	 *
	 * - Forwards the call to USimpleQuestEditorUtilities::SyncPinsByCategory, preserved for backwards compatibility pending callsite
	 * revisions
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

	/** Strips the Quest.Outcome. prefix, preserving any sub-hierarchy the designer authored. Falls back to GetTagLeafLabel. */
	static FText GetOutcomeLabel(FName TagName);
};
