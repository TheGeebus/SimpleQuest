// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Debug/SimpleCorePIEDebugChannel.h"
#include "Editor.h"
#include "SimpleCoreEditorLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "WorldState/WorldStateSubsystem.h"

void FSimpleCorePIEDebugChannel::Initialize()
{
    PostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(this, &FSimpleCorePIEDebugChannel::HandlePostPIEStarted);
    EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FSimpleCorePIEDebugChannel::HandleEndPIE);
    UE_LOG(LogSimpleCoreEditor, Verbose, TEXT("FSimpleCorePIEDebugChannel::Initialize : subscribed to PostPIEStarted/EndPIE"));
}

void FSimpleCorePIEDebugChannel::Shutdown()
{
    if (PostPIEStartedHandle.IsValid())
    {
        FEditorDelegates::PostPIEStarted.Remove(PostPIEStartedHandle);
        PostPIEStartedHandle.Reset();
    }
    if (EndPIEHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        EndPIEHandle.Reset();
    }
    if (UWorldStateSubsystem* WS = CachedWorldState.Get())
    {
        if (OnAnyFactChangedHandle.IsValid())
        {
            WS->OnAnyFactChanged.Remove(OnAnyFactChangedHandle);
        }
    }
    OnAnyFactChangedHandle.Reset();
    CachedWorldState.Reset();
    SessionHistory.Reset();
    NextSessionNumber = 1;
    bIsActive = false;
}

bool FSimpleCorePIEDebugChannel::IsActive() const
{
    return bIsActive && CachedWorldState.IsValid();
}

UWorldStateSubsystem* FSimpleCorePIEDebugChannel::GetWorldState() const
{
    return CachedWorldState.Get();
}

void FSimpleCorePIEDebugChannel::HandlePostPIEStarted(bool bIsSimulating)
{
    const bool bResolved = ResolvePIESubsystems();
    bIsActive = bResolved;
    UE_LOG(LogSimpleCoreEditor, Display, TEXT("FSimpleCorePIEDebugChannel : PIE started (simulating=%d, WorldState resolved=%d)"),
        bIsSimulating ? 1 : 0, bResolved ? 1 : 0);

    if (bResolved)
    {
        BeginNewSession();
        if (UWorldStateSubsystem* WS = CachedWorldState.Get())
        {
            OnAnyFactChangedHandle = WS->OnAnyFactChanged.AddRaw(this, &FSimpleCorePIEDebugChannel::HandleAnyFactChanged);
        }
    }
    OnDebugActiveChanged.Broadcast();
}

void FSimpleCorePIEDebugChannel::HandleEndPIE(bool bIsSimulating)
{
    // Finalize before resetting CachedWorldState — finalize reads the live subsystem to capture facts and game time.
    FinalizeInFlightSession();

    if (UWorldStateSubsystem* WS = CachedWorldState.Get())
    {
        if (OnAnyFactChangedHandle.IsValid())
        {
            WS->OnAnyFactChanged.Remove(OnAnyFactChangedHandle);
        }
    }
    OnAnyFactChangedHandle.Reset();

    CachedWorldState.Reset();
    bIsActive = false;
    UE_LOG(LogSimpleCoreEditor, Display, TEXT("FSimpleCorePIEDebugChannel : PIE ended"));
    OnDebugActiveChanged.Broadcast();
}

