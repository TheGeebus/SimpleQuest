// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Widgets/TypewriterTextBlock.h"
#include "Utilities/SimpleUILog.h"
#include "Engine/World.h"
#include "TimerManager.h"

UTypewriterTextBlock::UTypewriterTextBlock(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UTypewriterTextBlock::QueueText(const FString& InText, const bool bInterruptQueue, const bool bFlushQueue)
{
    if (bFlushQueue)
    {
        QueuedStrings.Empty();
    }

    if (bInterruptQueue)
    {
        QueuedStrings.EmplaceAt(0, InText);
    }
    else
    {
        QueuedStrings.Add(InText);
    }

    // Start displaying if the post-operation queue size matches the trigger condition: count == 1 means we transitioned
    // from idle (or flushed) to a single new string; bInterruptQueue means we explicitly want to restart.
    if (QueuedStrings.Num() == 1 || bInterruptQueue)
    {
        DisplayQueuedStrings();
    }
}

void UTypewriterTextBlock::QueueTexts(const TArray<FString>& InTexts, const bool bInterruptQueue, const bool bFlushQueue)
{
    // Empty input is a no-op — avoids unwanted re-display when bInterruptQueue is true with no new content to add.
    if (InTexts.IsEmpty())
    {
        return;
    }

    if (bFlushQueue)
    {
        QueuedStrings.Empty();
    }

    if (bInterruptQueue)
    {
        // Preserve InTexts ordering at the front of the queue: InTexts[0] ends up at index 0, InTexts[1] at index 1, etc.
        // Anything previously queued (after a non-flushing interrupt) lands after InTexts in the original order.
        int32 InsertIndex = 0;
        for (const FString& AnnouncementString : InTexts)
        {
            QueuedStrings.EmplaceAt(InsertIndex, AnnouncementString);
            InsertIndex++;
        }
    }
    else
    {
        QueuedStrings.Append(InTexts);
    }

    // Start displaying if the queue went from empty to populated (idle → active) or if we just interrupted.
    if (QueuedStrings.Num() == InTexts.Num() || bInterruptQueue)
    {
        DisplayQueuedStrings();
    }
}

void UTypewriterTextBlock::ClearTextQueue(const bool bStopDisplayImmediately)
{
    QueuedStrings.Empty();

    if (bStopDisplayImmediately && IsValid(GetWorld()))
    {
        FireEndForInflightDisplay();
        SetText(FText());
        GetWorld()->GetTimerManager().ClearTimer(NextStringDelayHandle);
        GetWorld()->GetTimerManager().ClearTimer(CharacterDisplayTimerHandle);

        // Paused state is tied to the queue's current head — clearing the queue invalidates it.
        bIsPaused = false;
        PausedRemainingCharDelay = 0.0f;
        PausedRemainingDwell = 0.0f;
    }
}

void UTypewriterTextBlock::SkipToEndOfCurrentString()
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown) return;

    // Valid only mid-typewriter. Dwell-state and idle have nothing to skip — the string is already shown.
    if (!HasInflightTypewriter()) return;

    bIsPaused = false;
    PausedRemainingCharDelay = 0.0f;
    PausedRemainingDwell = 0.0f;

    World->GetTimerManager().ClearTimer(CharacterDisplayTimerHandle);

    if (CharacterIndex < FullString.Len())
    {
        const FString Remainder = FullString.Mid(CharacterIndex);
        DisplayString = FullString;
        CharacterIndex = FullString.Len();
        SetText(FText::FromString(DisplayString));
        OnCharAdded.Broadcast(Remainder);
    }

    HandleDisplayStringEnd();
}

