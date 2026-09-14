// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/QuestRewardBase.h"

#include "SimpleQuestLog.h"
#include "Rewards/Modifiers/QuestRewardModifier.h"
#include "UObject/ObjectSaveContext.h"

void UQuestRewardBase::DispatchTryGrantReward(const FQuestRewardActivationContext& Incoming)
{
	// Route through the UFunction thunk so Blueprint overrides of the BlueprintNativeEvent fire.
	TryGrantReward(Incoming);
}

void UQuestRewardBase::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	// Pure adapter: the base grants nothing. Concrete subclasses override this; UGenericReward delivers its configured
	// RewardType + Payload. A Blueprint reward that doesn't implement TryGrantReward simply grants nothing.
}

TArray<FQuestRewardPreview> UQuestRewardBase::DescribeReward_Implementation(AActor* Viewer) const
{
	return {};						// pure adapter - nothing to advertise; concrete rewards override this
}

TArray<FQuestRewardPreview> UQuestRewardBase::DispatchDescribeReward(AActor* Viewer, FGameplayTag ResolvingQuestTag) const
{
	TArray<FQuestRewardPreview> Previews = DescribeReward(Viewer);		// routes to BP overrides

	// BUILT ONCE, HERE, rather than inside a modifier that happens to need it. Nothing has activated, so Provenance stays
	// Unknown and no outcome routed here - but the Viewer IS the actor this reward is about, and the resolving quest is the
	// same one the grant path names. Synthesizing it at the top is what lets a modifier answer both paths from one shape:
	// when this was a bare Viewer, a modifier could branch on the quest while granting and was blind to it while
	// advertising, which is how a reward ends up promising something the grant will refuse.
	FQuestRewardActivationContext AsIfActivating;
	AsIfActivating.Instigator        = Viewer;
	AsIfActivating.ResolvingQuestTag = ResolvingQuestTag;

	// *** MODIFIERS RUN HERE, not at the call sites. *** Three places ask a reward what it advertises - the reward
	// node, the manager's questline-level query, and the Blueprint library - and a pass added to each is a pass one of
	// them eventually forgets, leaving that surface promising a number nobody will receive. The grant path is the
	// asymmetric one on purpose: it lives in the node because lineage and publishing have to interleave with it.
	// NOTHING IS REMOVED HERE. A modifier that would drop the grant marks the preview with a blocker instead, so every
	// advertised reward survives to be rendered and the UI decides whether an unavailable one is greyed, filtered or
	// explained. That is why this walks forward now - there are no indices to protect from removal.
	for (FQuestRewardPreview& Preview : Previews)
	{
		ApplyModifiersToPreview(Preview, AsIfActivating);

		// Stamped AFTER the modifiers, mirroring the grant path's rule that provenance is written last and a modifier
		// cannot corrupt it.
		Preview.SourceTag  = ResolvingQuestTag;
		Preview.RewardGuid = RewardGuid;
	}
	return Previews;
}

void UQuestRewardBase::ApplyModifiersToPreview(FQuestRewardPreview& Preview,
                                               const FQuestRewardActivationContext& AsIfActivating) const
{
	for (const TObjectPtr<UQuestRewardModifier>& Modifier : Modifiers)
	{
		if (!Modifier) continue;

		// Same gate as the grant path, and deliberately at the same severity: an advertisement that silently skipped a
		// modifier is a number a player will not receive, which is worth as much noise as a grant that skipped one.
		const UScriptStruct* Payload = Preview.PreviewData.GetScriptStruct();
		if (!Modifier->HandlesPayload(Payload))
		{
			UE_LOG(LogSimpleQuestActivation, Warning,
				TEXT("%s on %s does not operate on payload '%s' - modifier skipped, preview unchanged."),
				*Modifier->GetClass()->GetName(), *GetClass()->GetName(),
				Payload ? *Payload->GetName() : TEXT("none"));
			continue;
		}

		Modifier->DispatchModifyPreview(Preview, AsIfActivating);
	}
}

