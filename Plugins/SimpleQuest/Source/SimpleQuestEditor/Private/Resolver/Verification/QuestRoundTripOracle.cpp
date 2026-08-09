// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/Verification/QuestRoundTripOracle.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Resolver/Verification/QuestVerificationPaths.h"
#include "SimpleQuestLog.h"


/** Load every *.tsv in a folder into a name->content map (content line-split, trailing empties culled). */
static TMap<FString, TArray<FString>> LoadFolderTsvs(const FString& Folder)
{
	TMap<FString, TArray<FString>> Out;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.tsv")), true, false);
	for (const FString& F : Files)
	{
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *(Folder / F)))
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
			Out.Add(F, Lines);
		}
	}
	return Out;
}

/**
 * Normalize the _RT round-trip marker out of a line. The import appends _RT to BOTH the sanitized identity (asset
 * name / tag namespace) and the raw authored QuestlineID field, so we strip the literal token "_RT" wherever it
 * directly follows an identifier character run. _RT is our reserved marker (never authored), so this is safe.
 */
static FString StripRTSuffix(const FString& Line, const FString& /*OriginalID*/)
{
	// Replace "_RT" occurrences. Broad but safe: _RT is reserved and appears only where the import added it.
	return Line.Replace(TEXT("_RT"), TEXT(""));
}

/**
 * Normalize the compiled tag namespace: the imported asset's tags read SimpleQuest.Questline.<ID>_RT..., the
 * source's read SimpleQuest.Questline.<ID>... — collapse the _RT form. Same single closed-form substitution as C,
 * applied to dump lines. (Also covers SimpleQuest.State.<ID>_RT... etc. because the _RT rides the ID segment.)
 */
FString StripRTFromDump(const FString& Line, const FString& OriginalID)
{
	return GQuestRoundTripSuffix;
}

/**
 * Collapse NSLOCTEXT("<namespace>", "<key>", "<source>") to just the source string. The namespace embeds the
 * PACKAGE GUID and the key is a per-string hash — both legitimately differ between an asset and its round-tripped
 * twin (different package => different loc identity), while the SOURCE string is the behavioral content. Closed-form,
 * like the _RT normalization: it erases a known-benign identity difference for the COMPARISON only; the export files
 * keep the full loc-preserving form on disk (the interlingua stays lossless). Handles multiple occurrences per line.
 */
static FString StripLocNamespaces(const FString& Line)
{
	FString Out = Line;
	int32 Start;
	while ((Start = Out.Find(TEXT("NSLOCTEXT("))) != INDEX_NONE)
	{
		// Find the matching close paren for this NSLOCTEXT( ... ).
		int32 Depth = 0, i = Start + 9;   // position at the '(' after NSLOCTEXT
		int32 Close = INDEX_NONE;
		for (; i < Out.Len(); ++i)
		{
			if (Out[i] == TEXT('(')) ++Depth;
			else if (Out[i] == TEXT(')')) { if (--Depth == 0) { Close = i; break; } }
		}
		if (Close == INDEX_NONE) break;   // malformed — leave as-is

		// Extract the third argument (the source string) between the last "\", \"" pair and the closing quote.
		const FString Inner = Out.Mid(Start, Close - Start + 1);   // full NSLOCTEXT(...)
		FString Source;
		// The source is the final "..."-quoted token before the ')'. Find the last '"' ... '"' pair.
		int32 LastQuote, PrevQuote;
		if (Inner.FindLastChar(TEXT('"'), LastQuote))
		{
			FString Head = Inner.Left(LastQuote);
			if (Head.FindLastChar(TEXT('"'), PrevQuote))
			{
				Source = Inner.Mid(PrevQuote + 1, LastQuote - PrevQuote - 1);
			}
		}
		Out = Out.Left(Start) + Source + Out.Mid(Close + 1);
	}
	return Out;
}

