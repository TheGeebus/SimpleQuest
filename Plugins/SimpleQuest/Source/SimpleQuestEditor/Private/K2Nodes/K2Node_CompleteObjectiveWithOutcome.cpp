// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "K2Nodes/Slate/SGraphNode_CompleteObjective.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Objectives/QuestObjective.h"
#include "Utilities/SimpleQuestEditorUtils.h"

#define LOCTEXT_NAMESPACE "K2Node_CompleteObjectiveWithOutcome"

// ---------------------------------------------------------------------------
// UEdGraphNode
// ---------------------------------------------------------------------------

void UK2Node_CompleteObjectiveWithOutcome::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// OutcomeTag input pin — wireable. Pin's DefaultValue widget is UE's standard FGameplayTag picker;
	// wiring auto-hides the picker (UE built-in pin behavior). Picker filter comes from GetPinMetaData.
	FEdGraphPinType OutcomePinType;
	OutcomePinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	OutcomePinType.PinSubCategoryObject = FGameplayTag::StaticStruct();
	UEdGraphPin* OutcomeTagPin = CreatePin(EGPD_Input, OutcomePinType, TEXT("OutcomeTag"));
	OutcomeTagPin->PinFriendlyName = LOCTEXT("OutcomeTagPin", "Outcome Tag");
	OutcomeTagPin->PinToolTip = LOCTEXT("OutcomeTagPinTooltip",
		"Outcome Tag\n"
		"Gameplay Tag Structure\n\n"
		"The outcome tag this completion fires. Set via the picker on the pin (static placement) or wire a\n"
		"runtime FGameplayTag for dynamic completions. When wired, set Path Name on the node body so the Step\n"
		"has a stable structural identity to route the completion through.").ToString();

	FEdGraphPinType CompletionContextPinType;
	CompletionContextPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	CompletionContextPinType.PinSubCategoryObject = FQuestObjectiveContext::StaticStruct();
	UEdGraphPin* DataPin = CreatePin(EGPD_Input, CompletionContextPinType, TEXT("CompletionContext"));
	DataPin->PinFriendlyName = LOCTEXT("ContextPin", "Completion Context");
	DataPin->PinToolTip = LOCTEXT("ContextPinTooltip",
		"Completion Context\n"
		"Quest Objective Context Structure\n\n"
		"Per-trigger telemetry delivered alongside this completion: actor lists, counts, and any typed payload the objective\n"
		"wants downstream subscribers to see. Rides the outbound FQuestEndedEvent and is readable via GetCompletionContext on\n"
		"the completed step. Leave unconnected for an empty context.").ToString();

	FEdGraphPinType ForwardParamsPinType;
	ForwardParamsPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	ForwardParamsPinType.PinSubCategoryObject = FQuestObjectiveActivationParams::StaticStruct();
	UEdGraphPin* ForwardPin = CreatePin(EGPD_Input, ForwardParamsPinType, TEXT("ForwardParams"));
	ForwardPin->PinFriendlyName = LOCTEXT("ForwardPin", "Forward Params");
	ForwardPin->PinToolTip = LOCTEXT("ForwardPinTooltip",
		"Forward Params\n"
		"Quest Objective Activation Params Structure\n\n"
		"Optional activation payload passed forward into any step(s) this completion activates next. Merges additively with\n"
		"the downstream step's authored defaults (TargetActors union, NumElementsRequired sums, CustomData / ActivationSource\n"
		"take caller-if-set). OriginChain is extended system-side regardless of whether this pin is connected — this payload\n"
		"is for the rest of the activation data you want propagated.\n\n"
		"Common uses: seeding a downstream kill-counter with actors the current step's objective discovered dynamically,\n"
		"carrying a dialogue-choice struct forward as CustomData, or stamping an ActivationSource the next objective needs.\n"
		"Leave unconnected to forward only the chain — the common case.").ToString();
}

