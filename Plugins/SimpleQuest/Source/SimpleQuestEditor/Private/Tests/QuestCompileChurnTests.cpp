// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "Graph/QuestlineGraphSchema.h"
#include "ISimpleQuestEditorModule.h"
#include "Display/QuestDisplayData.h"
#include "Misc/AutomationTest.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Objectives/CountingQuestObjective.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Utilities/QuestlineGraphCompiler.h"

namespace QuestCompileChurn_Internal
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FString FixtureDisplayName = TEXT("Fixture Display Name");
	static const FString FixtureDescription = TEXT("Fixture description text.");

	/**
	 * A questline built from nothing, in a real but never-saved package.
	 *
	 * Real, not transient, because UPackage::SetDirtyFlag skips dirty tracking entirely for the transient package - a
	 * transient fixture reports "not dirty" no matter what the compile did, which makes any dirty assertion vacuous.
	 *
	 * Built rather than duplicated from shipped content so the tests survive that content being renamed, re-authored, or
	 * slimmed.
	 *
	 * It DOES carry a display payload, and that is deliberate. An earlier version omitted one to avoid cleaning up the
	 * compiled display ini - but the ini is written by the module's batch entry points, not by Compile() itself, so a
	 * test driving the compiler directly never reaches it. The payload costs nothing and covers a real defect class.
	 */
	struct FCompileFixture
	{
		UQuestlineGraph* Graph = nullptr;
		UPackage* Package = nullptr;
		UQuestDisplayData* DisplayData = nullptr;

		explicit FCompileFixture(const TCHAR* PackageName)
		{
			Package = CreatePackage(PackageName);
			if (!Package) return;

			Graph = NewObject<UQuestlineGraph>(Package, UQuestlineGraph::StaticClass(),
				TEXT("QL_CompileFixture"), RF_Public | RF_Standalone | RF_Transactional);

			// Explicitly named so its path is identical on every compile - an auto-numbered name would show up as
			// drift in the stability test, since compiled nodes reference this object by path.
			DisplayData = NewObject<UQuestDisplayData>(Graph, UQuestDisplayData::StaticClass(), TEXT("DA_CompileFixture"));

			// Mirrors UQuestlineGraphFactory::FactoryCreateNew - the schema's default nodes are what give the graph its
			// Entry, and the compiler walks from there.
			Graph->QuestlineEdGraph = NewObject<UEdGraph>(Graph, NAME_None, RF_Transactional);
			Graph->QuestlineEdGraph->Schema = UQuestlineGraphSchema::StaticClass();
			Graph->QuestlineEdGraph->GetSchema()->CreateDefaultNodesForGraph(*Graph->QuestlineEdGraph);

			AddStep(TEXT("First"));
			AddStep(TEXT("Second"));
		}

		/** Compiling registers the fixture's tags into the project config, so drop them again. Registering an empty set is
		    the same route an asset deletion takes: the module diffs against the previous registration and removes the rest. */
		~FCompileFixture()
		{
			if (Package)
			{
				ISimpleQuestEditorModule::Get().RegisterCompiledTags(Package->GetName(), TArray<FName>());
			}
			if (Graph)
			{
				Graph->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
			}
		}

		void AddStep(const TCHAR* Label)
		{
			UQuestlineNode_Step* Step = NewObject<UQuestlineNode_Step>(Graph->QuestlineEdGraph, NAME_None, RF_Transactional);
			Step->CreateNewGuid();
			Step->NodeLabel = FText::FromString(Label);

			// The compiler errors out on a Step with no objective class, so the fixture needs a real one. Which objective is
			// irrelevant here - nothing exercises its behavior, only that the node compiles.
			Step->ObjectiveClass = UCountingQuestObjective::StaticClass();

			// Display payload on every step, so a compiled node that stops carrying it has somewhere to be caught.
			Step->DisplayName = FText::FromString(FixtureDisplayName);
			Step->Description = FText::FromString(FixtureDescription);
			Step->DisplayData = DisplayData;

			Step->AllocateDefaultPins();
			Graph->QuestlineEdGraph->AddNode(Step, false, false);
		}

		bool IsValid() const { return Graph != nullptr && Package != nullptr; }
	};

	/** Registry key, subobject name, and a checksum of serialized properties - keys prove the same nodes were produced,
	    names catch the renaming that churns a package's export table, the CRC catches everything else drifting. */
	struct FCompiledFingerprint
	{
		TArray<FName> Keys;
		TArray<FName> ObjectNames;
		TArray<uint32> PropertyCrcs;
	};

	FCompiledFingerprint Fingerprint(const UQuestlineGraph* Graph)
	{
		FCompiledFingerprint Out;
		for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : Graph->GetCompiledNodes())
		{
			Out.Keys.Add(Pair.Key);
			Out.ObjectNames.Add(Pair.Value ? Pair.Value->GetFName() : NAME_None);

			FArchiveObjectCrc32 Crc;
			Out.PropertyCrcs.Add(Pair.Value ? Crc.Crc32(Pair.Value) : 0);
		}
		return Out;
	}

	bool Compile(UQuestlineGraph* Graph)
	{
		TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
		return Compiler->Compile(Graph);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestCompile_RecompileIsStable, "SimpleQuest.Compile.RecompileIsStable",
	QuestCompileChurn_Internal::TestFlags)