/**
 * Collapse the package/outer-object path inside a serialized object reference to just its class and leaf name.
 * Instanced reward objects serialize as "Class'/Package/Asset.Asset:OuterNode.LeafObject'"; between an asset and its
 * round-tripped twin, the package path (where the asset lives) and the OuterNode index (a fresh import numbers its
 * UObjects differently) legitimately differ, while the reward's CLASS and field content — the behavioral payload —
 * are identical. Rewrite each "...'<path>:<outer>.<leaf>'" so only the final ".<leaf>" survives inside the quotes,
 * erasing the varying address. Compare-only, like the other normalizations: a genuinely different reward class or
 * leaf still differs after collapse; the export files keep the full path on disk.
 */
static FString StripObjectRefPaths(const FString& Line)
{
	FString Out;
	Out.Reserve(Line.Len());
	int32 i = 0;
	while (i < Line.Len())
	{
		const TCHAR C = Line[i];
		// A serialized object ref opens with a single quote following a class name and closes at the next single
		// quote. Object paths never contain a "'" internally, so a simple quote-to-quote span is unambiguous.
		if (C == TEXT('\''))
		{
			const int32 Close = Line.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i + 1);
			if (Close == INDEX_NONE) { Out.AppendChar(C); ++i; continue; }
			const FString Path = Line.Mid(i + 1, Close - i - 1);   // between the quotes
			// Keep only the substring after the last '.', if any — that's the leaf object name (e.g. XPReward_0).
			int32 Dot;
			const FString Leaf = Path.FindLastChar(TEXT('.'), Dot) ? Path.Mid(Dot + 1) : Path;
			Out.AppendChar(TEXT('\'')); Out.Append(Leaf); Out.AppendChar(TEXT('\''));
			i = Close + 1;
		}
		else { Out.AppendChar(C); ++i; }
	}
	return Out;
}

/**
 * Canonicalize Util_<GUID>__<suffix> compiled keys to stable positional placeholders — ordered by each util node's
 * PRESERVED identity (its AuthoredNodeGuid, which the import round-trips unchanged), NOT first-seen order. First-seen
 * is unstable across a round-trip (the import spawns util nodes in a different order than authored), so "#N = Nth
 * seen" points at DIFFERENT physical nodes in the src vs rt dumps. Keying the index off a preserved GUID makes both
 * dumps assign the same placeholder to the same logical node.
 * NOTE: the compiled util KEY is "Util_" + 32 hex + a tag-prefix SUFFIX (e.g. "Util_<hex>__QuickStart.Chapter_6").
 * Identify it by the Util_+32hex PREFIX but map/replace on the FULL key string (incl. suffix), so both passes agree.
 */
