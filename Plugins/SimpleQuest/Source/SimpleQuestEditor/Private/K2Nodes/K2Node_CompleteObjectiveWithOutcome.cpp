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
		"Outcome tags on these nodes are automatically discovered and\n"
		"reflected as output pins on the questline graph Step node.");
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

// ---------------------------------------------------------------------------
// UK2Node — compilation
// ---------------------------------------------------------------------------

void UK2Node_CompleteObjectiveWithOutcome::ExpandNode(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (!OutcomeTag.IsValid())
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("Err_NoTag", "@@: No outcome tag set — node cannot compile.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	// Spawn intermediate CallFunction → CompleteObjectiveWithOutcome(FGameplayTag)
	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->FunctionReference.SetSelfMember(FName(TEXT("CompleteObjectiveWithOutcome")));
	CallNode->AllocateDefaultPins();

	// Wire execution
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *CallNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then), *CallNode->GetThenPin());

	// Export the UPROPERTY FGameplayTag to the function's parameter pin
	UEdGraphPin* CallTagPin = CallNode->FindPin(TEXT("OutcomeTag"));
	if (CallTagPin)
	{
		FString ExportedValue;
		FGameplayTag::StaticStruct()->ExportText(ExportedValue, &OutcomeTag, nullptr, nullptr, PPF_None, nullptr);
		CallTagPin->DefaultValue = ExportedValue;
	}

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
