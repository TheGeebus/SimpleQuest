// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestResolutionRecord.generated.h"

/**
 * Rich-record layer companion to WorldState's boolean QuestState.<X>.Completed fact. WorldState answers
 * "did this quest resolve at all?" in O(1) via fact presence. This record answers "what are the details of
 * that resolution" — the specific OutcomeTag, when it resolved, how many times it has resolved across the
 * current session.
 *
 * Owned by UQuestManagerSubsystem as a TMap<FGameplayTag, FQuestResolutionRecord> keyed by quest tag.
 * Written atomically alongside the WorldState facts in UQuestManagerSubsystem::SetQuestResolved — never
 * touch one layer without the other. Queried by subscription catch-up paths to recover the outcome when
 * binding to an already-resolved quest.
 *
 * First instance of the two-layer state architecture (SimpleCore's WorldState = fast boolean layer;
 * per-plugin registries like this = typed rich-data layer). See TODO "DESIGN NOTE — TWO-LAYER STATE
 * ARCHITECTURE" for the broader pattern discussion.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestResolutionRecord
{
	GENERATED_BODY()

	/** The outcome the quest resolved with. Matches the OutcomeTag passed to SetQuestResolved. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag OutcomeTag;

	/** World time at resolution (GetTimeSeconds on the manager's world). Useful for recency filters and
	 *  chronological quest-log displays. */
	UPROPERTY(BlueprintReadOnly)
	double ResolutionTime = 0.0;

	/** How many times this quest has resolved in the current session. Subsumes the previous
	 *  UQuestManagerSubsystem::QuestCompletionCounts map — incremented each SetQuestResolved call. */
	UPROPERTY(BlueprintReadOnly)
	int32 ResolutionCount = 0;
};