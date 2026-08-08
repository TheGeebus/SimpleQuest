// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Graph/QuestlineGraphSchema.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/Utility/QuestlineNode_Reward.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/QuestBundleDiff.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestBundleTransforms.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestFlowConventions.h"
#include "Resolver/QuestInPlacePlan.h"
#include "Resolver/QuestInstancedChildren.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestRowRestore.h"
#include "Resolver/TsvQuestDataFormat.h"
#include "Rewards/XPReward.h"


// The mapping guard is the one place a drifted source is refused instead of silently dropping rows, so its REFUSALS are the
// behaviour worth pinning: a guard that stops refusing looks identical to a guard that has nothing to refuse. Each failing
// case is paired with a passing one — green is only trustworthy once the detector has gone red on a known-bad input.

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UQuestImportMapping* MakeMapping()
	{
		UQuestImportMapping* M = NewObject<UQuestImportMapping>(GetTransientPackage());
		M->DiscriminatorColumn = TEXT("type");
		return M;
	}

	// One node kind answering to the given source values, the first of which is its PrimaryValue.
	void AddKind(UQuestImportMapping& M, TSubclassOf<UQuestlineNodeBase> Class, std::initializer_list<const TCHAR*> Values)
	{
		FQuestDiscriminatorClass Entry;
		Entry.NodeClass = TSoftClassPtr<UQuestlineNodeBase>(Class);
		for (const TCHAR* V : Values) { Entry.Values.Add(V); }
		Entry.PrimaryValue = Entry.Values.Num() > 0 ? Entry.Values[0] : FString();
		M.DiscriminatorClasses.Add(MoveTemp(Entry));
	}

	void AddBinding(UQuestImportMapping& M, const TCHAR* SourceColumn, const TCHAR* TargetProperty,
	                EQuestAbsentFieldPolicy Policy = EQuestAbsentFieldPolicy::Preserve)
	{
		FQuestColumnBinding B;
		B.SourceColumn = SourceColumn;
		B.TargetProperty = TargetProperty;
		B.AbsentPolicy = Policy;
		M.Bindings.Add(MoveTemp(B));
	}
	
	// Content cells as "rowkey/column" -> value, ignoring table grouping and column ORDER. The round-trip is a claim about
	// CONTENT, not layout: the reverse pass legitimately rebuilds column order and merges tables.
	TMap<FString, FString> FlattenContent(const FQuestDataBundle& B)
	{
		TMap<FString, FString> Out;
		for (const TPair<FString, FQuestDataTable>& TP : B.TablesByType)
		{
			if (TP.Key == TEXT("questline_graph")) continue;
			for (const FQuestDataRow& R : TP.Value.Rows)
			{
				for (const TPair<FString, FQuestDataValue>& C : R.Cells)
				{
					if (C.Value.Kind != EQuestDataValueKind::Empty)
					{
						Out.Add(R.Key + TEXT("/") + C.Key, C.Value.StringForm);
					}
				}
			}
		}
		return Out;
	}

	void AddRow(FQuestDataTable& T, const TCHAR* Key, std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Cells)
	{
		FQuestDataRow R;
		R.Key = Key;
		for (const TPair<const TCHAR*, const TCHAR*>& C : Cells)
		{
			R.Cells.Add(C.Key, FQuestDataValue::MakeString(C.Value));
			T.Columns.AddUnique(C.Key);
		}
		T.Rows.Add(MoveTemp(R));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RoundTripVocabulary, "SimpleQuest.Resolver.RoundTrip.Vocabulary", TestFlags)
