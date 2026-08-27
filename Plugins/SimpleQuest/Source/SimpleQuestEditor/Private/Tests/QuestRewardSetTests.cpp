// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "ISimpleQuestEditorModule.h"
#include "Misc/AutomationTest.h"
#include "Nodes/Utility/QuestlineNode_Reward.h"
#include "Quests/QuestRewardNode.h"
#include "Rewards/CurrencyReward.h"
#include "Rewards/GenericReward.h"
#include "Rewards/RewardSetDataAsset.h"
#include "Rewards/XPReward.h"
#include "Tests/QuestGraphFixture.h"
#include "UObject/Package.h"
#include "Utilities/QuestlineGraphCompiler.h"

namespace
{
	constexpr EAutomationTestFlags RewardSetTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

class FQuestRewardSetTestAccess
{
public:
	static const TArray<TObjectPtr<UQuestRewardBase>>& Rewards(const UQuestRewardNode& Node) { return Node.Rewards; }
};

/**
 * A referenced reward set is flattened into the compiled node, ahead of the node's own inline rewards.
 *
 * Three claims in one sequence, and they are separable only because the three rewards are DIFFERENT CLASSES: that the
 * set's contents arrive at all, that they keep the order the set lists them in, and that they precede the inline ones.
 * Identical classes would make a reordering indistinguishable from the correct result - the same trap a palindrome
 * sequence set for an earlier ordering test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRewardSet_FlattensAheadOfInline, "SimpleQuest.Reward.SetFlattensAheadOfInline", RewardSetTestFlags)
bool FQuestRewardSet_FlattensAheadOfInline::RunTest(const FString& Parameters)
{
	QuestTestFixtures::FCompileFixture Fixture(TEXT("/Temp/QuestRewardSetFixture"));
	if (!TestTrue(TEXT("fixture built"), Fixture.IsValid()))
	{
		return false;
	}

	// The set lives in its own package because a soft reference has to resolve by PATH. It is never saved - a
	// TSoftObjectPtr finds an already-loaded object at that path without touching disk, which is all the compiler needs.
	UPackage* SetPackage = CreatePackage(TEXT("/Temp/QuestRewardSetAsset"));
	URewardSetDataAsset* Set = NewObject<URewardSetDataAsset>(SetPackage, URewardSetDataAsset::StaticClass(), TEXT("DA_RewardSetFixture"), RF_Public | RF_Standalone);
	Set->Rewards.Add(NewObject<UXPReward>(Set));
	Set->Rewards.Add(NewObject<UCurrencyReward>(Set));

	// One reward node: the set first, then an inline reward of a third class.
	UQuestlineNode_Reward* RewardEdNode = NewObject<UQuestlineNode_Reward>(Fixture.Graph->QuestlineEdGraph, NAME_None, RF_Transactional);
	RewardEdNode->CreateNewGuid();
	RewardEdNode->RewardSets.Add(Set);
	RewardEdNode->Rewards.Add(NewObject<UGenericReward>(RewardEdNode));
	RewardEdNode->AllocateDefaultPins();
	Fixture.Graph->QuestlineEdGraph->AddNode(RewardEdNode, false, false);

	const TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
	if (!TestTrue(TEXT("compile succeeds"), Compiler->Compile(Fixture.Graph)))
	{
		return false;
	}

	const UQuestRewardNode* Compiled = nullptr;
	for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : Fixture.Graph->GetCompiledNodes())
	{
		if (const UQuestRewardNode* AsReward = Cast<UQuestRewardNode>(Pair.Value))
		{
			Compiled = AsReward;
			break;
		}
	}
	if (!TestNotNull(TEXT("a reward node was compiled"), Compiled))
	{
		return false;
	}

	// THE SEQUENCE IS THE ASSERTION. Set contents in listed order, then inline.
	const TArray<FString> Expected = { TEXT("XPReward"), TEXT("CurrencyReward"), TEXT("GenericReward") };
	const TArray<TObjectPtr<UQuestRewardBase>>& Actual = FQuestRewardSetTestAccess::Rewards(*Compiled);

	if (TestEqual(TEXT("the compiled node holds the set's rewards plus the inline one"), Actual.Num(), Expected.Num()))
	{
		TSet<FString> Names;
		for (int32 i = 0; i < Expected.Num(); ++i)
		{
			if (TestNotNull(*FString::Printf(TEXT("reward %d is not null"), i), Actual[i].Get()))
			{
				TestEqual(*FString::Printf(TEXT("reward %d is the expected class"), i), Actual[i]->GetClass()->GetName(), Expected[i]);

				// Names must be distinct: the outer is shared, and two duplicates claiming one name is a hard crash
				// whenever their classes differ. This is the assertion that would catch a naming scheme collapsing.
				Names.Add(Actual[i]->GetName());
			}
		}
		TestEqual(TEXT("every compiled reward has a distinct subobject name"), Names.Num(), Expected.Num());
	}

	return true;
}

#endif

