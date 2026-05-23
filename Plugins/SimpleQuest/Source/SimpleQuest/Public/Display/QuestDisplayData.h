// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "QuestDisplayData.generated.h"

/**
 * Marker base class for adopter-extensible UI display data attached to Questline / Quest / Step content. Holds no fields
 * itself — adopters subclass to define their game's UI schema (Icon, Hints[], Callouts[], VoiceoverRef, ArtKey, parametrized
 * descriptions, etc.). Referenced per-node via the DisplayData UPROPERTY on UQuestNodeBase / UQuestlineNode_ContentBase /
 * UQuestlineGraph. Retrieved at runtime via UQuestStateSubsystem::GetDisplayData.
 *
 * Pattern mirror: same shape as the UQuestObjectiveConfig pattern from §3.4 Phase 1 — marker UPrimaryDataAsset base, adopters
 * subclass for typed schemas, the Step node carries a reference and the Objective casts on read.
 *
 * Adopter workflow:
 *   1. Subclass UQuestDisplayData and add typed UPROPERTYs for the game's UI needs.
 *   2. Create .uasset instances in the content browser, one per node that needs richer-than-DisplayName/Description data.
 *   3. Reference each instance from its node's DisplayData field.
 *   4. UI code calls UQuestStateSubsystem::GetDisplayData(Tag), casts to the expected subclass, reads typed fields.
 */
UCLASS(BlueprintType, EditInlineNew)
class SIMPLEQUEST_API UQuestDisplayData : public UPrimaryDataAsset
{
	GENERATED_BODY()
};