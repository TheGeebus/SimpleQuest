// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "ISimpleQuestEditorModule.h"
#include "Misc/AutomationTest.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestDataFormatIO.h"
#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/QuestExportOperations.h"
#include "Tests/QuestGraphFixture.h"

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestFormatIO_FingerprintDistinguishesContent, "SimpleQuest.Resolver.FingerprintDistinguishesContent", FormatIOTestFlags)
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
	return true;
}

/**
 * The public ExportQuestline and the shipped folder export must be the same export.
 *
 * They are two entry points onto one bundle: ExportQuestline serializes it in memory, QuestExport_Run serializes it and
 * then writes a folder around it. Nothing but discipline keeps the first from quietly diverging - a guard added to one
 * path, a mapping applied in one and not the other - and an adopter driving the module API would get a subtly different
 * export from the one the console produces, with nothing to point at.
 *
 * Compared against EACH OTHER rather than against a golden file, so it cannot go stale when a format changes its
 * layout: both sides move together, and only a difference BETWEEN the paths fails.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestFormatIO_PublicExportMatchesFolderExport, "SimpleQuest.Resolver.PublicExportMatchesFolderExport", FormatIOTestFlags)
bool FQuestFormatIO_PublicExportMatchesFolderExport::RunTest(const FString& Parameters)
{
	// Driven off the registry rather than a literal list, so a format added later is covered without touching this test.
	const TArray<FString> FormatNames = FQuestDataFormatRegistry::Get().GetRegisteredNames();
	if (!TestTrue(TEXT("at least one data format is registered"), FormatNames.Num() > 0))
	{
		// Without this the loop below runs zero times and the test passes having asserted nothing.
		return false;
	}

	for (const FString& FormatName : FormatNames)
	{
		const FString Tag = FString::Printf(TEXT("[%s]"), *FormatName);
		QuestTestFixtures::FCompileFixture Fixture(TEXT("/Temp/QuestExportEquivalence"));
		if (!TestTrue(*FString::Printf(TEXT("%s fixture built"), *Tag), Fixture.IsValid()))
		{
			continue;
		}

		// PATH A - the public module API. Nothing touches disk.
		TMap<FString, FString> ApiFiles;
		TArray<FString> ApiWarnings;
		FString ApiError;
		const bool bApiOk = ISimpleQuestEditorModule::Get().ExportQuestline(
			Fixture.Graph, FormatName, /*Mapping=*/nullptr, ApiFiles, ApiWarnings, ApiError);
		if (!TestTrue(*FString::Printf(TEXT("%s ExportQuestline succeeded (%s)"), *Tag, *ApiError), bApiOk))
		{
			continue;
		}

		// PATH B - the shipped folder export. Endpoint.Folder is left EMPTY so the destination derives, which is the
		// route the console and the round-trip harness take and the one the containment guard was written for.
		FQuestExportRequest Request;
		Request.Graph = Fixture.Graph;
		Request.Endpoint.FormatName = FormatName;

		FQuestExportOutcome Outcome;
		const bool bRunOk = QuestExport_Run(Request, Outcome);
		if (!TestTrue(*FString::Printf(TEXT("%s QuestExport_Run succeeded (%s)"), *Tag, *Outcome.Error), bRunOk))
		{
			continue;
		}

		// Read back through the same helper an adopter would use, and ask the provider for its extension rather than
		// assuming it derives from the name - FileExtension() is overridable.
		const TUniquePtr<ISimpleQuestDataFormat> Format = FQuestDataFormatRegistry::Get().Create(FormatName);
		TMap<FString, FString> FolderFiles;
		FString ReadError;
		const bool bReadOk = Format.IsValid()
			&& QuestDataFormatIO::ReadFilesFromFolder(Outcome.OutDir, Format->FileExtension(), FolderFiles, ReadError);

		if (TestTrue(*FString::Printf(TEXT("%s folder export read back (%s)"), *Tag, *ReadError), bReadOk))
		{
			// NOT a file count. An EMPTY bundle still serializes to a file in both shipped formats - JSON writes an
			// empty tablesByType, TSV writes its edges table - so "produced files" is true of an export carrying
			// nothing, and a guard that cannot fail is not a guard. The export has to be shown to contain the
			// FIXTURE'S OWN AUTHORED CONTENT before any comparison below means anything.
			FString AllFolderContent;
			for (const TPair<FString, FString>& File : FolderFiles) { AllFolderContent += File.Value; }
			TestTrue(*FString::Printf(TEXT("%s folder export carries the fixture's authored content"), *Tag), AllFolderContent.Contains(QuestTestFixtures::FixtureDisplayName));
			TestEqual(*FString::Printf(TEXT("%s same file count"), *Tag), ApiFiles.Num(), FolderFiles.Num());

			for (const TPair<FString, FString>& File : FolderFiles)
			{
				const FString* ApiContent = ApiFiles.Find(File.Key);
				if (TestNotNull(*FString::Printf(TEXT("%s API export contains '%s'"), *Tag, *File.Key), ApiContent))
				{
					TestEqual(*FString::Printf(TEXT("%s '%s' matches byte for byte"), *Tag, *File.Key),
						*ApiContent, File.Value);
				}
			}
		}

		// The derived destination is under the export root and this test put it there, so clearing it is ours to do.
		IFileManager::Get().DeleteDirectory(*Outcome.OutDir, false, true);
	}

	return true;
}

#endif

