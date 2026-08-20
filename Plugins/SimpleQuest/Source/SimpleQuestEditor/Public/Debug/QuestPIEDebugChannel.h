// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Debug/QuestNodeDebugState.h"
#include "Debug/QuestPrereqDebugState.h"
#include "Quests/Types/QuestEntryRecord.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Quests/Types/PrerequisiteExpression.h"


struct FQuestActivationBlocker;
class UQuestlineNode_ContentBase;
class UEdGraphNode;
class UWorldStateSubsystem;
class UQuestManagerSubsystem;
class UQuestStateSubsystem;


/**
 * Per-session snapshot of UQuestStateSubsystem registry contents. One entry per PIE session this editor run. While
 * in-flight, the three Maps are empty and FQuestPIEDebugChannel::GetXxxForSession proxies the live subsystem. On
 * EndPIE the channel copies the registry maps in and marks bInFlight=false.
 */
struct FQuestStateSessionSnapshot
{
	int32 SessionNumber = 0;
	double SessionStartRealTime = 0.0;  // FPlatformTime::Seconds() at PostPIEStarted
	double EndedAtGameTime = 0.0;       // PIE world's GetTimeSeconds() at EndPIE; 0.0 while in flight
	bool bInFlight = false;
	
	/** Registry maps captured at EndPIE — all empty while bInFlight=true (live data via subsystem proxy accessors). */
	TMap<FGameplayTag, FQuestResolutionRecord> Resolutions;
	TMap<FGameplayTag, FQuestEntryRecord> Entries;
	TMap<FGameplayTag, FQuestPrereqStatus> PrereqStatus;
};

/**
 * Multicast fired when SessionHistory mutates: new in-flight entry pushed (PostPIEStarted), in-flight transitions
 * to completed (EndPIE), or any subsystem-side mutation while in-flight (forwarded from UQuestStateSubsystem::OnAnyRegistryChanged).
 */
