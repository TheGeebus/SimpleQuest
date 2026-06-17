// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "TypewriterTextBlock.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDisplayTextStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDisplayTextEnd);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFinalDisplayDelayEnd);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharAdded, FString, AddedCharacters);

/**
 * A text widget that can queue messages to display, one after another, optionally using a typewriter effect to display
 * each character one at a time. Queue messages by calling QueueText or QueueTexts.
 *
 * Design-agnostic: styling flows through the parent UTextBlock's standard UMG properties (Font, Color, Justification,
 * ShadowOffset, etc.). Use this widget anywhere you'd use UTextBlock and configure styling in the UMG design panel
 * exactly the same way.
 *
 * - No children
 * - Text
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Typewriter Text"))
class SIMPLEUI_API UTypewriterTextBlock : public UTextBlock
{
    GENERATED_BODY()

public:
    explicit UTypewriterTextBlock(const FObjectInitializer& ObjectInitializer);

    /**
     * Sets the text to show. By default, the text passed to this function will be queued at the end of the texts to
     * display. This behavior can be altered by passing the appropriate flags.
     *
     * Empty InText is a no-op — adopters wiring lifecycle handlers to a shared widget can pass
     * unauthored FText fields through harmlessly without per-call IsEmpty checks. Call ClearTextQueue explicitly to
     * clear the display.
     * 
     * @param InText            The text to display.
     * @param bInterruptQueue   Optional: Whether to immediately interrupt the message queue and display this message instead. Default value is false.
     * @param bFlushQueue       Optional: Whether to discard all other queued messages. If this is false, queued messages will resume
     *                                    after an interrupting message finishes displaying. Default value is false.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void QueueText(const FText& InText, const bool bInterruptQueue = false, const bool bFlushQueue = false);

    /**
     * Sets the text to show. By default, the array of texts passed to this function will be queued in consecutive order
     * at the end of the texts to display. This behavior can be altered by passing the appropriate flags.
     *
     * Empty entries are filtered out at queue time — unauthored beats in adopter narrative arrays
     * pass through harmlessly. If every entry is empty, the whole call is a no-op. Call ClearTextQueue explicitly to
     * clear the display.
     * 
     * @param InTexts           The array of texts to display. Their order relative to one another in this input array will be maintained.
     * @param bInterruptQueue   Optional: Whether to immediately interrupt the message queue and display these texts instead. Default value is false.
     * @param bFlushQueue       Optional: Whether to discard all other queued messages. If this is false, queued messages will resume
     *                          after any interrupting messages finish displaying. Default value is false.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void QueueTexts(const TArray<FText>& InTexts, const bool bInterruptQueue = false, const bool bFlushQueue = false);

    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void ClearTextQueue(const bool bStopDisplayImmediately);

    /** Fires whenever the widget attempts to start displaying a new text, even if that text is empty. */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnDisplayTextStart OnDisplayTextStart;
    
    /**
     * Fires whenever the widget displays the final character in a text, even when display is aborted via interrupt or ClearTextQueue.
     * Occurs prior to the natural removal of the text from the queue. Queue may have been cleared externally, check GetQueuedTexts().IsEmpty()
     * before indexing. If not cleared externally, GetQueuedTexts()[0] will return the text that just finished displaying.
     */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnDisplayTextEnd OnDisplayTextEnd;
    
    /**
     * Fires after the queue fully drains and the dwell period following the final displayed text elapses naturally.
     * Semantically: "the typewriter has reached its idle state via natural means." Happens after the removal of the prior
     * text from the queue, so check GetQueuedTexts().IsEmpty() before indexing that array.
     *
     * Fires after: the final character is displayed in the last text in a queue of any length + NextTextDelay seconds.
     *
     * Does NOT fire after: ClearTextQueue or new content arriving during the dwell period.
     *
     * Bind to OnFinalDisplayDelayEnd when you want auto-hide / fade-out / advance-to-next-state patterns where the
     * widget's display cadence should drive the next action without mirroring NextTextDelay externally.
     */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnFinalDisplayDelayEnd OnFinalDisplayDelayEnd;
    
    /** Fires following the display of a new character during the typewriter effect. Only fires when bUseTypewriterEffect is true. */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnCharAdded OnCharAdded;

    /**
     * Instantly reveal the rest of the currently-displaying text. Fires OnCharAdded with the revealed remainder
     * (matching the per-char broadcast contract — adopter-side audio / particle hooks fire as if all remaining
     * characters appeared in one tick), then fires OnDisplayTextEnd and starts the standard NextTextDelay dwell.
     *
     * No-op when no typewriter is in flight (idle or in dwell — the text is already fully shown). Bind to a player
     * input event to give the "press to reveal" affordance common to dialogue UIs.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void SkipToEndOfCurrentText();

    /**
     * Instantly reveal the rest of the currently-displaying text, then immediately advance to the next queued
     * text without waiting for the NextTextDelay dwell. If the queue drains as a result, fires
     * OnFinalDisplayDelayEnd (the queue drained naturally — the user just accelerated the cadence).
     *
     * No-op when idle. From a dwell state (text already finished, waiting between texts), skips just the
     * dwell. From a mid-typewriter state, also reveals the in-flight text's remainder first.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void SkipToNextText();

    /**
     * Skip-the-cutscene primitive. Reveals the currently-displaying text's remainder + fires Start/End pairs
     * for every queued text in sequence (preserving event-pairing contract for telemetry / persistence), then
     * fires OnFinalDisplayDelayEnd. After the call, the queue is empty and the widget is idle.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void SkipAllRemaining();

    UFUNCTION(BlueprintPure, Category = "Typewriter")
    bool IsDisplaying() const;

    UFUNCTION(BlueprintPure, Category = "Typewriter")
    bool IsInDwell() const;

    UFUNCTION(BlueprintPure, Category = "Typewriter")
    bool IsIdle() const;

    /** 0-1 progress of the currently-displaying text. Returns 1.0 when no text is in flight (idle or dwell). */
    UFUNCTION(BlueprintPure, Category = "Typewriter")
    float GetCurrentDisplayProgress() const;

    /**
     * Insert text at a specific position in the queue. Clamps Index to [0, Num()]. If the widget is idle and
     * the new text lands at index 0, starts displaying. Existing in-flight display is unaffected unless Index
     * is 0 AND the widget is idle — explicit interrupt is still QueueText(..., bInterruptQueue=true).
     *
     * Empty InText is a no-op (matches QueueText / QueueTexts contract).
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void InsertTextAt(const FText& InText, const int32 Index);

    /**
     * Remove the queued text at Index. Index 0 (the currently-displaying text) aborts the in-flight display
     * and advances to the next — equivalent to SkipToNextText. Other indices just shrink the queue without
     * affecting display.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void RemoveTextAt(const int32 Index);

    /**
     * Replace the queued text at Index. Index 0 (the currently-displaying text) restarts display with the
     * new text (preserves Start/End event pairing for the abandoned in-flight). Other indices just swap.
     *
     * Empty InText is a no-op — keeps the existing queued text at Index untouched. Adopters
     * who want to remove the entry call RemoveTextAt instead.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void ReplaceTextAt(const FText& InText, const int32 Index);

    /**
     * Pause the typewriter / dwell at its current point. Remaining time on whichever timer is active is saved
     * and the timer is cleared. Resume restarts with the saved remaining time so the cadence picks up exactly
     * where it left off. Useful for modal interruptions — open a sub-menu, the dialogue pauses; close it, the
     * dialogue continues from the same character.
     *
     * No-op if already paused, or if neither a typewriter nor a dwell is active.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void PauseTypewriter();

    /** Resume from a previous PauseTypewriter call. No-op if not currently paused. */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void ResumeTypewriter();

    UFUNCTION(BlueprintPure, Category = "Typewriter")
    bool IsPaused() const { return bIsPaused; }
    