bool FSimpleCorePIEDebugChannel::ResolvePIESubsystems()
{
    if (!GEditor)
    {
        UE_LOG(LogSimpleCoreEditor, Warning, TEXT("FSimpleCorePIEDebugChannel::ResolvePIESubsystems : GEditor is null"));
        return false;
    }

    // GEditor->PlayWorld covers PIE + SIE cleanly. Fallback to world-context iteration for rare dedicated-server-only
    // startup paths where PlayWorld isn't populated. Mirrors FQuestPIEDebugChannel.
    UWorld* PIEWorld = GEditor->PlayWorld;
    if (!PIEWorld)
    {
        for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
        {
            if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
            {
                PIEWorld = Ctx.World();
                break;
            }
        }
    }

    if (!PIEWorld)
    {
        UE_LOG(LogSimpleCoreEditor, Warning, TEXT("FSimpleCorePIEDebugChannel::ResolvePIESubsystems : no PIE/SIE world found"));
        return false;
    }

    UGameInstance* GI = PIEWorld->GetGameInstance();
    if (!GI)
    {
        UE_LOG(LogSimpleCoreEditor, Warning, TEXT("FSimpleCorePIEDebugChannel::ResolvePIESubsystems : world '%s' has no GameInstance"),
            *PIEWorld->GetName());
        return false;
    }

    CachedWorldState = GI->GetSubsystem<UWorldStateSubsystem>();

    UE_LOG(LogSimpleCoreEditor, Display, TEXT("FSimpleCorePIEDebugChannel::ResolvePIESubsystems : world='%s', WorldState=%s"),
        *PIEWorld->GetName(),
        CachedWorldState.IsValid() ? TEXT("resolved") : TEXT("NULL"));

    return CachedWorldState.IsValid();
}

void FSimpleCorePIEDebugChannel::BeginNewSession()
{
    FWorldStateSessionSnapshot NewSession;
    NewSession.SessionNumber = NextSessionNumber++;
    NewSession.SessionStartRealTime = FPlatformTime::Seconds();
    NewSession.bInFlight = true;
    SessionHistory.Add(MoveTemp(NewSession));

    // FIFO trim — drop oldest entries until under the cap. RemoveAt(0) preserves chronological index order.
    while (SessionHistory.Num() > MaxStoredSessions)
    {
        SessionHistory.RemoveAt(0);
    }

    UE_LOG(LogSimpleCoreEditor, Verbose, TEXT("FSimpleCorePIEDebugChannel::BeginNewSession : session #%d started, %d total in history"),
        SessionHistory.Last().SessionNumber, SessionHistory.Num());

    OnSessionHistoryChanged.Broadcast();
}

void FSimpleCorePIEDebugChannel::FinalizeInFlightSession()
{
    if (SessionHistory.IsEmpty()) return;
    FWorldStateSessionSnapshot& Latest = SessionHistory.Last();
    if (!Latest.bInFlight) return;  // defensive — paired Begin/Finalize means this shouldn't happen, but no-op gracefully

    if (const UWorldStateSubsystem* WS = CachedWorldState.Get())
    {
        Latest.Facts = WS->GetAllFacts();
        if (const UWorld* World = WS->GetWorld())
        {
            Latest.EndedAtGameTime = World->GetTimeSeconds();
        }
    }
    Latest.bInFlight = false;

    UE_LOG(LogSimpleCoreEditor, Verbose, TEXT("FSimpleCorePIEDebugChannel::FinalizeInFlightSession : session #%d closed at t=%.2fs with %d fact(s)"),
        Latest.SessionNumber, Latest.EndedAtGameTime, Latest.Facts.Num());

    OnSessionHistoryChanged.Broadcast();
}

void FSimpleCorePIEDebugChannel::HandleAnyFactChanged()
{
    OnSessionHistoryChanged.Broadcast();
}

const FWorldStateSessionSnapshot* FSimpleCorePIEDebugChannel::GetSessionByIndex(int32 Index) const
{
    return SessionHistory.IsValidIndex(Index) ? &SessionHistory[Index] : nullptr;
}

const TMap<FGameplayTag, int32>& FSimpleCorePIEDebugChannel::GetFactsForSession(int32 Index) const
{
    static const TMap<FGameplayTag, int32> Empty;
    if (!SessionHistory.IsValidIndex(Index)) return Empty;
    const FWorldStateSessionSnapshot& Session = SessionHistory[Index];
    if (Session.bInFlight)
    {
        const UWorldStateSubsystem* WS = CachedWorldState.Get();
        return WS ? WS->GetAllFacts() : Empty;
    }
    return Session.Facts;
}