void UTypewriterTextBlock::SkipToNextString()
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown) return;

    const bool bIsTypewriting = HasInflightTypewriter();
    const bool bIsInDwell = HasInflightDwell();

    bIsPaused = false;
    PausedRemainingCharDelay = 0.0f;
    PausedRemainingDwell = 0.0f;
    
    if (!bIsTypewriting && !bIsInDwell) return;

    World->GetTimerManager().ClearTimer(CharacterDisplayTimerHandle);
    World->GetTimerManager().ClearTimer(NextStringDelayHandle);

    if (bIsTypewriting)
    {
        // Reveal the remainder + fire End for the in-flight string before advancing. Dwell-only path skips this
        // block — its End event already fired when the typewriter finished naturally.
        if (CharacterIndex < FullString.Len())
        {
            const FString Remainder = FullString.Mid(CharacterIndex);
            DisplayString = FullString;
            CharacterIndex = FullString.Len();
            SetText(FText::FromString(DisplayString));
            OnCharAdded.Broadcast(Remainder);
        }
        if (OnDisplayStringEnd.IsBound()) OnDisplayStringEnd.Broadcast();
    }

    if (!QueuedStrings.IsEmpty()) QueuedStrings.RemoveAt(0);

    if (QueuedStrings.IsEmpty())
    {
        HandleFinalDisplayDelayEnd();
    }
    else
    {
        DisplayQueuedStrings();
    }
}

void UTypewriterTextBlock::SkipAllRemaining()
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown) return;

    const bool bIsTypewriting = HasInflightTypewriter();
    World->GetTimerManager().ClearTimer(CharacterDisplayTimerHandle);
    World->GetTimerManager().ClearTimer(NextStringDelayHandle);

    bIsPaused = false;
    PausedRemainingCharDelay = 0.0f;
    PausedRemainingDwell = 0.0f;

    // Skip current in-flight string (if any) first.
    if (bIsTypewriting)
    {
        if (CharacterIndex < FullString.Len())
        {
            const FString Remainder = FullString.Mid(CharacterIndex);
            DisplayString = FullString;
            CharacterIndex = FullString.Len();
            SetText(FText::FromString(DisplayString));
            OnCharAdded.Broadcast(Remainder);
        }
        if (OnDisplayStringEnd.IsBound()) OnDisplayStringEnd.Broadcast();
    }
    if (!QueuedStrings.IsEmpty()) QueuedStrings.RemoveAt(0);

    // Fire Start/End for every remaining queued string. Adopters tracking per-string telemetry see one fire
    // per logical string regardless of whether it was revealed naturally or skipped.
    while (!QueuedStrings.IsEmpty())
    {
        FullString = QueuedStrings[0];
        DisplayString = FullString;
        CharacterIndex = FullString.Len();
        SetText(FText::FromString(DisplayString));
        OnDisplayStringStart.Broadcast();
        if (bUseTypewriterEffect && !FullString.IsEmpty())
        {
            OnCharAdded.Broadcast(FullString);
        }
        if (OnDisplayStringEnd.IsBound()) OnDisplayStringEnd.Broadcast();
        QueuedStrings.RemoveAt(0);
    }

    HandleFinalDisplayDelayEnd();
}

void UTypewriterTextBlock::ReleaseSlateResources(bool bReleaseChildren)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearAllTimersForObject(this);
    }
    Super::ReleaseSlateResources(bReleaseChildren);
}

bool UTypewriterTextBlock::IsDisplaying() const
{
    const UWorld* World = GetWorld();
    return IsValid(World) && World->GetTimerManager().IsTimerActive(CharacterDisplayTimerHandle);
}

bool UTypewriterTextBlock::IsInDwell() const
{
    const UWorld* World = GetWorld();
    return IsValid(World) && World->GetTimerManager().IsTimerActive(NextStringDelayHandle);
}

bool UTypewriterTextBlock::IsIdle() const
{
    return !IsDisplaying() && !IsInDwell() && !bIsPaused;
}

float UTypewriterTextBlock::GetCurrentDisplayProgress() const
{
    if (!IsDisplaying() || FullString.IsEmpty()) return 1.0f;
    return static_cast<float>(CharacterIndex) / static_cast<float>(FullString.Len());
}

void UTypewriterTextBlock::InsertTextAt(const FString& InText, const int32 Index)
{
    const int32 ClampedIndex = FMath::Clamp(Index, 0, QueuedStrings.Num());
    QueuedStrings.EmplaceAt(ClampedIndex, InText);

    // Auto-start when idle + the inserted string is now at the front of the queue. Mirrors QueueText's
    // count==1 trigger semantic — adopters who don't want auto-display should set bInterruptQueue or
    // bFlushQueue on the regular Queue* API instead of using Insert at index 0.
    if (IsIdle() && ClampedIndex == 0 && QueuedStrings.Num() == 1)
    {
        DisplayQueuedStrings();
    }
}

