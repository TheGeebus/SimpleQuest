// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Display/QuestDisplayData.h"
#include "QuestLifecycleDisplayData.generated.h"


/**
 * Sequence of narrative beats for one lifecycle event. Wrapping TArray<FText> in this struct keeps the per-outcome
 * map (CompletedBeatsByOutcome below) editable as clean per-entry rows in the Details panel and gives the "this is
 * a multi-message narration sequence" semantic a name. Designers think in beats — short sequential text reveals
 * rather than single blurbs — and the SimpleUI Typewriter widget consumes the array naturally via QueueTexts.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestNarrativeBeats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative", meta = (MultiLine = true))
	TArray<FText> Beats;
};

/**
 * Reference UQuestDisplayData subclass that organizes adopter-authored narrative beats by lifecycle event. Pair with
 * the two one-shot UPROPERTYs (DisplayName / Description) on content nodes when adopters need per-event narrative
 * sequences rather than a single description blurb for the node as a whole.
 *
 * Ships under Display/Samples/ as both a working test surface and an adopter template — the UQuestDisplayData base
 * class is a marker-only type, and concrete subclasses demonstrate how to organize richer data. Adopters can use
 * this class directly (for projects whose narrative needs match the shape) or fork it as a starting point.
 *
 * Consumer pattern (typically wired through the SimpleUI Typewriter widget or any text-display surface):
 *
 *   void UMyQuestNarrationHUD::OnQuestStarted_BP(FGameplayTag QuestTag, FGameplayTag MatchedChannel, FQuestEventPayload Payload, AActor* GiverActor)
 *   {
 *       UQuestStateSubsystem* QSS = GetGameInstance()->GetSubsystem<UQuestStateSubsystem>();
 *       if (UQuestLifecycleDisplayData* Narrative = Cast<UQuestLifecycleDisplayData>(QSS->GetDisplayData(QuestTag)))
 *       {
 *           if (!Narrative->StartedBeats.IsEmpty())
 *           {
 *               TypewriterWidget->QueueTexts(Narrative->StartedBeats);
 *           }
 *       }
 *   }
 *
 * Each beat is FText so localization, FText::Format substitutions, and rich-text markup all work through the
 * existing UE text pipeline. CompletedBeatsByOutcome is keyed by the outcome tag so per-outcome variants can be
 * authored (e.g., different narration sequences for Victory vs Defeat completions of the same quest).
 *
 * All arrays default to empty. Adopters author only the events they want narration for; UI checks IsEmpty before
 * queueing display.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestLifecycleDisplayData : public UQuestDisplayData
{
	GENERATED_BODY()

public:
	// ── Offer phase ──────────────────────────────────────────────────────────────────────────────

	/** Quest reached you — first-notice narration beats. Fires whenever the quest enters scope (giver-gated or not). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Offer Phase", meta = (MultiLine = true))
	TArray<FText> ActivatedBeats;

	/** Quest is now accept-ready (Activated AND prereqs satisfy). Distinct from Activated for designers who want
	 *  a "locked → ready" two-step UI flow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Offer Phase", meta = (MultiLine = true))
	TArray<FText> EnabledBeats;

	/** Accept-ready quest became no-longer-ready (prereqs flipped back unsatisfied). Rare; NOT-prereq edge cases. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Offer Phase", meta = (MultiLine = true))
	TArray<FText> DisabledBeats;

	/** A give attempt was refused. Narration for "you can't take this right now" feedback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Offer Phase", meta = (MultiLine = true))
	TArray<FText> GiveBlockedBeats;

	/** An activation attempt was refused (UnknownQuest / AlreadyLive / AlreadyPendingGiver / Blocked). Debug-leaning;
	 *  optional for adopters wanting refusal-fanfare gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Offer Phase", meta = (MultiLine = true))
	TArray<FText> ActivationFailedBeats;

	// ── Run phase ────────────────────────────────────────────────────────────────────────────────

	/** Quest entered Live state — objectives ticking. The "you took on the quest" narration moment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Run Phase", meta = (MultiLine = true))
	TArray<FText> StartedBeats;

	/** Objective progress tick during Live phase. Transient; UI typically updates a counter rather than displaying
	 *  full narration. Leave empty unless you want a specific tick-event narration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Run Phase", meta = (MultiLine = true))
	TArray<FText> ProgressBeats;

	/** Objective progress was refused during Live phase. Transient; UI may wish to display an effect when progress
	 *  is denied. Leave empty unless you need narration when progress is refused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|Run Phase", meta = (MultiLine = true))
	TArray<FText> ProgressRefusedBeats;

	// ── End phase ────────────────────────────────────────────────────────────────────────────────

protected:
	/**
	 * Per-outcome completion narration sequences. Designer authors one entry per outcome tag the quest can complete
	 * with; the UI looks up by the outcome carried on FQuestEndedEvent.OutcomeTag. Entries left unauthored fall
	 * through to CompletedBeatsDefault below. Private, use GetCompletedBeats to access.
	 */
	UPROPERTY(EditAnywhere, Category = "Narrative|End Phase",
		meta = (ForceInlineRow, Categories = "SimpleQuest.Outcome"))
	TMap<FGameplayTag, FQuestNarrativeBeats> CompletedBeatsByOutcome;

public:

	/** Fallback completion beats when the resolved outcome doesn't have an explicit CompletedBeatsByOutcome entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|End Phase", meta = (MultiLine = true))
	TArray<FText> CompletedBeatsDefault;

	/** Quest deactivated before completing (interrupt, cascade-deactivate, blocked-with-deactivate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|End Phase", meta = (MultiLine = true))
	TArray<FText> DeactivatedBeats;

	/** Quest's Blocked-state fact transitioned absent → present (SetBlocked utility node or BP-driven SetQuestBlocked). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|End Phase", meta = (MultiLine = true))
	TArray<FText> BlockedBeats;

	/** Quest's Blocked-state fact transitioned present → absent. Symmetric partner to BlockedBeats. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative|End Phase", meta = (MultiLine = true))
	TArray<FText> UnblockedBeats;

	// ── Convenience accessor ─────────────────────────────────────────────────────────────────────

	/**
	 * Returns the per-outcome completion beats for OutcomeTag, falling back to CompletedBeatsDefault if no entry
	 * exists for the queried outcome. Adopter UI calls this rather than CompletedBeatsByOutcome.Find to get the
	 * fallback chain for free.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Narrative")
	TArray<FText> GetCompletedBeats(FGameplayTag OutcomeTag) const
	{
		if (const FQuestNarrativeBeats* Found = CompletedBeatsByOutcome.Find(OutcomeTag))
		{
			return Found->Beats;
		}
		return CompletedBeatsDefault;
	}
};