// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "DeactivateQuestNode.generated.h"

/**
 * Runtime instance for the editor "Deactivate Quest" node - the counterpart to Start Questline, and the graph-native
 * half of USimpleQuestBlueprintLibrary::DeactivateQuest.
 *
 * Tears down each target without blocking it. Set Blocked's bAlsoDeactivateTargets reaches the same teardown, but it
 * writes the Blocked fact too, which persists as a re-entry gate - right for "nothing may start this", wrong for
 * "this is finished with". A target named here can be started again immediately.
 *
 * A target may be a Step, a container, or a whole questline's identity tag. The questline case needs no special
 * handling here: the manager's deactivation cascade falls back to the asset's own Entry-node Deactivated routing for
 * tags that mint no runtime instance, and a questline's Live fact is derived from its Steps rather than owned, so it
 * clears itself once they are down.
 */
UCLASS()
class SIMPLEQUEST_API UDeactivateQuestNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

protected:
	/** The quest node, container, or questline identity tags to deactivate. */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer TargetQuestTags;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};

