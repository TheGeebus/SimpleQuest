// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "ISimpleQuestEditorModule.h"
#include "Misc/AutomationTest.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "UObject/Package.h"
#include "Utilities/QuestlineGraphCompiler.h"

namespace QuestCompileChurn_Internal
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// A shipped QuickStart chapter, so the fixture exists in every checkout without depending on project content.
	const TCHAR* FixturePath = TEXT("/SimpleQuest/QuickStart/Chapters/03_SequentialSteps/QL_Ch3_SequentialSteps.QL_Ch3_SequentialSteps");

	// What a compile is allowed to be judged on: the registry key, the subobject's own name, and a checksum of its
	// serialized properties. Keys prove the same nodes were produced; names catch renaming, which is what churns the
	// package export table even when nothing about the graph changed; the CRC catches everything else drifting.
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestCompile_RecompileIsStable, "SimpleQuest.Compile.RecompileIsStable",
	QuestCompileChurn_Internal::TestFlags)
bool FQuestCompile_RecompileIsStable::RunTest(const FString& Parameters)
{
	using namespace QuestCompileChurn_Internal;

	// Compiling a graph nobody edited must produce the same compiled objects, under the same names, with the same
	// contents. Anything else rewrites the .uasset on every compile — so a code-only change that happens to open the
	// editor lands a binary diff, and an adopter's first compile dirties content they never touched.
	UQuestlineGraph* Source = LoadObject<UQuestlineGraph>(nullptr, FixturePath);
	if (!TestNotNull(TEXT("QuickStart Ch3 fixture loads"), Source)) return false;

	// Work on a transient duplicate so the test never dirties shipped content.
	UQuestlineGraph* Graph = DuplicateObject<UQuestlineGraph>(Source, GetTransientPackage());
	if (!TestNotNull(TEXT("Fixture duplicates"), Graph)) return false;
	
	// The duplicate needs an identity of its own: QuestlineID is the tag namespace and must be unique project-wide, so a
	// copy sharing the source's is refused by the compiler — correctly.
	Graph->SetQuestlineID(TEXT("QL_CompileChurnFixture"));

	TUniquePtr<FQuestlineGraphCompiler> First = ISimpleQuestEditorModule::Get().CreateCompiler();
	TestTrue(TEXT("First compile succeeds"), First->Compile(Graph));
	const FCompiledFingerprint Before = Fingerprint(Graph);
	TestTrue(TEXT("First compile produced nodes"), Before.Keys.Num() > 0);

	TUniquePtr<FQuestlineGraphCompiler> Second = ISimpleQuestEditorModule::Get().CreateCompiler();
	TestTrue(TEXT("Second compile succeeds"), Second->Compile(Graph));
	const FCompiledFingerprint After = Fingerprint(Graph);

	TestEqual(TEXT("Same compiled nodes"), After.Keys, Before.Keys);
	TestEqual(TEXT("Same subobject names — a rename churns the package with no graph change"), After.ObjectNames, Before.ObjectNames);
	TestEqual(TEXT("Same compiled contents"), After.PropertyCrcs, Before.PropertyCrcs);

	return true;
}

#endif

