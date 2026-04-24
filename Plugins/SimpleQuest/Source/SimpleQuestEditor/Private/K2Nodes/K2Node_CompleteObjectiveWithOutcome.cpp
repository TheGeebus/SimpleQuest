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

	FEdGraphPinType CompletionDataPinType;
	CompletionDataPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	CompletionDataPinType.PinSubCategoryObject = FQuestObjectiveContext::StaticStruct();

	UEdGraphPin* DataPin = CreatePin(EGPD_Input, CompletionDataPinType, TEXT("CompletionData"));
	DataPin->PinFriendlyName = LOCTEXT("DataPin", "Completion Data");
	DataPin->PinToolTip = LOCTEXT("DataPinTooltip",
		"Struct (FQuestObjectiveContext)\n\n"
		"Per-trigger telemetry delivered alongside this completion — actor lists, counts, and any typed payload the objective\n"
		"wants downstream subscribers to see. Rides the outbound FQuestEndedEvent and is readable via GetCompletionData on\n"
		"the completed step. Leave unconnected for an empty context.").ToString();

	FEdGraphPinType ForwardParamsPinType;
	ForwardParamsPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	ForwardParamsPinType.PinSubCategoryObject = FQuestObjectiveActivationParams::StaticStruct();

	UEdGraphPin* ForwardPin = CreatePin(EGPD_Input, ForwardParamsPinType, TEXT("ForwardParams"));
	ForwardPin->PinFriendlyName = LOCTEXT("ForwardPin", "Forward Params");
	ForwardPin->PinToolTip = LOCTEXT("ForwardPinTooltip",
		"Struct (FQuestObjectiveActivationParams)\n\n"
		"Optional activation payload passed forward into any step(s) this completion activates next. Merges additively with\n"
		"the downstream step's authored defaults (TargetActors union, NumElementsRequired sums, CustomData / ActivationSource\n"
		"take caller-if-set). OriginChain is extended system-side regardless of whether this pin is connected — this payload\n"
		"is for the rest of the activation data you want propagated.\n\n"
		"Common uses: seeding a downstream kill-counter with actors the current step's objective discovered dynamically,\n"
		"carrying a dialogue-choice struct forward as CustomData, or stamping an ActivationSource the next objective needs.\n"
		"Leave unconnected to forward only the chain — the common case.").ToString();
}

FText UK2Node_CompleteObjectiveWithOutcome::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedNodeTitle.IsOutOfDate(this))
	{
		if (OutcomeTag.IsValid())
		{
			FString TagString = OutcomeTag.GetTagName().ToString();
			static const FString Prefix = TEXT("Quest.Outcome.");
			if (TagString.StartsWith(Prefix))
			{
				TagString = TagString.RightChop(Prefix.Len());
			}

			TArray<FString> Segments;
			TagString.ParseIntoArray(Segments, TEXT("."));
			for (FString& Seg : Segments)
			{
				Seg = FName::NameToDisplayString(Seg, false);
			}

			FFormatNamedArguments Args;
			Args.Add(TEXT("Outcome"), FText::FromString(FString::Join(Segments, TEXT(": "))));
			CachedNodeTitle.SetCachedText(
				FText::Format(LOCTEXT("Title", "Complete - {Outcome}"), Args), this);
		}
		else
		{
			CachedNodeTitle.SetCachedText(LOCTEXT("TitleEmpty", "Complete Objective"), this);
		}
	}
	return CachedNodeTitle;
}

FText UK2Node_CompleteObjectiveWithOutcome::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Completes this objective with the specified outcome tag.\n\n"
		"Pins:\n"
		"  • Completion Data — per-trigger telemetry (actors, counts, typed payload) that rides the outbound\n"
		"    FQuestEndedEvent and is readable on the completed step via GetCompletionData. Leave unconnected for empty.\n"
		"  • Forward Params — optional activation payload propagated into the next step(s) this completion activates.\n"
		"    Merges additively with the downstream step's authored defaults. OriginChain is always extended system-side,\n"
		"    so leaving this unconnected still forwards chain-of-activation info to downstream objectives.\n\n"
		"Outcome tags on these nodes are automatically discovered and reflected as output pins on the\n"
		"questline graph Step node — no manual sync between outcome declarations and completion call sites.");
}

FLinearColor UK2Node_CompleteObjectiveWithOutcome::GetNodeTitleColor() const
{
	return SQ_ED_NODE_EXIT_ACTIVE;
}

void UK2Node_CompleteObjectiveWithOutcome::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CachedNodeTitle.MarkDirty();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
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
	if (MyPin->PinName == TEXT("CompletionData"))
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

	if (!OutcomeTag.IsValid())
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("Err_NoTag", "@@: No outcome tag set — node cannot compile.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->FunctionReference.SetSelfMember(FName(TEXT("CompleteObjectiveWithOutcome")));
	CallNode->AllocateDefaultPins();

	UEdGraphPin* CallTagPin = CallNode->FindPin(TEXT("OutcomeTag"));
	if (CallTagPin)
	{
		FString ExportedValue;
		FGameplayTag::StaticStruct()->ExportText(ExportedValue, &OutcomeTag, nullptr, nullptr, PPF_None, nullptr);
		CallTagPin->DefaultValue = ExportedValue;
	}

	UEdGraphPin* DataPin = FindPin(TEXT("CompletionData"));
	UEdGraphPin* CallDataPin = CallNode->FindPin(TEXT("InCompletionData"));
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

	if (!OutcomeTag.IsValid())
	{
		MessageLog.Warning(*LOCTEXT("Warn_NoTag", "@@: No outcome tag set on Complete Objective node.").ToString(),	this);
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

#undef LOCTEXT_NAMESPACE