bool FQuestCompile_RecompileIsStable::RunTest(const FString& Parameters)
{
	using namespace QuestCompileChurn_Internal;

	// Compiling a graph nobody edited must produce the same compiled objects, under the same names, with the same contents.
	// Anything else rewrites the .uasset on every compile, so a code-only change that happens to open the editor lands a
	// binary diff, and an adopter's first compile dirties content they never touched.
	FCompileFixture Fixture(TEXT("/Temp/QuestCompileStable"));
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;

	TestTrue(TEXT("First compile succeeds"), Compile(Fixture.Graph));
	const FCompiledFingerprint Before = Fingerprint(Fixture.Graph);
	TestTrue(TEXT("First compile produced nodes"), Before.Keys.Num() > 0);

	TestTrue(TEXT("Second compile succeeds"), Compile(Fixture.Graph));
	const FCompiledFingerprint After = Fingerprint(Fixture.Graph);

	TestEqual(TEXT("Same compiled nodes"), After.Keys, Before.Keys);
	TestEqual(TEXT("Same subobject names - a rename churns the package with no graph change"), After.ObjectNames, Before.ObjectNames);
	TestEqual(TEXT("Same compiled contents"), After.PropertyCrcs, Before.PropertyCrcs);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestCompile_RecompileDoesNotDirty, "SimpleQuest.Compile.RecompileDoesNotDirty",
	QuestCompileChurn_Internal::TestFlags)
bool FQuestCompile_RecompileDoesNotDirty::RunTest(const FString& Parameters)
{
	using namespace QuestCompileChurn_Internal;

	// Byte-stable output is not enough on its own: an unconditional Modify() marks the package dirty before the compile can
	// know whether anything will change, so the editor prompts to save untouched assets and they get committed out of habit.
	FCompileFixture Fixture(TEXT("/Temp/QuestCompileDirty"));
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;

	TestTrue(TEXT("First compile succeeds"), Compile(Fixture.Graph));
	Fixture.Package->SetDirtyFlag(false);

	TestTrue(TEXT("Second compile succeeds"), Compile(Fixture.Graph));
	TestFalse(TEXT("Recompiling an unchanged graph leaves the package clean"), Fixture.Package->IsDirty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestCompile_RealChangeStillDirties, "SimpleQuest.Compile.RealChangeStillDirties",
	QuestCompileChurn_Internal::TestFlags)
bool FQuestCompile_RealChangeStillDirties::RunTest(const FString& Parameters)
{
	using namespace QuestCompileChurn_Internal;

	// The seatbelt for the test above. Suppressing a spurious dirty flag is a small win; suppressing a real one loses the
	// user's work at editor close. This must stay green through any change that makes RecompileDoesNotDirty pass - an
	// in-place compiler that silently skips a write it should have made fails HERE and nowhere else.
	FCompileFixture Fixture(TEXT("/Temp/QuestCompileChange"));
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;

	TestTrue(TEXT("First compile succeeds"), Compile(Fixture.Graph));
	Fixture.Package->SetDirtyFlag(false);

	// Change the questline's identity: it is the tag namespace every compiled node is keyed under, so every key changes and
	// no fingerprint or diff can honestly call this unchanged.
	Fixture.Graph->SetQuestlineID(TEXT("QL_CompileFixture_Renamed"));

	TestTrue(TEXT("Second compile succeeds"), Compile(Fixture.Graph));
	TestTrue(TEXT("A graph that actually changed still dirties its package"), Fixture.Package->IsDirty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestCompile_CompiledNodeCarriesDisplayPayload, "SimpleQuest.Compile.CompiledNodeCarriesDisplayPayload",
	QuestCompileChurn_Internal::TestFlags)
bool FQuestCompile_CompiledNodeCarriesDisplayPayload::RunTest(const FString& Parameters)
{
	using namespace QuestCompileChurn_Internal;

	// The three churn tests above assert that compiling twice produces the SAME thing. None of them assert that it
	// produces the RIGHT thing - and a compiled node that quietly stops carrying its authored display fields is stable
	// in exactly the way they check for. That is not hypothetical: a bad edit anchor deleted the branch copying
	// DisplayName / Description / DisplayData onto non-linked nodes, every test here stayed green, and it surfaced only
	// when a quest showed default description text in the running game.
	FCompileFixture Fixture(TEXT("/Temp/QuestCompileDisplay"));
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	if (!TestTrue(TEXT("Compile succeeds"), Compile(Fixture.Graph))) return false;

	int32 Checked = 0;
	for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : Fixture.Graph->GetCompiledNodes())
	{
		const UQuestNodeBase* Node = Pair.Value;
		if (!Node) continue;

		TestEqual(*FString::Printf(TEXT("'%s' carries its authored DisplayName"), *Pair.Key.ToString()),
			Node->GetDisplayName().ToString(), FixtureDisplayName);
		TestEqual(*FString::Printf(TEXT("'%s' carries its authored Description"), *Pair.Key.ToString()),
			Node->GetDescription().ToString(), FixtureDescription);
		TestEqual(*FString::Printf(TEXT("'%s' carries its authored DisplayData"), *Pair.Key.ToString()),
			Node->GetDisplayData(), Fixture.DisplayData);
		++Checked;
	}

	// Without this the loop above passes vacuously on an empty map, which is the failure mode the whole test exists for.
	TestTrue(TEXT("At least one compiled node was checked"), Checked > 0);

	return true;
}

#endif

