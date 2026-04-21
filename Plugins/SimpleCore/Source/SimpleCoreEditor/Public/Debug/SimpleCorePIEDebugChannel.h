
// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorldStateSubsystem;

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

private:
	void HandlePostPIEStarted(bool bIsSimulating);
	void HandleEndPIE(bool bIsSimulating);

	/** Resolves GEditor->PlayWorld → GameInstance → UWorldStateSubsystem. Returns true on success. */
	bool ResolvePIESubsystems();

	TWeakObjectPtr<UWorldStateSubsystem> CachedWorldState;

	FDelegateHandle PostPIEStartedHandle;
	FDelegateHandle EndPIEHandle;

	bool bIsActive = false;
};
