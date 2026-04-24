// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Objectives/QuestObjective.h"
#include "CountingQuestObjective.generated.h"

/**
 * Objective subclass that provides a built-in counter (CurrentElements / MaxElements). Initializes MaxElements from
 * the NumElementsRequired parameter in OnObjectiveActivated.
 *
 * Designers have two paths:
 *   - AddProgress: one-call convenience — increments, checks threshold, fires progress or completion.
 *   - SetCurrentElements + explicit ReportProgress/CompleteObjectiveWithOutcome: manual control.
 *
 * Non-counting objectives (binary triggers, multi-counter, phase-based) should inherit from UQuestObjective directly and
 * use ReportProgress for custom progress semantics.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UCountingQuestObjective : public UQuestObjective
{
    GENERATED_BODY()

protected:
    virtual void OnObjectiveActivated_Implementation(const FQuestObjectiveActivationParams& Params) override;

    /**
     * Increments CurrentElements by Amount, then either completes (if threshold met) or reports progress. Fires exactly
     * one event — never both. Returns true if the objective completed.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
    bool AddProgress(const FQuestObjectiveContext& InContext, FGameplayTag OutcomeTag, int32 Amount = 1);

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
    int32 MaxElements = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
    int32 CurrentElements = 0;

public:
    FORCEINLINE int32 GetMaxElements() const { return MaxElements; }
    FORCEINLINE int32 GetCurrentElements() const { return CurrentElements; }

    /** Calls ReportProgress when the value changes. */
    UFUNCTION(BlueprintCallable)
    void SetCurrentElements(const int32 NewAmount);
};