// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "GameplayTagContainer.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node_CompleteObjectiveWithOutcome.generated.h"

/**
 * Blueprint node that completes the owning UQuestObjective with a designer-specified outcome tag.
 *
 * All instances of this node in an objective Blueprint are automatically discovered and reflected as outcome output pins
 * on the corresponding Step node in the questline graph editor. This eliminates manual synchronization between the PossibleOutcomes
 * CDO array and the actual completion call sites.
 *
 * Only appears in UQuestObjective subclass Blueprints.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UK2Node_CompleteObjectiveWithOutcome : public UK2Node
{
	GENERATED_BODY()

public:
	/** The outcome this node fires. Must be under Quest.Outcome.* */
	UPROPERTY(EditAnywhere, Category = "Outcome", meta = (Categories = "Quest.Outcome"))
	FGameplayTag OutcomeTag;

	/** Marks the cached title text as dirty so the next GetNodeTitle call rebuilds it. */
	void InvalidateCachedTitle() { CachedNodeTitle.MarkDirty(); }

	// — UEdGraphNode —
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

	// — UK2Node —
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual bool IsActionFilteredOut(const FBlueprintActionFilter& Filter) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;

private:
	FNodeTextCache CachedNodeTitle;
};
