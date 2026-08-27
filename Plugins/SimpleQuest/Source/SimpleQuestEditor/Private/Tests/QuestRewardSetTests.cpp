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

				// Names must be distinct: the outer is shared, and two duplicates claiming one name would be a hard
				// crash inside DuplicateObject. NOTE this assertion does not catch that - FlattenRewardSets refuses a
				// taken name and raises a compile error first, so a collision fails the compile rather than reaching
				// here. What this catches is a naming scheme that RENAMES instead of colliding. The nesting test
				// asserts the names themselves, which is the stronger check.
				Names.Add(Actual[i]->GetName());
			}
		}
		TestEqual(TEXT("every compiled reward has a distinct subobject name"), Names.Num(), Expected.Num());
	}

	return true;
}

/**
 * A set included by another set is flattened depth-first, ahead of the including set's own rewards.
 *
 * Three different classes again, for the same reason: the claim is about ORDER, and identical classes could not tell a
 * correct depth-first walk from one that emitted the containing set's rewards first. The distinct-names assertion is
 * the one that catches a naming scheme that collapses under nesting - which is a hard crash in the wild, not a
 * mismatch, so it has to be caught by a test rather than by a user.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRewardSet_NestedFlattenDepthFirst, "SimpleQuest.Reward.NestedSetsFlattenDepthFirst", RewardSetTestFlags)
bool FQuestRewardSet_NestedFlattenDepthFirst::RunTest(const FString& Parameters)
{
	QuestTestFixtures::FCompileFixture Fixture(TEXT("/Temp/QuestNestedSetFixture"));
	if (!TestTrue(TEXT("fixture built"), Fixture.IsValid()))
	{
		return false;
	}

	// Base holds the XP; Chapter includes Base and adds currency of its own. The node references Chapter only.
	UPackage* BasePackage = CreatePackage(TEXT("/Temp/QuestNestedSetBase"));
	URewardSetDataAsset* Base = NewObject<URewardSetDataAsset>(BasePackage, URewardSetDataAsset::StaticClass(), TEXT("DA_BaseBundle"), RF_Public);
	Base->Rewards.Add(NewObject<UXPReward>(Base));

	UPackage* ChapterPackage = CreatePackage(TEXT("/Temp/QuestNestedSetChapter"));
	URewardSetDataAsset* Chapter = NewObject<URewardSetDataAsset>(ChapterPackage, URewardSetDataAsset::StaticClass(), TEXT("DA_ChapterBundle"), RF_Public);
	Chapter->Sets.Add(Base);
	Chapter->Rewards.Add(NewObject<UCurrencyReward>(Chapter));

	UQuestlineNode_Reward* RewardEdNode = NewObject<UQuestlineNode_Reward>(Fixture.Graph->QuestlineEdGraph, NAME_None, RF_Transactional);
	RewardEdNode->CreateNewGuid();
	RewardEdNode->RewardSets.Add(Chapter);
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

	// Base's contents, then Chapter's own, then the node's inline reward.
	const TArray<FString> Expected = { TEXT("XPReward"), TEXT("CurrencyReward"), TEXT("GenericReward") };
	const TArray<TObjectPtr<UQuestRewardBase>>& Actual = FQuestRewardSetTestAccess::Rewards(*Compiled);

	if (TestEqual(TEXT("nested set, containing set and inline reward all arrived"), Actual.Num(), Expected.Num()))
	{
		// The names are the assertion, because they ARE the reward's path: one segment per set level, then its own
		// index. Two rewards can only collide by sharing both, which makes them the same reward.
		// A regression in that scheme no longer crashes. It used to - DuplicateObject dies on a taken name, inside
		// Compile, so the editor went down with no log and nothing downstream ever ran. FlattenRewardSetsInternal now
		// checks the name first and raises a compile error instead, which is what makes the sabotage demonstrable:
		// collapse the prefix and this test fails on "compile succeeds" with the duplicate name and the set chain
		// named in the log.
		const TArray<FString> ExpectedNames = { TEXT("SetReward_0_0_0"), TEXT("SetReward_0_0"), TEXT("Reward_0") };
		for (int32 i = 0; i < Expected.Num(); ++i)
		{
			if (TestNotNull(*FString::Printf(TEXT("reward %d is not null"), i), Actual[i].Get()))
			{
				TestEqual(*FString::Printf(TEXT("reward %d is the expected class"), i), Actual[i]->GetClass()->GetName(), Expected[i]);
				TestEqual(*FString::Printf(TEXT("reward %d's name encodes its path"), i), Actual[i]->GetName(), ExpectedNames[i]);
			}
		}
	}

	return true;
}

/**
 * A set that includes itself is refused, and everything else still compiles.
 *
 * The second half is the point. Refusing the cycle is easy; refusing it WITHOUT eating the set's legitimate rewards is
 * the behavior worth pinning, because the natural wrong implementation - bail out of the whole set on detecting a
 * cycle - would pass a test that only checked for the warning.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRewardSet_SelfIncludingSetRefused, "SimpleQuest.Reward.SelfIncludingSetIsRefused", RewardSetTestFlags)
bool FQuestRewardSet_SelfIncludingSetRefused::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("includes itself and was skipped"), ELogVerbosity::Warning);

	QuestTestFixtures::FCompileFixture Fixture(TEXT("/Temp/QuestCyclicSetFixture"));
	if (!TestTrue(TEXT("fixture built"), Fixture.IsValid()))
	{
		return false;
	}

	UPackage* Package = CreatePackage(TEXT("/Temp/QuestCyclicSetAsset"));
	URewardSetDataAsset* Loop = NewObject<URewardSetDataAsset>(Package, URewardSetDataAsset::StaticClass(), TEXT("DA_LoopBundle"), RF_Public);
	Loop->Sets.Add(Loop);
	Loop->Rewards.Add(NewObject<UXPReward>(Loop));

	UQuestlineNode_Reward* RewardEdNode = NewObject<UQuestlineNode_Reward>(Fixture.Graph->QuestlineEdGraph, NAME_None, RF_Transactional);
	RewardEdNode->CreateNewGuid();
	RewardEdNode->RewardSets.Add(Loop);
	RewardEdNode->Rewards.Add(NewObject<UGenericReward>(RewardEdNode));
	RewardEdNode->AllocateDefaultPins();
	Fixture.Graph->QuestlineEdGraph->AddNode(RewardEdNode, false, false);

	const TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
	if (!TestTrue(TEXT("compile still succeeds despite the cycle"), Compiler->Compile(Fixture.Graph)))
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

	// The self-reference is dropped; the set's own reward is granted ONCE, and the inline reward is untouched.
	const TArray<FString> Expected = { TEXT("XPReward"), TEXT("GenericReward") };
	const TArray<TObjectPtr<UQuestRewardBase>>& Actual = FQuestRewardSetTestAccess::Rewards(*Compiled);

	if (TestEqual(TEXT("the cycle cost nothing but itself"), Actual.Num(), Expected.Num()))
	{
		for (int32 i = 0; i < Expected.Num(); ++i)
		{
			if (TestNotNull(*FString::Printf(TEXT("reward %d is not null"), i), Actual[i].Get()))
			{
				TestEqual(*FString::Printf(TEXT("reward %d is the expected class"), i), Actual[i]->GetClass()->GetName(), Expected[i]);
			}
		}
	}

	return true;
}

#endif

