// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Quests/Types/QuestRewardActivationContext.h"
#include "Quests/Types/QuestRewardContext.h"
#include "Quests/Types/QuestRewardPayloads.h"
#include "Rewards/QuestLootDataTable.h"
#include "Rewards/QuestRewardBase.h"
#include "Rewards/QuestRewardModifier.h"
#include "Rewards/XPReward.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"

namespace
{
	constexpr EAutomationTestFlags ModifierTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

/**
 * Reaches in for the same reason the reward-set tests do: Modifiers and a clamp's bounds are authoring surface rather
 * than secrets, and the test should author them exactly the way a designer would.
 */
class FQuestRewardModifierTestAccess
{
public:
	static TArray<TObjectPtr<UQuestRewardModifier>>& Modifiers(UQuestRewardBase& Reward) { return Reward.Modifiers; }

	static UClampAmountModifier* MakeClamp(UObject* Outer, int32 Min, int32 Max)
	{
		UClampAmountModifier* Clamp = NewObject<UClampAmountModifier>(Outer);
		Clamp->MinAmount = Min;
		Clamp->MaxAmount = Max;
		return Clamp;
	}
};

namespace
{
	// RewardType is left invalid deliberately: ApplyModifiers never reads it, and setting one would imply it mattered.
	FQuestRewardContext MakeAmountGrant(int32 Amount)
	{
		FQuestRewardContext Grant;
		Grant.CustomData = FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount });
		return Grant;
	}
}

/**
 * Modifiers apply in ARRAY ORDER, and it takes the PAIR of runs to prove it.
 *
 * One clamp cannot distinguish an ordered walk from an unordered one, and neither can two IDENTICAL clamps - the same
 * trap the reward-set ordering test avoided by using three different classes. Two clamps with overlapping but
 * different bounds can: raising to at least 10 and then capping at 5 lands on 5, while capping at 5 and then raising
 * to at least 10 lands on 10. A run that produced the same number both ways would mean array order is not application
 * order, which is precisely the claim the Modifiers comment makes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRewardModifier_ApplyInArrayOrder, "SimpleQuest.Reward.ModifiersApplyInArrayOrder", ModifierTestFlags)
bool FQuestRewardModifier_ApplyInArrayOrder::RunTest(const FString& Parameters)
{
	UPackage* Package = CreatePackage(TEXT("/Temp/QuestRewardModifierOrder"));
	const FQuestRewardActivationContext Incoming;

	// Raise to at least 10, then cap at 5.
	{
		UXPReward* Reward = NewObject<UXPReward>(Package);
		TArray<TObjectPtr<UQuestRewardModifier>>& Mods = FQuestRewardModifierTestAccess::Modifiers(*Reward);
		Mods.Add(FQuestRewardModifierTestAccess::MakeClamp(Reward, 10, 1000));
		Mods.Add(FQuestRewardModifierTestAccess::MakeClamp(Reward, 0, 5));

		FQuestRewardContext Grant = MakeAmountGrant(50);
		TestTrue(TEXT("raise-then-cap kept the grant"), Reward->ApplyModifiers(Grant, Incoming));
		TestEqual(TEXT("raise-then-cap landed on the cap"), Grant.CustomData.Get<FQuestRewardAmount>().Amount, 5);
	}

	// The same two modifiers, reversed: cap at 5, then raise to at least 10.
	{
		UXPReward* Reward = NewObject<UXPReward>(Package);
		TArray<TObjectPtr<UQuestRewardModifier>>& Mods = FQuestRewardModifierTestAccess::Modifiers(*Reward);
		Mods.Add(FQuestRewardModifierTestAccess::MakeClamp(Reward, 0, 5));
		Mods.Add(FQuestRewardModifierTestAccess::MakeClamp(Reward, 10, 1000));

		FQuestRewardContext Grant = MakeAmountGrant(50);
		TestTrue(TEXT("cap-then-raise kept the grant"), Reward->ApplyModifiers(Grant, Incoming));
		TestEqual(TEXT("cap-then-raise landed on the floor"), Grant.CustomData.Get<FQuestRewardAmount>().Amount, 10);
	}

	return true;
}

/**
 * An amount modifier handed a payload it does not operate on leaves the grant ALONE - it neither drops it nor writes
 * over it.
 *
 * *** THIS IS A TEST OF THE DETECTOR, NOT THE TARGET. *** A refusal that quietly ate the grant, and one that stamped an
 * amount over a range, would both look identical to a working modifier from outside. Asserting the payload is STILL a
 * range carrying its original numbers is the only thing that separates the three.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRewardModifier_RefusesForeignPayload, "SimpleQuest.Reward.ModifierRefusesForeignPayload", ModifierTestFlags)
bool FQuestRewardModifier_RefusesForeignPayload::RunTest(const FString& Parameters)
{
	// Asserted rather than merely tolerated: a silent skip is the failure mode this whole contract exists to prevent.
	AddExpectedMessagePlain(TEXT("does not operate on payload 'QuestLootEntry'"), ELogVerbosity::Warning);

	UPackage* Package = CreatePackage(TEXT("/Temp/QuestRewardModifierPayload"));
	UXPReward* Reward = NewObject<UXPReward>(Package);
	FQuestRewardModifierTestAccess::Modifiers(*Reward).Add(FQuestRewardModifierTestAccess::MakeClamp(Reward, 0, 5));

	// A loot ROW, which is a real struct the reward system passes around and plainly not an amount. It deliberately
	// carries int fields the modifier could have mangled had the gate let it through - a payload with nothing to
	// scale would pass this test even with the gate removed.
	FQuestLootEntry Foreign;
	Foreign.MinAmount = 7;
	Foreign.MaxAmount = 9;

	FQuestRewardContext Grant;
	Grant.CustomData = FInstancedStruct::Make<FQuestLootEntry>(Foreign);

	TestTrue(TEXT("grant survived a modifier that could not handle it"),
		Reward->ApplyModifiers(Grant, FQuestRewardActivationContext()));

	const FQuestLootEntry* Untouched = Grant.CustomData.GetPtr<FQuestLootEntry>();
	if (!TestNotNull(TEXT("payload is still a loot row"), Untouched))
	{
		return false;
	}
	TestEqual(TEXT("loot row min untouched"), Untouched->MinAmount, 7);
	TestEqual(TEXT("loot row max untouched"), Untouched->MaxAmount, 9);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