static void CanonicalizeUtilKeys(TArray<FString>& Lines)
{
	// True if Line's first tab-delimited token is a util key (Util_ + 32 hex, then any suffix). Returns the full
	// key token (with suffix) via OutFullKey.
	auto ExtractUtilKey = [](const FString& Line, FString& OutFullKey) -> bool
	{
		if (!Line.StartsWith(TEXT("Util_"))) return false;
		int32 Tab;
		const FString Token = Line.FindChar(TEXT('\t'), Tab) ? Line.Left(Tab) : Line;
		// Need at least "Util_" + 32 hex; verify those 32 are hex.
		if (Token.Len() < 5 + 32) return false;
		for (int32 i = 0; i < 32; ++i) if (!FChar::IsHexDigit(Token[5 + i])) return false;
		OutFullKey = Token;
		return true;
	};

	// Pass 1: map each full util key -> its AuthoredNodeGuid (the round-trip-stable identity).
	TMap<FString, FString> AuthoredByKey;
	for (const FString& Line : Lines)
	{
		FString FullKey;
		if (!ExtractUtilKey(Line, FullKey)) continue;
		if (!Line.Contains(TEXT("\tAuthoredNodeGuid\t"))) continue;
		TArray<FString> Cols; Line.ParseIntoArray(Cols, TEXT("\t"), false);
		if (Cols.Num() >= 3) AuthoredByKey.Add(FullKey, Cols[2]);
	}

	// Assign canonical indices ordered by AuthoredNodeGuid (identical across dumps for the same logical node).
	TArray<TPair<FString, FString>> Ordered;   // (AuthoredNodeGuid, fullUtilKey)
	for (const TPair<FString, FString>& P : AuthoredByKey) Ordered.Add({ P.Value, P.Key });
	Ordered.Sort([](const TPair<FString,FString>& A, const TPair<FString,FString>& B){ return A.Key < B.Key; });

	TMap<FString, FString> TokenByKey;
	for (int32 i = 0; i < Ordered.Num(); ++i)
		TokenByKey.Add(Ordered[i].Value, FString::Printf(TEXT("Util_#%d"), i));

	// Pass 2: replace every occurrence of each full util key (as a KEY at line start, or as a REFERENCE inside a
	// value) with its token. Replace longest keys first so no key is a prefix of another mid-replacement (they
	// can't be — distinct GUIDs — but ordering by length is a cheap safety net).
	TArray<FString> KeysByLen;
	TokenByKey.GetKeys(KeysByLen);
	KeysByLen.Sort([](const FString& A, const FString& B){ return A.Len() > B.Len(); });
	for (FString& Line : Lines)
		for (const FString& Key : KeysByLen)
			Line.ReplaceInline(*Key, *TokenByKey[Key], ESearchCase::CaseSensitive);
}

/**
 * Reorder the compiled dump's util-node BLOCKS into a stable order for the COMPARE. DumpCompiled emits per-node
 * blocks; graph-level nodes are keyed by contextual tag (identical across dumps) and stay put, but util-node blocks
 * are keyed by the editor NodeGuid, so a fresh import emits them in a different file order than the original. That
 * order isn't semantic (the runtime keys nodes by tag/GUID, not file position), so sorting the blocks by each
 * node's PRESERVED AuthoredNodeGuid aligns the two dumps line-for-line without hiding any real difference — a node
 * that genuinely differs still diffs once both are in the same order. A "block" is a maximal run of consecutive
 * lines sharing the same first (tab-delimited) column; util blocks are the trailing run whose column starts "Util_".
 */
static void SortUtilBlocks(TArray<FString>& Lines)
{
	auto Col0 = [](const FString& Line) -> FString
	{ int32 Tab; return Line.FindChar(TEXT('\t'), Tab) ? Line.Left(Tab) : Line; };

	// Split into blocks by col0-change; remember each block's lines and (if a util block) its AuthoredNodeGuid.
	struct FBlock { FString Key; bool bUtil = false; FString SortKey; TArray<FString> BlockLines; };
	TArray<FBlock> Blocks;
	for (const FString& Line : Lines)
	{
		const FString Key = Col0(Line);
		if (Blocks.Num() == 0 || Blocks.Last().Key != Key)
		{
			FBlock B; B.Key = Key; B.bUtil = Key.StartsWith(TEXT("Util_"));
			Blocks.Add(MoveTemp(B));
		}
		FBlock& Cur = Blocks.Last();
		Cur.BlockLines.Add(Line);
		if (Cur.bUtil && Line.Contains(TEXT("\tAuthoredNodeGuid\t")))
		{
			TArray<FString> Cols; Line.ParseIntoArray(Cols, TEXT("\t"), false);
			if (Cols.Num() >= 3) Cur.SortKey = Cols[2];
		}
	}

	// Stable-partition: keep every non-util block exactly where it is; gather the util blocks and re-emit them
	// (in their original span) ordered by AuthoredNodeGuid. Util blocks form a contiguous trailing run, but we do
	// not rely on that — we rebuild the line list block-by-block, substituting the sorted util sequence in order.
	TArray<const FBlock*> UtilOrder;
	for (const FBlock& B : Blocks) if (B.bUtil) UtilOrder.Add(&B);
	UtilOrder.Sort([](const FBlock& A, const FBlock& B){ return A.SortKey < B.SortKey; });

	TArray<FString> Out; Out.Reserve(Lines.Num());
	int32 NextUtil = 0;
	for (const FBlock& B : Blocks)
	{
		const FBlock& Emit = B.bUtil ? *UtilOrder[NextUtil++] : B;
		Out.Append(Emit.BlockLines);
	}
	Lines = MoveTemp(Out);
}

