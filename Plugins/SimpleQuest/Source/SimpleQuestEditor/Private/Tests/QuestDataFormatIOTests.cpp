// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Resolver/QuestDataFormatIO.h"

namespace
{
	constexpr EAutomationTestFlags FormatIOTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

/**
 * The fingerprint exists to answer one question: are these the same files the plan was built from?
 *
 * It is the only thing standing between a reviewed plan and an apply of data nobody looked at. A folder source can be
 * re-read and compared directly; in-memory data cannot, so the caller handing back the same map is the entire
 * guarantee - and this turns that from an assumption into something checkable.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestFormatIO_FingerprintDistinguishesContent,
	"SimpleQuest.Resolver.FingerprintDistinguishesContent", FormatIOTestFlags)
bool FQuestFormatIO_FingerprintDistinguishesContent::RunTest(const FString& Parameters)
{
	using namespace QuestDataFormatIO;

	TMap<FString, FString> Base;
	Base.Add(TEXT("content.tsv"), TEXT("key\tgraph\nn_a\troot\n"));
	Base.Add(TEXT("edges.tsv"),   TEXT("from\ttype\tto\nn_a\tflow\tn_b\n"));

	// Identical contents must agree, or every apply refuses and the feature is unusable.
	TMap<FString, FString> Same = Base;
	TestEqual(TEXT("identical maps fingerprint the same"), FingerprintFiles(Same), FingerprintFiles(Base));

	// ORDER-INDEPENDENT. The two maps below are built by inserting in opposite orders. A hash that folded in map
	// iteration order would refuse a caller who assembled the same data differently - a false refusal that would look
	// like data corruption and be impossible to reproduce.
	TMap<FString, FString> Reordered;
	Reordered.Add(TEXT("edges.tsv"),   TEXT("from\ttype\tto\nn_a\tflow\tn_b\n"));
	Reordered.Add(TEXT("content.tsv"), TEXT("key\tgraph\nn_a\troot\n"));
	TestEqual(TEXT("insertion order does not change the fingerprint"), FingerprintFiles(Reordered), FingerprintFiles(Base));

	// CONTENT SENSITIVITY - the case the guard exists for. One edited cell must not fingerprint the same.
	TMap<FString, FString> EditedCell = Base;
	EditedCell[TEXT("content.tsv")] = TEXT("key\tgraph\nn_a\tOTHER\n");
	TestNotEqual(TEXT("an edited cell changes the fingerprint"), FingerprintFiles(EditedCell), FingerprintFiles(Base));

	// NAME SENSITIVITY. Renaming a file changes what gets imported just as surely as editing one does - a table read
	// under a different stem lands as a different type - so a fingerprint over contents alone would miss it.
	// The new name must SORT TO THE SAME POSITION as the old one. Rename content.tsv to something that sorts after
	// edges.tsv and the content sequence flips too, so the fingerprint changes whether or not names are hashed - and
	// the assertion passes without ever testing names. "contentx.tsv" still sorts before "edges.tsv", so contents
	// arrive in the same order and the NAME is the only thing that differs.
	TMap<FString, FString> RenamedFile;
	RenamedFile.Add(TEXT("contentx.tsv"), Base[TEXT("content.tsv")]);
	RenamedFile.Add(TEXT("edges.tsv"),    Base[TEXT("edges.tsv")]);
	TestNotEqual(TEXT("a renamed file changes the fingerprint"), FingerprintFiles(RenamedFile), FingerprintFiles(Base));

	// A DROPPED FILE. Fewer files means fewer rows imported, which is a different plan.
	TMap<FString, FString> Dropped = Base;
	Dropped.Remove(TEXT("edges.tsv"));
	TestNotEqual(TEXT("dropping a file changes the fingerprint"), FingerprintFiles(Dropped), FingerprintFiles(Base));

	// An empty map is a legitimate input to fingerprint even though it is not a legitimate import - the guard must not
	// crash on it, and it must not collide with real content.
	const TMap<FString, FString> Empty;
	TestNotEqual(TEXT("an empty map does not collide with real content"), FingerprintFiles(Empty), FingerprintFiles(Base));

	return true;
}

#endif