void UQuestRewardBase::PostLoad()
{
	Super::PostLoad();

	// BACKFILL for a reward saved before this field existed, and it must PERSIST: minting in memory alone would hand
	// the same reward a different identity every session, so its child row key would move on every export - the exact
	// churn this identity exists to remove. One dirty, one resave, quiet afterwards.
	// This is the ONLY mint on the load path. An earlier attempt minted in PostInitProperties with an RF_NeedLoad
	// guard, on the assumption that objects being deserialized carry that flag; they do not when the property system
	// constructs them while reading an instanced array, so the mint ran during load, the value looked already-set by
	// the time this ran, and the backfill never fired.
	if (!HasAnyFlags(RF_ClassDefaultObject) && !RewardGuid.IsValid())
	{
		RewardGuid = FGuid::NewGuid();
		if (UPackage* Package = GetPackage())
		{
			Package->SetDirtyFlag(true);
		}
		UE_LOG(LogSimpleQuestCompiler, Display, TEXT("UQuestRewardBase: minted identity for a reward saved before it had one (%s). Resave to persist."),
			   *GetPathName());
	}
}

void UQuestRewardBase::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);

	// The other half, and the reason there is no mint at construction: a reward added in the details panel this
	// session was never loaded, so PostLoad never saw it. Minting here means identity is settled exactly when it has
	// to persist, and it rides the save already in progress rather than dirtying the asset a second time.
	if (!HasAnyFlags(RF_ClassDefaultObject) && !RewardGuid.IsValid())
	{
		RewardGuid = FGuid::NewGuid();
	}
}

void UQuestRewardBase::DeliverReward(FGameplayTag InRewardType, const FInstancedStruct& InPayload, AActor* Recipient)
{
	FQuestRewardContext Grant;
	Grant.RewardType = InRewardType;
	Grant.CustomData = InPayload;
	Grant.Recipient  = Recipient;		// may be null: the reward node defaults it to the activation Instigator

	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestRewardBase::DeliverReward queued grant '%s' (explicit recipient: %s)"),
		*InRewardType.ToString(),
		Recipient ? TEXT("yes") : TEXT("no - defaults to instigator"));

	PendingGrants.Add(MoveTemp(Grant));
}

bool UQuestRewardBase::ApplyModifiers(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) const
{
	for (const TObjectPtr<UQuestRewardModifier>& Modifier : Modifiers)
	{ 
		if (!Modifier) continue;

		// THE GATE IS HERE, ONCE, rather than inside each modifier: a modifier that silently did nothing to a payload
		// it cannot handle is indistinguishable from one that worked. Name both structs so the fix is obvious from the
		// log alone - the answer is either a different modifier or a different reward.
		//
		// *** IT IS NOT WHAT KEEPS THE GRANT INTACT. *** UQuestRewardAmountModifier checks the payload again before
		// touching it, so a shipped amount modifier is guarded twice and deleting this would corrupt nothing. What it
		// protects is a THIRD-PARTY modifier that declares a payload type and has no second guard of its own - plus the
		// diagnostic itself. Verified by removing it: the grant survived untouched and only the warning was lost.
		const UScriptStruct* Payload = Grant.CustomData.GetScriptStruct();
		if (!Modifier->HandlesPayload(Payload))
		{
			UE_LOG(LogSimpleQuestActivation, Warning,
				TEXT("%s on %s does not operate on payload '%s' - modifier skipped, grant unchanged."),
				*Modifier->GetClass()->GetName(), *GetClass()->GetName(),
				Payload ? *Payload->GetName() : TEXT("none"));
			continue;
		}

		if (!Modifier->DispatchModifyGrant(Grant, Incoming))
		{
			UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("%s on %s dropped the grant."),
				*Modifier->GetClass()->GetName(), *GetClass()->GetName());
			return false;
		}
	}
	return true;
}


