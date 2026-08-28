// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestAdvancementHold.generated.h"

/**
 * Opaque handle to one active advancement hold, returned by UQuestManagerSubsystem::HoldQuestAdvancement and passed
 * back to release it.
 *
 * Deliberately carries only an id. The quest tag and reason live in the manager's registry, so a caller cannot alter
 * what it holds after the fact, and nothing here points at the holder - a hold outlives the object that placed it,
 * and in a networked game the holder may not even be on this machine.
 *
 * Zero is never issued and means "no hold" - the value a default-constructed handle carries, and what a refused hold
 * returns.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestAdvancementHold
{
	GENERATED_BODY()

	/** Registry key for this hold. Zero means no hold. */
	UPROPERTY(BlueprintReadOnly, Category = "SimpleQuest|Advancement")
	int32 Id = 0;

	/** True when this handle refers to a hold that was actually issued. Does not mean the hold is still active. */
	bool IsValid() const { return Id != 0; }

	bool operator==(const FQuestAdvancementHold& Other) const { return Id == Other.Id; }
};

