// Copyright 2026, Greg Bussell, All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/PrerequisiteExpressionTestHelpers.h"
#include "Utilities/QuestTagComposer.h"

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

// -------------------------------------------------------------------------------------------------
// ClassifyTag: every namespace prefix maps to exactly one EQuestTagKind. Closes the legacy-guard
// asymmetry class the centralized classifier was introduced to fix.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_Classify, "SimpleQuest.TagComposer.Classify", TestFlags)
bool FQuestTagComposer_Classify::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Identity tag"),
		FQuestTagComposer::ClassifyTag(TEXT("SimpleQuest.Quest.MyAsset.Step1")), EQuestTagKind::Identity);
	TestEqual(TEXT("State tag"),
		FQuestTagComposer::ClassifyTag(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Live")), EQuestTagKind::State);
	TestEqual(TEXT("Outcome tag"),
		FQuestTagComposer::ClassifyTag(TEXT("SimpleQuest.QuestOutcome.Combat.Victory")), EQuestTagKind::Outcome);
	TestEqual(TEXT("PrereqRule tag"),
		FQuestTagComposer::ClassifyTag(TEXT("SimpleQuest.QuestPrereqRule.MyRule")), EQuestTagKind::PrereqRule);
	TestEqual(TEXT("ActivationGroup tag"),
		FQuestTagComposer::ClassifyTag(TEXT("SimpleQuest.QuestActivationGroup.MyGroup")), EQuestTagKind::ActivationGroup);
	TestEqual(TEXT("Legacy outcome tag"),
		FQuestTagComposer::ClassifyTag(TEXT("Quest.Outcome.Victory")), EQuestTagKind::LegacyOutcome);
	TestEqual(TEXT("Foreign tag"),
		FQuestTagComposer::ClassifyTag(TEXT("Game.Combat.Damage")), EQuestTagKind::Unknown);
	TestEqual(TEXT("None tag"),
		FQuestTagComposer::ClassifyTag(NAME_None), EQuestTagKind::Unknown);

	// IsXTag accessors mirror ClassifyTag dispatch.
	TestTrue(TEXT("IsIdentityTag yes"), FQuestTagComposer::IsIdentityTag(TEXT("SimpleQuest.Quest.X")));
	TestFalse(TEXT("IsIdentityTag no"), FQuestTagComposer::IsIdentityTag(TEXT("SimpleQuest.QuestState.X.Live")));

	// IsOutcomeTag covers BOTH modern and legacy: closes the asymmetry bug.
	TestTrue(TEXT("IsOutcomeTag modern"), FQuestTagComposer::IsOutcomeTag(TEXT("SimpleQuest.QuestOutcome.X")));
	TestTrue(TEXT("IsOutcomeTag legacy"), FQuestTagComposer::IsOutcomeTag(TEXT("Quest.Outcome.X")));
	TestFalse(TEXT("IsOutcomeTag identity"), FQuestTagComposer::IsOutcomeTag(TEXT("SimpleQuest.Quest.X")));

	return true;
}

// -------------------------------------------------------------------------------------------------
// MakeIdentityTag: composition from prefix and ordered child segments. Output is always rooted under
// IdentityNamespace; segments are dot-joined.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_MakeIdentityTag, "SimpleQuest.TagComposer.MakeIdentityTag", TestFlags)
bool FQuestTagComposer_MakeIdentityTag::RunTest(const FString& Parameters)
{
	const FString MultiChildren[] = { TEXT("Step1"), TEXT("SubStep") };
	TestEqual(TEXT("Multi-segment identity"),
		FQuestTagComposer::MakeIdentityTag(TEXT("MyAsset"), MultiChildren),
		FName(TEXT("SimpleQuest.Quest.MyAsset.Step1.SubStep")));

	const FString SingleChild[] = { TEXT("Step1") };
	TestEqual(TEXT("Single-segment identity"),
		FQuestTagComposer::MakeIdentityTag(TEXT("MyAsset"), SingleChild),
		FName(TEXT("SimpleQuest.Quest.MyAsset.Step1")));

	TestEqual(TEXT("Empty children — asset-only identity"),
		FQuestTagComposer::MakeIdentityTag(TEXT("MyAsset"), TArrayView<const FString>{}),
		FName(TEXT("SimpleQuest.Quest.MyAsset")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// MakeStateFact: identity tag to state-fact with namespace transition. The "SimpleQuest.Quest.X"
// to "SimpleQuest.QuestState.X.<Leaf>" swap that historically lived as Mid(18) magic at 5+ sites.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_MakeStateFact, "SimpleQuest.TagComposer.MakeStateFact", TestFlags)
bool FQuestTagComposer_MakeStateFact::RunTest(const FString& Parameters)
{
	const FName Identity = TEXT("SimpleQuest.Quest.MyAsset.Step1");

	TestEqual(TEXT("Live"),
		FQuestTagComposer::MakeStateFact(Identity, EQuestStateLeaf::Live),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Live")));
	TestEqual(TEXT("Completed"),
		FQuestTagComposer::MakeStateFact(Identity, EQuestStateLeaf::Completed),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Completed")));
	TestEqual(TEXT("PendingGiver"),
		FQuestTagComposer::MakeStateFact(Identity, EQuestStateLeaf::PendingGiver),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.PendingGiver")));
	TestEqual(TEXT("Deactivated"),
		FQuestTagComposer::MakeStateFact(Identity, EQuestStateLeaf::Deactivated),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Deactivated")));
	TestEqual(TEXT("Blocked"),
		FQuestTagComposer::MakeStateFact(Identity, EQuestStateLeaf::Blocked),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Blocked")));

	TestEqual(TEXT("None input → None output"),
		FQuestTagComposer::MakeStateFact(NAME_None, EQuestStateLeaf::Live), NAME_None);

	// Tag already in state namespace passes through (no double-transition).
	const FName AlreadyState = TEXT("SimpleQuest.QuestState.MyAsset.Step1");
	TestEqual(TEXT("State-namespace input — leaf appended without re-rooting"),
		FQuestTagComposer::MakeStateFact(AlreadyState, EQuestStateLeaf::Live),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Live")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// SwapNamespacePrefix: idempotent and reversible. The canonical replacement for the magic-number
// Mid(18) prefix-swap pattern.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_SwapNamespacePrefix, "SimpleQuest.TagComposer.SwapNamespacePrefix", TestFlags)
bool FQuestTagComposer_SwapNamespacePrefix::RunTest(const FString& Parameters)
{
	const FName Identity = TEXT("SimpleQuest.Quest.A.B");
	const FName State = FQuestTagComposer::SwapNamespacePrefix(Identity,
		FQuestTagComposer::IdentityNamespace, FQuestTagComposer::StateNamespace);
	TestEqual(TEXT("Identity → State"), State, FName(TEXT("SimpleQuest.QuestState.A.B")));

	const FName BackToIdentity = FQuestTagComposer::SwapNamespacePrefix(State,
		FQuestTagComposer::StateNamespace, FQuestTagComposer::IdentityNamespace);
	TestEqual(TEXT("Round-trip Identity → State → Identity"), BackToIdentity, Identity);

	// No-match input passes through unchanged.
	const FName Foreign = TEXT("Game.Foo.Bar");
	TestEqual(TEXT("Non-matching prefix passes through"),
		FQuestTagComposer::SwapNamespacePrefix(Foreign,
			FQuestTagComposer::IdentityNamespace, FQuestTagComposer::StateNamespace),
		Foreign);
	return true;
}

// -------------------------------------------------------------------------------------------------
// GetLeafSegment and TryGetParentTag: paired decompose. GetLeafSegment(Tag) gives the trailing
// segment; TryGetParentTag(Tag) gives the rest. Composing the two should reproduce the input.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_LeafAndParent, "SimpleQuest.TagComposer.LeafAndParent", TestFlags)
bool FQuestTagComposer_LeafAndParent::RunTest(const FString& Parameters)
{
	const FName Multi = TEXT("SimpleQuest.Quest.MyAsset.Step1.SubStep");

	TestEqual(TEXT("GetLeafSegment trailing"),
		FQuestTagComposer::GetLeafSegment(Multi), FString(TEXT("SubStep")));

	FName Parent;
	TestTrue(TEXT("TryGetParentTag succeeds"), FQuestTagComposer::TryGetParentTag(Multi, Parent));
	TestEqual(TEXT("Parent matches"), Parent, FName(TEXT("SimpleQuest.Quest.MyAsset.Step1")));

	// Round-trip: concatenation of parent + "." + leaf reproduces input.
	const FString Recombined = Parent.ToString() + TEXT(".") + FQuestTagComposer::GetLeafSegment(Multi);
	TestEqual(TEXT("Round-trip parent + leaf"), FName(*Recombined), Multi);

	// Single-segment input: no parent.
	const FName Single = TEXT("Bare");
	FName NoParent;
	TestFalse(TEXT("TryGetParentTag fails on single-segment"),
		FQuestTagComposer::TryGetParentTag(Single, NoParent));
	TestEqual(TEXT("GetLeafSegment returns whole input"),
		FQuestTagComposer::GetLeafSegment(Single), FString(TEXT("Bare")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// EnumerateAncestors: walks leaf to root, invokes Visitor per ancestor. Visitor returning false
// short-circuits.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_EnumerateAncestors, "SimpleQuest.TagComposer.EnumerateAncestors", TestFlags)
bool FQuestTagComposer_EnumerateAncestors::RunTest(const FString& Parameters)
{
	const FName Deep = TEXT("SimpleQuest.Quest.A.B.C");

	// Walks all ancestors, leaf to root.
	TArray<FName> Visited;
	FQuestTagComposer::EnumerateAncestors(Deep, [&](FName Ancestor) -> bool
	{
		Visited.Add(Ancestor);
		return true;
	});
	TestEqual(TEXT("Ancestor count"), Visited.Num(), 4);
	TestEqual(TEXT("Ancestor [0]"), Visited[0], FName(TEXT("SimpleQuest.Quest.A.B")));
	TestEqual(TEXT("Ancestor [1]"), Visited[1], FName(TEXT("SimpleQuest.Quest.A")));
	TestEqual(TEXT("Ancestor [2]"), Visited[2], FName(TEXT("SimpleQuest.Quest")));
	TestEqual(TEXT("Ancestor [3]"), Visited[3], FName(TEXT("SimpleQuest")));

	// Short-circuit on first match.
	TArray<FName> Partial;
	FQuestTagComposer::EnumerateAncestors(Deep, [&](FName Ancestor) -> bool
	{
		Partial.Add(Ancestor);
		return Ancestor != FName(TEXT("SimpleQuest.Quest.A"));  // stop after this one
	});
	TestEqual(TEXT("Short-circuit visited count"), Partial.Num(), 2);
	TestEqual(TEXT("Short-circuit last visited"), Partial.Last(), FName(TEXT("SimpleQuest.Quest.A")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// TryStripOutcomePrefix: handles modern and legacy outcome namespaces; no-ops on non-outcome input.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_StripOutcomePrefix, "SimpleQuest.TagComposer.StripOutcomePrefix", TestFlags)
bool FQuestTagComposer_StripOutcomePrefix::RunTest(const FString& Parameters)
{
	{
		FString Modern = TEXT("SimpleQuest.QuestOutcome.Combat.Victory");
		TestTrue(TEXT("Modern strip reports true"), FQuestTagComposer::TryStripOutcomePrefix(Modern));
		TestEqual(TEXT("Modern stripped"), Modern, FString(TEXT("Combat.Victory")));
	}
	{
		FString Legacy = TEXT("Quest.Outcome.Combat.Victory");
		TestTrue(TEXT("Legacy strip reports true"), FQuestTagComposer::TryStripOutcomePrefix(Legacy));
		TestEqual(TEXT("Legacy stripped"), Legacy, FString(TEXT("Combat.Victory")));
	}
	{
		FString Foreign = TEXT("Game.Foo.Bar");
		TestFalse(TEXT("Non-outcome reports false"), FQuestTagComposer::TryStripOutcomePrefix(Foreign));
		TestEqual(TEXT("Non-outcome unchanged"), Foreign, FString(TEXT("Game.Foo.Bar")));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// FormatOutcomeForDisplay: canonical "Combat: Boss Defeated" formatter. Single source so K2Node,
// QuestlineNodeBase::GetOutcomeLabel, and Prereq Examiner all render identically.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_FormatOutcomeForDisplay, "SimpleQuest.TagComposer.FormatOutcomeForDisplay", TestFlags)
bool FQuestTagComposer_FormatOutcomeForDisplay::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Modern multi-segment"),
		FQuestTagComposer::FormatOutcomeForDisplay(TEXT("SimpleQuest.QuestOutcome.Combat.BossDefeated")).ToString(),
		FString(TEXT("Combat: Boss Defeated")));
	TestEqual(TEXT("Modern single-segment"),
		FQuestTagComposer::FormatOutcomeForDisplay(TEXT("SimpleQuest.QuestOutcome.Victory")).ToString(),
		FString(TEXT("Victory")));
	TestEqual(TEXT("Legacy multi-segment"),
		FQuestTagComposer::FormatOutcomeForDisplay(TEXT("Quest.Outcome.Combat.BossDefeated")).ToString(),
		FString(TEXT("Combat: Boss Defeated")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// MakeNodePathFact and MakeEntryPathFact: both perform identity to state namespace transition AND
// strip outcome prefix from path identity. Verifies the dual decompose/compose path.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTagComposer_PathFacts, "SimpleQuest.TagComposer.PathFacts", TestFlags)
bool FQuestTagComposer_PathFacts::RunTest(const FString& Parameters)
{
	const FName Node = TEXT("SimpleQuest.Quest.MyAsset.Step1");
	const FName Outcome = TEXT("SimpleQuest.QuestOutcome.Victory");

	TestEqual(TEXT("MakeNodePathFact strips outcome prefix"),
		FQuestTagComposer::MakeNodePathFact(Node, Outcome),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Path.Victory")));

	TestEqual(TEXT("MakeEntryPathFact strips outcome prefix"),
		FQuestTagComposer::MakeEntryPathFact(Node, Outcome),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.EntryPath.Victory")));

	// Bare path identity (dynamic placement): no prefix to strip.
	const FName BarePath = TEXT("DynamicVictory");
	TestEqual(TEXT("MakeNodePathFact passes bare identity"),
		FQuestTagComposer::MakeNodePathFact(Node, BarePath),
		FName(TEXT("SimpleQuest.QuestState.MyAsset.Step1.Path.DynamicVictory")));

	// None path identity: None output.
	TestEqual(TEXT("MakeNodePathFact None → None"),
		FQuestTagComposer::MakeNodePathFact(Node, NAME_None), NAME_None);
	return true;
}

// -------------------------------------------------------------------------------------------------
// FPrerequisiteExpression builders: verify Leaf_Resolution and Leaf_Entry stamp the right Type and
// shared (LeafQuestTag, LeafOutcomeTag) payload.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrerequisiteExpression_AddResolutionLeaf, "SimpleQuest.PrerequisiteExpression.AddResolutionLeaf", TestFlags)
bool FPrerequisiteExpression_AddResolutionLeaf::RunTest(const FString& Parameters)
{
	// Verifies the production builder stamps Type=Leaf_Resolution and appends a single node. Field-forwarding
	// for the (Quest, Outcome) pair is not asserted here: the FName to FGameplayTag resolution path inside the
	// builder requires registered tags, which would pollute the gameplay tag picker. Multi-node and field-
	// shape assertions go via FPrerequisiteExpressionTestHelpers (see CollectLeavesEntry below).
	FPrerequisiteExpression Expr;
	const int32 Idx = Expr.AddResolutionLeaf(FName(TEXT("Test.Quest.Unregistered")), FGameplayTag());

	TestEqual(TEXT("Index returned"), Idx, 0);
	TestTrue(TEXT("Node added"), Expr.Nodes.IsValidIndex(Idx));
	TestEqual(TEXT("Type is Leaf_Resolution"),
		Expr.Nodes[Idx].Type, EPrerequisiteExpressionType::Leaf_Resolution);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrerequisiteExpression_AddEntryLeaf, "SimpleQuest.PrerequisiteExpression.AddEntryLeaf", TestFlags)
bool FPrerequisiteExpression_AddEntryLeaf::RunTest(const FString& Parameters)
{
	// Verifies the production builder stamps Type=Leaf_Entry, appends a single node, and leaves the bridge
	// LeafTag default-invalid. The bridge-LeafTag absence assertion is meaningful without registered tags
	// because AddEntryLeaf never assigns Node.LeafTag at all (Entry leaves render via LeafQuestTag /
	// LeafOutcomeTag directly). Field-forwarding assertions on the (Quest, Outcome) pair would require
	// tag registration; see helper-based tests for shape-level coverage.
	FPrerequisiteExpression Expr;
	const int32 Idx = Expr.AddEntryLeaf(FName(TEXT("Test.Quest.Unregistered")), FGameplayTag());

	TestEqual(TEXT("Index returned"), Idx, 0);
	TestTrue(TEXT("Node added"), Expr.Nodes.IsValidIndex(Idx));
	TestEqual(TEXT("Type is Leaf_Entry"),
		Expr.Nodes[Idx].Type, EPrerequisiteExpressionType::Leaf_Entry);
	TestFalse(TEXT("Entry leaf has no bridge LeafTag"),
		Expr.Nodes[Idx].LeafTag.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrerequisiteExpression_CollectLeavesEntry, "SimpleQuest.PrerequisiteExpression.CollectLeavesEntry", TestFlags)
bool FPrerequisiteExpression_CollectLeavesEntry::RunTest(const FString& Parameters)
{
	// Construct an Entry-leaf node directly via the test helpers. Bypasses the production builder's tag-
	// resolution path so the test runs without registering fixture tags. CollectLeaves only cares about
	// per-node Type and field shape, so default-invalid tags are sufficient to verify the descriptor
	// emission walks Leaf_Entry nodes the same way it walks Leaf and Leaf_Resolution.
	FPrerequisiteExpression Expr;
	FPrerequisiteExpressionTestHelpers::AppendEntryLeaf(Expr, FGameplayTag(), FGameplayTag());

	TArray<FPrereqLeafDescriptor> Leaves;
	Expr.CollectLeaves(Leaves);

	TestEqual(TEXT("One leaf collected"), Leaves.Num(), 1);
	TestEqual(TEXT("Descriptor type is Leaf_Entry"),
		Leaves[0].Type, EPrerequisiteExpressionType::Leaf_Entry);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS