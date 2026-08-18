// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/QuestRewardBase.h"

#include "SimpleQuestLog.h"
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
	return {};						// pure adapter — nothing to advertise; concrete rewards override this
}

TArray<FQuestRewardPreview> UQuestRewardBase::DispatchDescribeReward(AActor* Viewer) const
{
	return DescribeReward(Viewer);		// routes to BP overrides
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
		Recipient ? TEXT("yes") : TEXT("no — defaults to instigator"));

	PendingGrants.Add(MoveTemp(Grant));
}
