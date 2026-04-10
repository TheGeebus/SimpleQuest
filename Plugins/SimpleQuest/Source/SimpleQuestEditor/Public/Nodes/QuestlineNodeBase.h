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

};
