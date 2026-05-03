
// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"


class UWorldStateSubsystem;

/**
 * Per-session snapshot of WorldState facts. One entry per PIE session this editor run. While the session is in-flight,
 * Facts is empty and FSimpleCorePIEDebugChannel::GetFactsForSession proxies the live subsystem so consumers see live
 * mutations. On EndPIE the channel copies WorldState->GetAllFacts() into Facts and marks bInFlight=false.
 */
struct FWorldStateSessionSnapshot
{
	int32 SessionNumber = 0;
	double SessionStartRealTime = 0.0;  // FPlatformTime::Seconds() at PostPIEStarted — clock-time, monotonic across sessions
	double EndedAtGameTime = 0.0;       // PIE world's GetTimeSeconds() at EndPIE; 0.0 while in flight
	bool bInFlight = false;
	/** Facts captured at EndPIE — empty while bInFlight=true (live data flows via GetFactsForSession's subsystem proxy). */
	TMap<FGameplayTag, int32> Facts;
};

/** Multicast fired when SessionHistory mutates: new in-flight entry pushed (PostPIEStarted), in-flight transitions to
 *  completed (EndPIE), or any subsystem-side mutation while the in-flight session is live (forwarded from
 *  UWorldStateSubsystem::OnAnyFactChanged). Inspection-surface refresh hook for views post-Phase-2c. */
DECLARE_MULTICAST_DELEGATE(FOnSessionHistoryChanged);

/**
 * Editor-side PIE debug hook for the SimpleCore subsystems. Mirrors FQuestPIEDebugChannel's lifecycle pattern —
 * module-scoped, subscribed to FEditorDelegates::PostPIEStarted + EndPIE, caches weak pointers into the PIE world's
 * subsystems so inspection surfaces can query without resolving the world themselves each paint.
 *
 * Scope today is UWorldStateSubsystem. Other SimpleCore subsystems (SignalSubsystem tracing, future adds) can piggyback
 * without a rename.
 */
class SIMPLECOREEDITOR_API FSimpleCorePIEDebugChannel
{
public:
	FSimpleCorePIEDebugChannel() = default;
	~FSimpleCorePIEDebugChannel() = default;

	/** Subscribes to FEditorDelegates::PostPIEStarted + EndPIE. Call once during module StartupModule. */
	void Initialize();

	/** Unsubscribes and clears cached subsystem pointers. Call during module ShutdownModule. */
	void Shutdown();

	/** True when PIE is running AND the WorldState subsystem was successfully resolved in the PIE world. */
	bool IsActive() const;

	/** Weak-pointer accessor for the PIE-world's WorldState. Valid only while IsActive(). */
	UWorldStateSubsystem* GetWorldState() const;

	/** Broadcasts on PIE-active transitions — true on PostPIEStarted success, false on EndPIE. Paint invalidation hook. */
	FSimpleMulticastDelegate OnDebugActiveChanged;

	/** Broadcasts when SessionHistory entries are added, transition in-flight → completed, or live-mutate (via
	 *  forwarded UWorldStateSubsystem::OnAnyFactChanged). View-side refresh hook. Bind via AddRaw, unbind via Remove. */
	FOnSessionHistoryChanged OnSessionHistoryChanged;

	/** Read-only access to the full session history. Index 0 is oldest; last index is newest (in-flight if PIE active). */
	const TArray<FWorldStateSessionSnapshot>& GetSessionHistory() const { return SessionHistory; }

	/** Returns the snapshot at Index, or nullptr if out of range. */
	const FWorldStateSessionSnapshot* GetSessionByIndex(int32 Index) const;

	/**
	 * Returns the facts map for the session at Index. For the in-flight session, proxies the live subsystem so callers
	 * see live mutations. For completed sessions, returns the captured snapshot. Returns an empty static map if Index
	 * is invalid or (for in-flight) the cached subsystem is no longer valid.
	 */
	const TMap<FGameplayTag, int32>& GetFactsForSession(int32 Index) const;

private:
	void HandlePostPIEStarted(bool bIsSimulating);
	void HandleEndPIE(bool bIsSimulating);

	/** Resolves GEditor->PlayWorld → GameInstance → UWorldStateSubsystem. Returns true on success. */
	bool ResolvePIESubsystems();

	/** Pushes a new in-flight session snapshot, applies the FIFO memory cap, fires OnSessionHistoryChanged. */
	void BeginNewSession();

	/** Closes the latest in-flight session: copies live facts in, captures EndedAtGameTime, marks bInFlight=false,
	 *  fires OnSessionHistoryChanged. No-op if SessionHistory is empty or the latest entry is already completed. */
	void FinalizeInFlightSession();

	/** Forwarded from CachedWorldState->OnAnyFactChanged while PIE is active — re-broadcasts as OnSessionHistoryChanged. */
	void HandleAnyFactChanged();

	/** Memory cap: maximum sessions retained in SessionHistory. Older entries are FIFO-evicted on push. */
	static constexpr int32 MaxStoredSessions = 50;

	TWeakObjectPtr<UWorldStateSubsystem> CachedWorldState;

	FDelegateHandle PostPIEStartedHandle;
	FDelegateHandle EndPIEHandle;
	FDelegateHandle OnAnyFactChangedHandle;

	TArray<FWorldStateSessionSnapshot> SessionHistory;
	int32 NextSessionNumber = 1;

	bool bIsActive = false;
};