void UTypewriterTextBlock::RemoveTextAt(const int32 Index)
{
    if (!QueuedStrings.IsValidIndex(Index)) return;

    // Removing index 0 = abort the in-flight display + advance. Delegates to SkipToNextString so the event
    // sequencing matches the natural advance path.
    if (Index == 0)
    {
        SkipToNextString();
        return;
    }

    QueuedStrings.RemoveAt(Index);
}

void UTypewriterTextBlock::ReplaceTextAt(const FString& InText, const int32 Index)
{
    if (!QueuedStrings.IsValidIndex(Index)) return;

    QueuedStrings[Index] = InText;

    // Replacing the in-flight string: restart display with the new content. FireEndForInflightDisplay (via
    // DisplayQueuedStrings) preserves event pairing for the abandoned in-flight reveal.
    if (Index == 0)
    {
        DisplayQueuedStrings();
    }
}

void UTypewriterTextBlock::DisplayQueuedStrings()
{
    // Fire End for any in-flight string we're about to abandon. Covers the Queue*Text-with-bFlushQueue and
    // Queue*Text-with-bInterruptQueue mid-typewriter cases — those paths reach here with the typewriter still
    // active and the in-flight string about to be replaced. When called via the natural HandleNextStringDelayEnd
    // path, the typewriter timer is already inactive (cleared in HandleDisplayStringEnd) and the helper no-ops.
    FireEndForInflightDisplay();

    DisplayString = FString();
    CharacterIndex = 0;

    // Any new display abandons the paused-in-flight string (if any). Resetting here keeps the invariant
    // that bIsPaused only describes a paused version of the string currently at QueuedStrings[0].
    bIsPaused = false;
    PausedRemainingCharDelay = 0.0f;
    PausedRemainingDwell = 0.0f;

    UWorld* World = GetWorld();
    if (!IsValid(World) || QueuedStrings.IsEmpty())
    {
        return;
    }

    World->GetTimerManager().ClearTimer(NextStringDelayHandle);
    World->GetTimerManager().ClearTimer(CharacterDisplayTimerHandle);

    FullString = QueuedStrings[0];

    UE_LOG(LogSimpleUI, Verbose, TEXT("UTypewriterTextBlock::DisplayQueuedStrings : starting display (length=%d, queue size=%d)"),
        FullString.Len(),
        QueuedStrings.Num());

    // Empty queued string — broadcast a symmetric Start/End pair so adopters can rely on paired events, then
    // advance to the next queued entry.
    if (FullString.IsEmpty())
    {
        SetText(FText());
        OnDisplayStringStart.Broadcast();
        HandleDisplayStringEnd();
        return;
    }

    OnDisplayStringStart.Broadcast();
    AppendDisplayText();
}

void UTypewriterTextBlock::AppendDisplayText()
{
    // Defensive against late-firing timers post-destruction or world teardown.
    if (HasAnyFlags(RF_BeginDestroyed))
    {
        return;
    }
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown)
    {
        return;
    }

    if (CharacterIndex >= FullString.Len())
    {
        HandleDisplayStringEnd();
        return;
    }

    if (bUseTypewriterEffect)
    {
        // Append next character. If consecutive-space coalescing is enabled, keep appending while consecutive spaces
        // run so a multi-space gap reveals in one tick rather than one-space-per-tick. The OnCharAdded broadcast
        // carries the actual chunk (one character normally; multiple coalesced spaces when applicable), matching
        // the delegate's plural FString naming.
        const int32 ChunkStart = CharacterIndex;
        while (CharacterIndex < FullString.Len())
        {
            const TCHAR Char = FullString[CharacterIndex];
            DisplayString.AppendChar(Char);
            CharacterIndex++;

            const bool bCoalesceConsecutiveSpaces = bTreatConsecutiveSpacesAsOne
                && Char == TEXT(' ')
                && CharacterIndex < FullString.Len()
                && FullString[CharacterIndex] == TEXT(' ');
            if (!bCoalesceConsecutiveSpaces)
            {
                break;
            }
        }

        const FString AppendedChunk = FullString.Mid(ChunkStart, CharacterIndex - ChunkStart);
        OnCharAdded.Broadcast(AppendedChunk);
    }
    else
    {
        // Non-typewriter mode: show the full string immediately. Advance CharacterIndex so the end-of-string check
        // below evaluates consistently.
        DisplayString = FullString;
        CharacterIndex = FullString.Len();
    }

    SetText(FText::FromString(DisplayString));

    if (!bUseTypewriterEffect || CharacterIndex >= FullString.Len())
    {
        HandleDisplayStringEnd();
    }
    else
    {
        // Schedule the next character append. FMath::Min/Max defensively sort so a designer-supplied reversed
        // (Max, Min) range still produces valid RandRange output.
        const float MinDelay = FMath::Min(DisplayDelayMinMax.X, DisplayDelayMinMax.Y);
        const float MaxDelay = FMath::Max(DisplayDelayMinMax.X, DisplayDelayMinMax.Y);
        World->GetTimerManager().SetTimer(CharacterDisplayTimerHandle,
            this,
            &ThisClass::AppendDisplayText,
            FMath::RandRange(MinDelay, MaxDelay),
            false);
    }
}

