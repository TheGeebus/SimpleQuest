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
    CachedWorldState.Reset();
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
    OnDebugActiveChanged.Broadcast();
}

void FSimpleCorePIEDebugChannel::HandleEndPIE(bool bIsSimulating)
{
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

