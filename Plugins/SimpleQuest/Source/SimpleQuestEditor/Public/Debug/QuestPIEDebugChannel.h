// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Debug/QuestNodeDebugState.h"
#include "Debug/QuestPrereqDebugState.h"

class UQuestlineNode_ContentBase;
class UEdGraphNode;
class UWorldStateSubsystem;
class UQuestManagerSubsystem;

/**
 * Editor-side shared infrastructure for the PIE graph debug overlay (agenda item 7, Session A). Hooks BeginPIE/EndPIE at
 * the module level, caches weak pointers to the PIE world's UQuestManagerSubsystem + UWorldStateSubsystem, and exposes a
 * per-node query that editor graph panels can call during OnPaint to drive state visualization.
 *
 * Lifecycle is module-scoped — one instance owned by FSimpleQuestEditor, constructed in StartupModule, destroyed in
 * ShutdownModule. Delegate subscriptions are established / cleaned in Initialize() and Shutdown().
 *
 * PIE-active detection is passive: IsActive() returns true only between PostPIEStarted and EndPIE. Graph panels call
 * QueryNodeState per paint per visible node — cheap (weak-ptr check + hashed FGameplayTag lookup + small fact-slot
 * pattern match). No delta-subscriptions for Session A; if per-paint polling becomes a cost later, subscribe to
 * FactAddedEvent/FactRemovedEvent and invalidate panel paint on changes instead.
 */
class SIMPLEQUESTEDITOR_API FQuestPIEDebugChannel
{
public:
	FQuestPIEDebugChannel() = default;
	~FQuestPIEDebugChannel() = default;

	/** Subscribes to FEditorDelegates::PostPIEStarted and EndPIE. Call once during module StartupModule. */
	void Initialize();

	/** Unsubscribes and clears cached subsystem pointers. Call during module ShutdownModule. */
	void Shutdown();

	/** True when PIE is running AND the PIE world's subsystems were successfully resolved. */
	bool IsActive() const;

	/**
	 * Resolves the node's compiled FGameplayTag, looks up its WorldState state facts, and returns the highest-priority
	 * state leaf currently set. Returns EQuestNodeDebugState::Unknown when not in PIE, when the node type doesn't participate
	 * in runtime state (combinators, utility nodes, portal nodes), when no compiled tag resolves, or when no state facts
	 * are present for the node.
	 */
	EQuestNodeDebugState QueryNodeState(const UEdGraphNode* EditorNode) const;

	/**
	 * Classifies a prereq-expression leaf's current state given the compiled fact tag it checks and the compiled tag of
	 * the source content node it reads from. Returns Unknown when not in PIE or when either tag is invalid. See
	 * EPrereqDebugState for the classification semantics.
	 */
	EPrereqDebugState QueryLeafState(const FGameplayTag& LeafFact, const FGameplayTag& SourceRuntimeTag, const UQuestlineNode_ContentBase* LeafSourceNode = nullptr) const;

	/** Convenience raw-fact lookup — returns true if the PIE world's WorldState has the given fact asserted. False otherwise
		(including when not in PIE). */
	bool HasFact(const FGameplayTag& FactTag) const;

	/** Broadcasts true on PostPIEStarted success, false on EndPIE. Useful for panel paint invalidation. */
	FSimpleMulticastDelegate OnDebugActiveChanged;

private:
	void HandlePostPIEStarted(bool bIsSimulating);
	void HandleEndPIE(bool bIsSimulating);

	/** Resolves the PIE world's subsystems into cached weak pointers. Returns true on success. */
	bool ResolvePIESubsystems();

	/** Walks editor node → containing UQuestlineGraph → CompiledNodes lookup by QuestGuid. Returns invalid tag if not resolvable. */
	FGameplayTag ResolveRuntimeTag(const UEdGraphNode* EditorNode) const;

	TWeakObjectPtr<UWorldStateSubsystem> CachedWorldState;

	TWeakObjectPtr<UQuestManagerSubsystem> CachedQuestManager;

	FDelegateHandle PostPIEStartedHandle;
	FDelegateHandle EndPIEHandle;

	bool bIsActive = false;
};