protected:
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    void DisplayQueuedTexts();
    void AppendDisplayText();
    void HandleDisplayTextEnd();
    void HandleNextTextDelayEnd();
    void HandleFinalDisplayDelayEnd() const;
    
    /**
     * Preserves Start/End event pairing for any abort/interrupt path. If a text is in-flight — including a
     * paused one — broadcasts OnDisplayTextEnd before the abort/interrupt destroys its display state. No-op
     * when no in-flight typewriter exists. Called from any path that's about to kill an in-flight display
     * (ClearTextQueue with bStopDisplayImmediately, or DisplayQueuedTexts triggered by a Queue*Text with
     * bFlushQueue or bInterruptQueue while another text is mid-reveal).
     */
    void FireEndForInflightDisplay() const;

    /**
     * True when a typewriter is in flight, including a paused one. Read by FireEndForInflightDisplay and the
     * skip functions so paused-in-flight texts are treated as still-active for abort / completion purposes.
     */
    bool HasInflightTypewriter() const;

    /** True when a NextTextDelay dwell is in flight, including a paused one. */
    bool HasInflightDwell() const;

    UPROPERTY(Transient)
    TArray<FText> QueuedTexts;

    /**
     * Enable character-by-character display with a randomized delay between each character defined by DisplayDelayMinMax.
     * If this is false, each queued text will display after NextTextDelay seconds.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter")
    bool bUseTypewriterEffect = true;

    /**
     * Whether to append any consecutive space characters in the provided text all at once (true) or one-by-one (false).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter", AdvancedDisplay)
    bool bTreatConsecutiveSpacesAsOne = true;

    /**
     * The delay between the display of each character when using the typewriter effect.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter")
    FVector2D DisplayDelayMinMax{0.04f, 0.1f};

    /**
     * The delay between the display of each text in the queue. If using a typewriter effect, it is the delay following
     * the display of the final character in a text before the next text in the queue begins to display.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter")
    float NextTextDelay{5.f};

    FString DisplayString{};
    FString FullString{};
    int32 CharacterIndex{0};
    FTimerHandle CharacterDisplayTimerHandle;
    FTimerHandle NextTextDelayHandle;

    /** Paused-state snapshot, captured by PauseTypewriter and consumed by ResumeTypewriter. */
    bool bIsPaused = false;
    float PausedRemainingCharDelay = 0.0f;
    float PausedRemainingDwell = 0.0f;    

public:
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void GetQueuedTexts(TArray<FText>& OutQueuedTexts) const;

    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void SetUseTypewriterEffect(const bool InUseTypewriterEffect);
    FORCEINLINE bool GetUseTypewriterEffect() const { return bUseTypewriterEffect; }

    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void SetDisplayDelayMinMax(const FVector2D& InDisplayDelayRange);
    FORCEINLINE FVector2D GetDisplayDelayMinMax() const { return DisplayDelayMinMax; }

    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void SetNextTextDelay(const float InNextTextDelay);
    FORCEINLINE float GetNextTextDelay() const { return NextTextDelay; }

    void SetTreatConsecutiveSpacesAsOne(const bool InTreatAsOneCharacter) { bTreatConsecutiveSpacesAsOne = InTreatAsOneCharacter; }
    FORCEINLINE bool GetTreatConsecutiveSpacesAsOne() const { return bTreatConsecutiveSpacesAsOne; }
};