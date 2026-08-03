// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/Utility/QuestlineNode_Reward.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestBundleTransforms.h"
#include "Resolver/QuestDataBundle.h"
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

	QuestBundle_RestoreCell(GuidProp, ValuePtr, Cell);

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
	QuestBundle_ReattachInstanced(Owner, TEXT("n_reward"), Bundle, Consumed, Warnings);

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
	QuestBundle_ReattachInstanced(Owner, TEXT("n_reward"), Bundle, Consumed, Warnings);

	TestEqual(TEXT("Declared children rebuild the container"), Owner->Rewards.Num(), 1);
	if (Owner->Rewards.Num() == 1)
	{
		TestNotEqual(TEXT("The declared child replaced the existing instance"), Owner->Rewards[0].Get(), static_cast<UQuestRewardBase*>(Original));
		TestTrue(TEXT("The rebuilt child has the declared class"), Owner->Rewards[0] && Owner->Rewards[0]->IsA<UXPReward>());
	}
	TestTrue(TEXT("The child row was consumed"), Consumed.Contains(TEXT("n_reward/Rewards[0]")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