bool FQuestResolver_RoundTripVocabulary::RunTest(const FString& Parameters)
{
	// THE property that keeps the two directions honest: a studio-shaped bundle taken forward through the recipe and then
	// back again must arrive with the same content. Any drift between the forward and reverse reads shows up here — which is
	// the whole reason the recipe is modelled as ONE dictionary rather than two translations.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });
	AddKind(*M, UQuestlineNode_Exit::StaticClass(), { TEXT("finish") });
	AddBinding(*M, TEXT("label"), TEXT("NodeLabel"));

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("n_go"),   { { TEXT("graph"), TEXT("root") }, { TEXT("type"), TEXT("objective") }, { TEXT("label"), TEXT("Go There") } });
	AddRow(Content, TEXT("n_exit"), { { TEXT("graph"), TEXT("root") }, { TEXT("type"), TEXT("finish") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	const TMap<FString, FString> Before = FlattenContent(Bundle);

	TArray<FString> Warnings;
	TestTrue(TEXT("Forward mapping accepted"), QuestBundle_ApplyMapping(Bundle, *M, Warnings));
	QuestBundle_ApplyReverseMapping(Bundle, *M, {}, {}, Warnings);

	const TMap<FString, FString> After = FlattenContent(Bundle);
	TestEqual(TEXT("Same number of cells survive the round trip"), After.Num(), Before.Num());
	for (const TPair<FString, FString>& Cell : Before)
	{
		const FString* Got = After.Find(Cell.Key);
		TestTrue(*FString::Printf(TEXT("Cell present after round trip: %s"), *Cell.Key), Got != nullptr);
		if (Got) { TestEqual(*FString::Printf(TEXT("Cell value: %s"), *Cell.Key), *Got, Cell.Value); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RoundTripWiring, "SimpleQuest.Resolver.RoundTrip.Wiring", TestFlags)
bool FQuestResolver_RoundTripWiring::RunTest(const FString& Parameters)
{
	// Wiring authored as a COLUMN becomes edges going forward and must become the same column coming back. This is the exact
	// failure that shipped once by hand: the edges were claimed and removed while the cells were never written, so the
	// relationship vanished with nothing reported. A qualified binding is used so the match needs no node lookup.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });
	AddKind(*M, UQuestlineNode_Exit::StaticClass(), { TEXT("finish") });

	FQuestWireBinding Wire;
	Wire.SourceColumn = TEXT("on_solved");
	Wire.EdgeVerb = TEXT("outcome");
	Wire.Qualifier = TEXT("SimpleQuest.Outcome.Solved");
	M->WireBindings.Add(MoveTemp(Wire));

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("n_go"),   { { TEXT("graph"), TEXT("root") }, { TEXT("type"), TEXT("objective") }, { TEXT("on_solved"), TEXT("n_exit") } });
	AddRow(Content, TEXT("n_exit"), { { TEXT("graph"), TEXT("root") }, { TEXT("type"), TEXT("finish") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	TArray<FString> Warnings;
	TestTrue(TEXT("Forward mapping accepted"), QuestBundle_ApplyMapping(Bundle, *M, Warnings));
	QuestBundle_ApplyWireBindings(Bundle, *M, Warnings);
	TestEqual(TEXT("Wire column became an edge"), Bundle.Edges.Num(), 1);

	QuestBundle_ApplyReverseMapping(Bundle, *M, {}, {}, Warnings);
	TestEqual(TEXT("Edge was consumed back into the column"), Bundle.Edges.Num(), 0);

	const TMap<FString, FString> After = FlattenContent(Bundle);
	const FString* Restored = After.Find(TEXT("n_go/on_solved"));
	TestTrue(TEXT("Wire column restored"), Restored != nullptr);
	if (Restored) { TestEqual(TEXT("Wire target restored"), *Restored, FString(TEXT("n_exit"))); }
	return true;
}

// ── NormalizeDiscriminatorValue: the ONE definition both the panel and the import match through ─────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_NormalizeValue, "SimpleQuest.Resolver.NormalizeDiscriminatorValue", TestFlags)
bool FQuestResolver_NormalizeValue::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Trims surrounding whitespace"), NormalizeDiscriminatorValue(TEXT("  step  ")), FString(TEXT("step")));
	TestEqual(TEXT("Folds case"),                   NormalizeDiscriminatorValue(TEXT("STEP")),      FString(TEXT("step")));
	TestEqual(TEXT("Both together"),                NormalizeDiscriminatorValue(TEXT(" Step ")),    FString(TEXT("step")));
	TestEqual(TEXT("Already normal is unchanged"),  NormalizeDiscriminatorValue(TEXT("step")),      FString(TEXT("step")));
	return true;
}

// ── BuildDiscriminatorClassMap: the shared builder the guard AND the router both read ───────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ClassMapSynonyms, "SimpleQuest.Resolver.ClassMap.Synonyms", TestFlags)
bool FQuestResolver_ClassMapSynonyms::RunTest(const FString& Parameters)
{
	// One class answering to two studio words. Both must land, keyed normalized — this is the positive case that keeps the
	// collision test below honest.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective"), TEXT("Task") });

	TMap<FString, UClass*> ByValue;
	TArray<FText> Errors;
	TestTrue(TEXT("Clean map builds"), BuildDiscriminatorClassMap(*M, ByValue, Errors));
	TestEqual(TEXT("No errors"), Errors.Num(), 0);
	TestEqual(TEXT("Both synonyms routed"), ByValue.Num(), 2);
	TestTrue(TEXT("Normalized key present"), ByValue.Contains(TEXT("task")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ClassMapCollision, "SimpleQuest.Resolver.ClassMap.NormalizedCollision", TestFlags)
bool FQuestResolver_ClassMapCollision::RunTest(const FString& Parameters)
{
	// "Step" and " step " normalize identically. Left unchecked one would silently overwrite the other, so the builder must
	// REFUSE rather than pick a winner.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("Step") });
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT(" step ") });

	TMap<FString, UClass*> ByValue;
	TArray<FText> Errors;
	TestFalse(TEXT("Collision refused"), BuildDiscriminatorClassMap(*M, ByValue, Errors));
	TestTrue(TEXT("Reported why"), Errors.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ClassMapUnresolvable, "SimpleQuest.Resolver.ClassMap.UnresolvableClass", TestFlags)
bool FQuestResolver_ClassMapUnresolvable::RunTest(const FString& Parameters)
{
	// A value whose class can't resolve would route its rows nowhere. Refuse at authoring time, not at row-routing time.
	UQuestImportMapping* M = MakeMapping();
	FQuestDiscriminatorClass Entry;                       // NodeClass deliberately left null
	Entry.Values.Add(TEXT("objective"));
	M->DiscriminatorClasses.Add(MoveTemp(Entry));

	TMap<FString, UClass*> ByValue;
	TArray<FText> Errors;
	TestFalse(TEXT("Unresolvable class refused"), BuildDiscriminatorClassMap(*M, ByValue, Errors));
	TestTrue(TEXT("Reported why"), Errors.Num() > 0);
	return true;
}

// ── ValidateMappingAgainstSource: the shared refusal both the panel and the import enforce ──────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_GuardAccepts, "SimpleQuest.Resolver.Guard.AcceptsCleanMapping", TestFlags)
bool FQuestResolver_GuardAccepts::RunTest(const FString& Parameters)
{
	// The positive case. Without it, a guard that refuses EVERYTHING would still pass every test below.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });
	AddBinding(*M, TEXT("label"), TEXT("NodeLabel"));

	TArray<FText> Errors;
	const bool bOk = ValidateMappingAgainstSource(*M, { TEXT("type"), TEXT("label") }, { TEXT("objective") }, Errors);
	TestTrue(TEXT("Clean mapping accepted"), bOk);
	TestEqual(TEXT("No errors"), Errors.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_GuardUnmappedValue, "SimpleQuest.Resolver.Guard.UnmappedDiscriminatorValue", TestFlags)
bool FQuestResolver_GuardUnmappedValue::RunTest(const FString& Parameters)
{
	// A value present in the SOURCE with no class mapped: its rows would be silently dropped. This is the drift the guard
	// exists for — the "cinematic" case.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });

	TArray<FText> Errors;
	const bool bOk = ValidateMappingAgainstSource(*M, { TEXT("type") }, { TEXT("objective"), TEXT("cinematic") }, Errors);
	TestFalse(TEXT("Unmapped source value refused"), bOk);
	TestTrue(TEXT("Reported why"), Errors.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_GuardMissingColumn, "SimpleQuest.Resolver.Guard.BoundColumnAbsent", TestFlags)
bool FQuestResolver_GuardMissingColumn::RunTest(const FString& Parameters)
{
	// A binding naming a column the source doesn't have — the source drifted under a recipe that still looks valid.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });
	AddBinding(*M, TEXT("headline"), TEXT("NodeLabel"));

	TArray<FText> Errors;
	const bool bOk = ValidateMappingAgainstSource(*M, { TEXT("type"), TEXT("label") }, { TEXT("objective") }, Errors);
	TestFalse(TEXT("Absent bound column refused"), bOk);
	TestTrue(TEXT("Reported why"), Errors.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_GuardNoneRequire, "SimpleQuest.Resolver.Guard.UnmappedButRequired", TestFlags)
bool FQuestResolver_GuardNoneRequire::RunTest(const FString& Parameters)
{
	// Require means "refuse if the source has no value" — but an unmapped binding has no column to take a value FROM, so the
	// pair is self-contradictory and can never be satisfied.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });
	AddBinding(*M, nullptr, TEXT("NodeLabel"), EQuestAbsentFieldPolicy::Require);   // SourceColumn left None

	TArray<FText> Errors;
	const bool bOk = ValidateMappingAgainstSource(*M, { TEXT("type") }, { TEXT("objective") }, Errors);
	TestFalse(TEXT("None + Require refused"), bOk);
	TestTrue(TEXT("Reported why"), Errors.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_GuardDoubleDutyAdvises, "SimpleQuest.Resolver.Guard.DiscriminatorAlsoBoundIsAdvisory", TestFlags)
bool FQuestResolver_GuardDoubleDutyAdvises::RunTest(const FString& Parameters)
{
	// Binding the discriminator column to a property is legal — routing reads the column independently, so nothing is
	// corrupted — but unusual enough to surface. It must WARN and still ACCEPT: this pins the warn-don't-block decision so a
	// later change can't quietly promote it to a refusal.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });
	AddBinding(*M, TEXT("type"), TEXT("NodeLabel"));

	TArray<FText> Errors;
	TArray<FText> Warnings;
	const bool bOk = ValidateMappingAgainstSource(*M, { TEXT("type") }, { TEXT("objective") }, Errors, &Warnings);
	TestTrue(TEXT("Still accepted"), bOk);
	TestEqual(TEXT("Not an error"), Errors.Num(), 0);
	TestTrue(TEXT("Advised"), Warnings.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RoundTripNestedGraphLevel, "SimpleQuest.Resolver.RoundTrip.NestedGraphLevel", TestFlags)
bool FQuestResolver_RoundTripNestedGraphLevel::RunTest(const FString& Parameters)
{
	// A file the reverse read produces must be readable by the forward read. Nesting is where that can quietly stop being
	// true: rows are addressed by KEY, but the level a row belongs to is named by a SEPARATE cell, and import pairs the two
	// by string equality. Restating one without the other yields a file whose children point at a container that no longer
	// answers to that name — and import drops them without a word rather than failing. Flat fixtures cannot see this.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Step::StaticClass(), { TEXT("objective") });
	AddBinding(*M, TEXT("label"), TEXT("NodeLabel"));

	const FString ContainerGuid = TEXT("4D88DDB0450CAE0BE0549F9F56892550");
	const FString ChildGuid     = TEXT("643F4B0946C4741C952AACB8AC82550B");

	// The canonical shape the exporter produces: GUID keys, and a child whose level is its container's GUID.
	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, *ContainerGuid, { { TEXT("graph"), TEXT("root") },       { TEXT("class"), TEXT("QuestlineNode_Quest") } });
	AddRow(Content, *ChildGuid,     { { TEXT("graph"), *ContainerGuid },     { TEXT("class"), TEXT("QuestlineNode_Step") },
	                                  { TEXT("NodeLabel"), TEXT("Go There") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	// What import recorded on the nodes: both came from a studio source, so both carry their original key.
	TMap<FString, FString> SourceKeyByGuid;
	SourceKeyByGuid.Add(ContainerGuid, TEXT("chapter_1"));
	SourceKeyByGuid.Add(ChildGuid,     TEXT("c1_step"));

	TArray<FString> Warnings;
	QuestBundle_ApplyReverseMapping(Bundle, *M, SourceKeyByGuid, {}, Warnings);

	auto FindRow = [&Bundle](const FString& Key) -> const FQuestDataRow*
	{
		for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
		{
			for (const FQuestDataRow& Row : Table.Value.Rows) { if (Row.Key == Key) return &Row; }
		}
		return nullptr;
	};

	const FQuestDataRow* Child = FindRow(TEXT("c1_step"));
	TestNotNull(TEXT("Child row was restated to the studio key"), Child);
	if (Child)
	{
		// The defect: the key becomes 'c1_step' while the level stays the container's GUID.
		TestEqual(TEXT("Child's level is restated to the container's studio key"), Child->Get(TEXT("graph")), FString(TEXT("chapter_1")));
	}
	TestNotNull(TEXT("Container row was restated to the studio key"), FindRow(TEXT("chapter_1")));

	// The property that actually matters, stated directly: every level a row names must be a level some row declares,
	// or the forward read cannot pair them. Checking the invariant rather than only the one cell keeps this test honest
	// if the restatement is ever moved or reimplemented.
	for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
	{
		if (Table.Key == TEXT("questline_graph")) continue;
		for (const FQuestDataRow& Row : Table.Value.Rows)
		{
			const FString Level = Row.Get(TEXT("graph"));
			if (Level.IsEmpty() || Level == TEXT("root")) continue;
			TestNotNull(*FString::Printf(TEXT("Level '%s' named by row '%s' is declared by some row"), *Level, *Row.Key), FindRow(Level));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_TagCellRejectsForeignStruct, "SimpleQuest.Resolver.RestoreCell.TagCellRejectsForeignStruct", TestFlags)
bool FQuestResolver_TagCellRejectsForeignStruct::RunTest(const FString& Parameters)
{
	// A cell's Kind and its destination are paired by NAME alone: a mapping binds any column onto any property, and a source
	// column reaches a property just by spelling its name. So a tag-shaped value CAN arrive at a struct that is not a tag, and
	// "it is a struct" does not justify the cast — an FGameplayTagContainer is twice the size of an FGuid, so restoring one
	// over the other writes past the value. Callers that restore onto an exactly-sized allocation make that a heap overflow.
	//
	// The destination is backed by ZEROED slack rather than a poison pattern, because an assignment READS its destination
	// before writing: zeroes read as a well-formed empty container, so the unguarded write is well-defined and the failure is
	// an assertion. Any non-zero byte pattern there would instead be read as a live allocator pointer and freed.
	FProperty* GuidProp = UQuestlineNodeBase::StaticClass()->FindPropertyByName(TEXT("QuestGuid"));
	TestNotNull(TEXT("QuestGuid property resolved"), GuidProp);
	if (!GuidProp) { return false; }

	// The refusal is supposed to be LOUD, so declare the warning rather than tolerating it: this both keeps the run clean
	// and asserts the guard reported itself. A silent refusal would be a different bug, and this catches it.
	AddExpectedMessagePlain(TEXT("a tag-container value was bound to 'QuestGuid'"), ELogVerbosity::Warning);

	const FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(TEXT("SimpleQuest.Outcome.Solved"), /*ErrorIfNotFound*/ false);
	TestTrue(TEXT("Test tag resolved (an empty container would write nothing observable)"), TestTag.IsValid());
	if (!TestTag.IsValid()) { return false; }

	const int32 ValueBytes = GuidProp->GetSize();
	const int32 SlackBytes = sizeof(FGameplayTagContainer) + 64;

	TArray<uint8> Buffer;
	Buffer.SetNumZeroed(ValueBytes + SlackBytes);
	void* ValuePtr = Buffer.GetData();
	GuidProp->InitializeValue(ValuePtr);

	FQuestDataValue Cell;
	Cell.Kind = EQuestDataValueKind::TagContainer;
	Cell.TagContainer.AddTag(TestTag);

	// The RETURN is now the contract, not just the absence of a write. Before RestoreCell reported failure, a caller
	// could refuse a value, dirty the package and count it as a change that happened - and the assertions below could
	// not tell that apart from a clean refusal, because both leave the bytes alone.
	const bool bWrote = RestoreQuestCell(GuidProp, ValuePtr, Cell);
	TestFalse(TEXT("A refused restore reports that it did not write"), bWrote);

	// Nothing may be written beyond the destination property's own footprint.
	bool bSlackClean = true;
	for (int32 Idx = ValueBytes; Idx < Buffer.Num(); ++Idx)
	{
		if (Buffer[Idx] != 0) { bSlackClean = false; break; }
	}
	TestTrue(TEXT("A tag-container cell aimed at a non-tag struct wrote nothing past the value"), bSlackClean);
	TestFalse(TEXT("The non-tag struct's own bytes were overwritten"), static_cast<FGuid*>(ValuePtr)->IsValid());
	if (bSlackClean)
	{
		GuidProp->DestroyValue(ValuePtr);
	}
	else
	{
		// An unguarded write left a live container straddling the value; release what it allocated rather than leaking it.
		static_cast<FGameplayTagContainer*>(ValuePtr)->~FGameplayTagContainer();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ReattachSilenceIsNotEmptiness, "SimpleQuest.Resolver.Reattach.SilenceIsNotEmptiness", TestFlags)
bool FQuestResolver_ReattachSilenceIsNotEmptiness::RunTest(const FString& Parameters)
{
	// The destructive reading of an unmentioned property. Restoring onto an owner that already holds authored children must
	// not treat "the source never mentioned this" as "the source wants this empty" — those are different statements, and only
	// one of them is in the data. This is latent while every owner is freshly constructed, and becomes real the moment a
	// restore targets an asset that already exists.
	UQuestlineNode_Reward* Owner = NewObject<UQuestlineNode_Reward>(GetTransientPackage());
	Owner->Rewards.Add(NewObject<UXPReward>(Owner));
	TestEqual(TEXT("Owner starts with one authored reward"), Owner->Rewards.Num(), 1);

	FQuestDataBundle Bundle;   // says nothing about this owner at all
	TSet<FString> Consumed;
	TArray<FString> Warnings;
	ReattachQuestInstancedChildren(Owner, TEXT("n_reward"), Bundle, Consumed, Warnings);

	TestEqual(TEXT("A source that never mentions the property leaves its children alone"), Owner->Rewards.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ReattachDeclaredChildrenStillRebuild, "SimpleQuest.Resolver.Reattach.DeclaredChildrenRebuild", TestFlags)
bool FQuestResolver_ReattachDeclaredChildrenStillRebuild::RunTest(const FString& Parameters)
{
	// The other half, and the reason the preserve rule is trustworthy: a guard that skipped too eagerly would make the test
	// above pass by disabling reattachment entirely. A source that DOES declare children must still replace the contents.
	UQuestlineNode_Reward* Owner = NewObject<UQuestlineNode_Reward>(GetTransientPackage());
	UXPReward* Original = NewObject<UXPReward>(Owner);
	Owner->Rewards.Add(Original);

	FQuestDataBundle Bundle;
	FQuestDataTable Children;
	AddRow(Children, TEXT("n_reward/Rewards[0]"), { { TEXT("class"), TEXT("XPReward") } });
	Bundle.TablesByType.Add(TEXT("xp_reward"), MoveTemp(Children));

	TSet<FString> Consumed;
	TArray<FString> Warnings;
	ReattachQuestInstancedChildren(Owner, TEXT("n_reward"), Bundle, Consumed, Warnings);

	TestEqual(TEXT("Declared children rebuild the container"), Owner->Rewards.Num(), 1);
	if (Owner->Rewards.Num() == 1)
	{
		TestNotEqual(TEXT("The declared child replaced the existing instance"), Owner->Rewards[0].Get(), static_cast<UQuestRewardBase*>(Original));
		TestTrue(TEXT("The rebuilt child has the declared class"), Owner->Rewards[0] && Owner->Rewards[0]->IsA<UXPReward>());
	}
	TestTrue(TEXT("The child row was consumed"), Consumed.Contains(TEXT("n_reward/Rewards[0]")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RoundTripInstancedChildKeys, "SimpleQuest.Resolver.RoundTrip.InstancedChildKeys", TestFlags)
bool FQuestResolver_RoundTripInstancedChildKeys::RunTest(const FString& Parameters)
{
	// The same partial-restatement shape as nested levels, one layer down. A row's KEY and everything that REFERS to that key
	// have to move into the studio's vocabulary together. An instanced child is referred to twice — by the owner segment of
	// its own key, and by the contains edge that reaches it — so restating only the owner's row splits both.
	UQuestImportMapping* M = MakeMapping();
	AddKind(*M, UQuestlineNode_Reward::StaticClass(), { TEXT("loot") });

	const FString OwnerGuid = TEXT("4D88DDB0450CAE0BE0549F9F56892550");
	const FString ChildKey  = OwnerGuid + TEXT("/Rewards[0]");

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, *OwnerGuid, { { TEXT("graph"), TEXT("root") }, { TEXT("class"), TEXT("QuestlineNode_Reward") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	FQuestDataTable Children;
	AddRow(Children, *ChildKey, { { TEXT("class"), TEXT("XPReward") } });
	Bundle.TablesByType.Add(TEXT("xp_reward"), MoveTemp(Children));

	Bundle.Edges.Add({ OwnerGuid, TEXT("contains(Rewards[0])"), ChildKey });

	TMap<FString, FString> SourceKeyByGuid;
	SourceKeyByGuid.Add(OwnerGuid, TEXT("give_loot"));

	TArray<FString> Warnings;
	QuestBundle_ApplyReverseMapping(Bundle, *M, SourceKeyByGuid, {}, Warnings);

	auto FindRow = [&Bundle](const FString& Key) -> const FQuestDataRow*
	{
		for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
		{
			for (const FQuestDataRow& Row : Table.Value.Rows) { if (Row.Key == Key) return &Row; }
		}
		return nullptr;
	};

	TestNotNull(TEXT("The owner row was restated to the studio key"), FindRow(TEXT("give_loot")));
	TestNotNull(TEXT("The child key's owner segment followed it"), FindRow(TEXT("give_loot/Rewards[0]")));

	// A child row is ours, not theirs — it must not be collapsed into the content file the studio authored.
	const FQuestDataTable* ContentTable = Bundle.TablesByType.Find(TEXT("content"));
	TestNotNull(TEXT("A content table exists"), ContentTable);
	if (ContentTable)
	{
		bool bChildLeaked = false;
		for (const FQuestDataRow& Row : ContentTable->Rows) { if (Row.Key.Contains(TEXT("/"))) { bChildLeaked = true; break; } }
		TestFalse(TEXT("A child row leaked into the studio's content table"), bChildLeaked);
	}

	// The invariant, stated directly: every edge endpoint must name a row that exists. This is what actually breaks on a
	// partial restatement, and it keeps the test honest if the mechanism is ever moved or reimplemented.
	for (const FQuestDataEdge& Edge : Bundle.Edges)
	{
		TestNotNull(*FString::Printf(TEXT("Edge from-endpoint '%s' names a row"), *Edge.From), FindRow(Edge.From));
		TestNotNull(*FString::Printf(TEXT("Edge to-endpoint '%s' names a row"), *Edge.To), FindRow(Edge.To));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_BlankColumnIsDeclared, "SimpleQuest.Resolver.Read.BlankColumnIsDeclared", TestFlags)
bool FQuestResolver_BlankColumnIsDeclared::RunTest(const FString& Parameters)
{
	// The distinction the whole absent-field question rests on. A column the source DECLARES and leaves blank is a positive
	// statement — "this field is at its default" — while a column the source never had is silence. Dropping blank cells at the
	// read arm collapses the two, so nothing downstream can tell a designer CLEARING a field from a narrow source that never
	// mentioned it. The declaration has to ride on the ROW, because that is the only thing every later transform carries.
	const FString TempDir = FPaths::ProjectIntermediateDir() / TEXT("QuestResolverTests") / TEXT("BlankColumnIsDeclared");
	IFileManager::Get().DeleteDirectory(*TempDir, /*RequireExists*/ false, /*Tree*/ true);
	IFileManager::Get().MakeDirectory(*TempDir, /*Tree*/ true);

	// The header declares NodeLabel and the second row leaves it blank. Nothing declares Description at all.
	const FString Tsv =
		TEXT("key\tgraph\tclass\tNodeLabel\n")
		TEXT("n_a\troot\tQuestlineNode_Step\tGo There\n")
		TEXT("n_b\troot\tQuestlineNode_Step\t\n");
	TestTrue(TEXT("Fixture written"),
		FFileHelper::SaveStringToFile(Tsv, *(TempDir / TEXT("content.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FQuestDataBundle Bundle;
	FTsvQuestDataFormat Format;
	TestTrue(TEXT("Fixture read"), Format.ReadBundle(TempDir, Bundle));

	const FQuestDataTable* Content = Bundle.TablesByType.Find(TEXT("content"));
	TestNotNull(TEXT("Content table parsed"), Content);
	if (Content)
	{
		const FQuestDataRow* Blank = nullptr;
		const FQuestDataRow* Filled = nullptr;
		for (const FQuestDataRow& Row : Content->Rows)
		{
			if (Row.Key == TEXT("n_b")) { Blank = &Row; }
			if (Row.Key == TEXT("n_a")) { Filled = &Row; }
		}
		TestNotNull(TEXT("Blank-cell row parsed"), Blank);
		TestNotNull(TEXT("Filled-cell row parsed"), Filled);

		if (Filled)
		{
			// Regression guard: a column that carries a value must still arrive exactly as it did before.
			const FQuestDataValue* V = Filled->Cells.Find(TEXT("NodeLabel"));
			TestNotNull(TEXT("A populated column still produces a cell"), V);
			if (V) { TestEqual(TEXT("Its value survives"), V->StringForm, FString(TEXT("Go There"))); }
		}
		if (Blank)
		{
			const FQuestDataValue* V = Blank->Cells.Find(TEXT("NodeLabel"));
			TestNotNull(TEXT("A declared-but-blank column produces a cell"), V);
			if (V) { TestTrue(TEXT("That cell is Empty-kinded"), V->Kind == EQuestDataValueKind::Empty); }

			// The other half, and the reason this is not just "always add a cell": silence must stay silence.
			TestFalse(TEXT("An undeclared column produces no cell"), Blank->Cells.Contains(TEXT("Description")));
		}
	}

	IFileManager::Get().DeleteDirectory(*TempDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_BlankGateColumnsAreSilent, "SimpleQuest.Resolver.Read.BlankGateColumnsAreSilent", TestFlags)
bool FQuestResolver_BlankGateColumnsAreSilent::RunTest(const FString& Parameters)
{
	// A rectangular source declares every gate column it uses ANYWHERE, so a row that uses one leaves the others blank — that
	// is the ordinary shape, not an authoring error. Once a declared-but-blank column carries a cell, the convention scan
	// starts finding those blanks, and the conflict branch runs BEFORE the operand check: without a guard at the lookup, a row
	// legitimately gated by one convention gets accused of double-declaring a prerequisite it never wrote.
	const FString TempDir = FPaths::ProjectIntermediateDir() / TEXT("QuestResolverTests") / TEXT("BlankGateColumns");
	IFileManager::Get().DeleteDirectory(*TempDir, /*RequireExists*/ false, /*Tree*/ true);
	IFileManager::Get().MakeDirectory(*TempDir, /*Tree*/ true);

	const FString Tsv =
		TEXT("key\tgraph\tclass\tunlock_after\tunlock_any\tunlock_unless\n")
		TEXT("n_a\troot\tQuestlineNode_Step\t\t\t\n")
		TEXT("n_b\troot\tQuestlineNode_Step\t\t\t\n")
		TEXT("n_x\troot\tQuestlineNode_Step\t(n_a,n_b)\t\t\n")
		TEXT("n_y\troot\tQuestlineNode_Step\t\t(n_a,n_b)\t\n")
		TEXT("n_z\troot\tQuestlineNode_Step\t\t\tn_a\n");
	TestTrue(TEXT("Fixture written"),
		FFileHelper::SaveStringToFile(Tsv, *(TempDir / TEXT("content.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FQuestDataBundle Bundle;
	FTsvQuestDataFormat Format;
	TestTrue(TEXT("Fixture read"), Format.ReadBundle(TempDir, Bundle));

	auto CountRows = [&Bundle]()
	{
		int32 N = 0;
		for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType) { N += Table.Value.Rows.Num(); }
		return N;
	};
	const int32 RowsBefore = CountRows();

	TArray<FString> Warnings;
	ApplyQuestFlowConventions(Bundle, Warnings);

	// The regression: one gate per row, three rows gated, and NOTHING to say about the blanks.
	for (const FString& W : Warnings) { AddInfo(FString::Printf(TEXT("unexpected warning: %s"), *W)); }
	TestEqual(TEXT("A blank gate column produces no warning"), Warnings.Num(), 0);
	TestEqual(TEXT("One combinator synthesized per gated row"), CountRows(), RowsBefore + 3);

	// The gate columns are studio vocabulary, never properties — they must be stripped whether used or blank.
	for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
	{
		for (const FQuestDataRow& Row : Table.Value.Rows)
		{
			TestFalse(*FString::Printf(TEXT("unlock_after stripped from '%s'"), *Row.Key), Row.Cells.Contains(TEXT("unlock_after")));
			TestFalse(*FString::Printf(TEXT("unlock_any stripped from '%s'"), *Row.Key), Row.Cells.Contains(TEXT("unlock_any")));
			TestFalse(*FString::Printf(TEXT("unlock_unless stripped from '%s'"), *Row.Key), Row.Cells.Contains(TEXT("unlock_unless")));
		}
	}

	IFileManager::Get().DeleteDirectory(*TempDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_NodeKeyIndexAnswersToBothNames, "SimpleQuest.Resolver.Identity.BothNames", TestFlags)
bool FQuestResolver_NodeKeyIndexAnswersToBothNames::RunTest(const FString& Parameters)
{
	// A node has two legitimate names, and which one a source uses is a fact about the SOURCE, not the node: a canonical
	// export writes GUID keys for everything, a studio's own file writes its semantic keys. Indexing under one name only
	// makes the other source read as a wholesale rewrite of an asset it actually matches exactly.
	const FString GuidA = TEXT("4D88DDB0450CAE0BE0549F9F56892550");
	const FString GuidB = TEXT("643F4B0946C4741C952AACB8AC82550B");

	TMap<FString, FString> SourceKeyByGuid;
	SourceKeyByGuid.Add(GuidA, TEXT("kill_boss"));   // imported from a studio source
	// GuidB carries no studio key — it was authored in the editor.

	TMap<FString, FString> GuidByKey;
	TArray<FString> Ambiguous;
	BuildQuestNodeKeyIndex(SourceKeyByGuid, { GuidA, GuidB }, GuidByKey, Ambiguous);

	TestEqual(TEXT("No ambiguity in the clean case"), Ambiguous.Num(), 0);
	TestEqual(TEXT("A studio-keyed node answers to its semantic key"), GuidByKey.FindRef(TEXT("kill_boss")), GuidA);
	TestEqual(TEXT("...and still answers to its GUID"), GuidByKey.FindRef(GuidA), GuidA);
	TestEqual(TEXT("An editor-authored node answers to its GUID"), GuidByKey.FindRef(GuidB), GuidB);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_NodeKeyIndexRefusesAmbiguity, "SimpleQuest.Resolver.Identity.RefusesAmbiguity", TestFlags)
bool FQuestResolver_NodeKeyIndexRefusesAmbiguity::RunTest(const FString& Parameters)
{
	// Nothing clears ImportSourceKey on paste, so duplicating an imported node leaves two nodes answering to one name.
	// Picking a winner by hash order would plan an edit to one copy and a deletion of the other, chosen by nothing the
	// designer can see. Both claimants must leave the index so the caller can refuse that key specifically.
	const FString GuidA = TEXT("4D88DDB0450CAE0BE0549F9F56892550");
	const FString GuidB = TEXT("643F4B0946C4741C952AACB8AC82550B");

	TMap<FString, FString> SourceKeyByGuid;
	SourceKeyByGuid.Add(GuidA, TEXT("kill_boss"));
	SourceKeyByGuid.Add(GuidB, TEXT("kill_boss"));   // the pasted copy kept the original's provenance

	TMap<FString, FString> GuidByKey;
	TArray<FString> Ambiguous;
	BuildQuestNodeKeyIndex(SourceKeyByGuid, { GuidA, GuidB }, GuidByKey, Ambiguous);

	TestTrue(TEXT("The contested key is reported"), Ambiguous.Contains(TEXT("kill_boss")));
	TestFalse(TEXT("The contested key resolves to nobody"), GuidByKey.Contains(TEXT("kill_boss")));
	// The GUIDs are still unambiguous, so a GUID-keyed source can still address either node precisely.
	TestEqual(TEXT("Each node still answers to its own GUID"), GuidByKey.FindRef(GuidA), GuidA);
	TestEqual(TEXT("...both of them"), GuidByKey.FindRef(GuidB), GuidB);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_LevelResolvesToOneNamespace, "SimpleQuest.Resolver.Identity.LevelNamespace", TestFlags)
bool FQuestResolver_LevelResolvesToOneNamespace::RunTest(const FString& Parameters)
{
	// A level is spelled differently depending on who wrote it: our export names an inner level by the container's GUID, a
	// studio's source names it by their own key. Comparing those raw reports "needs rebuilding" for nodes that never moved.
	const FString ContainerGuid = TEXT("4D88DDB0450CAE0BE0549F9F56892550");

	TMap<FString, FString> SourceKeyByGuid;
	SourceKeyByGuid.Add(ContainerGuid, TEXT("chapter_1"));

	TMap<FString, FString> GuidByKey;
	TArray<FString> Ambiguous;
	BuildQuestNodeKeyIndex(SourceKeyByGuid, { ContainerGuid }, GuidByKey, Ambiguous);

	TestEqual(TEXT("The studio spelling resolves to the container"), ResolveQuestLevelToGuid(TEXT("chapter_1"), GuidByKey), ContainerGuid);
	TestEqual(TEXT("Our spelling resolves to the same container"), ResolveQuestLevelToGuid(ContainerGuid, GuidByKey), ContainerGuid);
	TestEqual(TEXT("The top level is its own name"), ResolveQuestLevelToGuid(TEXT("root"), GuidByKey), FString(TEXT("root")));
	// A level naming nothing we know passes through, so a caller can tell "unreachable" from "root".
	TestEqual(TEXT("An unknown level passes through"), ResolveQuestLevelToGuid(TEXT("nope"), GuidByKey), FString(TEXT("nope")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ScratchSeedIsTheAbsentPolicy, "SimpleQuest.Resolver.Compare.ScratchSeed", TestFlags)
bool FQuestResolver_ScratchSeedIsTheAbsentPolicy::RunTest(const FString& Parameters)
{
	// The plan's contract is to SIMULATE the apply, and the apply starts from the property's live value — so the simulation
	// must too. RestoreCell deliberately leaves some cells unwritten (an Empty one above all), and whatever the scratch was
	// seeded with is what survives that. Seeding from zero therefore reports a change to nothing-in-particular for every
	// declared-but-blank column. Seeding from the current value makes an absent cell mean "leave it alone" — which is
	// precisely Preserve, and seeding from the class default instead would be Reset. The seed IS the policy.
	UQuestlineNode_Exit* Node = NewObject<UQuestlineNode_Exit>(GetTransientPackage());
	FProperty* Prop = UQuestlineNode_Exit::StaticClass()->FindPropertyByName(TEXT("OutcomeTag"));
	TestNotNull(TEXT("OutcomeTag property resolved"), Prop);
	if (!Prop) { return false; }

	void* Live = Prop->ContainerPtrToValuePtr<void>(Node);
	Prop->ImportText_Direct(TEXT("(TagName=\"SimpleQuest.Outcome.Solved\")"), Live, nullptr, PPF_None);

	// An EMPTY cell says nothing, so the property keeps what it had.
	FQuestDataValue Blank;   // Kind::Empty
	const FQuestDataValue Preserved = TypeQuestCellLikeProperty(Prop, Blank, Live);
	TestEqual(TEXT("An empty cell preserves the property's current value"), Preserved.Tag.ToString(),
		FString(TEXT("SimpleQuest.Outcome.Solved")));

	// A populated cell overrides it, seed or no seed — otherwise the seed would swallow real edits.
	const FQuestDataValue Populated = FQuestDataValue::MakeString(TEXT("(TagName=\"SimpleQuest.Outcome.Triumph\")"));
	const FQuestDataValue Overridden = TypeQuestCellLikeProperty(Prop, Populated, Live);
	TestEqual(TEXT("A populated cell still overrides the seed"), Overridden.Tag.ToString(),
		FString(TEXT("SimpleQuest.Outcome.Triumph")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_SelfRowIsPlanned, "SimpleQuest.Resolver.Plan.SelfRow", TestFlags)
bool FQuestResolver_SelfRowIsPlanned::RunTest(const FString& Parameters)
{
	// The questline's OWN properties are authored data like any node's, and a re-import writes them. A plan that describes
	// only nodes tells a designer their questline is unchanged while a re-import is about to rename it.
	UQuestlineGraph* Graph = NewObject<UQuestlineGraph>(GetTransientPackage());
	FProperty* IdProp = UQuestlineGraph::StaticClass()->FindPropertyByName(TEXT("QuestlineID"));
	TestNotNull(TEXT("QuestlineID property resolved"), IdProp);
	if (!IdProp) { return false; }
	IdProp->ImportText_Direct(TEXT("Authored"), IdProp->ContainerPtrToValuePtr<void>(Graph), nullptr, PPF_None);

	FQuestDataBundle Bundle;
	FQuestDataTable Self;
	AddRow(Self, TEXT("whatever"), { { TEXT("class"), TEXT("QuestlineGraph") }, { TEXT("QuestlineID"), TEXT("Renamed") } });
	Bundle.TablesByType.Add(TEXT("questline_graph"), MoveTemp(Self));

	const TMap<FString, const FQuestDataRow*> NoNodes;
	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NoNodes, {}, Plan);

	const FQuestNodePlanEntry* SelfEntry = nullptr;
	for (const FQuestNodePlanEntry& E : Plan.Entries) { if (E.bIsQuestlineSelf) { SelfEntry = &E; break; } }
	TestNotNull(TEXT("The questline itself is planned"), SelfEntry);
	if (SelfEntry)
	{
		TestEqual(TEXT("Its changed property is reported"), SelfEntry->Changes.Num(), 1);
		if (SelfEntry->Changes.Num() == 1)
		{
			TestEqual(TEXT("...and it is QuestlineID"), SelfEntry->Changes[0].Property, FString(TEXT("QuestlineID")));
			TestEqual(TEXT("...reading the incoming value"), SelfEntry->Changes[0].IncomingText, FString(TEXT("Renamed")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_SynthesizedSelfRowAssertsNothing, "SimpleQuest.Resolver.Plan.SynthesizedSelfRow", TestFlags)
bool FQuestResolver_SynthesizedSelfRowAssertsNothing::RunTest(const FString& Parameters)
{
	// A DataTable has no self row at all, so one is FABRICATED to satisfy the "exactly one questline row" rule, stamped with
	// the table's asset name. That name is ours. Diffing it would let a source that never described the questline rename it —
	// and on a re-import the designer's authored identity would quietly become a filename.
	UQuestlineGraph* Graph = NewObject<UQuestlineGraph>(GetTransientPackage());
	FProperty* IdProp = UQuestlineGraph::StaticClass()->FindPropertyByName(TEXT("QuestlineID"));
	if (!IdProp) { return false; }
	IdProp->ImportText_Direct(TEXT("Authored"), IdProp->ContainerPtrToValuePtr<void>(Graph), nullptr, PPF_None);

	FQuestDataBundle Bundle;
	FQuestDataTable Self;
	AddRow(Self, TEXT("DT_Rewards"), { { TEXT("class"), TEXT("QuestlineGraph") }, { TEXT("QuestlineID"), TEXT("DT_Rewards") } });
	Bundle.TablesByType.Add(TEXT("questline_graph"), MoveTemp(Self));
	Bundle.bSelfRowSynthesized = true;   // the read arm fabricated it

	const TMap<FString, const FQuestDataRow*> NoNodes;
	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NoNodes, {}, Plan);

	for (const FQuestNodePlanEntry& E : Plan.Entries)
	{
		if (!E.bIsQuestlineSelf) continue;
		TestEqual(TEXT("A fabricated self row proposes no change"), E.Changes.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_InstancedChildrenAreDiffed, "SimpleQuest.Resolver.Plan.InstancedChildren", TestFlags)
bool FQuestResolver_InstancedChildrenAreDiffed::RunTest(const FString& Parameters)
{
	// A reward is authored data living in its OWN row, not in a cell of its owner's. A plan that walks only a node's own
	// cells reports "nothing would change" while a re-import is about to rewrite every reward the questline grants — the
	// single most valuable thing an in-place preview could tell a designer, and the one it was silent about.
	UQuestlineNode_Reward* Owner = NewObject<UQuestlineNode_Reward>(GetTransientPackage());
	UXPReward* Reward = NewObject<UXPReward>(Owner);
	Owner->Rewards.Add(Reward);

	FProperty* AmountProp = UXPReward::StaticClass()->FindPropertyByName(TEXT("Amount"));
	TestNotNull(TEXT("Amount property resolved"), AmountProp);
	if (!AmountProp) { return false; }
	AmountProp->ImportText_Direct(TEXT("10"), AmountProp->ContainerPtrToValuePtr<void>(Reward), nullptr, PPF_None);

	FQuestDataBundle Bundle;
	FQuestDataTable Children;
	AddRow(Children, TEXT("n_reward/Rewards[0]"), { { TEXT("class"), TEXT("XPReward") }, { TEXT("Amount"), TEXT("99") } });
	Bundle.TablesByType.Add(TEXT("xpreward"), MoveTemp(Children));

	FQuestNodePlanEntry Entry;
	FQuestInPlacePlan Plan;
	DiffQuestInstancedChildren(Owner, TEXT("n_reward"), Bundle, Entry, Plan);

	TestEqual(TEXT("The reward's changed field is reported"), Entry.Changes.Num(), 1);
	if (Entry.Changes.Num() == 1)
	{
		// The path is what makes a nested change actionable — "Amount changed" on a node with four rewards says nothing.
		TestEqual(TEXT("...under its path"), Entry.Changes[0].Property, FString(TEXT("Rewards[0].Amount")));
		TestEqual(TEXT("...reading the incoming value"), Entry.Changes[0].IncomingText, FString(TEXT("99")));
		TestEqual(TEXT("...and the current one"), Entry.Changes[0].CurrentText, FString(TEXT("10")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_SilentSourceLeavesChildrenAlone, "SimpleQuest.Resolver.Plan.SilentChildren", TestFlags)
bool FQuestResolver_SilentSourceLeavesChildrenAlone::RunTest(const FString& Parameters)
{
	// The plan must agree with the restore path about silence, or it describes an apply that will not happen. The restore
	// path skips a property the bundle declares no children for; a plan reporting those rewards as removals would tell a
	// designer their data is about to vanish when nothing of the sort is pending.
	UQuestlineNode_Reward* Owner = NewObject<UQuestlineNode_Reward>(GetTransientPackage());
	Owner->Rewards.Add(NewObject<UXPReward>(Owner));

	FQuestDataBundle Bundle;   // says nothing about this owner at all

	FQuestNodePlanEntry Entry;
	FQuestInPlacePlan Plan;
	DiffQuestInstancedChildren(Owner, TEXT("n_reward"), Bundle, Entry, Plan);

	TestEqual(TEXT("A source that never mentions the property proposes no change"), Entry.Changes.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_EdgesCompareAcrossNamespaces, "SimpleQuest.Resolver.Plan.EdgeNamespaces", TestFlags)
bool FQuestResolver_EdgesCompareAcrossNamespaces::RunTest(const FString& Parameters)
{
	// Live wiring is always GUID-keyed; a studio's file names the same nodes by its own keys. Comparing the two raw would
	// report every unchanged edge as an addition AND a removal — a plan claiming a questline's entire wiring is being
	// rebuilt when nothing moved. Canonicalizing both sides first is the whole job.
	const FString GuidA = TEXT("4D88DDB0450CAE0BE0549F9F56892550");
	const FString GuidB = TEXT("643F4B0946C4741C952AACB8AC82550B");

	TMap<FString, FString> GuidByKey;
	GuidByKey.Add(TEXT("kill_boss"), GuidA);
	GuidByKey.Add(TEXT("guard_post"), GuidB);
	GuidByKey.Add(GuidA, GuidA);
	GuidByKey.Add(GuidB, GuidB);

	TArray<FQuestDataEdge> Incoming = { { TEXT("kill_boss"), TEXT("activates(Any Outcome)"), TEXT("guard_post") } };
	TArray<FQuestDataEdge> Live     = { { GuidA,             TEXT("activates(Any Outcome)"), GuidB } };

	TArray<FQuestDataEdge> Added, Removed;
	CompareQuestEdges(Incoming, Live, GuidByKey, Added, Removed);
	TestEqual(TEXT("The same edge in two spellings is not an addition"), Added.Num(), 0);
	TestEqual(TEXT("...nor a removal"), Removed.Num(), 0);

	// Rewire the target: one edge goes, one arrives.
	Added.Reset(); Removed.Reset();
	TArray<FQuestDataEdge> Rewired = { { TEXT("kill_boss"), TEXT("activates(Any Outcome)"), TEXT("somewhere_else") } };
	CompareQuestEdges(Rewired, Live, GuidByKey, Added, Removed);
	TestEqual(TEXT("A rewire is one addition"), Added.Num(), 1);
	TestEqual(TEXT("...and one removal"), Removed.Num(), 1);

	// Containment is described elsewhere in the plan; counting it here would double-report.
	Added.Reset(); Removed.Reset();
	TArray<FQuestDataEdge> Contains = { { TEXT("kill_boss"), TEXT("contains(Rewards[0])"), TEXT("kill_boss/Rewards[0]") } };
	CompareQuestEdges(Contains, {}, GuidByKey, Added, Removed);
	TestEqual(TEXT("A contains edge is not wiring"), Added.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_PlanRefusesUndeliverableRows, "SimpleQuest.Resolver.Plan.RefusesUndeliverable", TestFlags)
bool FQuestResolver_PlanRefusesUndeliverableRows::RunTest(const FString& Parameters)
{
	// Resolving a deliberately-bogus class exhausts every lookup before refusing, and the engine narrates each miss. Declare
	// both so the run stays clean AND the exhaustion is asserted — a refusal that skipped the lookup would be a different bug.
	AddExpectedMessagePlain(TEXT("Short type name \"NoSuchNodeClass\" provided for TryFindType"), EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Failed to find object 'Class None.NoSuchNodeClass'"), EAutomationExpectedMessageFlags::Contains, 1);
	
	// A plan is a promise the apply step keeps. Promising to CREATE a node whose class does not resolve, or one whose level
	// nothing declares, is a promise the spawn path refuses — so the plan would report work that silently never happens, and
	// a designer reading it would believe their source landed. Refuse at planning time, where it is still free.
	UQuestlineGraph* Graph = NewObject<UQuestlineGraph>(GetTransientPackage());

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("n_bogus"),  { { TEXT("graph"), TEXT("root") },        { TEXT("class"), TEXT("NoSuchNodeClass") } });
	AddRow(Content, TEXT("n_orphan"), { { TEXT("graph"), TEXT("no_such_box") }, { TEXT("class"), TEXT("QuestlineNode_Step") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	for (const FQuestDataRow& Row : Bundle.TablesByType[TEXT("content")].Rows) { NodeRowsByKey.Add(Row.Key, &Row); }

	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);

	TestEqual(TEXT("Neither undeliverable row is promised as a create"), Plan.CountOf(EQuestNodePlanAction::Create), 0);
	TestEqual(TEXT("Both are refused instead"), Plan.Refusals.Num(), 2);
	TestFalse(TEXT("A plan carrying refusals is not a no-op"), Plan.IsNoOp());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ApplyWritesNestedChanges, "SimpleQuest.Resolver.Apply.NestedPath", TestFlags)
bool FQuestResolver_ApplyWritesNestedChanges::RunTest(const FString& Parameters)
{
	// Apply has to find the live property a change names, and a change names it by PATH. Those paths cannot be parsed: a
	// map-key segment carries an exported tag, dots and brackets included, so splitting on punctuation would mis-resolve.
	// Apply therefore re-walks the object with the same walk that produced the paths and matches the longest prefix — which
	// also means a path the plan can produce is always one apply can resolve, by construction.
	UQuestlineNode_Reward* Owner = NewObject<UQuestlineNode_Reward>(GetTransientPackage());
	UXPReward* Reward = NewObject<UXPReward>(Owner);
	Owner->Rewards.Add(Reward);

	FProperty* AmountProp = UXPReward::StaticClass()->FindPropertyByName(TEXT("Amount"));
	TestNotNull(TEXT("Amount property resolved"), AmountProp);
	if (!AmountProp) { return false; }
	void* AmountPtr = AmountProp->ContainerPtrToValuePtr<void>(Reward);
	AmountProp->ImportText_Direct(TEXT("10"), AmountPtr, nullptr, PPF_None);

	FQuestPropertyChange Change;
	Change.Property      = TEXT("Rewards[0].Amount");
	Change.IncomingValue = FQuestDataValue::MakeNumber(TEXT("99"));

	TArray<FString> Skipped;
	const int32 Written = QuestBundle_ApplyChangesToObject(Owner, TEXT("n_reward"), { Change }, Skipped);

	TestEqual(TEXT("The nested change was written"), Written, 1);
	TestEqual(TEXT("Nothing was skipped"), Skipped.Num(), 0);

	FString After;
	AmountProp->ExportTextItem_Direct(After, AmountPtr, nullptr, nullptr, PPF_None);
	TestEqual(TEXT("The live property now holds the incoming value"), After, FString(TEXT("99")));

	// A path naming nothing must be reported, never silently dropped — apply's failure mode is a designer believing a change
	// landed when it did not.
	Skipped.Reset();
	FQuestPropertyChange Bogus;
	Bogus.Property      = TEXT("Rewards[9].Nope");
	Bogus.IncomingValue = FQuestDataValue::MakeNumber(TEXT("1"));
	TestEqual(TEXT("An unresolvable path writes nothing"), QuestBundle_ApplyChangesToObject(Owner, TEXT("n_reward"), { Bogus }, Skipped), 0);
	TestEqual(TEXT("...and is reported"), Skipped.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_AbsentPolicySelectsTheSeed, "SimpleQuest.Resolver.Plan.AbsentPolicy", TestFlags)
bool FQuestResolver_AbsentPolicySelectsTheSeed::RunTest(const FString& Parameters)
{
	// A declared-but-blank cell is a statement about the default — but WHICH default, and whether blank is even allowed, is
	// the recipe's call. All three answers come out of one knob: what the comparison scratch is seeded with. Preserve seeds
	// from the live value so an absent cell changes nothing; Reset seeds from the class default so it resets; Require does
	// not seed at all, it refuses. Nothing else about the comparison differs between them.
	// (This exercises the PLANNER. Apply separately declines identity-bearing properties, which is a different guard.)
	auto MakeGraph = []()
	{
		UQuestlineGraph* G = NewObject<UQuestlineGraph>(GetTransientPackage());
		FProperty* P = UQuestlineGraph::StaticClass()->FindPropertyByName(TEXT("QuestlineID"));
		if (P) { P->ImportText_Direct(TEXT("Authored"), P->ContainerPtrToValuePtr<void>(G), nullptr, PPF_None); }
		return G;
	};
	auto MakeBundle = []()
	{
		FQuestDataBundle B;
		FQuestDataTable Self;
		FQuestDataRow Row;
		Row.Key = TEXT("whatever");
		Row.Cells.Add(TEXT("class"), FQuestDataValue::MakeString(TEXT("QuestlineGraph")));
		Row.Cells.Add(TEXT("QuestlineID"), FQuestDataValue::MakeEmpty());   // DECLARED, and left blank
		Self.Rows.Add(MoveTemp(Row));
		B.TablesByType.Add(TEXT("questline_graph"), MoveTemp(Self));
		return B;
	};
	auto SelfChanges = [](const FQuestInPlacePlan& Plan)
	{
		for (const FQuestNodePlanEntry& E : Plan.Entries) { if (E.bIsQuestlineSelf) return E.Changes.Num(); }
		return -1;
	};

	const TMap<FString, const FQuestDataRow*> NoNodes;

	{
		FQuestAbsentPolicyResolver Policies;   // Preserve is the default default
		FQuestInPlacePlan Plan;
		const FQuestDataBundle Bundle = MakeBundle();
		QuestBundle_PlanInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
		TestEqual(TEXT("Preserve: a blank cell proposes no change"), SelfChanges(Plan), 0);
		TestEqual(TEXT("Preserve: and refuses nothing"), Plan.Refusals.Num(), 0);
	}
	{
		FQuestAbsentPolicyResolver Policies;
		Policies.Default = EQuestAbsentFieldPolicy::Reset;
		FQuestInPlacePlan Plan;
		const FQuestDataBundle Bundle = MakeBundle();
		QuestBundle_PlanInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
		TestEqual(TEXT("Reset: a blank cell proposes the default"), SelfChanges(Plan), 1);
	}
	{
		FQuestAbsentPolicyResolver Policies;
		Policies.Default = EQuestAbsentFieldPolicy::Require;
		FQuestInPlacePlan Plan;
		const FQuestDataBundle Bundle = MakeBundle();
		QuestBundle_PlanInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
		TestEqual(TEXT("Require: a blank cell is refused"), Plan.Refusals.Num(), 1);
		TestEqual(TEXT("Require: and nothing is proposed"), SelfChanges(Plan), 0);
	}

	// A per-property override beats the default, which is the whole point of per-binding policy.
	{
		FQuestAbsentPolicyResolver Policies;
		Policies.Default = EQuestAbsentFieldPolicy::Reset;
		Policies.ByProperty.Add(TEXT("QuestlineID"), EQuestAbsentFieldPolicy::Preserve);
		FQuestInPlacePlan Plan;
		const FQuestDataBundle Bundle = MakeBundle();
		QuestBundle_PlanInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
		TestEqual(TEXT("A per-property override wins over the default"), SelfChanges(Plan), 0);
	}
	return true;
}

namespace
{
	// A real questline graph, in the transient package — the same four steps the asset factory performs. Cheap enough that
	// the mutating half of in-place can be unit-tested rather than only exercised by hand against a disposable asset, which
	// matters most for the operations that DESTROY.
	UQuestlineGraph* MakeTransientQuestlineGraph()
	{
		UQuestlineGraph* Graph = NewObject<UQuestlineGraph>(GetTransientPackage());
		Graph->QuestlineEdGraph = NewObject<UEdGraph>(Graph, NAME_None, RF_Transactional);
		Graph->QuestlineEdGraph->Schema = UQuestlineGraphSchema::StaticClass();
		Graph->QuestlineEdGraph->GetSchema()->CreateDefaultNodesForGraph(*Graph->QuestlineEdGraph);
		return Graph;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ApplyRefusesUntrustworthyPlan, "SimpleQuest.Resolver.Apply.RefusesUntrustworthyPlan", TestFlags)
bool FQuestResolver_ApplyRefusesUntrustworthyPlan::RunTest(const FString& Parameters)
{
	// A refusal or a contested key says the planner could not describe the source — not that one row is unusual. Applying
	// the parts it DID describe would be acting on a description already known to be incomplete, so apply declines wholesale.
	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	TestNotNull(TEXT("Transient questline graph built"), Graph->QuestlineEdGraph.Get());

	FQuestInPlacePlan Plan;
	Plan.Refusals.Add(TEXT("something the planner could not deliver"));

	FQuestDataBundle Bundle;
	const TMap<FString, const FQuestDataRow*> NoRows;
	FQuestApplyResult Result;
	QuestBundle_ApplyPlan(*Graph, Plan, Bundle, NoRows, Result, FQuestApplyOptions());

	TestTrue(TEXT("Apply refused the plan"), Result.bRefused);
	TestEqual(TEXT("...and wrote nothing"), Result.PropertiesWritten, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ApplyCreatesDeclaredNodes, "SimpleQuest.Resolver.Apply.CreatesNodes", TestFlags)
bool FQuestResolver_ApplyCreatesDeclaredNodes::RunTest(const FString& Parameters)
{
	// The first ADDITIVE mutation. A source declaring a node the asset does not have plans as a create; applying it has to
	// actually put a node in the graph, at the level the row names, with its properties restored — and then the same source
	// planned again must have nothing left to create. That last part is the fixpoint, and it is what distinguishes "a node
	// was spawned" from "a node was spawned that the planner still does not recognise as the row's node".
	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	const int32 NodesBefore = Graph->QuestlineEdGraph->Nodes.Num();

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("n_new"), { { TEXT("graph"), TEXT("root") }, { TEXT("class"), TEXT("QuestlineNode_Step") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	for (const FQuestDataRow& Row : Bundle.TablesByType[TEXT("content")].Rows) { NodeRowsByKey.Add(Row.Key, &Row); }

	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The new row plans as a create"), Plan.CountOf(EQuestNodePlanAction::Create), 1);

	FQuestApplyResult Result;
	QuestBundle_ApplyPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
	TestEqual(TEXT("One node was created"), Result.NodesCreated, 1);
	TestEqual(TEXT("...and it is actually in the graph"), Graph->QuestlineEdGraph->Nodes.Num(), NodesBefore + 1);

	FQuestInPlacePlan Replanned;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Replanned);
	TestEqual(TEXT("Re-planning has nothing left to create"), Replanned.CountOf(EQuestNodePlanAction::Create), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ApplyWiresDeclaredEdges, "SimpleQuest.Resolver.Apply.WiresEdges", TestFlags)
bool FQuestResolver_ApplyWiresDeclaredEdges::RunTest(const FString& Parameters)
{
	// Wiring is the relationship half of a questline, and until now apply could build every node a source declared and leave
	// them all disconnected. The fixpoint is the real assertion: after applying, the same source must report no wiring delta,
	// which means the link that was made is the link the comparison recognises — not merely that MakeLinkTo was called.
	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	const UQuestlineNodeBase* Entry = nullptr;
	for (UEdGraphNode* N : Graph->QuestlineEdGraph->Nodes) { if ((Entry = Cast<UQuestlineNodeBase>(N)) != nullptr) break; }
	TestNotNull(TEXT("The schema's default entry node is present"), Entry);
	if (!Entry) { return false; }

	const FString EntryKey = Entry->QuestGuid.ToString(EGuidFormats::Digits);

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, *EntryKey,   { { TEXT("graph"), TEXT("root") }, { TEXT("class"), *Entry->GetClass()->GetName() } });
	AddRow(Content, TEXT("n_step"), { { TEXT("graph"), TEXT("root") }, { TEXT("class"), TEXT("QuestlineNode_Step") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));
	Bundle.Edges.Add({ EntryKey, TEXT("activates(Entered)"), TEXT("n_step") });

	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	for (const FQuestDataRow& Row : Bundle.TablesByType[TEXT("content")].Rows) { NodeRowsByKey.Add(Row.Key, &Row); }

	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The step plans as a create"), Plan.CountOf(EQuestNodePlanAction::Create), 1);
	TestEqual(TEXT("The edge plans as an addition"), Plan.AddedEdges.Num(), 1);

	FQuestApplyResult Result;
	QuestBundle_ApplyPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
	TestEqual(TEXT("The node was created"), Result.NodesCreated, 1);
	TestEqual(TEXT("The edge was wired"), Result.EdgesChanged, 1);

	FQuestInPlacePlan Replanned;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Replanned);
	TestEqual(TEXT("Re-planning has nothing left to create"), Replanned.CountOf(EQuestNodePlanAction::Create), 0);
	TestEqual(TEXT("Re-planning has no wiring delta"), Replanned.AddedEdges.Num(), 0);
	TestEqual(TEXT("...in either direction"), Replanned.RemovedEdges.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ApplyDeletesOrphansOnlyWhenAsked, "SimpleQuest.Resolver.Apply.DeletesOrphans", TestFlags)
bool FQuestResolver_ApplyDeletesOrphansOnlyWhenAsked::RunTest(const FString& Parameters)
{
	// The only operation in this arc that DESTROYS, so it is opt-in twice over: the plan reports an orphan regardless, and
	// apply removes one only when explicitly permitted. The schema's default entry node is a genuine orphan here — it exists,
	// no source mentions it, and it sits in a level the source DOES declare, which is exactly the case the scoping rule
	// admits. A node outside the declared levels would not be an orphan at all.
	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	const int32 NodesBefore = Graph->QuestlineEdGraph->Nodes.Num();
	TestEqual(TEXT("The harness starts with the schema's default node"), NodesBefore, 1);

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("n_step"), { { TEXT("graph"), TEXT("root") }, { TEXT("class"), TEXT("QuestlineNode_Step") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	for (const FQuestDataRow& Row : Bundle.TablesByType[TEXT("content")].Rows) { NodeRowsByKey.Add(Row.Key, &Row); }

	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The unmentioned node plans as an orphan"), Plan.CountOf(EQuestNodePlanAction::Orphan), 1);

	// Not permitted: the orphan is reported and left exactly where it is.
	{
		FQuestApplyResult Result;
		FQuestApplyOptions Options;   // bDeleteOrphanedNodes stays false
		QuestBundle_ApplyPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, Options);
		TestEqual(TEXT("Nothing is deleted without permission"), Result.NodesDeleted, 0);
		TestTrue(TEXT("The orphan is still in the graph"), Graph->QuestlineEdGraph->Nodes.Num() >= NodesBefore);
	}

	// Permitted: it goes, and re-planning no longer sees an orphan.
	{
		FQuestInPlacePlan Fresh;
		QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Fresh);
		FQuestApplyResult Result;
		FQuestApplyOptions Options;
		Options.bDeleteOrphanedNodes = true;
		QuestBundle_ApplyPlan(*Graph, Fresh, Bundle, NodeRowsByKey, Result, Options);
		TestEqual(TEXT("The orphan is deleted when asked"), Result.NodesDeleted, 1);
		FQuestInPlacePlan Replanned;
		QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Replanned);
		TestEqual(TEXT("Re-planning reports no orphan"), Replanned.CountOf(EQuestNodePlanAction::Orphan), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ApplyIsOneUndoStep, "SimpleQuest.Resolver.Apply.UndoRevertsWholeApply", TestFlags)
bool FQuestResolver_ApplyIsOneUndoStep::RunTest(const FString& Parameters)
{
	/**
	 * "One undo reverses an entire apply" is a promise the resolver makes and, until this test, nothing checked. Every other
	 * apply test runs with no transaction open, so GUndo is null and every Modify() inside ApplyPlan is a no-op - the whole
	 * lot could be deleted and the suite would stay green.
	 * TWO conditions gate SaveToTransactionBuffer, not one: an open transaction AND RF_Transactional on the object. A test
	 * that opens a transaction over objects lacking the flag proves nothing, so this leans on MakeTransientQuestlineGraph
	 * setting it on the inner UEdGraph and on SpawnNodeFromRow setting it on every node it creates.
	 */
	if (!GEditor)
	{
		AddError(TEXT("No GEditor - this test needs the editor transaction buffer"));
		return false;
	}

	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	const int32 NodesBefore = Graph->QuestlineEdGraph->Nodes.Num();

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("n_undo"), { { TEXT("graph"), TEXT("root") }, { TEXT("class"), TEXT("QuestlineNode_Step") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	for (const FQuestDataRow& Row : Bundle.TablesByType[TEXT("content")].Rows) { NodeRowsByKey.Add(Row.Key, &Row); }

	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The row plans as a create"), Plan.CountOf(EQuestNodePlanAction::Create), 1);

	// ApplyPlan deliberately does not own the transaction - its caller does - so the test has to BE the caller.
	GEditor->BeginTransaction(NSLOCTEXT("QuestResolverTests", "ApplyUndoTest", "Apply Quest Import"));
	FQuestApplyResult Result;
	QuestBundle_ApplyPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
	GEditor->EndTransaction();

	TestEqual(TEXT("One node was created"), Result.NodesCreated, 1);
	TestEqual(TEXT("...and it is in the graph"), Graph->QuestlineEdGraph->Nodes.Num(), NodesBefore + 1);

	// The assertion that earns the test. UEdGraph::AddNode does NOT Modify() - it appends and notifies - so the Level->Modify()
	// inside the create pass is the only thing recording the node array. Remove it and this line is what goes red.
	GEditor->UndoTransaction();
	TestEqual(TEXT("Undo reversed the create"), Graph->QuestlineEdGraph->Nodes.Num(), NodesBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_PasteClearsImportProvenance, "SimpleQuest.Resolver.Identity.PasteClearsProvenance", TestFlags)
bool FQuestResolver_PasteClearsImportProvenance::RunTest(const FString& Parameters)
{
	// Pasting a node inside a graph is the ENTIRE duplicate-identity failure mode. The GUID is already regenerated, but the
	// studio key the node was imported under is not — so the copy still claims to be the source row's node, and one row
	// resolves to two nodes. The planner refuses that collision rather than picking a winner by hash order, which is correct
	// but leaves a designer holding a plan they cannot act on until they fix it by hand. Better not to create it.
	UQuestlineNode_Step* Node = NewObject<UQuestlineNode_Step>(GetTransientPackage());
	Node->ImportSourceKey = TEXT("kill_boss");
	const FGuid BeforeGuid = Node->QuestGuid;

	Node->PostPasteNode();

	TestTrue(TEXT("Paste mints a fresh identity"), Node->QuestGuid != BeforeGuid);
	TestTrue(TEXT("Paste clears the import provenance"), Node->ImportSourceKey.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_UndoRestoresAMovedNode, "SimpleQuest.Resolver.Apply.UndoRestoresMovedNode", TestFlags)
bool FQuestResolver_UndoRestoresAMovedNode::RunTest(const FString& Parameters)
{
	/**
	 * A move is a REPARENT, so undoing one has to put the node back in the graph it came FROM - not merely take it out of
	 * the one it went to. Those two outcomes are indistinguishable to a test that only counts the destination, and the
	 * failure between them is real: a half-reverted move leaves the node parented to the container but absent from both
	 * node arrays, belonging to no graph at all. Asserting membership on BOTH sides is the only way to tell them apart,
	 * and it is the assertion UndoRevertsWholeApply structurally cannot make, because a create has no source graph.
	 */
	if (!GEditor)
	{
		AddError(TEXT("No GEditor - this test needs the editor transaction buffer"));
		return false;
	}

	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	UEdGraph* Root = Graph->QuestlineEdGraph;

	// Built directly rather than through an import, so the move is the ONLY thing under test - a create landing in the
	// same run would make a failure ambiguous between the two passes.
	UQuestlineNode_Quest* Container = NewObject<UQuestlineNode_Quest>(Root, NAME_None, RF_Transactional);
	Container->QuestGuid = FGuid::NewGuid();
	Root->AddNode(Container);
	Container->PostPlacedNewNode();   // this is what builds the inner graph

	UQuestlineNode_Step* Step = NewObject<UQuestlineNode_Step>(Root, NAME_None, RF_Transactional);
	Step->QuestGuid = FGuid::NewGuid();
	Root->AddNode(Step);
	Step->PostPlacedNewNode();

	UEdGraph* Inner = Container->GetInnerGraph();
	TestNotNull(TEXT("The container built an inner graph"), Inner);
	if (!Inner) { return false; }

	const FString ContainerKey = Container->QuestGuid.ToString(EGuidFormats::Digits);
	const FString StepKey      = Step->QuestGuid.ToString(EGuidFormats::Digits);

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, *ContainerKey, { { TEXT("graph"), TEXT("root") },  { TEXT("class"), TEXT("QuestlineNode_Quest") } });
	AddRow(Content, *StepKey,      { { TEXT("graph"), *ContainerKey }, { TEXT("class"), TEXT("QuestlineNode_Step") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	for (const FQuestDataRow& Row : Bundle.TablesByType[TEXT("content")].Rows) { NodeRowsByKey.Add(Row.Key, &Row); }

	FQuestInPlacePlan Plan;
	QuestBundle_PlanInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("Nothing is refused - the source declares no wiring to cross a boundary"), Plan.Refusals.Num(), 0);

	const FQuestNodePlanEntry* Entry = Plan.Entries.FindByPredicate(
		[&StepKey](const FQuestNodePlanEntry& E){ return E.Key == StepKey; });
	TestNotNull(TEXT("The Step is planned"), Entry);
	if (!Entry) { return false; }
	TestTrue(TEXT("...as a move"), Entry->bMoved);

	GEditor->BeginTransaction(NSLOCTEXT("QuestResolverTests", "MoveUndoTest", "Apply Quest Import"));
	FQuestApplyResult Result;
	QuestBundle_ApplyPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
	GEditor->EndTransaction();

	TestEqual(TEXT("One node moved"), Result.NodesMoved, 1);
	TestTrue(TEXT("The Step is in the container"), Inner->Nodes.Contains(Step));
	TestFalse(TEXT("...and gone from the graph it left"), Root->Nodes.Contains(Step));

	GEditor->UndoTransaction();

	// BOTH sides, deliberately. "Gone from the container" is equally true of a node that ended up nowhere.
	TestTrue(TEXT("Undo returned the Step to the graph it came from"), Root->Nodes.Contains(Step));
	TestFalse(TEXT("...and took it back out of the container"), Inner->Nodes.Contains(Step));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
