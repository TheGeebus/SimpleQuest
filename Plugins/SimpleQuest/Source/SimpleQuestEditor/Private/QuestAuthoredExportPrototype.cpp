// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


// PROTOTYPE — Resolver, Phase 2 authored-graph export (the round-trip source layer). Serializes a questline's AUTHORED
// editor nodes — every UPROPERTY(EditAnywhere) config field, reflection-driven, skipping Transient view-state — as a
// structured node/property table. This is the lossless-structured interlingua form (NOT the readable compiled projection):
// machine fields expected, prettiness is a later panel concern. Flat-config half only; the pin-wiring table is a separate
// probe. Read-only, console-triggered. Not shipped API.

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Nodes/QuestlineNodeBase.h"

namespace
{
	// Escape tabs/newlines in a serialized property value so a value with embedded whitespace can't break the TSV row.
	FString Sanitize(const FString& In)
	{
		return In.Replace(TEXT("\t"), TEXT("\\t")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
	}

	// The authored node's stable identity — every UQuestlineNodeBase carries QuestGuid. Fall back to NodeGuid for
	// non-quest nodes (comments etc.) so every row is keyed by something.
	FString NodeKey(const UEdGraphNode* Node)
	{
		if (const UQuestlineNodeBase* QN = Cast<UQuestlineNodeBase>(Node))
		{
			return QN->QuestGuid.ToString(EGuidFormats::Digits);
		}
		return Node->NodeGuid.ToString(EGuidFormats::Digits);
	}

	void ExportAuthored(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportAuthored: usage 'SimpleQuest.ExportAuthored <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportAuthored: couldn't load questline asset or its authored graph '%s'."), *Args[0]);
			return;
		}

		TArray<FString> Rows;
		Rows.Add(TEXT("node_guid\tnode_type\tproperty\tvalue"));

		int32 NodeCount = 0, PropCount = 0;
		// NOTE: top-level authored graph only for this probe — inner Quest graphs + linked assets are separate walks
		// (the nesting is itself part of the round-trip problem; prove the flat single-graph case first).
		for (const UEdGraphNode* Node : Graph->QuestlineEdGraph->Nodes)
		{
			if (!Node) continue;
			// Skip pure-cosmetic graph furniture that carries no authored quest data.
			if (!Node->IsA<UQuestlineNodeBase>()) continue;

			const FString Guid = NodeKey(Node);
			const FString Type = Node->GetClass()->GetName();   // e.g. QuestlineNode_Step, QuestlineNode_Reward
			++NodeCount;

			// Reflection walk: every EditAnywhere property that isn't Transient is authored config. ExportTextItem is
			// the same value->text conversion T3D uses, so it's round-trip-faithful.
			for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
			{
				const FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;        // not designer-editable → not authored config
				if (Prop->HasAnyPropertyFlags(CPF_Transient)) continue;    // view-state / derived → skip

				FString ValueStr;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node);
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, /*Default*/ nullptr, nullptr, PPF_None);

				Rows.Add(FString::Printf(TEXT("%s\t%s\t%s\t%s"), *Guid, *Type, *Prop->GetName(), *Sanitize(ValueStr)));
				++PropCount;
			}
		}

		const FString OutDir = FPaths::ProjectSavedDir() / TEXT("QuestExport");
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
		const FString OutPath = FPaths::ConvertRelativePathToFull(OutDir / (Graph->GetQuestlineID() + TEXT("_authored.tsv")));
		if (FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")), *OutPath))
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("ExportAuthored: '%s' — %d node(s), %d property row(s), wrote '%s'."),
				*Graph->GetQuestlineID(), NodeCount, PropCount, *OutPath);
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportAuthored: failed to write '%s'."), *OutPath);
		}
	}

		// One typed edge in the unified relationship table. `Type` is what makes routing / prereq / nesting all one primitive.
	struct FAuthoredEdge { FString From; FString Type; FString To; };

	// Classify a wire by the OUTPUT pin's category — this is the crux of "routing and prereq-wiring are one mechanism,
	// distinguished only by pin category." Activation flow, prereq wiring, outcome routing, deactivation are all pin edges.
	FString WireType(FName PinCategory)
	{
		if (PinCategory == TEXT("QuestActivation"))    return TEXT("activates");
		if (PinCategory == TEXT("QuestPrerequisite"))  return TEXT("gated-by");    // reads output->input as "consumer gated-by source"
		if (PinCategory == TEXT("QuestOutcome"))       return TEXT("outcome");
		if (PinCategory == TEXT("QuestDeactivate"))    return TEXT("deactivates");
		return FString::Printf(TEXT("wire:%s"), *PinCategory.ToString());
	}

	// Recurse instanced object-properties as "contains" edges (nesting = routing). A Reward node contains its reward
	// config sub-objects; each sub-object becomes an edge to a synthetic child key. Proves nesting is the same primitive.
	void CollectNestingEdges(const UObject* Owner, const FString& OwnerKey, TArray<FAuthoredEdge>& Out)
	{
		for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient)) continue;

			// Instanced object arrays (e.g. Reward's TArray<Instanced UQuestRewardBase>) — the case that broke flat export.
			if (const FArrayProperty* ArrP = CastField<FArrayProperty>(Prop))
			{
				const FObjectProperty* InnerObj = CastField<FObjectProperty>(ArrP->Inner);
				if (InnerObj && ArrP->Inner->HasAnyPropertyFlags(CPF_InstancedReference))
				{
					FScriptArrayHelper Helper(ArrP, Prop->ContainerPtrToValuePtr<void>(Owner));
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						const UObject* Sub = InnerObj->GetObjectPropertyValue(Helper.GetRawPtr(i));
						if (!Sub) continue;
						const FString ChildKey = FString::Printf(TEXT("%s/%s[%d]:%s"),
							*OwnerKey, *Prop->GetName(), i, *Sub->GetClass()->GetName());
						Out.Add({ OwnerKey, TEXT("contains"), ChildKey });
						CollectNestingEdges(Sub, ChildKey, Out);   // recurse — a reward config could itself nest
					}
				}
			}
			// (Single instanced object properties would go here too; rewards use the array form, so this probe covers that.)
		}
	}

	void ExportEdges(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportEdges: usage 'SimpleQuest.ExportEdges <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportEdges: couldn't load questline asset/graph '%s'."), *Args[0]);
			return;
		}

		TArray<FAuthoredEdge> Edges;
		for (const UEdGraphNode* Node : Graph->QuestlineEdGraph->Nodes)
		{
			const UQuestlineNodeBase* QN = Cast<UQuestlineNodeBase>(Node);
			if (!QN) continue;
			const FString FromKey = QN->QuestGuid.ToString(EGuidFormats::Digits);

			// WIRE edges: every OUTPUT pin's links become {this node -> (category) -> linked node}.
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked || !Linked->GetOwningNodeUnchecked()) continue;
					const UQuestlineNodeBase* ToQN = Cast<UQuestlineNodeBase>(Linked->GetOwningNode());
					const FString ToKey = ToQN ? ToQN->QuestGuid.ToString(EGuidFormats::Digits)
											   : Linked->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Digits);
					// Annotate the edge type with the source pin name so outcome routing keeps its outcome label.
					const FString Type = FString::Printf(TEXT("%s(%s)"), *WireType(Pin->PinType.PinCategory), *Pin->PinName.ToString());
					Edges.Add({ FromKey, Type, ToKey });
				}
			}

			// NESTING edges: instanced sub-objects (the flat-export cut) as "contains" edges.
			CollectNestingEdges(QN, FromKey, Edges);
		}

		TArray<FString> Rows;
		Rows.Add(TEXT("from\ttype\tto"));
		for (const FAuthoredEdge& E : Edges) Rows.Add(FString::Printf(TEXT("%s\t%s\t%s"), *E.From, *E.Type, *E.To));

		const FString QLID = Graph->GetQuestlineID().IsEmpty() ? Graph->GetName() : Graph->GetQuestlineID();
		const FString OutDir = FPaths::ProjectSavedDir() / TEXT("QuestExport");
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
		const FString OutPath = FPaths::ConvertRelativePathToFull(OutDir / (QLID + TEXT("_edges.tsv")));
		if (FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")), *OutPath))
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("ExportEdges: '%s' — %d edge(s), wrote '%s'."), *QLID, Edges.Num(), *OutPath);
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportEdges: failed to write '%s'."), *OutPath);
		}
	}
}

static FAutoConsoleCommand GExportAuthoredCmd(
	TEXT("SimpleQuest.ExportAuthored"),
	TEXT("PROTOTYPE: serialize a questline's authored editor-node config (reflection-driven, EditAnywhere non-Transient) "
		"to Saved/QuestExport/<QuestlineID>_authored.tsv. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportAuthored));

static FAutoConsoleCommand GExportEdgesCmd(
	TEXT("SimpleQuest.ExportEdges"),
	TEXT("PROTOTYPE: serialize a questline's authored relationships (wire edges by pin category + instanced-object "
		"nesting) as a unified from/type/to table to Saved/QuestExport/<QuestlineID>_edges.tsv. Arg: questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportEdges));
