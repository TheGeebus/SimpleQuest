// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/DataTable.h"
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
#include "Resolver/QuestExportOperations.h"
#include "Resolver/QuestFlowConventions.h"
#include "Resolver/QuestInPlaceApply.h"
#include "Resolver/QuestInPlacePlan.h"
#include "Resolver/QuestInPlacePlanner.h"
#include "Resolver/QuestInstancedChildren.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestRowApply.h"
#include "Resolver/QuestRowPlanner.h"
#include "Resolver/QuestRowRestore.h"
#include "Resolver/TsvQuestDataFormat.h"
#include "Rewards/XPReward.h"
#include "ScopedTransaction.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Resolver/QuestDataFormatIO.h"
#include "Resolver/QuestExportOutput.h"
#include "Resolver/QuestPlanReport.h"
#include "Rewards/LootTableReward.h"
#include "Rewards/ScaledAmountReward.h"
#include "Tests/QuestResolverTestRow.h"
#include "Utilities/SimpleQuestEditorUtils.h"


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

	/**
	 * Read an int UPROPERTY by name. The reward classes keep their fields protected, and reflection is not a workaround
	 * here - it is the same surface RestoreQuestRowProperties writes through, so an assertion made this way exercises
	 * exactly the path the pipeline uses. Returns MIN_int32 for a missing property so a renamed field fails loudly
	 * instead of reading as a plausible zero.
	 */
	int32 ReadIntProperty(const UObject* Obj, const TCHAR* PropName)
	{
		if (!Obj) { return MIN_int32; }
		const FIntProperty* Prop = FindFProperty<FIntProperty>(Obj->GetClass(), FName(PropName));
		return Prop ? Prop->GetPropertyValue_InContainer(Obj) : MIN_int32;
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

	// The RETURN is now the contract, not just the absence of a write. Before RestoreQuestCell reported failure, a caller
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
	TMap<FString, FString> Files;
	FString GatherError;
	TestTrue(TEXT("Fixture gathered"), QuestDataFormatIO::ReadFilesFromFolder(TempDir, Format.FileExtension(), Files, GatherError));
	TestTrue(TEXT("Fixture read"), Format.ReadBundle(Files, Bundle));

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
	TMap<FString, FString> Files;
	FString GatherError;
	TestTrue(TEXT("Fixture gathered"), QuestDataFormatIO::ReadFilesFromFolder(TempDir, Format.FileExtension(), Files, GatherError));
	TestTrue(TEXT("Fixture read"), Format.ReadBundle(Files, Bundle));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_KeyHeaderSurvivesAWrite, "SimpleQuest.Resolver.Write.KeyHeaderSurvives", TestFlags)