void UTypewriterTextBlock::HandleDisplayStringEnd()
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown)
    {
        return;
    }

    World->GetTimerManager().ClearTimer(CharacterDisplayTimerHandle);
    if (OnDisplayStringEnd.IsBound()) OnDisplayStringEnd.Broadcast();

    UE_LOG(LogSimpleUI, Verbose, TEXT("UTypewriterTextBlock::HandleDisplayStringEnd : display complete (queue remaining=%d)"),
        QueuedStrings.Num());
    
    World->GetTimerManager().SetTimer(NextStringDelayHandle, this, &ThisClass::HandleNextStringDelayEnd, NextStringDelay);
}

void UTypewriterTextBlock::HandleNextStringDelayEnd()
{
    // Late-fire guard — if the world tore down between timer-set and timer-fire, skip the transition entirely.
    const UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown)
    {
        return;
    }    

    // Empty queue at this point means the queue was externally drained (ClearTextQueue, etc.) — the natural-completion
    // semantic doesn't apply, so we bail without popping or broadcasting OnFinalDisplayDelayEnd. The in-flight string's
    // own OnDisplayStringEnd already fired in HandleDisplayStringEnd; that paired event survives the abort. Only the
    // "queue naturally drained" semantic suppresses.
    if (QueuedStrings.IsEmpty()) return;
    QueuedStrings.RemoveAt(0);
        
    if (QueuedStrings.IsEmpty())
    {
        HandleFinalDisplayDelayEnd();
    }
    else
    {
        DisplayQueuedStrings();
    }
}

void UTypewriterTextBlock::HandleFinalDisplayDelayEnd() const
{
    if (OnFinalDisplayDelayEnd.IsBound()) OnFinalDisplayDelayEnd.Broadcast();
}

void UTypewriterTextBlock::FireEndForInflightDisplay() const
{
    if (HasInflightTypewriter())
    {
        if (OnDisplayStringEnd.IsBound()) OnDisplayStringEnd.Broadcast();
    }
}

void UTypewriterTextBlock::PauseTypewriter()
{
    if (bIsPaused) return;

    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown) return;

    FTimerManager& Timers = World->GetTimerManager();
    bool bSavedAnything = false;

    if (Timers.IsTimerActive(CharacterDisplayTimerHandle))
    {
        PausedRemainingCharDelay = Timers.GetTimerRemaining(CharacterDisplayTimerHandle);
        Timers.ClearTimer(CharacterDisplayTimerHandle);
        bSavedAnything = true;
    }
    if (Timers.IsTimerActive(NextStringDelayHandle))
    {
        PausedRemainingDwell = Timers.GetTimerRemaining(NextStringDelayHandle);
        Timers.ClearTimer(NextStringDelayHandle);
        bSavedAnything = true;
    }

    // Only flip the paused flag if something was actually saved — pausing while idle is a clean no-op rather
    // than a state-engagement that future Resume calls would have to interpret.
    if (bSavedAnything)
    {
        bIsPaused = true;
        UE_LOG(LogSimpleUI, Verbose,
            TEXT("UTypewriterTextBlock::PauseTypewriter : paused (charDelayRemaining=%.3f, dwellRemaining=%.3f)"),
            PausedRemainingCharDelay, PausedRemainingDwell);
    }
}