/**
 * Sort all (TagName="...") tokens within a line into canonical order. Some compiled fields render nested tag
 * lists whose order is collection order, not semantics — most are already sorted by the dump, but nested-in-a-map
 * arrays (ReachableStepsByActivatePin's StepTags) aren't reached by the dump's map sort. Sorting the tokens line-
 * wise normalizes those for the COMPARE only; it's order-only so it can't hide a real difference (a differing tag
 * SET produces a differing sorted form), and it's a harmless no-op on lines the dump already sorted.
 */
static FString SortTagNameTokens(const FString& Line)
{
	// Find all "(TagName=\"...\")" occurrences, in order.
	TArray<FString> Tokens;
	int32 Search = 0;
	for (;;)
	{
		const int32 Start = Line.Find(TEXT("(TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Search);
		if (Start == INDEX_NONE) break;
		// A token ends at the ')' that closes this (TagName="..."). The tag string has no ')' inside it.
		const int32 End = Line.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (End == INDEX_NONE) break;
		Tokens.Add(Line.Mid(Start, End - Start + 1));
		Search = End + 1;
	}
	if (Tokens.Num() < 2) return Line;   // nothing to reorder

	// Reorder ONLY when the tokens form a pure comma-separated run — i.e. between each adjacent pair there is
	// nothing but a single ",". That is the one shape this normalizer targets (a nested-in-a-map StepTags array the
	// dump's map-sort can't reach). On any line where (TagName=...) tokens are interleaved with OTHER content
	// (e.g. a rewards line: XPReward(Amount=69) sits between currency tags), reordering would splice out that
	// content and mask real differences — so such lines are returned verbatim. Walk the token positions; if any
	// gap between consecutive tokens is not exactly ",", bail unchanged.
	int32 FirstStart = INDEX_NONE, LastEnd = INDEX_NONE, Cursor = 0;
	bool bContiguousList = true;
	for (int32 t = 0; t < Tokens.Num(); ++t)
	{
		const int32 At = Line.Find(Tokens[t], ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
		if (At == INDEX_NONE) { bContiguousList = false; break; }
		if (t == 0) FirstStart = At;
		else if (Line.Mid(LastEnd, At - LastEnd) != TEXT(",")) { bContiguousList = false; break; }  // gap must be a lone comma
		LastEnd = At + Tokens[t].Len();
		Cursor = LastEnd;
	}
	if (!bContiguousList || FirstStart == INDEX_NONE) return Line;   // not a bare tag list — leave structure intact

	TArray<FString> Sorted = Tokens;
	Sorted.Sort();
	return Line.Left(FirstStart) + FString::Join(Sorted, TEXT(",")) + Line.Mid(LastEnd);
}

/**
 * Diff two ordered line arrays after applying Norm to each. Returns up to MaxReport mismatches as human lines.
 * Both files are deterministic + identically ordered by construction, so a positional line-by-line diff is valid
 * and pinpoints the offending row/property directly.
 */
static TArray<FString> DiffNormalized(const TArray<FString>& A, const TArray<FString>& B, TFunctionRef<FString(const FString&)> Norm, int32 MaxReport = 20)
{
	TArray<FString> Diffs;
	const int32 N = FMath::Max(A.Num(), B.Num());
	for (int32 i = 0; i < N && Diffs.Num() < MaxReport; ++i)
	{
		const FString LA = A.IsValidIndex(i) ? Norm(A[i]) : TEXT("<missing>");
		const FString LB = B.IsValidIndex(i) ? Norm(B[i]) : TEXT("<missing>");
		if (LA != LB) Diffs.Add(FString::Printf(TEXT("  L%d:\n    src: %s\n    rt : %s"), i + 1, *LA, *LB));
	}
	return Diffs;
}

/** Authored folder fixpoint: compare the source export folder against the _RT re-export, file by file, _RT-normalized. */
int32 CompareQuestExportFolders(const FString& SrcFolder, const FString& RtFolder, const FString& OriginalID)
{
	const TMap<FString, TArray<FString>> Src = LoadFolderTsvs(SrcFolder);
	// The _RT folder's files carry the _RT stem on questline_graph? No — file stems are type names, not the ID; only
	// the ID-derived FOLDER name differs. So match files by identical name.
	const TMap<FString, TArray<FString>> Rt = LoadFolderTsvs(RtFolder);

	// FAIL CLOSED when there is nothing to compare. With both sides empty the loops below run zero times and this
	// returns a clean 0 — reporting a PASS for a comparison it never made. B2 already refuses a missing dump; C should
	// refuse an empty pair for the same reason: an oracle that can't tell "identical" from "absent" isn't an oracle.
	if (Src.IsEmpty() && Rt.IsEmpty())
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("[authored] neither folder yielded any .tsv — nothing was compared. src='%s' rt='%s'"),
			*SrcFolder, *RtFolder);
		return 1;
	}

	int32 Mismatches = 0;
	for (const auto& Pair : Src)
	{
		const TArray<FString>* RtLines = Rt.Find(Pair.Key);
		if (!RtLines)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("[authored] file '%s' present in source, absent in round-trip."), *Pair.Key);
			++Mismatches; continue;
		}
		const TArray<FString> Diffs = DiffNormalized(Pair.Value, *RtLines,
			[&](const FString& L){ return StripLocNamespaces(StripRTSuffix(L, OriginalID)); });
		if (Diffs.Num() > 0)
		{
			++Mismatches;
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("[authored] '%s' differs (%d line(s)):\n%s"),
				*Pair.Key, Diffs.Num(), *FString::Join(Diffs, TEXT("\n")));
		}
	}
	for (const auto& Pair : Rt)
		if (!Src.Contains(Pair.Key))
		{ UE_LOG(LogSimpleQuestResolver, Warning, TEXT("[authored] file '%s' present in round-trip, absent in source."), *Pair.Key); ++Mismatches; }
	return Mismatches;
}