bool FQuestResolver_KeyHeaderSurvivesAWrite::RunTest(const FString& Parameters)
{
	// A studio calls its key column what it likes, and the parse used to consume that name and throw it away - so a bundle
	// knew what its keys WERE but not what they were CALLED. Asserted at the PROVIDER, because no production path reaches
	// the live arm: every export builds its tables from the graph, so the writer always takes the fallback. That is exactly
	// why it is pinned here rather than left for whoever adds the first read-to-write path to discover.
	const FString ReadDir  = FPaths::ProjectIntermediateDir() / TEXT("QuestResolverTests") / TEXT("KeyHeaderSurvives_In");
	const FString WriteDir = FPaths::ProjectIntermediateDir() / TEXT("QuestResolverTests") / TEXT("KeyHeaderSurvives_Out");
	for (const FString& Dir : { ReadDir, WriteDir })
	{
		IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
	}

	const FString Tsv =
		TEXT("quest_id\tgraph\tclass\tNodeLabel\n")
		TEXT("n_a\troot\tQuestlineNode_Step\tGo There\n");
	TestTrue(TEXT("Fixture written"),
		FFileHelper::SaveStringToFile(Tsv, *(ReadDir / TEXT("content.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FTsvQuestDataFormat Format;
	FQuestDataBundle Bundle;
	TMap<FString, FString> ReadFiles;
	FString ReadGatherError;
	TestTrue(TEXT("Fixture gathered"), QuestDataFormatIO::ReadFilesFromFolder(ReadDir, Format.FileExtension(), ReadFiles, ReadGatherError));
	TestTrue(TEXT("Fixture read"), Format.ReadBundle(ReadFiles, Bundle));

	if (const FQuestDataTable* Content = Bundle.TablesByType.Find(TEXT("content")))
	{
		// Two different questions about one header, and the answer to each must not contaminate the other: the name is
		// recorded, and it stays OUT of the bindable columns because a key cannot bind to a property.
		TestEqual(TEXT("The key column's name survives the parse"), Content->KeyColumn, FString(TEXT("quest_id")));
		TestFalse(TEXT("...and does not join the bindable columns"), Content->Columns.Contains(TEXT("quest_id")));
	}
	else
	{
		AddError(TEXT("Content table did not parse"));
		return false;
	}

	// THE LIVE ARM: a table that knows its key's name writes that name back.
	TMap<FString, FString> WrittenFiles;
	FString WriteError;
	TestTrue(TEXT("Read bundle serialized"), Format.WriteBundle(Bundle, WrittenFiles));
	TestTrue(TEXT("Read bundle written"), QuestDataFormatIO::WriteFilesToFolder(WrittenFiles, WriteDir, WriteError));
	FString ReadBack;
	TestTrue(TEXT("Written file loaded"), FFileHelper::LoadFileToString(ReadBack, *(WriteDir / TEXT("content.tsv"))));
	TArray<FString> ReadBackLines;
	ReadBack.ParseIntoArrayLines(ReadBackLines, /*CullEmpty*/ false);
	TestTrue(TEXT("The written file has a header"), ReadBackLines.Num() > 0);
	if (ReadBackLines.Num() > 0)
	{
		TestTrue(TEXT("The studio's key header comes back out, not ours"), ReadBackLines[0].StartsWith(TEXT("quest_id\t")));
	}

	// THE INERT ARM, and the one that matters more today: a table BUILT rather than read carries no name and must still
	// write "key". Every current export takes this path, so getting it wrong would change output nobody asked to change.
	FQuestDataBundle Built;
	FQuestDataTable& BuiltTable = Built.TablesByType.Add(TEXT("content"));
	BuiltTable.Columns = { TEXT("graph"), TEXT("class") };
	FQuestDataRow BuiltRow;
	BuiltRow.Key = TEXT("n_a");
	BuiltRow.Cells.Add(TEXT("graph"), FQuestDataValue::MakeString(TEXT("root")));
	BuiltRow.Cells.Add(TEXT("class"), FQuestDataValue::MakeString(TEXT("QuestlineNode_Step")));
	BuiltTable.Rows.Add(MoveTemp(BuiltRow));

	IFileManager::Get().DeleteDirectory(*WriteDir, /*RequireExists*/ false, /*Tree*/ true);
	IFileManager::Get().MakeDirectory(*WriteDir, /*Tree*/ true);
	TMap<FString, FString> BuiltFiles;
	FString BuiltWriteError;
	TestTrue(TEXT("Built bundle serialized"), Format.WriteBundle(Built, BuiltFiles));
	TestTrue(TEXT("Built bundle written"), QuestDataFormatIO::WriteFilesToFolder(BuiltFiles, WriteDir, BuiltWriteError));
	FString BuiltBack;
	TestTrue(TEXT("Built file loaded"), FFileHelper::LoadFileToString(BuiltBack, *(WriteDir / TEXT("content.tsv"))));
	TArray<FString> BuiltLines;
	BuiltBack.ParseIntoArrayLines(BuiltLines, /*CullEmpty*/ false);
	if (BuiltLines.Num() > 0)
	{
		TestTrue(TEXT("A table carrying no key name still writes 'key'"), BuiltLines[0].StartsWith(TEXT("key\t")));
	}

	IFileManager::Get().DeleteDirectory(*ReadDir,  /*RequireExists*/ false, /*Tree*/ true);
	IFileManager::Get().DeleteDirectory(*WriteDir, /*RequireExists*/ false, /*Tree*/ true);
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
	// must too. RestoreQuestCell deliberately leaves some cells unwritten (an Empty one above all), and whatever the scratch was
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
	PlanQuestInPlace(*Graph, Bundle, NoNodes, {}, Plan);

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
	PlanQuestInPlace(*Graph, Bundle, NoNodes, {}, Plan);

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

	// No live nodes in this fixture, and the qualifiers here are already in resolved form - nothing to resolve against.
	// An empty map short-circuits the pin lookup, leaving this test measuring what it has always measured: that
	// ENDPOINT canonicalization works. The qualifier half is covered separately, against a real graph.
	const TMap<FString, const UQuestlineNodeBase*> NoNodes;

	TArray<FQuestDataEdge> Incoming = { { TEXT("kill_boss"), TEXT("activates(Any Outcome)"), TEXT("guard_post") } };
	TArray<FQuestDataEdge> Live     = { { GuidA,             TEXT("activates(Any Outcome)"), GuidB } };

	TArray<FQuestDataEdge> Added, Removed;
	CompareQuestEdges(Incoming, Live, GuidByKey, NoNodes, Added, Removed);
	TestEqual(TEXT("The same edge in two spellings is not an addition"), Added.Num(), 0);
	TestEqual(TEXT("...nor a removal"), Removed.Num(), 0);

	// Rewire the target: one edge goes, one arrives.
	Added.Reset(); Removed.Reset();
	TArray<FQuestDataEdge> Rewired = { { TEXT("kill_boss"), TEXT("activates(Any Outcome)"), TEXT("somewhere_else") } };
	CompareQuestEdges(Rewired, Live, GuidByKey, NoNodes, Added, Removed);
	TestEqual(TEXT("A rewire is one addition"), Added.Num(), 1);
	TestEqual(TEXT("...and one removal"), Removed.Num(), 1);

	// Containment is described elsewhere in the plan; counting it here would double-report.
	Added.Reset(); Removed.Reset();
	TArray<FQuestDataEdge> Contains = { { TEXT("kill_boss"), TEXT("contains(Rewards[0])"), TEXT("kill_boss/Rewards[0]") } };
	CompareQuestEdges(Contains, {}, GuidByKey, NoNodes, Added, Removed);
	TestEqual(TEXT("A contains edge is not wiring"), Added.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_PlanRefusesUndeliverableRows, "SimpleQuest.Resolver.Plan.RefusesUndeliverable", TestFlags)
bool FQuestResolver_PlanRefusesUndeliverableRows::RunTest(const FString& Parameters)
{
	// Resolving a deliberately-bogus class exhausts every lookup before refusing, and the engine narrates each miss. Declare
	// both so the run stays clean AND the exhaustion is asserted — a refusal that skipped the lookup would be a different bug.
	// The second match deliberately stops before the class name: engines differ on whether a failed lookup is narrated with a
	// package prefix ("Class None.X" vs "Class X"), and the name itself is already pinned by the line above.
	AddExpectedMessagePlain(TEXT("Short type name \"NoSuchNodeClass\" provided for TryFindType"), EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Failed to find object 'Class"), EAutomationExpectedMessageFlags::Contains, 1);
	
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
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);

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
	const int32 Written = ApplyQuestChangesToObject(Owner, TEXT("n_reward"), { Change }, Skipped);

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
	TestEqual(TEXT("An unresolvable path writes nothing"), ApplyQuestChangesToObject(Owner, TEXT("n_reward"), { Bogus }, Skipped), 0);
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
		PlanQuestInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
		TestEqual(TEXT("Preserve: a blank cell proposes no change"), SelfChanges(Plan), 0);
		TestEqual(TEXT("Preserve: and refuses nothing"), Plan.Refusals.Num(), 0);
	}
	{
		FQuestAbsentPolicyResolver Policies;
		Policies.Default = EQuestAbsentFieldPolicy::Reset;
		FQuestInPlacePlan Plan;
		const FQuestDataBundle Bundle = MakeBundle();
		PlanQuestInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
		TestEqual(TEXT("Reset: a blank cell proposes the default"), SelfChanges(Plan), 1);
	}
	{
		FQuestAbsentPolicyResolver Policies;
		Policies.Default = EQuestAbsentFieldPolicy::Require;
		FQuestInPlacePlan Plan;
		const FQuestDataBundle Bundle = MakeBundle();
		PlanQuestInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
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
		PlanQuestInPlace(*MakeGraph(), Bundle, NoNodes, {}, Plan, Policies);
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
	ApplyQuestPlan(*Graph, Plan, Bundle, NoRows, Result, FQuestApplyOptions());

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
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The new row plans as a create"), Plan.CountOf(EQuestNodePlanAction::Create), 1);

	FQuestApplyResult Result;
	ApplyQuestPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
	TestEqual(TEXT("One node was created"), Result.EntitiesCreated, 1);
	TestEqual(TEXT("...and it is actually in the graph"), Graph->QuestlineEdGraph->Nodes.Num(), NodesBefore + 1);

	FQuestInPlacePlan Replanned;
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Replanned);
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
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The step plans as a create"), Plan.CountOf(EQuestNodePlanAction::Create), 1);
	TestEqual(TEXT("The edge plans as an addition"), Plan.AddedEdges.Num(), 1);

	FQuestApplyResult Result;
	ApplyQuestPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
	TestEqual(TEXT("The node was created"), Result.EntitiesCreated, 1);
	TestEqual(TEXT("The edge was wired"), Result.EdgesChanged, 1);

	FQuestInPlacePlan Replanned;
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Replanned);
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
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The unmentioned node plans as an orphan"), Plan.CountOf(EQuestNodePlanAction::Orphan), 1);

	// Not permitted: the orphan is reported and left exactly where it is.
	{
		FQuestApplyResult Result;
		FQuestApplyOptions Options;   // bDeleteOrphanedNodes stays false
		ApplyQuestPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, Options);
		TestEqual(TEXT("Nothing is deleted without permission"), Result.EntitiesDeleted, 0);
		TestTrue(TEXT("The orphan is still in the graph"), Graph->QuestlineEdGraph->Nodes.Num() >= NodesBefore);
	}

	// Permitted: it goes, and re-planning no longer sees an orphan.
	{
		FQuestInPlacePlan Fresh;
		PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Fresh);
		FQuestApplyResult Result;
		FQuestApplyOptions Options;
		Options.bDeleteOrphanedNodes = true;
		ApplyQuestPlan(*Graph, Fresh, Bundle, NodeRowsByKey, Result, Options);
		TestEqual(TEXT("The orphan is deleted when asked"), Result.EntitiesDeleted, 1);
		FQuestInPlacePlan Replanned;
		PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Replanned);
		TestEqual(TEXT("Re-planning reports no orphan"), Replanned.CountOf(EQuestNodePlanAction::Orphan), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ApplyIsOneUndoStep, "SimpleQuest.Resolver.Apply.UndoRevertsWholeApply", TestFlags)
bool FQuestResolver_ApplyIsOneUndoStep::RunTest(const FString& Parameters)
{
	/**
	 * "One undo reverses an entire apply" is a promise the resolver makes and, until this test, nothing checked. Every other
	 * apply test runs with no transaction open, so GUndo is null and every Modify() inside ApplyQuestPlan is a no-op - the whole
	 * lot could be deleted and the suite would stay green.
	 * TWO conditions gate SaveToTransactionBuffer, not one: an open transaction AND RF_Transactional on the object. A test
	 * that opens a transaction over objects lacking the flag proves nothing, so this leans on MakeTransientQuestlineGraph
	 * setting it on the inner UEdGraph and on SpawnQuestNodeFromRow setting it on every node it creates.
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
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("The row plans as a create"), Plan.CountOf(EQuestNodePlanAction::Create), 1);

	// ApplyQuestPlan deliberately does not own the transaction - its caller does - so the test has to BE the caller.
	GEditor->BeginTransaction(NSLOCTEXT("QuestResolverTests", "ApplyUndoTest", "Apply Quest Import"));
	FQuestApplyResult Result;
	ApplyQuestPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
	GEditor->EndTransaction();

	TestEqual(TEXT("One node was created"), Result.EntitiesCreated, 1);
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
	PlanQuestInPlace(*Graph, Bundle, NodeRowsByKey, {}, Plan);
	TestEqual(TEXT("Nothing is refused - the source declares no wiring to cross a boundary"), Plan.Refusals.Num(), 0);

	const FQuestNodePlanEntry* Entry = Plan.Entries.FindByPredicate(
		[&StepKey](const FQuestNodePlanEntry& E){ return E.Key == StepKey; });
	TestNotNull(TEXT("The Step is planned"), Entry);
	if (!Entry) { return false; }
	TestTrue(TEXT("...as a move"), Entry->bMoved);

	GEditor->BeginTransaction(NSLOCTEXT("QuestResolverTests", "MoveUndoTest", "Apply Quest Import"));
	FQuestApplyResult Result;
	ApplyQuestPlan(*Graph, Plan, Bundle, NodeRowsByKey, Result, FQuestApplyOptions());
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

// THE ENDPOINT -> BUNDLE SEAM, EXERCISED FOR REAL. Every other test in this file hand-builds its FQuestDataBundle, so the
// suite pins bundle -> plan -> apply and takes the READER on faith. A divergence there — a different key spelling, a cell
// degraded to a string, a missing self row — passes all 37 while the system is wrong. This is the only test that can
// contradict the shape the others assume, and it is the arm reverse-apply will be built on top of.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_DataTableReadProducesTypedBundle,
	"SimpleQuest.Resolver.DataTableReadProducesTypedBundle", TestFlags)
bool FQuestResolver_DataTableReadProducesTypedBundle::RunTest(const FString& Parameters)
{
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_ResolverReadProbe"));
	Table->RowStruct = FQuestResolverTestRow::StaticStruct();
	Table->AddToRoot();   // the soft pointer below resolves through the live object; don't let GC race the assertions

	FQuestResolverTestRow Boss;
	Boss.type   = TEXT("step");
	Boss.label  = TEXT("Kill the boss");
	Boss.amount = 5;
	Table->AddRow(TEXT("kill_boss"), Boss);

	// Left at struct defaults deliberately. A DataTable row HAS a value for every field — absence is not expressible —
	// so these must arrive as cells that are PRESENT with Kind::Empty, never as missing cells.
	FQuestResolverTestRow Talk;
	Talk.type  = TEXT("step");
	Talk.label = TEXT("Talk to the smith");
	Table->AddRow(TEXT("talk_smith"), Talk);

	FQuestDataEndpoint Endpoint;
	Endpoint.Kind  = EQuestEndpointKind::DataTable;
	Endpoint.Table = TSoftObjectPtr<UDataTable>(Table);

	FQuestDataBundle Bundle;
	FString Error;
	const bool bRead = ReadEndpointBundle(Endpoint, Bundle, Error);
	if (!TestTrue(TEXT("A loadable table with rows reads"), bRead))
	{
		AddError(FString::Printf(TEXT("ReadEndpointBundle failed: %s"), *Error));
		Table->RemoveFromRoot();
		return false;
	}

	const FQuestDataTable* Content = Bundle.TablesByType.Find(TEXT("content"));
	const FQuestDataTable* Self    = Bundle.TablesByType.Find(TEXT("questline_graph"));
	TestNotNull(TEXT("The flat content table is stemmed 'content'"), Content);
	TestNotNull(TEXT("A self-row table is synthesized"), Self);
	TestTrue(TEXT("bSelfRowSynthesized marks that identity as ours, not the author's"), Bundle.bSelfRowSynthesized);
	if (!Content || !Self) { Table->RemoveFromRoot(); return false; }

	// Columns are the row struct's AUTHORED field names — the spelling reverse-apply must write back.
	TestEqual(TEXT("One column per row-struct field"), Content->Columns.Num(), 4);
	TestTrue(TEXT("Columns carry authored names"),
		Content->Columns.Contains(TEXT("type"))   && Content->Columns.Contains(TEXT("label")) &&
		Content->Columns.Contains(TEXT("amount")) && Content->Columns.Contains(TEXT("outcome")));

	// Row.Key is the row NAME. This is the convention a CLAIM is expressed in, so reverse-apply addresses rows the same way.
	const FQuestDataRow* BossRow = Content->Rows.FindByPredicate([](const FQuestDataRow& R){ return R.Key == TEXT("kill_boss"); });
	const FQuestDataRow* TalkRow = Content->Rows.FindByPredicate([](const FQuestDataRow& R){ return R.Key == TEXT("talk_smith"); });
	TestNotNull(TEXT("Row.Key is the row NAME"), BossRow);
	TestNotNull(TEXT("Every row is read"), TalkRow);

	if (BossRow)
	{
		const FQuestDataValue* Label  = BossRow->Cells.Find(TEXT("label"));
		const FQuestDataValue* Amount = BossRow->Cells.Find(TEXT("amount"));
		if (TestNotNull(TEXT("A string column produces a cell"), Label))
		{
			TestEqual(TEXT("A string field is Kind::String"), (int32)Label->Kind, (int32)EQuestDataValueKind::String);
			TestEqual(TEXT("...carrying its value"), Label->StringForm, FString(TEXT("Kill the boss")));
		}
		// TYPED, not degraded. This arm has the FProperty in hand, so a number must NOT arrive as a String the way a
		// text provider would deliver it — that difference is the whole reason the DataTable arm is not a format provider.
		if (TestNotNull(TEXT("A numeric column produces a cell"), Amount))
		{
			TestEqual(TEXT("A numeric field is Kind::Number, not String"), (int32)Amount->Kind, (int32)EQuestDataValueKind::Number);
			TestEqual(TEXT("...carrying its value"), Amount->StringForm, FString(TEXT("5")));
		}
	}

	if (TalkRow)
	{
		const FQuestDataValue* Amount  = TalkRow->Cells.Find(TEXT("amount"));
		const FQuestDataValue* Outcome = TalkRow->Cells.Find(TEXT("outcome"));
		if (TestNotNull(TEXT("An at-default numeric field is still PRESENT as a cell"), Amount))
		{
			TestEqual(TEXT("...with Kind::Empty"), (int32)Amount->Kind, (int32)EQuestDataValueKind::Empty);
		}
		if (TestNotNull(TEXT("An unset tag field is present too"), Outcome))
		{
			TestEqual(TEXT("...and Empty"), (int32)Outcome->Kind, (int32)EQuestDataValueKind::Empty);
		}
	}

	// The synthesized identity is the sanitized ASSET NAME — the analogue of the folder name a file source is read from.
	if (Self->Rows.Num() == 1)
	{
		const FString Expected = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Table->GetName());
		TestEqual(TEXT("Self-row key is the sanitized asset name"), Self->Rows[0].Key, Expected);
		TestEqual(TEXT("Self row declares the graph class"), Self->Rows[0].Get(TEXT("class")), FString(TEXT("QuestlineGraph")));
		TestEqual(TEXT("Self row carries QuestlineID"), Self->Rows[0].Get(TEXT("QuestlineID")), Expected);
	}
	else
	{
		AddError(FString::Printf(TEXT("Expected exactly one synthesized self row, got %d"), Self->Rows.Num()));
	}

	// THE PAIRED KNOWN-BAD, matching this file's stated discipline: green is only trustworthy once the reader has gone red.
	// An empty table must REFUSE rather than yield an empty bundle, which a planner would read as "delete everything".
	UDataTable* EmptyTable = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_ResolverReadProbeEmpty"));
	EmptyTable->RowStruct = FQuestResolverTestRow::StaticStruct();
	EmptyTable->AddToRoot();

	FQuestDataEndpoint EmptyEndpoint;
	EmptyEndpoint.Kind  = EQuestEndpointKind::DataTable;
	EmptyEndpoint.Table = TSoftObjectPtr<UDataTable>(EmptyTable);

	FQuestDataBundle EmptyBundle;
	FString EmptyError;
	TestFalse(TEXT("A table with no rows is refused"), ReadEndpointBundle(EmptyEndpoint, EmptyBundle, EmptyError));
	TestTrue(TEXT("...with a stated reason"), !EmptyError.IsEmpty());

	EmptyTable->RemoveFromRoot();
	Table->RemoveFromRoot();
	return true;
}

// THE OUTBOUND PLANNER'S DISPOSITIONS. Territory here is not a policy the test configures - it is the incoming key set,
// because reverse mapping keys every row by the node's claim. So this also pins the claim model itself: a row nothing
// claims must be COUNTED and never entered, which is what stops a graph describing two rows from proposing anything
// about the rest of a studio's table.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RowPlanClaimsOnlyWhatItKeys,
	"SimpleQuest.Resolver.RowPlanClaimsOnlyWhatItKeys", TestFlags)
bool FQuestResolver_RowPlanClaimsOnlyWhatItKeys::RunTest(const FString& Parameters)
{
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_RowPlanProbe"));
	Table->RowStruct = FQuestResolverTestRow::StaticStruct();
	Table->AddToRoot();

	FQuestResolverTestRow Boss;
	Boss.type  = TEXT("step");
	Boss.label = TEXT("Slay the boss");
	Table->AddRow(TEXT("kill_boss"), Boss);

	FQuestResolverTestRow Patrol;
	Patrol.type  = TEXT("step");
	Patrol.label = TEXT("Walk the wall");
	Table->AddRow(TEXT("patrol"), Patrol);

	// Belongs to the studio. No node claims it, so the plan must not mention it beyond the count.
	FQuestResolverTestRow Theirs;
	Theirs.type  = TEXT("step");
	Theirs.label = TEXT("Not ours");
	Table->AddRow(TEXT("studio_only"), Theirs);

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("kill_boss"),  { { TEXT("type"), TEXT("step") }, { TEXT("label"), TEXT("Kill the boss") } });
	AddRow(Content, TEXT("patrol"),     { { TEXT("type"), TEXT("step") }, { TEXT("label"), TEXT("Walk the wall") } });
	AddRow(Content, TEXT("talk_smith"), { { TEXT("type"), TEXT("step") }, { TEXT("label"), TEXT("Talk to the smith") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	UQuestImportMapping* Mapping = MakeMapping();   // discriminator "type"; no bindings, so nothing can be unwritable

	FQuestInPlacePlan Plan;
	PlanQuestRowsIntoTable(Bundle, *Table, *Mapping, Plan);

	TestEqual(TEXT("The plan states which way it points"), (int32)Plan.Direction, (int32)EQuestPlanDirection::IntoTable);
	TestTrue(TEXT("Nothing is refused"), Plan.Refusals.IsEmpty());
	TestEqual(TEXT("One entry per claimed row, and only those"), Plan.Entries.Num(), 3);

	// A row in the table that no node claims: counted, never entered. This is the whole territory rule in one assertion.
	TestEqual(TEXT("An unclaimed row is counted"), Plan.UnclaimedRowCount, 1);
	TestFalse(TEXT("...and never entered"),
		Plan.Entries.ContainsByPredicate([](const FQuestNodePlanEntry& E){ return E.Key == TEXT("studio_only"); }));

	const FQuestNodePlanEntry* Boss2   = Plan.Entries.FindByPredicate([](const FQuestNodePlanEntry& E){ return E.Key == TEXT("kill_boss"); });
	const FQuestNodePlanEntry* Patrol2 = Plan.Entries.FindByPredicate([](const FQuestNodePlanEntry& E){ return E.Key == TEXT("patrol"); });
	const FQuestNodePlanEntry* Smith   = Plan.Entries.FindByPredicate([](const FQuestNodePlanEntry& E){ return E.Key == TEXT("talk_smith"); });

	if (TestNotNull(TEXT("A claimed row the table already has is an Update"), Boss2))
	{
		TestEqual(TEXT("...Update"), (int32)Boss2->Action, (int32)EQuestNodePlanAction::Update);
		TestEqual(TEXT("...with exactly the differing cell"), Boss2->Changes.Num(), 1);
		if (Boss2->Changes.Num() == 1)
		{
			TestEqual(TEXT("...naming the column"), Boss2->Changes[0].Property, FString(TEXT("label")));
			TestEqual(TEXT("...showing what the table holds"), Boss2->Changes[0].CurrentText, FString(TEXT("Slay the boss")));
		}
	}
	// A cell the graph and the table agree on is not a change. Without this, every apply would rewrite every row and
	// "already matches" could never be said honestly.
	if (TestNotNull(TEXT("A row that already agrees still plans"), Patrol2))
	{
		TestEqual(TEXT("...as an Update"), (int32)Patrol2->Action, (int32)EQuestNodePlanAction::Update);
		TestEqual(TEXT("...carrying no changes"), Patrol2->Changes.Num(), 0);
	}
	if (TestNotNull(TEXT("A claimed row the table lacks is a Create"), Smith))
	{
		TestEqual(TEXT("...Create"), (int32)Smith->Action, (int32)EQuestNodePlanAction::Create);
	}

	// Columns the bundle never mentions are untouched: amount and outcome are absent from every row above, and the
	// declare-versus-silence contract says a source that omits a property is not asserting anything about it.
	TestTrue(TEXT("An omitted column produces no change anywhere"),
		!Plan.Entries.ContainsByPredicate([](const FQuestNodePlanEntry& E)
		{
			return E.Changes.ContainsByPredicate([](const FQuestPropertyChange& C)
			{
				return C.Property == TEXT("amount") || C.Property == TEXT("outcome");
			});
		}));

	Table->RemoveFromRoot();
	return true;
}

// THE REFUSALS, because a planner that has stopped refusing looks exactly like one with nothing to refuse. Each case
// here is a way an outbound write could quietly lose data in an asset we do not own.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RowPlanRefusesUnwritableAndAmbiguous,
	"SimpleQuest.Resolver.RowPlanRefusesUnwritableAndAmbiguous", TestFlags)
bool FQuestResolver_RowPlanRefusesUnwritableAndAmbiguous::RunTest(const FString& Parameters)
{
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_RowPlanRefuseProbe"));
	Table->RowStruct = FQuestResolverTestRow::StaticStruct();
	Table->AddToRoot();

	FQuestResolverTestRow Boss;
	Boss.type = TEXT("step");
	Table->AddRow(TEXT("kill_boss"), Boss);

	// A column the RECIPE binds that the destination has no field for. Inbound this is a warning - the graph ignores it.
	// Outbound it is data loss into someone else's table, so it must refuse.
	UQuestImportMapping* Mapping = MakeMapping();
	AddBinding(*Mapping, TEXT("reward_gold"), TEXT("Amount"));

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("kill_boss"), { { TEXT("type"), TEXT("step") }, { TEXT("reward_gold"), TEXT("50") } });
	// Two rows answering to one key: neither is planned, and neither claimant is picked by iteration order.
	AddRow(Content, TEXT("twin"), { { TEXT("type"), TEXT("step") } });
	AddRow(Content, TEXT("twin"), { { TEXT("type"), TEXT("step") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	FQuestInPlacePlan Plan;
	PlanQuestRowsIntoTable(Bundle, *Table, *Mapping, Plan);

	TestTrue(TEXT("A bound column with no destination field is refused"),
		Plan.Refusals.ContainsByPredicate([](const FString& R){ return R.Contains(TEXT("reward_gold")); }));
	TestTrue(TEXT("A duplicated key is reported ambiguous"), Plan.AmbiguousKeys.Contains(TEXT("twin")));
	TestFalse(TEXT("...and neither claimant is planned"),
		Plan.Entries.ContainsByPredicate([](const FQuestNodePlanEntry& E){ return E.Key == TEXT("twin"); }));

	// A table with no row struct has no layout to write into at all - refused before any row is examined.
	UDataTable* NoStruct = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_RowPlanNoStruct"));
	NoStruct->AddToRoot();
	FQuestInPlacePlan StructlessPlan;
	PlanQuestRowsIntoTable(Bundle, *NoStruct, *Mapping, StructlessPlan);
	TestFalse(TEXT("A table with no row struct is refused"), StructlessPlan.Refusals.IsEmpty());
	TestEqual(TEXT("...before planning anything"), StructlessPlan.Entries.Num(), 0);

	// And the bundle-shape guard: no flat content table means reverse mapping never ran.
	FQuestDataBundle Unmapped;
	FQuestInPlacePlan UnmappedPlan;
	PlanQuestRowsIntoTable(Unmapped, *Table, *Mapping, UnmappedPlan);
	TestFalse(TEXT("A bundle with no content table is refused"), UnmappedPlan.Refusals.IsEmpty());

	NoStruct->RemoveFromRoot();
	Table->RemoveFromRoot();
	return true;
}

// THE DISPATCH - which 67 green tests say nothing about, because nothing reached it. "Destination ownership picks the
// verb" is the rule this entire direction rests on, and an operation that quietly took the FOLDER path with a table
// endpoint would be indistinguishable from one that worked, right up until it wrote a marker into someone's content dir.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ExportPlansRatherThanWritesIntoATable,
	"SimpleQuest.Resolver.ExportPlansRatherThanWritesIntoATable", TestFlags)
bool FQuestResolver_ExportPlansRatherThanWritesIntoATable::RunTest(const FString& Parameters)
{
	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();

	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_ExportDispatchProbe"));
	Table->RowStruct = FQuestResolverTestRow::StaticStruct();
	Table->AddToRoot();

	FQuestResolverTestRow Theirs;
	Theirs.type  = TEXT("step");
	Theirs.label = TEXT("Not ours");
	Table->AddRow(TEXT("studio_only"), Theirs);

	FQuestExportRequest Request;
	Request.Graph          = Graph;
	Request.Endpoint.Kind  = EQuestEndpointKind::DataTable;
	Request.Endpoint.Table = TSoftObjectPtr<UDataTable>(Table);

	// WITHOUT a recipe. A canonical export writes OUR vocabulary, which is not a destination column set - refused up
	// front rather than discovered one unmatched column at a time.
	{
		FQuestExportOutcome Out;
		TestFalse(TEXT("A table destination with no Mapping is refused"), QuestExport_Run(Request, Out));
		TestFalse(TEXT("...nothing exported"), Out.bExported);
		TestFalse(TEXT("...and nothing planned"), Out.bPlanned);
		TestTrue(TEXT("...with a reason naming the Mapping"), Out.Error.Contains(TEXT("Mapping")));
	}

	// WITH one. The same call now PLANS, and must not fall through to folders, ownership markers and staged replacement.
	{
		Request.Mapping = MakeMapping();

		FQuestExportOutcome Out;
		TestTrue(TEXT("A table destination with a Mapping succeeds"), QuestExport_Run(Request, Out));
		TestTrue(TEXT("...error-free"), Out.Error.IsEmpty());
		TestTrue(TEXT("...having PLANNED"), Out.bPlanned);
		TestFalse(TEXT("...and explicitly NOT exported"), Out.bExported);
		TestEqual(TEXT("The plan states its direction"), (int32)Out.RowPlan.Direction, (int32)EQuestPlanDirection::IntoTable);

		// Filed under the QUESTLINE. The broker holds one record per questline and the panel finds by the asset it is
		// open on, so a plan keyed to the table is a plan nothing ever looks for.
		TestEqual(TEXT("The plan is filed under the questline"), Out.RowPlan.TargetAssetPath, Graph->GetPathName());

		// And still says which table it would write into. The two paths are easy to conflate precisely because one of them
		// is absent by default, so assert them as a PAIR - a destination silently equal to the questline passes neither.
		TestEqual(TEXT("...while naming the table it writes into"), Out.RowPlan.DestinationAssetPath, Table->GetPathName());

		// The studio's row is theirs. Counted, never entered - the territory rule, reached through the real operation
		// rather than by calling the planner directly.
		TestTrue(TEXT("An unclaimed row is counted"), Out.RowPlan.UnclaimedRowCount >= 1);
		TestFalse(TEXT("...and never entered"), Out.RowPlan.Entries.ContainsByPredicate(
			[](const FQuestNodePlanEntry& E){ return E.Key == TEXT("studio_only"); }));

		// Proof the folder arm never ran: OutDir is assigned below the dispatch, so it can only be set if we fell through.
		TestTrue(TEXT("No export folder was resolved"), Out.OutDir.IsEmpty());
		TestEqual(TEXT("No files were written"), Out.FilesWritten, 0);
	}

	Table->RemoveFromRoot();
	return true;
}

// APPLY WRITES WHAT THE PLAN NAMES AND NOTHING ELSE. The declare-versus-silence contract reaching into an asset we do
// not own: a field the source never mentioned must survive untouched, and a created row must hold the STRUCT's defaults
// rather than whatever the allocation contained.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RowApplyWritesOnlyWhatThePlanNames,
	"SimpleQuest.Resolver.RowApplyWritesOnlyWhatThePlanNames", TestFlags)
bool FQuestResolver_RowApplyWritesOnlyWhatThePlanNames::RunTest(const FString& Parameters)
{
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_RowApplyProbe"), RF_Transactional);
	Table->RowStruct = FQuestResolverTestRow::StaticStruct();
	Table->AddToRoot();

	FQuestResolverTestRow Boss;
	Boss.type   = TEXT("step");
	Boss.label  = TEXT("Slay the boss");
	Boss.amount = 7;                       // never mentioned by the source below - must survive
	Table->AddRow(TEXT("kill_boss"), Boss);

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("kill_boss"),  { { TEXT("type"), TEXT("step") }, { TEXT("label"), TEXT("Kill the boss") } });
	AddRow(Content, TEXT("talk_smith"), { { TEXT("type"), TEXT("step") }, { TEXT("label"), TEXT("Talk to the smith") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	UQuestImportMapping* Mapping = MakeMapping();

	FQuestInPlacePlan Plan;
	PlanQuestRowsIntoTable(Bundle, *Table, *Mapping, Plan);

	TMap<FString, const FQuestDataRow*> RowsByKey;
	for (const FQuestDataRow& R : Bundle.TablesByType[TEXT("content")].Rows) { RowsByKey.Add(R.Key, &R); }

	FQuestApplyResult Result;
	ApplyQuestRowPlan(*Table, Plan, RowsByKey, Result);

	TestFalse(TEXT("A clean plan is not refused"), Result.bRefused);
	TestEqual(TEXT("One row was created"), Result.EntitiesCreated, 1);
	TestTrue(TEXT("Something was written"), Result.ChangedAnything());

	FQuestResolverTestRow* Updated = Table->FindRow<FQuestResolverTestRow>(TEXT("kill_boss"), TEXT("test"), false);
	if (TestNotNull(TEXT("The updated row is still there"), Updated))
	{
		TestEqual(TEXT("The named cell was written"), Updated->label, FString(TEXT("Kill the boss")));
		// The whole point. A source that says nothing about a field is not asserting that it should be cleared.
		TestEqual(TEXT("A field the source never mentioned is untouched"), Updated->amount, 7);
	}

	FQuestResolverTestRow* Created = Table->FindRow<FQuestResolverTestRow>(TEXT("talk_smith"), TEXT("test"), false);
	if (TestNotNull(TEXT("The created row exists"), Created))
	{
		TestEqual(TEXT("...carrying what the source gave it"), Created->label, FString(TEXT("Talk to the smith")));
		// Seeded from FStructOnScope, so an unmentioned field holds the struct default rather than allocation garbage.
		TestEqual(TEXT("...and struct defaults for everything else"), Created->amount, 0);
	}

	Table->RemoveFromRoot();
	return true;
}


// UNDO RESTORES THE TABLE. This exists for exactly one line of the applier: Modify() BEFORE the write rather than after.
// The transaction buffer snapshots at the moment Modify is called, so the wrong order records the already-changed state
// and undo silently restores nothing - which is the live defect in ApplyTagRenamesToLoadedAssets. No other assertion in
// this file would notice a regression putting it back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RowApplyUndoRestoresTheTable,
	"SimpleQuest.Resolver.RowApplyUndoRestoresTheTable", TestFlags)
bool FQuestResolver_RowApplyUndoRestoresTheTable::RunTest(const FString& Parameters)
{
	if (!GEditor) { AddError(TEXT("No GEditor - this test cannot verify undo, rather than passing vacuously.")); return false; }

	// RF_Transactional is load-bearing: without it Modify() records nothing and every assertion below would pass for
	// the wrong reason. MakeTransientQuestlineGraph flags its EdGraph the same way.
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_RowApplyUndoProbe"), RF_Transactional);
	Table->RowStruct = FQuestResolverTestRow::StaticStruct();
	Table->AddToRoot();

	FQuestResolverTestRow Boss;
	Boss.type  = TEXT("step");
	Boss.label = TEXT("Slay the boss");
	Table->AddRow(TEXT("kill_boss"), Boss);

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("kill_boss"), { { TEXT("type"), TEXT("step") }, { TEXT("label"), TEXT("Kill the boss") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	UQuestImportMapping* Mapping = MakeMapping();
	FQuestInPlacePlan Plan;
	PlanQuestRowsIntoTable(Bundle, *Table, *Mapping, Plan);

	TMap<FString, const FQuestDataRow*> RowsByKey;
	for (const FQuestDataRow& R : Bundle.TablesByType[TEXT("content")].Rows) { RowsByKey.Add(R.Key, &R); }

	{
		// The CALLER owns the transaction - the applier deliberately opens none, so this mirrors what the toolkit does.
		FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestTests", "RowApplyUndo", "Write rows"));
		FQuestApplyResult Result;
		ApplyQuestRowPlan(*Table, Plan, RowsByKey, Result);
		TestTrue(TEXT("The apply wrote something"), Result.ChangedAnything());
	}

	// Asserted BEFORE the undo so the test cannot pass by nothing having happened in the first place.
	FQuestResolverTestRow* AfterApply = Table->FindRow<FQuestResolverTestRow>(TEXT("kill_boss"), TEXT("test"), false);
	if (!TestNotNull(TEXT("The row survives the apply"), AfterApply)) { Table->RemoveFromRoot(); return false; }
	TestEqual(TEXT("The apply changed the value"), AfterApply->label, FString(TEXT("Kill the boss")));

	TestTrue(TEXT("The transaction can be undone"), GEditor->UndoTransaction());

	// FindRow again rather than reusing the pointer: undo restores the row map wholesale, so the old allocation is gone.
	FQuestResolverTestRow* AfterUndo = Table->FindRow<FQuestResolverTestRow>(TEXT("kill_boss"), TEXT("test"), false);
	if (TestNotNull(TEXT("The row is back after undo"), AfterUndo))
	{
		TestEqual(TEXT("Undo restored the original value"), AfterUndo->label, FString(TEXT("Slay the boss")));
	}

	Table->RemoveFromRoot();
	return true;
}


// A PLAN CARRYING REFUSALS WRITES NOTHING AT ALL. Half-applying a description already known to be incomplete is the one
// outcome that leaves a studio's table in a state no one planned.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RowApplyRefusesWholesale,
	"SimpleQuest.Resolver.RowApplyRefusesWholesale", TestFlags)
bool FQuestResolver_RowApplyRefusesWholesale::RunTest(const FString& Parameters)
{
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_RowApplyRefuseProbe"), RF_Transactional);
	Table->RowStruct = FQuestResolverTestRow::StaticStruct();
	Table->AddToRoot();

	FQuestResolverTestRow Boss;
	Boss.type  = TEXT("step");
	Boss.label = TEXT("Slay the boss");
	Table->AddRow(TEXT("kill_boss"), Boss);

	// A bound column the destination has no field for - the planner refuses, so the apply must decline everything,
	// including the perfectly writable 'label' change sitting in the same plan.
	UQuestImportMapping* Mapping = MakeMapping();
	AddBinding(*Mapping, TEXT("reward_gold"), TEXT("Amount"));

	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	AddRow(Content, TEXT("kill_boss"), { { TEXT("type"), TEXT("step") }, { TEXT("label"), TEXT("Kill the boss") },
										 { TEXT("reward_gold"), TEXT("50") } });
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	FQuestInPlacePlan Plan;
	PlanQuestRowsIntoTable(Bundle, *Table, *Mapping, Plan);
	TestFalse(TEXT("The planner did refuse"), Plan.Refusals.IsEmpty());

	TMap<FString, const FQuestDataRow*> RowsByKey;
	for (const FQuestDataRow& R : Bundle.TablesByType[TEXT("content")].Rows) { RowsByKey.Add(R.Key, &R); }

	FQuestApplyResult Result;
	ApplyQuestRowPlan(*Table, Plan, RowsByKey, Result);

	TestTrue(TEXT("The apply refuses"), Result.bRefused);
	TestFalse(TEXT("...having written nothing"), Result.ChangedAnything());

	FQuestResolverTestRow* Untouched = Table->FindRow<FQuestResolverTestRow>(TEXT("kill_boss"), TEXT("test"), false);
	if (TestNotNull(TEXT("The row is still there"), Untouched))
	{
		TestEqual(TEXT("...exactly as it was"), Untouched->label, FString(TEXT("Slay the boss")));
	}

	Table->RemoveFromRoot();
	return true;
}

// THE AUTHORED-NAME PATH, which every other test in this file is structurally incapable of reaching. FQuestResolverTestRow
// is NATIVE, and for a C++ struct GetAuthoredNameForField and GetFName are identical - so a resolver that looked columns
// up by the wrong one passed the entire suite and was caught only by a hand-run against the single UserDefinedStruct
// fixture in the repo. That fixture is untracked and cannot be regenerated. This is its replacement.
//
// A Blueprint-authored struct mangles member names with a GUID suffix ("label_2_A1B2..."), while every bundle column
// carries the AUTHORED name. The failure mode is silent: a miss reads as "this column matches no property", which is a
// legitimate thing to say about a column that genuinely has none - so the differ compared NOTHING and reported no
// changes, which looks exactly like agreement.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ColumnsResolveByAuthoredNameOnBlueprintStructs,
	"SimpleQuest.Resolver.ColumnsResolveByAuthoredNameOnBlueprintStructs", TestFlags)
bool FQuestResolver_ColumnsResolveByAuthoredNameOnBlueprintStructs::RunTest(const FString& Parameters)
{
	UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(
		GetTransientPackage(), TEXT("S_ResolverAuthoredNameProbe"), RF_Transient);
	if (!TestNotNull(TEXT("A Blueprint struct can be created"), Struct)) { return false; }
	Struct->AddToRoot();

	FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;
	TestTrue(TEXT("A string member can be added"), FStructureEditorUtils::AddVariable(Struct, StringType));

	// Found by TYPE rather than by name, so this depends on nothing about how UE names a fresh member.
	FStrProperty* StringProp = nullptr;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (FStrProperty* S = CastField<FStrProperty>(*It)) { StringProp = S; break; }
	}
	if (!TestNotNull(TEXT("The string member is reflected"), StringProp)) { Struct->RemoveFromRoot(); return false; }

	const FString Authored = Struct->GetAuthoredNameForField(StringProp);

	// THE FIXTURE'S OWN PRECONDITION, asserted rather than assumed. If UE ever stops mangling, every assertion below
	// would still pass while testing nothing at all - which is precisely the failure this test exists to prevent.
	if (!TestNotEqual(TEXT("A Blueprint struct's property name differs from its authored name"),
		StringProp->GetName(), Authored))
	{
		AddError(TEXT("This fixture no longer exhibits the divergence it was built to cover - the test is inert, not passing."));
		Struct->RemoveFromRoot();
		return false;
	}

	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_AuthoredNameProbe"), RF_Transactional);
	Table->RowStruct = Struct;
	Table->AddToRoot();

	{
		FStructOnScope Seed(Struct);
		StringProp->SetPropertyValue(StringProp->ContainerPtrToValuePtr<void>(Seed.GetStructMemory()), TEXT("before"));
		Table->AddRow(TEXT("r_one"), Seed.GetStructMemory(), Struct);
	}

	// The bundle names its column the way a SOURCE does - the authored name. Resolving that against the struct is the
	// whole behaviour under test.
	FQuestDataBundle Bundle;
	FQuestDataTable Content;
	FQuestDataRow Row;
	Row.Key = TEXT("r_one");
	Row.Cells.Add(Authored, FQuestDataValue::MakeString(TEXT("after")));
	Content.Columns.Add(Authored);
	Content.Rows.Add(MoveTemp(Row));
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));

	UQuestImportMapping* Mapping = MakeMapping();

	FQuestInPlacePlan Plan;
	PlanQuestRowsIntoTable(Bundle, *Table, *Mapping, Plan);

	TestTrue(TEXT("Nothing is refused"), Plan.Refusals.IsEmpty());
	// The bug's signature was a WARNING here plus zero changes. Both halves are asserted: an unmatched column would
	// warn, and comparing nothing would report no change.
	TestTrue(TEXT("No column is reported unmatched"), Plan.Warnings.IsEmpty());
	if (TestEqual(TEXT("The row is planned"), Plan.Entries.Num(), 1))
	{
		TestEqual(TEXT("...as an Update"), (int32)Plan.Entries[0].Action, (int32)EQuestNodePlanAction::Update);
		if (TestEqual(TEXT("...with the differing cell detected"), Plan.Entries[0].Changes.Num(), 1))
		{
			TestEqual(TEXT("...named by its AUTHORED name"), Plan.Entries[0].Changes[0].Property, Authored);
			TestEqual(TEXT("...showing what the table holds"), Plan.Entries[0].Changes[0].CurrentText, FString(TEXT("before")));
		}
	}

	// And the applier resolves the same way - it had the identical bug in both of its loops.
	TMap<FString, const FQuestDataRow*> RowsByKey;
	for (const FQuestDataRow& R : Bundle.TablesByType[TEXT("content")].Rows) { RowsByKey.Add(R.Key, &R); }

	FQuestApplyResult Result;
	ApplyQuestRowPlan(*Table, Plan, RowsByKey, Result);
	TestEqual(TEXT("The write lands"), Result.PropertiesWritten, 1);

	if (uint8* const* Written = Table->GetRowMap().Find(TEXT("r_one")))
	{
		TestEqual(TEXT("...into the mangled property behind the authored name"),
			StringProp->GetPropertyValue(StringProp->ContainerPtrToValuePtr<void>(*Written)), FString(TEXT("after")));
	}

	Table->RemoveFromRoot();
	Struct->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_EdgeQualifierResolvesToPin, "SimpleQuest.Resolver.Plan.EdgeQualifierResolvesToPin", TestFlags)
bool FQuestResolver_EdgeQualifierResolvesToPin::RunTest(const FString& Parameters)
{
	/**
	 * A qualifier is a SELECTOR, not a value. A source authors "activates()" meaning "this node's primary forward pin",
	 * while an edge read back off the graph carries that pin's actual name - so a clean re-import used to report every
	 * wire-bound edge as both a removal and an addition. The two must resolve to the same pin before being compared.
	 * The live edge is built from the pin the node ACTUALLY has rather than from a literal: hard-coding the name would
	 * make this test agree with a future rename instead of noticing it.
	 */
	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	UEdGraph* Root = Graph->QuestlineEdGraph;

	UQuestlineNode_Step* From = NewObject<UQuestlineNode_Step>(Root, NAME_None, RF_Transactional);
	From->QuestGuid = FGuid::NewGuid();
	Root->AddNode(From);
	From->PostPlacedNewNode();
	From->AllocateDefaultPins();

	UQuestlineNode_Step* To = NewObject<UQuestlineNode_Step>(Root, NAME_None, RF_Transactional);
	To->QuestGuid = FGuid::NewGuid();
	Root->AddNode(To);
	To->PostPlacedNewNode();
	To->AllocateDefaultPins();

	const UEdGraphPin* Forward = nullptr;
	for (const UEdGraphPin* Pin : From->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("QuestActivation")) { Forward = Pin; break; }
	}
	TestNotNull(TEXT("A content node has a forward activation pin"), Forward);
	if (!Forward) { return false; }

	const FString FromKey = From->QuestGuid.ToString(EGuidFormats::Digits);
	const FString ToKey   = To->QuestGuid.ToString(EGuidFormats::Digits);

	TMap<FString, FString> GuidByKey;
	GuidByKey.Add(FromKey, FromKey);
	GuidByKey.Add(ToKey, ToKey);

	TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
	NodeByGuid.Add(FromKey, From);
	NodeByGuid.Add(ToKey, To);

	const TArray<FQuestDataEdge> Live = { { FromKey, FString::Printf(TEXT("activates(%s)"), *Forward->PinName.ToString()), ToKey } };

	TArray<FQuestDataEdge> Added, Removed;
	const TArray<FQuestDataEdge> Unqualified = { { FromKey, TEXT("activates()"), ToKey } };
	CompareQuestEdges(Unqualified, Live, GuidByKey, NodeByGuid, Added, Removed);
	TestEqual(TEXT("An unqualified selector matches the pin it selects, not a literal empty name"), Added.Num(), 0);
	TestEqual(TEXT("...and reports no removal either"), Removed.Num(), 0);

	// THE DETECTOR HAS TO FAIL ON KNOWN-BAD or the clean result above proves only that the differ went quiet. A wire
	// that genuinely moved must still report, through the same resolution path that just made an unchanged one silent.
	Added.Reset(); Removed.Reset();
	const TArray<FQuestDataEdge> Rewired = { { FromKey, TEXT("activates()"), TEXT("somewhere_else") } };
	CompareQuestEdges(Rewired, Live, GuidByKey, NodeByGuid, Added, Removed);
	TestEqual(TEXT("A real rewire is still one addition"), Added.Num(), 1);
	TestEqual(TEXT("...and one removal"), Removed.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_EdgeQualifierMatchesOutcomeLabel, "SimpleQuest.Resolver.Plan.EdgeQualifierMatchesOutcomeLabel", TestFlags)
bool FQuestResolver_EdgeQualifierMatchesOutcomeLabel::RunTest(const FString& Parameters)
{
	/**
	 * An outcome pin's NAME is the full gameplay tag, but a studio authors - and the mapping panel offers - the
	 * namespace-stripped label. The differ has to see those as the same wire or a clean re-import reports every
	 * outcome edge as a removal and an addition.
	 * The pin is created directly rather than through a Step: outcome pins are discovered from an objective
	 * BLUEPRINT's K2 nodes, so a real Step needs a content asset, while what the resolver actually reads is just a
	 * QuestOutcome output pin whose FName is a tag. This builds that exact shape with no asset in the way.
	 */
	UQuestlineGraph* Graph = MakeTransientQuestlineGraph();
	UEdGraph* Root = Graph->QuestlineEdGraph;

	UQuestlineNode_Step* From = NewObject<UQuestlineNode_Step>(Root, NAME_None, RF_Transactional);
	From->QuestGuid = FGuid::NewGuid();
	Root->AddNode(From);
	From->PostPlacedNewNode();
	From->AllocateDefaultPins();

	UQuestlineNode_Step* To = NewObject<UQuestlineNode_Step>(Root, NAME_None, RF_Transactional);
	To->QuestGuid = FGuid::NewGuid();
	Root->AddNode(To);
	To->PostPlacedNewNode();
	To->AllocateDefaultPins();

	const FName OutcomeTag(TEXT("SimpleQuest.Outcome.Solved"));
	const UEdGraphPin* OutcomePin = From->CreatePin(EGPD_Output, TEXT("QuestOutcome"), OutcomeTag);
	TestNotNull(TEXT("The outcome pin was created"), OutcomePin);
	if (!OutcomePin) { return false; }

	// The authored form, derived the way the mapping panel derives it. The inequality below is not decoration: if the
	// label ever equalled the pin name, this test would silently be exercising the EXACT-NAME branch and proving
	// nothing about the case it exists for.
	const FString AuthoredLabel = FSimpleQuestEditorUtilities::GetOutcomeLabel(OutcomeTag).ToString();
	TestNotEqual(TEXT("The authored label really does differ from the pin name"), AuthoredLabel, OutcomeTag.ToString());

	const FString FromKey = From->QuestGuid.ToString(EGuidFormats::Digits);
	const FString ToKey   = To->QuestGuid.ToString(EGuidFormats::Digits);

	TMap<FString, FString> GuidByKey;
	GuidByKey.Add(FromKey, FromKey);
	GuidByKey.Add(ToKey, ToKey);

	TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
	NodeByGuid.Add(FromKey, From);
	NodeByGuid.Add(ToKey, To);

	const TArray<FQuestDataEdge> Live     = { { FromKey, FString::Printf(TEXT("outcome(%s)"), *OutcomeTag.ToString()), ToKey } };
	const TArray<FQuestDataEdge> Authored = { { FromKey, FString::Printf(TEXT("outcome(%s)"), *AuthoredLabel),        ToKey } };

	TArray<FQuestDataEdge> Added, Removed;
	CompareQuestEdges(Authored, Live, GuidByKey, NodeByGuid, Added, Removed);
	TestEqual(TEXT("A label-form outcome edge matches the full-tag pin it names"), Added.Num(), 0);
	TestEqual(TEXT("...and reports no removal either"), Removed.Num(), 0);

	// Known-bad: a label naming no pin on this node must NOT quietly match. Without this the test passes just as well
	// against a differ that resolved everything to nothing.
	Added.Reset(); Removed.Reset();
	const TArray<FQuestDataEdge> Wrong = { { FromKey, TEXT("outcome(Nonexistent)"), ToKey } };
	CompareQuestEdges(Wrong, Live, GuidByKey, NodeByGuid, Added, Removed);
	TestEqual(TEXT("An outcome naming no pin is still an addition"), Added.Num(), 1);
	TestEqual(TEXT("...and the real edge is still a removal"), Removed.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_MixedClassChildrenKeepIndexOrder, "SimpleQuest.Resolver.Reattach.MixedClassChildrenKeepIndexOrder", TestFlags)
bool FQuestResolver_MixedClassChildrenKeepIndexOrder::RunTest(const FString& Parameters)
{
	/**
	 * Every instanced-child fixture in this suite until now used exactly ONE element, so the ordering path was never
	 * exercised at all. Reward order is GRANT ORDER - the codebase's own note calls it the one compiled ordering that
	 * is semantics rather than noise - so a rebuild that returns the right children in the wrong sequence is a real
	 * defect that nothing here could see.
	 * The rows are declared OUT OF ORDER and spread across three per-class tables, which is exactly how they land on
	 * disk: a child row is filed under its own class's stem, so a three-element mixed array is three files. If the
	 * rebuild followed row-encounter order rather than the bracket index, this test fails.
	 */
	UQuestlineNode_Reward* Owner = NewObject<UQuestlineNode_Reward>(GetTransientPackage());

	FQuestDataBundle Bundle;
	{
		FQuestDataTable Loot;
		AddRow(Loot, TEXT("n_reward/Rewards[2]"), { { TEXT("class"), TEXT("LootTableReward") }, { TEXT("RollCount"), TEXT("7") } });
		Bundle.TablesByType.Add(TEXT("loot_table_reward"), MoveTemp(Loot));

		FQuestDataTable Xp;
		AddRow(Xp, TEXT("n_reward/Rewards[0]"), { { TEXT("class"), TEXT("XPReward") }, { TEXT("Amount"), TEXT("11") } });
		Bundle.TablesByType.Add(TEXT("xp_reward"), MoveTemp(Xp));

		FQuestDataTable Scaled;
		AddRow(Scaled, TEXT("n_reward/Rewards[1]"), { { TEXT("class"), TEXT("ScaledAmountReward") }, { TEXT("BaseAmount"), TEXT("22") } });
		Bundle.TablesByType.Add(TEXT("scaled_amount_reward"), MoveTemp(Scaled));
	}

	TSet<FString> Consumed;
	TArray<FString> Warnings;
	ReattachQuestInstancedChildren(Owner, TEXT("n_reward"), Bundle, Consumed, Warnings);

	TestEqual(TEXT("All three declared children were rebuilt"), Owner->Rewards.Num(), 3);
	if (Owner->Rewards.Num() != 3) { return false; }

	// Class AND value at each slot: class alone would pass if two same-class children swapped.
	const UXPReward* First = Cast<UXPReward>(Owner->Rewards[0].Get());
	const UScaledAmountReward* Second = Cast<UScaledAmountReward>(Owner->Rewards[1].Get());
	const ULootTableReward* Third = Cast<ULootTableReward>(Owner->Rewards[2].Get());

	TestNotNull(TEXT("Index 0 is the XP reward declared as [0]"), First);
	TestNotNull(TEXT("Index 1 is the scaled reward declared as [1]"), Second);
	TestNotNull(TEXT("Index 2 is the loot reward declared as [2]"), Third);
	if (First)  { TestEqual(TEXT("...carrying its own restored value"), ReadIntProperty(First,  TEXT("Amount")),     11); }
	if (Second) { TestEqual(TEXT("...carrying its own restored value"), ReadIntProperty(Second, TEXT("BaseAmount")), 22); }
	if (Third)  { TestEqual(TEXT("...carrying its own restored value"), ReadIntProperty(Third,  TEXT("RollCount")),   7); }

	TestEqual(TEXT("Every child row was consumed"), Consumed.Num(), 3);
	TestEqual(TEXT("No warnings on a well-formed rebuild"), Warnings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ChildIndicesSortNumerically, "SimpleQuest.Resolver.Reattach.ChildIndicesSortNumerically", TestFlags)
bool FQuestResolver_ChildIndicesSortNumerically::RunTest(const FString& Parameters)
{
	/**
	 * TWELVE elements, because the bug only appears past ten. Child keys are text, and sorted as text "[10]" lands
	 * before "[1]" - ']' is 0x5D, '0' is 0x30 - so a lexicographic order puts the whole teens block in the middle of
	 * the single digits. The reattach parses the bracket to an int and sorts numerically; this is the test that can
	 * tell those two apart, and no existing fixture has more than one element to tell them apart WITH.
	 * Declared shuffled so the sort has to do work rather than accidentally agreeing with insertion order.
	 */
	UQuestlineNode_Reward* Owner = NewObject<UQuestlineNode_Reward>(GetTransientPackage());

	static constexpr int32 Count = 12;
	const int32 DeclareOrder[Count] = { 10, 3, 11, 0, 7, 1, 9, 4, 2, 8, 5, 6 };

	FQuestDataBundle Bundle;
	FQuestDataTable Xp;
	for (int32 i = 0; i < Count; ++i)
	{
		// Amount == the element's own index, so a wrong ORDER is visible as a wrong VALUE at that slot.
		const FString Key    = FString::Printf(TEXT("n_reward/Rewards[%d]"), DeclareOrder[i]);
		const FString Amount = FString::FromInt(DeclareOrder[i]);
		AddRow(Xp, *Key, { { TEXT("class"), TEXT("XPReward") }, { TEXT("Amount"), *Amount } });
	}
	Bundle.TablesByType.Add(TEXT("xp_reward"), MoveTemp(Xp));

	TSet<FString> Consumed;
	TArray<FString> Warnings;
	ReattachQuestInstancedChildren(Owner, TEXT("n_reward"), Bundle, Consumed, Warnings);

	TestEqual(TEXT("All twelve children were rebuilt"), Owner->Rewards.Num(), Count);
	if (Owner->Rewards.Num() != Count) { return false; }

	for (int32 i = 0; i < Count; ++i)
	{
		const UXPReward* At = Cast<UXPReward>(Owner->Rewards[i].Get());
		if (!At) { AddError(FString::Printf(TEXT("Slot %d is not an XPReward"), i)); continue; }
		TestEqual(*FString::Printf(TEXT("Slot %d holds the child declared as [%d]"), i, i), ReadIntProperty(At, TEXT("Amount")), i);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_RefusesDuplicateChildKey, "SimpleQuest.Resolver.Validate.RefusesDuplicateChildKey", TestFlags)
bool FQuestResolver_RefusesDuplicateChildKey::RunTest(const FString& Parameters)
{
	/**
	 * THE KNOWN-BAD FOR THE DUPLICATE-KEY GUARD. Child keys carry an array index minted from the live array length, so
	 * two branches each appending one reward both mint Rewards[1] - and because a child row is filed under its own
	 * CLASS, those two rows land in different files with no textual overlap for a merge to conflict on. The merge is
	 * clean and the corpus is wrong.
	 * Left unguarded the import does not fail: FindQuestChildRow returns the first key match while walking a TMap, so
	 * one reward is built TWICE and the other never, with the array length coincidentally correct.
	 */
	FQuestDataBundle Bundle;

	FQuestDataTable Self;
	AddRow(Self, TEXT("TestLine"), { { TEXT("class"), TEXT("QuestlineGraph") } });
	Bundle.TablesByType.Add(TEXT("questline_graph"), MoveTemp(Self));

	// The SAME key in two per-class tables - the exact shape two independent appends produce.
	FQuestDataTable Xp;
	AddRow(Xp, TEXT("n_reward/Rewards[1]"), { { TEXT("class"), TEXT("XPReward") } });
	Bundle.TablesByType.Add(TEXT("xp_reward"), MoveTemp(Xp));

	FQuestDataTable Currency;
	AddRow(Currency, TEXT("n_reward/Rewards[1]"), { { TEXT("class"), TEXT("ScaledAmountReward") } });
	Bundle.TablesByType.Add(TEXT("scaled_amount_reward"), MoveTemp(Currency));

	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	TSet<FString> AllRowKeys;
	FString Error;
	const bool bValid = QuestBundle_Validate(Bundle, NodeRowsByKey, AllRowKeys, Error);

	TestFalse(TEXT("Two rows claiming one key is refused"), bValid);
	TestTrue(TEXT("...and the refusal names the offending key"), Error.Contains(TEXT("n_reward/Rewards[1]")));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_ExportMarkerCarriesItsRecipe, "SimpleQuest.Resolver.Export.MarkerCarriesRecipe", TestFlags)
bool FQuestResolver_ExportMarkerCarriesItsRecipe::RunTest(const FString& Parameters)
{
	// The marker is a COMMITTED file that a reader arriving with nothing but the folder has to understand - a build server,
	// a batch validator, a teammate's checkout. It had no coverage at all until this, which is a poor state for a format
	// that lives in other people's repositories and is about to gain a field.
	const FString Dir = FPaths::ProjectIntermediateDir() / TEXT("QuestResolverTests") / TEXT("ExportMarker");
	IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);

	// ROUND TRIP - everything written comes back, the recipe included.
	FQuestExportMarker Written;
	Written.Format      = TEXT("TSV");
	Written.SourceAsset = TEXT("/Game/Quests/QL_Thing.QL_Thing");
	Written.Mapping     = TEXT("/Game/Quests/DA_Recipe.DA_Recipe");
	Written.Files       = { TEXT("content.tsv"), TEXT("edges.tsv") };
	TestTrue(TEXT("Marker written"), WriteQuestExportMarker(Dir, Written));

	FQuestExportMarker ReadBack;
	TestTrue(TEXT("Marker read"), ReadQuestExportMarker(Dir, ReadBack));
	TestEqual(TEXT("The format survives"),      ReadBack.Format,       Written.Format);
	TestEqual(TEXT("The source asset survives"), ReadBack.SourceAsset, Written.SourceAsset);
	TestEqual(TEXT("The Mapping survives"),       ReadBack.Mapping,     Written.Mapping);
	TestEqual(TEXT("The file list survives"),    ReadBack.Files.Num(), Written.Files.Num());
	TestTrue(TEXT("An export claims its folder"), ReadBack.bOwned);

	// A HAND-PLACED marker: provenance without ownership. This is the case the field exists for - a corpus we did not
	// write, describing itself so a reader can find it, while refusing to be overwritten.
	FQuestExportMarker Disclaimed = Written;
	Disclaimed.bOwned = false;
	TestTrue(TEXT("Disclaimed marker written"), WriteQuestExportMarker(Dir, Disclaimed));
	FQuestExportMarker DisclaimedBack;
	TestTrue(TEXT("Disclaimed marker read"), ReadQuestExportMarker(Dir, DisclaimedBack));
	TestFalse(TEXT("A disclaimed folder is not ours to replace"), DisclaimedBack.bOwned);
	TestEqual(TEXT("...while still naming its questline"), DisclaimedBack.SourceAsset, Written.SourceAsset);
	TestEqual(TEXT("...and its Mapping"),                   DisclaimedBack.Mapping,     Written.Mapping);

	// A CANONICAL export names no recipe, and blank has to come back blank rather than be guessed at.
	FQuestExportMarker Canonical;
	Canonical.Format      = TEXT("TSV");
	Canonical.SourceAsset = TEXT("/Game/Quests/QL_Thing.QL_Thing");
	TestTrue(TEXT("Canonical marker written"), WriteQuestExportMarker(Dir, Canonical));
	FQuestExportMarker CanonicalBack;
	TestTrue(TEXT("Canonical marker read"), ReadQuestExportMarker(Dir, CanonicalBack));
	TestTrue(TEXT("A canonical export names no Mapping"), CanonicalBack.Mapping.IsEmpty());

	// A MARKER WRITTEN BEFORE RECIPES WERE RECORDED still reads, and reads as canonical. That is the compatibility promise
	// the field was added under, so it is asserted rather than assumed - and it is the arm that would break somebody's
	// existing corpus rather than merely disappoint them.
	const FString Legacy =
		TEXT("# a comment line, which carries no equals sign\n")
		TEXT("Format=TSV\n")
		TEXT("SourceAsset=/Game/Quests/QL_Old.QL_Old\n")
		TEXT("File=content.tsv\n");
	TestTrue(TEXT("Legacy marker written"), FFileHelper::SaveStringToFile(Legacy, *(Dir / GQuestExportMarkerName),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	FQuestExportMarker LegacyBack;
	TestTrue(TEXT("Legacy marker read"), ReadQuestExportMarker(Dir, LegacyBack));
	TestEqual(TEXT("...with its source asset intact"), LegacyBack.SourceAsset, FString(TEXT("/Game/Quests/QL_Old.QL_Old")));
	TestEqual(TEXT("...and its file list intact"),     LegacyBack.Files.Num(), 1);
	TestTrue(TEXT("...and no Mapping, which reads as canonical"), LegacyBack.Mapping.IsEmpty());
	// The compatibility half of the ownership default: a marker predating the field was written by our export, so an
	// absent Owned line must read as OURS. Defaulting the other way would make every existing export folder refuse itself.
	TestTrue(TEXT("...and is still ours to replace"), LegacyBack.bOwned);

	// Left in place when something failed, so the file can be opened rather than reconstructed from assertions.
	if (!HasAnyErrors())
	{
		IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_PlanJsonIsDeterministic, "SimpleQuest.Resolver.Report.PlanJsonIsDeterministic", TestFlags)
bool FQuestResolver_PlanJsonIsDeterministic::RunTest(const FString& Parameters)
{
	// The artifact's entire value is that a build server can diff this commit's run against the last one, so a field that
	// moves between runs turns every diff into noise. RENDERING THE SAME PLAN TWICE WOULD NOT TEST THAT - one TMap iterated
	// twice inside one process usually yields the same order, so an unsorted map passes that version of the test happily.
	// This builds the same CONTENT twice with every collection filled in the OPPOSITE ORDER, which is what a different
	// discovery order, a different map history or a different machine actually hands the renderer.
	auto MakeEntry = [](const FString& Key, const FString& Label, bool bReversed)
	{
		FQuestNodePlanEntry Entry;
		Entry.Key              = Key;
		Entry.Action           = EQuestNodePlanAction::Update;
		Entry.Guid             = Key + TEXT("-guid");
		Entry.ClassName        = TEXT("QuestlineNode_Step");
		Entry.CurrentClassName = TEXT("QuestlineNode_Step");
		Entry.GraphCell        = TEXT("root");
		Entry.CurrentGraphCell = TEXT("root");
		Entry.Label            = Label;

		FQuestPropertyChange First;
		First.Property     = TEXT("Alpha");
		First.Kind         = EQuestPropertyChangeKind::Edit;
		First.CurrentText  = TEXT("1");
		First.IncomingText = TEXT("2");

		FQuestPropertyChange Second;
		Second.Property     = TEXT("Beta");
		Second.Kind         = EQuestPropertyChangeKind::ChildAdded;
		Second.IncomingText = TEXT("new");

		if (bReversed) { Entry.Changes = { Second, First }; }
		else           { Entry.Changes = { First, Second }; }
		return Entry;
	};

	auto MakeItems = [&MakeEntry](bool bReversed)
	{
		FQuestInPlacePlan Plan;
		Plan.Direction          = EQuestPlanDirection::IntoGraph;
		Plan.TargetAssetPath    = TEXT("/Game/Q/QL_Thing.QL_Thing");
		Plan.UntouchedNodeCount = 2;

		const FQuestNodePlanEntry Alpha = MakeEntry(TEXT("n_alpha"), TEXT("Alpha Node"), bReversed);
		const FQuestNodePlanEntry Beta  = MakeEntry(TEXT("n_beta"),  TEXT("Beta Node"),  bReversed);
		if (bReversed) { Plan.Entries = { Beta, Alpha }; } else { Plan.Entries = { Alpha, Beta }; }

		const FQuestDataEdge EdgeOne { TEXT("n_alpha"), TEXT("activates()"), TEXT("n_beta")  };
		const FQuestDataEdge EdgeTwo { TEXT("n_beta"),  TEXT("activates()"), TEXT("n_alpha") };
		if (bReversed) { Plan.AddedEdges = { EdgeTwo, EdgeOne }; Plan.RemovedEdges = { EdgeOne, EdgeTwo }; }
		else           { Plan.AddedEdges = { EdgeOne, EdgeTwo }; Plan.RemovedEdges = { EdgeTwo, EdgeOne }; }

		if (bReversed)
		{
			Plan.Refusals      = { TEXT("refusal two"), TEXT("refusal one") };
			Plan.Warnings      = { TEXT("warning two"), TEXT("warning one") };
			Plan.AmbiguousKeys = { TEXT("k_two"), TEXT("k_one") };
			Plan.LabelByKey.Add(TEXT("n_beta"),  TEXT("Beta Node"));
			Plan.LabelByKey.Add(TEXT("n_alpha"), TEXT("Alpha Node"));
		}
		else
		{
			Plan.Refusals      = { TEXT("refusal one"), TEXT("refusal two") };
			Plan.Warnings      = { TEXT("warning one"), TEXT("warning two") };
			Plan.AmbiguousKeys = { TEXT("k_one"), TEXT("k_two") };
			Plan.LabelByKey.Add(TEXT("n_alpha"), TEXT("Alpha Node"));
			Plan.LabelByKey.Add(TEXT("n_beta"),  TEXT("Beta Node"));
		}

		// A SECOND plan, so the run-level sort is exercised too. Discovery walks a directory, and directory enumeration
		// order is not something a committed artifact may inherit.
		FQuestInPlacePlan Other = Plan;
		Other.TargetAssetPath = TEXT("/Game/Q/QL_Other.QL_Other");

		FQuestPlanRunItem ItemA;
		ItemA.Questline = Plan.TargetAssetPath;
		ItemA.bPlanned  = true;
		ItemA.Plan = Plan;
		ItemA.Source.Folder   = TEXT("thing");
		ItemA.Source.Format   = TEXT("TSV");
		ItemA.Source.RowCount = 7;

		FQuestPlanRunItem ItemB;
		ItemB.Questline = Other.TargetAssetPath;
		ItemB.bPlanned  = true;
		ItemB.Plan = Other;
		ItemB.Source.Folder   = TEXT("other");
		ItemB.Source.Format   = TEXT("TSV");
		ItemB.Source.RowCount = 3;

		// A corpus whose asset does not exist yet: validated, never compared, and carrying NO plan. It goes in the fixture
		// because a status that is never rendered is a status whose ordering was never exercised - and because its empty
		// plan is exactly the shape that would read as "clean" if the status derivation ever lost its middle branch.
		FQuestPlanRunItem ItemC;
		ItemC.Questline = TEXT("/Game/Q/QL_Uncreated.QL_Uncreated");
		ItemC.bPlanned  = false;
		ItemC.Source.Folder   = TEXT("uncreated");
		ItemC.Source.Format   = TEXT("TSV");
		ItemC.Source.RowCount = 11;

		TArray<FQuestPlanRunItem> Items;
		if (bReversed) { Items = { ItemA, ItemC, ItemB }; } else { Items = { ItemB, ItemA, ItemC }; }
		return Items;
	};

	const FString Forward = BuildQuestPlanRunJson(MakeItems(/*bReversed*/ false), TEXT("Data/Quests"), TEXT("refusals"));
	const FString Reverse = BuildQuestPlanRunJson(MakeItems(/*bReversed*/ true),  TEXT("Data/Quests"), TEXT("refusals"));

	// Guard against the DEGENERATE PASS: two empty strings are equal, and so are two renders that dropped everything they
	// were given. An equality test with nothing on either side of it is the easiest green in the world to write by accident.
	TestTrue(TEXT("The render is non-trivial"),        Forward.Len() > 200);
	TestTrue(TEXT("...and states its schema version"), Forward.Contains(TEXT("\"schemaVersion\"")));
	TestTrue(TEXT("...and carries both entries"),      Forward.Contains(TEXT("n_alpha")) && Forward.Contains(TEXT("n_beta")));
	TestTrue(TEXT("...and quarantines its labels"),    Forward.Contains(TEXT("labelByKey")));
	TestTrue(TEXT("...and all three plans"),           Forward.Contains(TEXT("QL_Thing")) && Forward.Contains(TEXT("QL_Other"))
													   && Forward.Contains(TEXT("QL_Uncreated")));

	// The status the empty plan must NOT get. An uncreated corpus and a perfectly-matching asset both render zero entries
	// and zero counts, so this is the one assertion standing between them.
	TestTrue(TEXT("An uncreated corpus reports validated"), Forward.Contains(TEXT("\"status\": \"validated\"")));
	TestTrue(TEXT("...and its row count survives"),         Forward.Contains(TEXT("\"rowCount\": 11")));

	if (Forward != Reverse)
	{
		// Inequality between two multi-KB strings is unreadable, and the one thing worth knowing is WHICH field moved.
		int32 At = 0;
		while (At < Forward.Len() && At < Reverse.Len() && Forward[At] == Reverse[At]) { ++At; }
		const int32 From = FMath::Max(0, At - 80);
		AddInfo(FString::Printf(TEXT("first difference at offset %d"), At));
		AddInfo(FString::Printf(TEXT("  forward: %s"), *Forward.Mid(From, 200)));
		AddInfo(FString::Printf(TEXT("  reverse: %s"), *Reverse.Mid(From, 200)));
	}
	TestEqual(TEXT("Both renders are the same length"),       Forward.Len(), Reverse.Len());
	TestTrue(TEXT("Insertion order never reaches the file"),  Forward == Reverse);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestResolver_EnumeratedColumnsUseAuthoredNames,
	"SimpleQuest.Resolver.EnumeratedColumnsUseAuthoredNames", TestFlags)
bool FQuestResolver_EnumeratedColumnsUseAuthoredNames::RunTest(const FString& Parameters)
{
	// The PLANNER matching authored names is covered next door. This covers the other end of the same fact: what the
	// column ENUMERATOR offers a designer to pick from. The two must agree, because a picker that offers a name the
	// planner cannot resolve produces a recipe that refuses every row of the struct it was built against.
	UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(
		GetTransientPackage(), TEXT("S_ResolverEnumeratedNameProbe"), RF_Transient);
	if (!TestNotNull(TEXT("A Blueprint struct can be created"), Struct)) { return false; }
	Struct->AddToRoot();

	FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;
	TestTrue(TEXT("A string member can be added"), FStructureEditorUtils::AddVariable(Struct, StringType));

	FStrProperty* StringProp = nullptr;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (FStrProperty* S = CastField<FStrProperty>(*It)) { StringProp = S; break; }
	}
	if (!TestNotNull(TEXT("The string member is reflected"), StringProp)) { Struct->RemoveFromRoot(); return false; }

	const FString Authored = Struct->GetAuthoredNameForField(StringProp);
	const FString Internal = StringProp->GetName();

	// THE FIXTURE'S OWN PRECONDITION, asserted rather than assumed - the same guard the planner-side test carries. If UE
	// ever stops mangling, the assertions below would all pass while distinguishing nothing.
	if (!TestNotEqual(TEXT("A Blueprint struct's property name differs from its authored name"), Internal, Authored))
	{
		AddError(TEXT("This fixture no longer exhibits the divergence it was built to cover - the test is inert, not passing."));
		Struct->RemoveFromRoot();
		return false;
	}

	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TEXT("DT_EnumeratedNameProbe"), RF_Transactional);
	Table->RowStruct = Struct;
	Table->AddToRoot();

	const FQuestSourceColumns Columns = EnumerateDataTableColumns(TSoftObjectPtr<UDataTable>(Table));

	TestTrue(TEXT("The table reads"), Columns.bReadable);
	TestTrue(TEXT("The enumerator offers the AUTHORED name"), Columns.Columns.Contains(FName(*Authored)));
	// The half that would still pass on the old behaviour if only the line above were checked, since a picker could
	// legitimately offer both.
	TestFalse(TEXT("...and not the internal one"), Columns.Columns.Contains(FName(*Internal)));

	Table->RemoveFromRoot();
	Struct->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
