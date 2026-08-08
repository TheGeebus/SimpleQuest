// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


// PROTOTYPE — Resolver, Phase 2 authored-graph export. Serializes a questline's AUTHORED model as the interlingua
// folder: one entity table per node/sub-object type (reflection-driven — every EditAnywhere non-Transient UPROPERTY;
// instanced sub-objects explode to child rows in their own type tables) plus one knot-collapsed edge table where
// routing, prereq wiring, deactivation, and nesting are all {from, type, to}. Quest containers' inner graphs recurse;
// LinkedQuestline placements do NOT (the LinkedGraph soft path column is the cross-folder foreign key — the linked
// asset's content belongs to its own export). This is the lossless-structured interlingua form (NOT a readable
// projection): machine fields expected, prettiness is a later panel concern. Read-only, console-triggered. Not shipped API.

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "SimpleQuestLog.h"
#include "Resolver/QuestDataValue.h"
#include "Quests/QuestlineGraph.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestBundleTransforms.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestReflectionUtils.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	// Table file stem for a class: strip the QuestlineNode_ prefix, snake_case the remainder. Underscores insert only on a
	// lower→upper boundary so acronym runs stay together. Derived display, not identity — collisions can't occur because
	// class names are unique and the transform is injective enough for this corpus.
	FString TypeStem(const UClass* Class)
	{
		FString Name = Class->GetName();
		Name.RemoveFromStart(TEXT("QuestlineNode_"));
		FString Out;
		for (int32 i = 0; i < Name.Len(); ++i)
		{
			const TCHAR C = Name[i];
			if (FChar::IsUpper(C) && i > 0 && !FChar::IsUpper(Name[i - 1]))
			{
				Out.AppendChar(TEXT('_'));
			}
			Out.AppendChar(FChar::ToLower(C));
		}
		return Out;
	}

	// Node row key — QuestGuid digits. Every UQuestlineNodeBase carries QuestGuid (base-class field), so no fallback needed.
	FString NodeKeyOf(const UQuestlineNodeBase* Node)
	{
		return Node->QuestGuid.ToString(EGuidFormats::Digits);
	}

	// True when a property's value(s) are Instanced UObjects — the shapes that must explode to child rows instead of
	// serializing as a dangling object path. Recurses array inners, map values, and struct fields so container-wrapped
	// instanced data (e.g. TMap<FGameplayTag, FQuestRewardSet> wrapping an instanced array) classifies correctly.
	bool IsInstancedBearing(const FProperty* Prop)
	{
		return IsQuestInstancedBearing(Prop);   // one definition, shared with the reader that must agree about what a child IS
	}

	void CollectEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells, FQuestDataBundle& Bundle);

	// Emit child rows + contains edges for every instanced object reachable from Prop on the entity keyed OwnerKey.
	// PathPrefix is the property path so far relative to OwnerKey (e.g. "Rewards" or "QuestlineRewards[<key>].Rewards");
	// it becomes both the contains-edge qualifier and the child row's synthetic key suffix, so edge and key corroborate.
	void RecurseInstanced(const FProperty* Prop, const void* ValuePtr, const FString& OwnerKey, const FString& PathPrefix, FQuestDataBundle& Bundle)
	{
		ForEachQuestInstancedChild(Prop, ValuePtr, OwnerKey, PathPrefix,
			[&Bundle, &OwnerKey](const FString& ChildKey, const FString& Path, const UObject* Child)
			{
				Bundle.Edges.Add({ OwnerKey, FString::Printf(TEXT("contains(%s)"), *Path), ChildKey });
				CollectEntityRow(Child, ChildKey, {}, Bundle);   // which recurses this child's own instanced properties
			});
	}

	// Serialize Entity into its type table (creating the table + capturing the column list on first encounter of the class)
	// and recurse instanced-bearing properties into child rows. Graph nodes and instanced sub-objects share this one path —
	// only the key differs. ExtraCells injects structural columns (e.g. "graph") that aren't reflected properties; per-class
	// column consistency holds because node rows always pass the same shape, sub-object rows always pass none, and no class
	// appears as both.
	void CollectEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells, FQuestDataBundle& Bundle)
	{
		const UClass* Class = Entity->GetClass();
		FQuestDataTable& Table = Bundle.TablesByType.FindOrAdd(TypeStem(Class));

		// First row of this type: capture columns from class reflection — "class" leads (a row stays self-describing when
		// copied out of its file), then injected structural columns, then EditAnywhere non-Transient properties in
		// reflection order. Every column always written; instanced-bearing properties are child rows, not columns.
		if (Table.Columns.IsEmpty())
		{
			Table.Columns.Add(TEXT("class"));
			for (const TPair<FString, FString>& Extra : ExtraCells)
			{
				Table.Columns.Add(Extra.Key);
			}
			for (TFieldIterator<FProperty> It(Class); It; ++It)
			{
				if (!IsAuthoredConfigProperty(*It) || IsInstancedBearing(*It))
				{
					continue;
				}
				Table.Columns.Add(It->GetName());
			}
		}

		// CDO of the entity's class — the per-property default the Q6 rule compares against (BuildValue emits Empty when
		// the live value equals it). GetDefaultObject(true) guarantees a non-null CDO (a null default would make
		// FProperty::Identical treat every struct prop as different).
		const UObject* DefaultObject = Class->GetDefaultObject(/*bCreateIfNeeded*/ true);

		FQuestDataRow Row;
		Row.Key = Key;
		{
			FQuestDataValue ClassCell;
			ClassCell.Kind = EQuestDataValueKind::String;
			ClassCell.StringForm = Class->GetName();
			Row.Cells.Add(TEXT("class"), ClassCell);
		}
		for (const TPair<FString, FString>& Extra : ExtraCells)
		{
			FQuestDataValue ExtraCell;
			ExtraCell.Kind = EQuestDataValueKind::String;
			ExtraCell.StringForm = Extra.Value;
			Row.Cells.Add(Extra.Key, ExtraCell);
		}
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!IsAuthoredConfigProperty(Prop))
			{
				continue;
			}
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Entity);
			if (IsInstancedBearing(Prop))
			{
				RecurseInstanced(Prop, ValuePtr, Key, Prop->GetName(), Bundle);
				continue;
			}
			const void* DefaultPtr = DefaultObject ? Prop->ContainerPtrToValuePtr<void>(DefaultObject) : nullptr;
			Row.Cells.Add(Prop->GetName(), BuildQuestDataValue(Prop, ValuePtr, DefaultPtr));
		}
		Table.Rows.Add(MoveTemp(Row));
	}

	// Emit knot-collapsed wire edges for one node: every output pin's terminals via the traversal policy's forward walk
	// (works for any output pin — the zero-knot case degenerates to the direct link). Fresh Visited per source pin: the
	// walker's visited set is node-granular, so sharing one across pins would suppress legitimate edges from later pins.
	void CollectEdgesForNode(const UQuestlineNodeBase* Node, const FQuestlineGraphTraversalPolicy& Policy, FQuestDataBundle& Bundle)
	{
		CollectQuestWireEdges(Node, Policy, Bundle.Edges);
	}

	// Recursively collect one graph level: entity rows + wire edges for every non-knot questline node (content, utility,
	// portal, prereq alike), contains edges + recursion for Quest inner graphs. Knots get no rows and no outgoing edges —
	// they're collapsed into the wire walk. LinkedQuestline placements are NOT recursed: the LinkedGraph soft-path column
	// on their own row is the cross-folder FK. GraphCell = "root" at top level, else the owning Quest container's key.
	void CollectGraph(const UEdGraph* Graph, const FString& GraphCell, const FQuestlineGraphTraversalPolicy& Policy, FQuestDataBundle& Bundle)
	{
		if (!Graph)
		{
			return;
		}
		for (const UEdGraphNode* RawNode : Graph->Nodes)
		{
			const UQuestlineNodeBase* Node = Cast<UQuestlineNodeBase>(RawNode);
			if (!Node)
			{
				continue;   // comment bubbles and other non-questline graph furniture
			}
			if (Node->IsPassThroughNode())
			{
				++Bundle.KnotsCollapsed;
				continue;
			}

			const FString Key = NodeKeyOf(Node);
			TMap<FString, FString> Extra;
			Extra.Add(TEXT("graph"), GraphCell);
			CollectEntityRow(Node, Key, Extra, Bundle);
			CollectEdgesForNode(Node, Policy, Bundle);

			// Quest container: contains edge to each inner node, then recurse. Emitted here (not inside the recursion)
			// so the edge's from-side is unambiguous.
			if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
			{
				if (const UEdGraph* Inner = QuestNode->GetInnerGraph())
				{
					for (const UEdGraphNode* InnerRaw : Inner->Nodes)
					{
						const UQuestlineNodeBase* InnerNode = Cast<UQuestlineNodeBase>(InnerRaw);
						if (!InnerNode || InnerNode->IsPassThroughNode())
						{
							continue;
						}
						Bundle.Edges.Add({ Key, TEXT("contains(InnerGraph)"), NodeKeyOf(InnerNode) });
					}
					CollectGraph(Inner, Key, Policy, Bundle);
				}
			}
		}
	}
	
	// An export folder holds exactly ONE export, so a re-export must remove what the previous one left. This marker is what
	// distinguishes our output from a folder a person authored: path derivation is many-to-one (two questline IDs can
	// sanitize to the same segment), so a name check alone can never be trusted to answer "is this ours to replace?".
	const TCHAR* GExportMarkerName = TEXT(".simplequest-export");

	struct FExportMarker
	{
		FString Format;
		FString SourceAsset;
		TArray<FString> Files;   // what the previous export wrote — the ONLY things a replacement may remove
	};

	bool ReadExportMarker(const FString& Folder, FExportMarker& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Folder / GExportMarkerName))) return false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
		for (const FString& Line : Lines)
		{
			FString Key, Value;
			if (!Line.Split(TEXT("="), &Key, &Value)) continue;   // comment/blank lines have no '='
			if (Key == TEXT("Format"))           { Out.Format = Value; }
			else if (Key == TEXT("SourceAsset")) { Out.SourceAsset = Value; }
			else if (Key == TEXT("File"))        { Out.Files.Add(Value); }
		}
		return true;
	}

	bool WriteExportMarker(const FString& Folder, const FExportMarker& Marker)
	{
		TArray<FString> Lines;
		Lines.Add(TEXT("# Written by SimpleQuest. It marks this folder as export output, which a later export of the same"));
		Lines.Add(TEXT("# questline will replace. Delete this file if you want your own edits here left alone — SimpleQuest"));
		Lines.Add(TEXT("# will then refuse to export here rather than overwrite them."));
		Lines.Add(FString::Printf(TEXT("Format=%s"), *Marker.Format));
		Lines.Add(FString::Printf(TEXT("SourceAsset=%s"), *Marker.SourceAsset));
		for (const FString& File : Marker.Files) { Lines.Add(FString::Printf(TEXT("File=%s"), *File)); }
		// Force UTF-8: SaveStringToFile's AutoDetect silently switches to UTF-16 the moment any non-ASCII character appears
		// in the text, which would make this adopter-facing file unreadable in a plain editor and inconsistent with the data
		// files beside it.
		return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *(Folder / GExportMarkerName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/** Filenames directly in a folder (never recursive). Empty when the folder doesn't exist. */
	TArray<FString> FilesIn(const FString& Folder)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*")), /*Files*/ true, /*Dirs*/ false);
		return Found;
	}

	void ExportQuestlineCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: usage 'SimpleQuest.ExportQuestline <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: couldn't load questline asset or its authored graph '%s'."), *Args[0]);
			return;
		}

		FQuestDataBundle Bundle;
		const TUniquePtr<FQuestlineGraphTraversalPolicy> Policy = MakeUnique<FQuestlineGraphTraversalPolicy>();

		// Questline-self row: the asset's own authored fields (QuestlineID / DisplayName / Description / DisplayData /
		// ResettableReplay as columns; QuestlineRewards explodes through the instanced recursion into reward child rows).
		// Keyed by the SANITIZED EffectiveID — the same segment form compiled tags use, so the export key aligns with
		// tag identity and stays interchange-safe (no spaces/punctuation in keys or folder names).
		const FString SelfKey = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
		
		// The key can come out EMPTY from input a designer can type: a whitespace-only QuestlineID is not IsEmpty(), so the
		// asset-name fallback never fires, and the sanitizer trims it to nothing. An empty segment appends only a separator,
		// so the destination would collapse to the export ROOT and scatter this export across every other questline's output.
		// Refuse rather than write somewhere unintended, and name the field to fix.
		if (SelfKey.IsEmpty())
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: '%s' has a QuestlineID that reduces to an empty export key "
				"(raw value: '%s'). Give it at least one letter, digit or underscore — or clear the field entirely to fall back "
				"to the asset name. Nothing exported."),
				*Args[0],
				*Graph->GetEffectiveID());
			return;
		}
		CollectEntityRow(Graph, SelfKey, {}, Bundle);

		CollectGraph(Graph->QuestlineEdGraph, TEXT("root"), *Policy, Bundle);

		// Optional studio-shape restatement. Absent = canonical export (our vocabulary), byte-identical to before.
		TArray<FString> Warnings;
		if (const UQuestImportMapping* Mapping = LoadQuestMappingArg(Args))
		{
			TMap<FString, FString> SourceKeyByGuid;
			TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
			CollectQuestNodeIdentity(Graph->QuestlineEdGraph, SourceKeyByGuid, NodeByGuid);
			QuestBundle_ApplyReverseMapping(Bundle, *Mapping, SourceKeyByGuid, NodeByGuid, Warnings);
		}
		for (const FString& W : Warnings) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: %s"), *W); }
		
		// Prove containment structurally instead of trusting the string that produced it — the destination must be exactly one
		// level below the export root. Holds even if the key derivation changes or is later fed from somewhere new.
		const FString ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport"));
		const FString OutDir = FPaths::ConvertRelativePathToFull(ExportRoot / SelfKey);
		{
			FString NormRoot = ExportRoot;  FPaths::NormalizeDirectoryName(NormRoot);
			FString NormOut  = OutDir;      FPaths::NormalizeDirectoryName(NormOut);
			if (NormOut == NormRoot || FPaths::GetPath(NormOut) != NormRoot)
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: refusing — destination '%s' is not a direct child of the "
					"export root '%s' (export key '%s'). Nothing exported."),
					*NormOut,
					*NormRoot,
					*SelfKey);
				return;
			}
		}
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: destination '%s'."), *OutDir);

		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("ExportQuestline"));
		if (!Format)
		{
			return;   // the unregistered-format error was already logged; nothing exported.
		}
		// OWNERSHIP — never replace a folder we didn't write. This is the guard that survives a NAME COLLISION: two
		// questline IDs can sanitize to one folder, and a hand-authored source folder sitting at that name would otherwise
		// be overwritten by an export.
		FExportMarker Previous;
		const bool bHadMarker = ReadExportMarker(OutDir, Previous);
		const TArray<FString> Existing = FilesIn(OutDir);
		if (Existing.Num() > 0 && !bHadMarker)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: refusing — '%s' already holds %d file(s) and carries no "
				"SimpleQuest export marker, so an export did not write it. Exporting would replace its contents. Move or delete "
				"that folder, or give this questline a different QuestlineID. Nothing written."),
				*OutDir,
				Existing.Num());
			return;
		}
		if (bHadMarker && !Previous.SourceAsset.IsEmpty() && Previous.SourceAsset != Args[0])
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: refusing — '%s' holds the export of a DIFFERENT questline "
				"('%s'). Their IDs reduce to the same folder name, so each would overwrite the other. Give one a distinct "
				"QuestlineID. Nothing written."),
				*OutDir,
				*Previous.SourceAsset);
			return;
		}

		// STAGE — write the complete new export beside the destination. NOTHING is deleted until it exists on disk, so a
		// failed, refused or interrupted write leaves the previous export exactly as it was. The sanitizer can never emit a
		// '.', so this name cannot collide with a real destination.
		const FString Staging = OutDir + TEXT(".incoming");
		IFileManager::Get().DeleteDirectory(*Staging, /*RequireExists*/ false, /*Tree*/ true);
		if (!Format->WriteBundle(Bundle, Staging))
		{
			IFileManager::Get().DeleteDirectory(*Staging, false, true);
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: the %s provider failed to write. '%s' is unchanged."),
				*Format->FormatName(),
				*OutDir);
			return;
		}

		FExportMarker Marker;
		Marker.Format = Format->FormatName();
		Marker.SourceAsset = Args[0];
		Marker.Files = FilesIn(Staging);   // enumerated, not reported — works for any provider, including one that ignores us

		// REPLACE — remove only what the PREVIOUS export recorded. Never a directory, never read-only: a read-only file is
		// protected on purpose, and a subdirectory can't contribute to the stale-shape problem because the reader doesn't
		// recurse. Any failure aborts with the finished copy left in place and named.
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
		int32 Removed = 0;
		for (const FString& Old : Previous.Files)
		{
			const FString OldPath = OutDir / Old;
			if (!IFileManager::Get().FileExists(*OldPath)) continue;
			if (!IFileManager::Get().Delete(*OldPath, /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ false))
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: couldn't remove '%s' from the previous export — it may be "
					"read-only or open elsewhere. '%s' is unchanged; the finished new export is at '%s'."),
					*OldPath,
					*OutDir,
					*Staging);
				return;
			}
			++Removed;
		}
		if (bHadMarker) { IFileManager::Get().Delete(*(OutDir / GExportMarkerName), false, false, false); }

		WriteExportMarker(Staging, Marker);
		for (const FString& New : Marker.Files)
		{
			if (!IFileManager::Get().Move(*(OutDir / New), *(Staging / New)))
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: couldn't move '%s' into place — '%s' is now PARTIAL and "
					"should not be imported. The complete export is at '%s'."),
					*New,
					*OutDir,
					*Staging);
				return;
			}
		}
		IFileManager::Get().Move(*(OutDir / GExportMarkerName), *(Staging / GExportMarkerName));
		IFileManager::Get().DeleteDirectory(*Staging, false, true);   // scratch only; a failure here is not data loss

		int32 RowTotal = 0;
		for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		{
			RowTotal += TablePair.Value.Rows.Num();
		}
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: '%s' — %d entity row(s) across %d type(s), %d edge(s), %d knot(s) "
			"collapsed. Wrote %d file(s) to '%s'; removed %d from the previous export."),
			*SelfKey,
			RowTotal,
			Bundle.TablesByType.Num(),
			Bundle.Edges.Num(),
			Bundle.KnotsCollapsed,
			Marker.Files.Num(),
			*OutDir,
			Removed);
	}
}

static FAutoConsoleCommand GExportQuestlineCmd(
	TEXT("SimpleQuest.ExportQuestline"),
	TEXT("PROTOTYPE: export a questline's authored model as the interlingua folder — per-type entity tables "
		"(reflection-driven, instanced sub-objects as child rows) + one knot-collapsed edge table — to "
		"Saved/QuestExport/<QuestlineID>/. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportQuestlineCmd));