FName UK2Node_CompleteObjectiveWithOutcome::ResolvePathIdentity() const
{
	// 1. Designer-authored PathName wins (dynamic with explicit identity).
	if (!PathName.IsNone())
	{
		return PathName;
	}

	const UEdGraphPin* OutcomeTagPin = FindPin(TEXT("OutcomeTag"));
	if (!OutcomeTagPin)
	{
		return NAME_None;
	}

	// 2. Wired without PathName: auto-numbered "Dynamic N" identity per stable DynamicIndex. EnsureDynamicIndex-
	//    Allocated runs at wire-connect / paste / PathName-cleared time so each placement carries its own index.
	//    INDEX_NONE fallback (allocation hadn't fired yet — defensive) maps to "Dynamic 1" so the path identity
	//    is at least non-empty.
	if (OutcomeTagPin->LinkedTo.Num() > 0)
	{
		const int32 DisplayNumber = (DynamicIndex >= 0 ? DynamicIndex : 0) + 1;
		return FName(*FString::Printf(TEXT("Dynamic %d"), DisplayNumber));
	}

	// 3. Static placement: parse the OutcomeTag pin's DefaultValue.
	if (OutcomeTagPin->DefaultValue.IsEmpty())
	{
		return NAME_None;
	}

	FGameplayTag OutcomeTag;
	FGameplayTag::StaticStruct()->ImportText(*OutcomeTagPin->DefaultValue, &OutcomeTag, nullptr, PPF_None, nullptr, FString());
	return OutcomeTag.IsValid() ? OutcomeTag.GetTagName() : NAME_None;
}

void UK2Node_CompleteObjectiveWithOutcome::EnsureDynamicIndexAllocated()
{
	// Only meaningful in dynamic-without-PathName state. If the node isn't there, leave DynamicIndex sticky
	// (designer might toggle back later).
	const UEdGraphPin* OutcomeTagPin = FindPin(TEXT("OutcomeTag"));
	const bool bWired = OutcomeTagPin && OutcomeTagPin->LinkedTo.Num() > 0;
	if (!bWired || !PathName.IsNone())
	{
		return;
	}

	// Collect used indices from sibling K2 placements in this BP (excluding self). Cross-graph scan so
	// per-event-graph and macro libraries within the same BP are deduplicated.
	TSet<int32> UsedIndices;
	if (UBlueprint* OwnerBP = FBlueprintEditorUtils::FindBlueprintForNode(this))
	{
		TArray<UEdGraph*> AllGraphs;
		OwnerBP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			TArray<UK2Node_CompleteObjectiveWithOutcome*> Siblings;
			Graph->GetNodesOfClass(Siblings);
			for (const UK2Node_CompleteObjectiveWithOutcome* Sibling : Siblings)
			{
				if (Sibling != this && Sibling->DynamicIndex >= 0)
				{
					UsedIndices.Add(Sibling->DynamicIndex);
				}
			}
		}
	}

	// Reallocate iff our current index is unset OR collides with a sibling (paste / duplicate case).
	if (DynamicIndex < 0 || UsedIndices.Contains(DynamicIndex))
	{
		int32 NewIndex = 0;
		while (UsedIndices.Contains(NewIndex))
		{
			++NewIndex;
		}

		const FScopedTransaction Transaction(LOCTEXT("AllocateDynamicIndex", "Allocate Dynamic Pin Index"));
		Modify();
		DynamicIndex = NewIndex;
		CachedNodeTitle.MarkDirty();
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
}

void UK2Node_CompleteObjectiveWithOutcome::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (Pin && Pin->PinName == TEXT("OutcomeTag"))
	{
		EnsureDynamicIndexAllocated();
		// Title might have changed (entered/left dynamic mode); invalidate cache.
		CachedNodeTitle.MarkDirty();
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
}

void UK2Node_CompleteObjectiveWithOutcome::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
	// Catches initial placement, paste, and duplicate. The collision-check inside EnsureDynamicIndexAllocated
	// reallocates a paste/duplicate's stale-inherited index automatically.
	EnsureDynamicIndexAllocated();
}

FText UK2Node_CompleteObjectiveWithOutcome::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedNodeTitle.IsOutOfDate(this))
	{
		const FName ResolvedPath = ResolvePathIdentity();
		if (ResolvedPath.IsNone())
		{
			CachedNodeTitle.SetCachedText(LOCTEXT("TitleEmpty", "Complete Objective"), this);
		}
		else
		{
			// Strip the SimpleQuest.QuestOutcome. prefix if present (static placements resolve to the
			// outcome tag's full FName; dynamic placements resolve to a bare name like "Dynamic" or the
			// designer-authored PathName, which won't carry the prefix and pass through unmodified).
			FString PathString = ResolvedPath.ToString();
			static const FString OutcomePrefix = TEXT("SimpleQuest.QuestOutcome.");
			if (PathString.StartsWith(OutcomePrefix))
			{
				PathString = PathString.RightChop(OutcomePrefix.Len());
			}

			// Prettify multi-segment paths (e.g., "Combat.BossDefeated" → "Combat: Boss Defeated").
			TArray<FString> Segments;
			PathString.ParseIntoArray(Segments, TEXT("."));
			for (FString& Seg : Segments)
			{
				Seg = FName::NameToDisplayString(Seg, false);
			}

			FFormatNamedArguments Args;
			Args.Add(TEXT("Path"), FText::FromString(FString::Join(Segments, TEXT(": "))));
			CachedNodeTitle.SetCachedText(
				FText::Format(LOCTEXT("Title", "Complete - {Path}"), Args), this);
		}
	}
	return CachedNodeTitle;
}