DECLARE_MULTICAST_DELEGATE(FOnQuestStateSessionHistoryChanged);

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
	 * Why the node backing EditorNode cannot currently proceed, in the same vocabulary the runtime's blocker API uses.
	 * Empty when nothing gates it, when PIE is inactive, or when the node has no registered runtime instance.
	 *
	 * Orthogonal to QueryNodeState: a node can be gated at any point in its lifecycle, so the two are read together. Only
	 * the reasons meaningful for a node already in the graph are reported — Blocked and PrereqUnmet. The give-flow
	 * reasons answer "why can't this be started", which is the wrong question for an overlay.
	 */
	TArray<FQuestActivationBlocker> QueryNodeGating(const UEdGraphNode* EditorNode) const;
	
	/**
	 * Live prerequisite evaluation for the runtime instance backing EditorNode, with per-leaf detail. Returns false when PIE
	 * is inactive, when the node resolves to no compiled instance, or when the manager has not registered one - all cases the
	 * caller must render as unknown, since a default-constructed status reads as trivially satisfied.
	 *
	 * Evaluated on demand rather than read from the state subsystem's cached prereq status, which is populated only for
	 * giver-gated quests and cleared as soon as they leave giver state.
	 */
	bool TryGetPrereqStatusForNode(const UEdGraphNode* EditorNode, FQuestPrereqStatus& OutStatus) const;

	/**
	 * Live state of one prerequisite leaf, identified the way the graph identifies it: the content node it reads from and
	 * which completion path on that node it requires. Pass bAnyOutcome for the Any Outcome sentinel, which the compiler
	 * expands into one leaf per path — satisfied when any of them is.
	 *
	 * SourceTag may be in the opened asset's own namespace while the running instance is a placement under a parent's, so
	 * it is matched through the runtime alias index rather than by equality.
	 */
	EPrereqDebugState QueryLeafStateForSource(const UEdGraphNode* OwnerNode, FGameplayTag SourceTag, FName PathIdentity, bool bAnyOutcome) const;

	/**
	 * Convenience raw-fact lookup — returns true if the PIE world's WorldState has the given fact asserted. False otherwise
	 * (including when not in PIE).
	 */
	bool HasFact(const FGameplayTag& FactTag) const;

	/**
	 * Weak-pointer accessor for the PIE-world's QuestStateSubsystem. Returns nullptr when not in PIE or when the
	 * subsystem failed to resolve. Used by the Quest State facts panel view to walk resolution / entry / prereq
	 * registries during PIE. Independent of IsActive() — the view checks this getter directly so QuestState resolution
	 * failures don't gate the rest of the channel's queries (which only need WorldState + QuestManager).
	 */
	UQuestStateSubsystem* GetQuestStateSubsystem() const;
	
	/**
	 * Current PIE world time in seconds (matches the manager's GetTimeSeconds() time domain). Returns 0.0 when
	 * not in PIE or when the cached subsystems aren't resolved. Live-paintable via Text_Lambda binding.
	 */
	double GetCurrentGameTimeSeconds() const;

	/** Broadcasts true on PostPIEStarted success, false on EndPIE. Useful for panel paint invalidation. */
	FSimpleMulticastDelegate OnDebugActiveChanged;

	/** Broadcasts when SessionHistory entries are added, transition in-flight → completed, or live-mutate (via
	 *  forwarded UQuestStateSubsystem::OnAnyRegistryChanged). View-side refresh hook. Bind via AddRaw, unbind via Remove. */
	FOnQuestStateSessionHistoryChanged OnSessionHistoryChanged;

	/** Read-only access to the full session history. Index 0 is oldest; last index is newest (in-flight if PIE active). */
	const TArray<FQuestStateSessionSnapshot>& GetSessionHistory() const { return SessionHistory; }

	/** Returns the snapshot at Index, or nullptr if out of range. */
	const FQuestStateSessionSnapshot* GetSessionByIndex(int32 Index) const;

	/** Per-dataset proxies. For the in-flight session, return references to the live subsystem maps; for completed
	 *  sessions, return the captured snapshot. Static empty fallbacks cover invalid Index or in-flight with the
	 *  cached subsystem unresolvable. */
	const TMap<FGameplayTag, FQuestResolutionRecord>& GetResolutionsForSession(int32 Index) const;
	const TMap<FGameplayTag, FQuestEntryRecord>& GetEntriesForSession(int32 Index) const;
	const TMap<FGameplayTag, FQuestPrereqStatus>& GetPrereqStatusForSession(int32 Index) const;

private:
	void HandlePostPIEStarted(bool bIsSimulating);
	void HandleEndPIE(bool bIsSimulating);

	/** Resolves the PIE world's subsystems into cached weak pointers. Returns true on success. */
	bool ResolvePIESubsystems();

	/** Walks editor node → containing UQuestlineGraph → CompiledNodes lookup by QuestGuid. Returns invalid tag if not resolvable. */
	FGameplayTag ResolveRuntimeTag(const UEdGraphNode* EditorNode) const;

	/** Pushes a new in-flight session snapshot, applies the FIFO memory cap, fires OnSessionHistoryChanged. */
	void BeginNewSession();

	/** Closes the latest in-flight session: copies live registries in, captures EndedAtGameTime, marks bInFlight=false,
	 *  fires OnSessionHistoryChanged. No-op if SessionHistory is empty or latest entry is already completed. */
	void FinalizeInFlightSession();

	/** Forwarded from CachedQuestState->OnAnyRegistryChanged while PIE is active — re-broadcasts as OnSessionHistoryChanged. */
	void HandleAnyRegistryChanged();

	/** Memory cap — maximum sessions retained in SessionHistory. Older entries are FIFO-evicted on push. */
	static constexpr int32 MaxStoredSessions = 50;

	TWeakObjectPtr<UWorldStateSubsystem> CachedWorldState;

	TWeakObjectPtr<UQuestManagerSubsystem> CachedQuestManager;

	TWeakObjectPtr<UQuestStateSubsystem> CachedQuestState;

	FDelegateHandle PostPIEStartedHandle;
	FDelegateHandle EndPIEHandle;
	FDelegateHandle OnAnyRegistryChangedHandle;

	TArray<FQuestStateSessionSnapshot> SessionHistory;
	int32 NextSessionNumber = 1;

	bool bIsActive = false;
};