void UTypewriterTextBlock::ResumeTypewriter()
{
    if (!bIsPaused) return;

    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown) return;

    FTimerManager& Timers = World->GetTimerManager();

    // Typewriter and dwell states are mutually exclusive per pause cycle, but the if-chain handles both for
    // robustness — at most one branch runs per Resume call.
    if (PausedRemainingCharDelay > 0.0f)
    {
        Timers.SetTimer(CharacterDisplayTimerHandle, this, &ThisClass::AppendDisplayText, PausedRemainingCharDelay, false);
    }
    if (PausedRemainingDwell > 0.0f)
    {
        Timers.SetTimer(NextStringDelayHandle, this, &ThisClass::HandleNextStringDelayEnd, PausedRemainingDwell);
    }

    UE_LOG(LogSimpleUI, Verbose, TEXT("UTypewriterTextBlock::ResumeTypewriter : resumed"));

    bIsPaused = false;
    PausedRemainingCharDelay = 0.0f;
    PausedRemainingDwell = 0.0f;
}

bool UTypewriterTextBlock::HasInflightTypewriter() const
{
    const UWorld* World = GetWorld();
    if (!IsValid(World)) return false;
    return World->GetTimerManager().IsTimerActive(CharacterDisplayTimerHandle)
        || (bIsPaused && PausedRemainingCharDelay > 0.0f);
}

bool UTypewriterTextBlock::HasInflightDwell() const
{
    const UWorld* World = GetWorld();
    if (!IsValid(World)) return false;
    return World->GetTimerManager().IsTimerActive(NextStringDelayHandle)
        || (bIsPaused && PausedRemainingDwell > 0.0f);
}

void UTypewriterTextBlock::GetQueuedStrings(TArray<FString>& OutQueuedStrings) const
{
    OutQueuedStrings = QueuedStrings;
}

void UTypewriterTextBlock::SetDisplayDelayMinMax(const FVector2D& InDisplayDelayRange)
{
    DisplayDelayMinMax = InDisplayDelayRange;

    // Re-schedule the active character timer with the new range so designers can speed up / slow down mid-
    // display (e.g., a held fast-forward button). The current timer's elapsed time is discarded — the new
    // range governs from now. Trade-off accepted: precise residual-time math would compound complexity for
    // negligible designer-visible benefit.
    UWorld* World = GetWorld();
    if (!IsValid(World) || !World->GetTimerManager().IsTimerActive(CharacterDisplayTimerHandle)) return;

    const float MinDelay = FMath::Min(DisplayDelayMinMax.X, DisplayDelayMinMax.Y);
    const float MaxDelay = FMath::Max(DisplayDelayMinMax.X, DisplayDelayMinMax.Y);
    World->GetTimerManager().SetTimer(CharacterDisplayTimerHandle,
        this, &ThisClass::AppendDisplayText,
        FMath::RandRange(MinDelay, MaxDelay), false);
}

void UTypewriterTextBlock::SetNextStringDelay(const float InNextStringDelay)
{
    NextStringDelay = InNextStringDelay;

    // Re-schedule the active dwell timer if running. Same elapsed-time-discarded contract as
    // SetDisplayDelayMinMax — the new dwell value governs from now.
    UWorld* World = GetWorld();
    if (!IsValid(World) || !World->GetTimerManager().IsTimerActive(NextStringDelayHandle)) return;

    World->GetTimerManager().SetTimer(NextStringDelayHandle,
        this, &ThisClass::HandleNextStringDelayEnd, NextStringDelay);
}

void UTypewriterTextBlock::SetUseTypewriterEffect(const bool InUseTypewriterEffect)
{
    const bool bWasUsingTypewriter = bUseTypewriterEffect;
    bUseTypewriterEffect = InUseTypewriterEffect;

    // Mid-typewriter ON-to-OFF toggle: skip to end of current string. "Stop being patient about per-char
    // reveal; show everything from here." The OFF-to-ON direction is a no-op — if the OFF-mode short-circuit
    // had already revealed the full string and we're in dwell, there's no typewriter state to "resume." Future
    // queued strings will use typewriter mode naturally.
    if (bWasUsingTypewriter && !InUseTypewriterEffect)
    {
        SkipToEndOfCurrentString();
    }
}
