// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestActivationBlocker.generated.h"

/**
 * Reason a quest is not currently activatable / startable. One enum value per distinct cause; the structured
 * FQuestActivationBlocker carries the enum plus any contextual data (e.g., which prereq leaves are unsatisfied
 * for the PrereqUnmet case). Returned in arrays by UQuestManagerSubsystem::QueryQuestActivationBlockers and
 * FQuestGiveBlockedEvent so designer-side dialogue / UI can surface contextual refusal text.
 */
UENUM(BlueprintType)
enum class EQuestActivationBlocker : uint8
{
	/** One or more prereq leaves are unsatisfied. UnsatisfiedLeafTags lists which. */
	PrereqUnmet,

	/** Quest's Blocked fact is set — externally locked out via SetBlocked. ClearBlocked required to re-enable. */
	Blocked,

	/** Quest's Live fact is set — already running. Cannot be re-given while active. */
	AlreadyLive,

	/** Quest is not in a giver-gated PendingGiver state — the giver isn't currently offering it. */
	NotPendingGiver,

	/** QuestTag isn't registered in the runtime tag manager. Stale or never-compiled tag. */
	UnknownQuest,
};

/**
 * Structured "why can't this quest be started right now" entry. Returned in arrays from the activation-blocker
 * query API and the give-blocked event. One entry per distinct blocker condition; an empty array means the
 * quest is currently startable.
 *
 * UnsatisfiedLeafTags is populated only when Reason == PrereqUnmet; for other reasons the array is empty.
 * Designers consuming this branch on Reason and produce contextual dialogue / UI accordingly.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestActivationBlocker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest|Blocker")
	EQuestActivationBlocker Reason = EQuestActivationBlocker::PrereqUnmet;

	/** For Reason == PrereqUnmet: leaf tags that evaluated false. Empty for all other reasons. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest|Blocker")
	TArray<FGameplayTag> UnsatisfiedLeafTags;
};