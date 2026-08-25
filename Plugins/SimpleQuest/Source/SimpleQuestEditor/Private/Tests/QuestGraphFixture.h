// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "Graph/QuestlineGraphSchema.h"
#include "ISimpleQuestEditorModule.h"
#include "Display/QuestDisplayData.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Objectives/CountingQuestObjective.h"
#include "Quests/QuestlineGraph.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

/**
 * Shared questline fixtures for automation tests. Extracted from the compile-churn tests once a second suite needed the
 * same thing - a real questline graph with a real EdGraph - so the two cannot drift apart into subtly different notions
 * of what a minimal questline is.
 */
namespace QuestTestFixtures
{
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
}

#endif

