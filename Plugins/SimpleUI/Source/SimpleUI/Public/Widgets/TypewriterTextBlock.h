// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "TypewriterTextBlock.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDisplayStringStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDisplayStringEnd);
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
     * Sets the text to show. By default, the string passed to this function will be queued at the end of the strings to
     * display. This behavior can be altered by passing the appropriate flags.
     * 
     * @param InText            The string to display.
     * @param bInterruptQueue   Optional: Whether to immediately interrupt the message queue and display this message instead. Default value is false.
     * @param bFlushQueue       Optional: Whether to discard all other queued messages. If this is false, queued messages will resume
     *                                    after an interrupting message finishes displaying. Default value is false.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void QueueText(const FString& InText, const bool bInterruptQueue = false, const bool bFlushQueue = false);

    /**
     * Sets the text to show. By default, the array of strings passed to this function will be queued in consecutive order
     * at the end of the strings to display. This behavior can be altered by passing the appropriate flags.
     * 
     * @param InTexts           The array of strings to display. Their order relative to one another in this input array will be maintained.
     * @param bInterruptQueue   Optional: Whether to immediately interrupt the message queue and display these strings instead. Default value is false.
     * @param bFlushQueue       Optional: Whether to discard all other queued messages. If this is false, queued messages will resume
     *                          after any interrupting messages finish displaying. Default value is false.
     */
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void QueueTexts(const TArray<FString>& InTexts, const bool bInterruptQueue = false, const bool bFlushQueue = false);

    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void ClearTextQueue(const bool bStopDisplayImmediately);

    /** Fires whenever the widget attempts to start displaying a new string, even if that string is empty. */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnDisplayStringStart OnDisplayStringStart;
    /**
     * Fires whenever the widget displays the final character in a string, even when display is aborted via interrupt or ClearTextQueue.
     * Occurs prior to the natural removal of the string from the queue. Queue may have been cleared externally, check GetQueuedStrings().IsEmpty()
     * before indexing. If not cleared externally, GetQueuedStrings()[0] will return the string that just finished displaying.
     */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnDisplayStringEnd OnDisplayStringEnd;
    /**
     * Fires after the queue fully drains and the dwell period following the final displayed string elapses naturally.
     * Semantically: "the typewriter has reached its idle state via natural means." Happens after the removal of the prior
     * string from the queue, so check GetQueuedStrings().IsEmpty() before indexing that array.
     *
     * Fires after: the final character is displayed in the last string in a queue of any length + NextStringDelay seconds.
     *
     * Does NOT fire after: ClearTextQueue or new content arriving during the dwell period.
     *
     * Bind to OnFinalDisplayDelayEnd when you want auto-hide / fade-out / advance-to-next-state patterns where the
     * widget's display cadence should drive the next action without mirroring NextStringDelay externally.
     */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnFinalDisplayDelayEnd OnFinalDisplayDelayEnd;
    /** Fires following the display of a new character during the typewriter effect. Only fires when bUseTypewriterEffect is true. */
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Typewriter")
    FOnCharAdded OnCharAdded;

protected:
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:

    void DisplayQueuedStrings();
    void AppendDisplayText();
    void HandleDisplayStringEnd();
    void HandleNextStringDelayEnd();
    void HandleFinalDisplayDelayEnd() const;
    /**
     * Preserves Start/End event pairing for any abort/interrupt path. If a string is mid-typewriter
     * (CharacterDisplayTimerHandle is active), broadcasts OnDisplayStringEnd for that in-flight string before the
     * abort/interrupt destroys its typewriter state. No-op when no typewriter is active. Called from any path that's
     * about to kill an in-flight display (ClearTextQueue with bStopDisplayImmediately, or DisplayQueuedStrings being
     * triggered by a Queue*Text with bFlushQueue or bInterruptQueue while another string is mid-reveal).
     */
    void FireEndForInflightDisplay();

    UPROPERTY(Transient)
    TArray<FString> QueuedStrings;

    /**
     * Enable character-by-character display with a randomized delay between each character defined by DisplayDelayMinMax.
     * If this is false, each queued string will display after NextStringDelay seconds.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter")
    bool bUseTypewriterEffect = true;

    /**
     * Whether to append any consecutive space characters in the provided string all at once (true) or one-by-one (false).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter", AdvancedDisplay)
    bool bTreatConsecutiveSpacesAsOne = true;

    /**
     * The delay between the display of each character when using the typewriter effect.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter")
    FVector2D DisplayDelayMinMax{0.04f, 0.1f};

    /**
     * The delay between the display of each string in the queue. If using a typewriter effect, it is the delay following
     * the display of the final character in a string before the next string in the queue begins to display.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "Typewriter")
    float NextStringDelay{5.f};

    FString DisplayString{};
    FString FullString{};
    int32 CharacterIndex{0};
    FTimerHandle CharacterDisplayTimerHandle;
    FTimerHandle NextStringDelayHandle;

public:
    UFUNCTION(BlueprintCallable, Category = "Typewriter")
    void GetQueuedStrings(TArray<FString>& OutQueuedStrings) const;

    void SetUseTypewriterEffect(const bool InUseTypewriterEffect) { bUseTypewriterEffect = InUseTypewriterEffect; }
    FORCEINLINE bool GetUseTypewriterEffect() const { return bUseTypewriterEffect; }

    void SetTreatConsecutiveSpacesAsOne(const bool InTreatAsOneCharacter) { bTreatConsecutiveSpacesAsOne = InTreatAsOneCharacter; }
    FORCEINLINE bool GetTreatConsecutiveSpacesAsOne() const { return bTreatConsecutiveSpacesAsOne; }

    void SetDisplayDelayMinMax(const FVector2D& InDisplayDelayRange) { DisplayDelayMinMax = InDisplayDelayRange; }
    FORCEINLINE FVector2D GetDisplayDelayMinMax() const { return DisplayDelayMinMax; }

    void SetNextStringDelay(const float InNextStringDelay) { NextStringDelay = InNextStringDelay; }
    FORCEINLINE float GetNextStringDelay() const { return NextStringDelay; }
};