FText UK2Node_CompleteObjectiveWithOutcome::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Completes this objective with the specified outcome tag.\n\n"
		"Pins:\n"
		"  • Outcome Tag — a gameplay tag representing the named Outcome for this Completion Path. Outcomes are reusable\n"
		"       broadcast channels independent of Quest event hierarchy. Subscribers may listen for all events of a given\n"
		"       Outcome type or filter Quest event broadcasts by Outcome as needed.\n"
		"  • Completion Context — per-trigger telemetry (actors, counts, typed payload) that rides the outbound QuestEndedEvent\n"
		"		and is readable on the completed step via GetCompletionContext. Leave unconnected for empty.\n"
		"  • Forward Params — optional activation payload propagated into the next step(s) this completion activates.\n"
		"       Merges additively with the downstream step's authored defaults. OriginChain is always extended system-side,\n"
		"       so leaving this unconnected still forwards chain-of-activation info to downstream objectives.\n\n"
		"Outcome tags set via the tag picker on these nodes are automatically discovered and reflected as output pins on the\n"
		"Questline Graph's Step node: no manual sync between outcome declarations and completion call sites.\n\n"
		"The optional Path Name field can be used to specify a friendly name for the Completion Path output pin that will\n"
		"appear on the parent Step node in a Questline Graph. This field takes priority over any tag-generated pin label.\n"
		"If neither the Path Name nor the Outcome Tag are set at compile time, a default name will be generated to differentiate\n"
		"each Completion Path.\n\n"
		"Complete Quest nodes may share the same Path Name. Nodes that share a Path Name will all feed the same output pin\n"
		"on the Step node in their parent graph.");
}

FLinearColor UK2Node_CompleteObjectiveWithOutcome::GetNodeTitleColor() const
{
	return SQ_ED_NODE_EXIT_ACTIVE;
}

void UK2Node_CompleteObjectiveWithOutcome::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CachedNodeTitle.MarkDirty();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

void UK2Node_CompleteObjectiveWithOutcome::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	// Pin's picker changes refresh the cached title (static-placement title derives from OutcomeTag pin's value).
	if (Pin && Pin->PinName == TEXT("OutcomeTag"))
	{
		CachedNodeTitle.MarkDirty();
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
}

TSharedPtr<SGraphNode> UK2Node_CompleteObjectiveWithOutcome::CreateVisualWidget()
{
	return SNew(SGraphNode_CompleteObjective, this);
}

FSlateIcon UK2Node_CompleteObjectiveWithOutcome::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	return FSlateIcon("SimpleQuestStyle", "ClassIcon.QuestlineGraph");
}

bool UK2Node_CompleteObjectiveWithOutcome::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin->PinName == TEXT("CompletionContext"))
	{
		if (OtherPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			OutReason = TEXT("Completion Data only accepts struct types.");
			return true;
		}
	}
	if (MyPin->PinName == TEXT("ForwardParams"))
	{
		if (OtherPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			OutReason = TEXT("Forward Params only accepts struct types.");
			return true;
		}
	}
	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}


// ---------------------------------------------------------------------------
// UK2Node — compilation
// ---------------------------------------------------------------------------

