// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "QuestlineNodeBase.generated.h"

enum class EQuestPinRole : uint8;
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

	/**
	 * Returns the semantic role played by the given pin on this node. Base-class default maps the codebase's inline
	 * pin-name literals to roles (Activate→ExecIn, Forward→ExecForwardOut, Condition_N→PrereqIn, etc.). Override on
	 * node classes whose pin names don't conform to the defaults (e.g., Knot's KnotIn/KnotOut).
	 */
	virtual EQuestPinRole GetPinRole(const UEdGraphPin* Pin) const;

	/** Returns the first pin on this node matching the given role, or nullptr. */
	UEdGraphPin* GetPinByRole(EQuestPinRole Role) const;
	
	/** Returns the role of a pin whose owning node is a UQuestlineNodeBase; None otherwise (or on null input). */
	static EQuestPinRole GetPinRoleOf(const UEdGraphPin* Pin);
	
	/** Returns the first pin on Node matching Role if Node is a UQuestlineNodeBase. Null on mismatch or absence. */
	static UEdGraphPin* FindPinByRole(const UEdGraphNode* Node, EQuestPinRole Role);

	/** Appends every pin on this node matching the given role into OutPins. */
	void GetPinsByRole(EQuestPinRole Role, TArray<UEdGraphPin*>& OutPins) const;
	
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
	 * Syncs this node's pins of a category to match DesiredPinNames. See FSimpleQuestEditorUtilities::SyncPinsByCategory for
	 * full contract — pins are added/orphaned/removed to converge on the desired set, and the final pin order within the
	 * category matches DesiredPinNames so toggle-off-then-on returns a pin to its original position. Callers control ordering
	 * by sorting DesiredPinNames as appropriate (alphabetical for outcome-style pins, ordinal for Condition_N-style pins).
	 *
	 * Calls Modify() and NotifyGraphChanged() only when something actually changed. No-ops cleanly.
	 *
	 * @param Direction              Pin direction to operate on.
	 * @param PinCategory            Pin category to sync.
	 * @param DesiredPinNames        Authoritative final order of pins in this category. Existing pins are rewritten to match.
	 * @param InsertBeforeCategories Categories before which newly-added pins are inserted. Pass empty to append at the end.
	 */
	void SyncPinsByCategory(EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories = {});

	/** Returns the leaf segment of a dotted tag name (everything after the last '.'). */
	static FText GetTagLeafLabel(FName TagName);

	/** Strips the Quest.Outcome. prefix, preserving any sub-hierarchy the designer authored. Falls back to GetTagLeafLabel. */
	static FText GetOutcomeLabel(FName TagName);

	static bool SortPinsAlphabetically(const FName& A, const FName& B) { return A.Compare(B) < 0; }
};
