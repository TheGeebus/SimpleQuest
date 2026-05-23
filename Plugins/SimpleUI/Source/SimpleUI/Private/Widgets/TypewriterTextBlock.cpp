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
        // Fire End for any in-flight string BEFORE clearing the timers — the helper's IsTimerActive check needs the
        // typewriter timer still set to recognize that a display was in progress.
        FireEndForInflightDisplay();

        SetText(FText());
        GetWorld()->GetTimerManager().ClearTimer(NextStringDelayHandle);
        GetWorld()->GetTimerManager().ClearTimer(CharacterDisplayTimerHandle);
    }
}

void UTypewriterTextBlock::ReleaseSlateResources(bool bReleaseChildren)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearAllTimersForObject(this);
    }
    Super::ReleaseSlateResources(bReleaseChildren);
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

void UTypewriterTextBlock::FireEndForInflightDisplay()
{
    const UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    if (World->GetTimerManager().IsTimerActive(CharacterDisplayTimerHandle))
    {
        if (OnDisplayStringEnd.IsBound()) OnDisplayStringEnd.Broadcast();
    }
}

void UTypewriterTextBlock::GetQueuedStrings(TArray<FString>& OutQueuedStrings) const
{
    OutQueuedStrings = QueuedStrings;
}