// Compiled-dump comparison, _RT-prefix-normalized, order-preserving.
int32 CompareQuestCompiledDumps(const FString& SrcDump, const FString& RtDump, const FString& OriginalID)
{
	FString SrcText, RtText;
	if (!FFileHelper::LoadFileToString(SrcText, *SrcDump)) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("[compiled] source dump missing: %s"), *SrcDump); return 1; }
	if (!FFileHelper::LoadFileToString(RtText, *RtDump))  { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("[compiled] round-trip dump missing: %s"), *RtDump); return 1; }
	TArray<FString> SrcLines, RtLines;
	SrcText.ParseIntoArrayLines(SrcLines, false);
	RtText.ParseIntoArrayLines(RtLines, false);
	
	// Util_ keys are internal routing handles keyed on the editor NodeGuid, which a fresh import regenerates —
	// canonicalize each dump's util GUIDs to positional placeholders (referentially faithful) before comparing.
	CanonicalizeUtilKeys(SrcLines);
	CanonicalizeUtilKeys(RtLines);
	SortUtilBlocks(SrcLines);
	SortUtilBlocks(RtLines);

	const TArray<FString> Diffs = DiffNormalized(SrcLines, RtLines,
		[&](const FString& L){ return StripObjectRefPaths(StripLocNamespaces(SortTagNameTokens(StripRTFromDump(L, OriginalID)))); });
	if (Diffs.Num() > 0)
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("[compiled] compiled dumps differ (%d line(s)):\n%s"), Diffs.Num(), *FString::Join(Diffs, TEXT("\n")));
	return Diffs.Num();
}