void UK2Node_CompleteObjectiveWithOutcome::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* OutcomeTagPin = FindPin(TEXT("OutcomeTag"));
	const bool bOutcomeTagWired = OutcomeTagPin && OutcomeTagPin->LinkedTo.Num() > 0;
	const bool bHasStaticTag = OutcomeTagPin && !OutcomeTagPin->DefaultValue.IsEmpty();

	// Static placement validation: must have a tag set OR be wired. Wired-without-PathName is no longer
	// an error — the path identity auto-falls-back to "Dynamic" via ResolvePathIdentity.
	if (!bOutcomeTagWired && !bHasStaticTag)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("Err_NoTag", "@@: No outcome tag set (and no wired override) — node cannot compile.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->FunctionReference.SetSelfMember(FName(TEXT("CompleteObjectiveWithOutcome")));
	CallNode->AllocateDefaultPins();

	// OutcomeTag: wire if connected, else copy our pin's DefaultValue to the call's OutcomeTag pin DefaultValue.
	UEdGraphPin* CallTagPin = CallNode->FindPin(TEXT("OutcomeTag"));
	if (CallTagPin && OutcomeTagPin)
	{
		if (bOutcomeTagWired)
		{
			CompilerContext.MovePinLinksToIntermediate(*OutcomeTagPin, *CallTagPin);
		}
		else
		{
			CallTagPin->DefaultValue = OutcomeTagPin->DefaultValue;
		}
	}

	// PathIdentity: single resolution via ResolvePathIdentity (PathName > "Dynamic" > OutcomeTag leaf).
	UEdGraphPin* CallPathIdentityPin = CallNode->FindPin(TEXT("PathIdentity"));
	if (CallPathIdentityPin)
	{
		CallPathIdentityPin->DefaultValue = ResolvePathIdentity().ToString();
	}

	UEdGraphPin* DataPin = FindPin(TEXT("CompletionContext"));
	UEdGraphPin* CallDataPin = CallNode->FindPin(TEXT("InCompletionContext"));
	if (DataPin && CallDataPin)
	{
		CompilerContext.MovePinLinksToIntermediate(*DataPin, *CallDataPin);
	}

	UEdGraphPin* ForwardPin = FindPin(TEXT("ForwardParams"));
	UEdGraphPin* CallForwardPin = CallNode->FindPin(TEXT("InForwardParams"));
	if (ForwardPin && CallForwardPin)
	{
		CompilerContext.MovePinLinksToIntermediate(*ForwardPin, *CallForwardPin);
	}

	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *CallNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then), *CallNode->GetThenPin());

	BreakAllNodeLinks();
}

void UK2Node_CompleteObjectiveWithOutcome::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	const UEdGraphPin* OutcomeTagPin = FindPin(TEXT("OutcomeTag"));
	const bool bOutcomeTagWired = OutcomeTagPin && OutcomeTagPin->LinkedTo.Num() > 0;
	const bool bHasStaticTag = OutcomeTagPin && !OutcomeTagPin->DefaultValue.IsEmpty();

	if (!bOutcomeTagWired && !bHasStaticTag)
	{
		MessageLog.Warning(*LOCTEXT("Warn_NoTag", "@@: No outcome tag set on Complete Objective node (and no wired override).").ToString(), this);
	}
}

// ---------------------------------------------------------------------------
// UK2Node — palette registration
// ---------------------------------------------------------------------------

void UK2Node_CompleteObjectiveWithOutcome::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_CompleteObjectiveWithOutcome::GetMenuCategory() const
{
	return LOCTEXT("Category", "Simple Quest|Objectives");
}

bool UK2Node_CompleteObjectiveWithOutcome::IsActionFilteredOut(const FBlueprintActionFilter& Filter)
{
	for (const UBlueprint* BP : Filter.Context.Blueprints)
	{
		if (!BP) continue;
		UClass* TestClass = BP->GeneratedClass
			? static_cast<UClass*>(BP->GeneratedClass)
			: BP->ParentClass.Get();
		if (TestClass && TestClass->IsChildOf(UQuestObjective::StaticClass()))
		{
			return false;
		}
	}
	return true;
}

bool UK2Node_CompleteObjectiveWithOutcome::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	if (!Super::IsCompatibleWithGraph(TargetGraph)) return false;

	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	if (!Blueprint) return false;

	UClass* TestClass = Blueprint->GeneratedClass
		? static_cast<UClass*>(Blueprint->GeneratedClass)
		: Blueprint->ParentClass.Get();
	return TestClass && TestClass->IsChildOf(UQuestObjective::StaticClass());
}

FString UK2Node_CompleteObjectiveWithOutcome::GetPinMetaData(FName InPinName, FName InKey)
{
	// FGameplayTag struct-pin customization reads "Categories" key to filter the picker namespace.
	if (InPinName == TEXT("OutcomeTag") && InKey == TEXT("Categories"))
	{
		return TEXT("SimpleQuest.QuestOutcome");
	}
	return Super::GetPinMetaData(InPinName, InKey);
}

void UK2Node_CompleteObjectiveWithOutcome::PostLoad()
{
	Super::PostLoad();

	if (OutcomeTag_DEPRECATED.IsValid())
	{
		if (UEdGraphPin* OutcomeTagPin = FindPin(TEXT("OutcomeTag")))
		{
			if (OutcomeTagPin->DefaultValue.IsEmpty())
			{
				FString ExportedValue;
				FGameplayTag::StaticStruct()->ExportText(ExportedValue, &OutcomeTag_DEPRECATED, nullptr, nullptr, PPF_None, nullptr);
				OutcomeTagPin->DefaultValue = ExportedValue;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
