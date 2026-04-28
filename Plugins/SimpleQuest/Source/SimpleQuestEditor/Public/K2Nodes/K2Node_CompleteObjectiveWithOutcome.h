// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "GameplayTagContainer.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node_CompleteObjectiveWithOutcome.generated.h"

/**
 * Blueprint node that completes the owning UQuestObjective with an outcome tag and an optional structural path
 * identity. All instances are automatically discovered and reflected as exec output pins on the corresponding Step
 * node in the questline graph editor — no manual sync between outcome / path declarations and completion call sites.
 *
 * Two authoring modes:
 *   Static  — leave PathName empty. Set the OutcomeTag via the pin's tag picker (filtered to SimpleQuest.QuestOutcome).
 *             PathIdentity auto-derives from OutcomeTag.GetTagName(); the Step's exec pin shows the outcome leaf as
 *             its label. This is the common case and matches the pre-Bundle-Y authoring shape.
 *   Dynamic — set PathName to a short authored identity (e.g., "DynamicVictory"). Wire a runtime FGameplayTag into
 *             the OutcomeTag pin to override the static value at completion time. The Step's exec pin uses PathName
 *             as its identity, so routing is stable regardless of what runtime tag flows through.
 *
 * Only appears in UQuestObjective subclass Blueprints.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UK2Node_CompleteObjectiveWithOutcome : public UK2Node
{
	GENERATED_BODY()

public:
	/**
	 * Optional structural path identity for this completion route. Empty = static placement (PathIdentity auto-derives
	 * from the OutcomeTag pin's value). Non-empty = dynamic placement (PathIdentity is this name; OutcomeTag pin can
	 * be wired to a runtime tag value). Required when the OutcomeTag pin is wired.
	 */
	UPROPERTY(EditAnywhere, Category = "Path")
	FName PathName;
	
	/**
	 * Resolves the path identity for this placement using the priority cascade:
	 *   1. Author-specified PathName (dynamic with explicit identity)
	 *   2. "Dynamic" sentinel if OutcomeTag pin is wired but no PathName authored (auto-fallback)
	 *   3. OutcomeTag pin's static DefaultValue tag name (static placement)
	 *   4. NAME_None (misconfigured — no PathName, no wire, no static tag)
	 *
	 * Used by ExpandNode (passes to CompleteObjectiveWithOutcome's PathIdentity arg), GetNodeTitle (drives
	 * the on-node title), and DiscoverObjectivePaths (Step pin generation). Single source of truth.
	 */
	FName ResolvePathIdentity() const;

	/**
	 * Idempotent allocation of DynamicIndex for the wired-without-PathName case. If the node is currently in
	 * dynamic-without-PathName state AND DynamicIndex is unallocated (or collides with a sibling K2 placement
	 * in this BP), assigns the lowest-available non-negative integer not in use by any sibling.
	 *
	 * Called from PinConnectionListChanged (wire connect on OutcomeTag), PostPlacedNewNode (paste/duplicate),
	 * and the Slate widget's OnPathNameCommitted (PathName cleared). Sticky across reconstruct: once allocated,
	 * the index persists even if the node briefly leaves dynamic mode (e.g., wire disconnected) so re-entering
	 * dynamic mode reuses the same identity.
	 */
	void EnsureDynamicIndexAllocated();

	// — UEdGraphNode —
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostPlacedNewNode() override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	// — UK2Node —
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual bool IsActionFilteredOut(const FBlueprintActionFilter& Filter) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;

	/**
	 * Filter the OutcomeTag pin's gameplay tag picker to the SimpleQuest.QuestOutcome namespace. UE's struct-pin
	 * customization for FGameplayTag reads the "Categories" metadata via this override.
	 */
	virtual FString GetPinMetaData(FName InPinName, FName InKey) override;
	virtual void PostLoad() override;

	void InvalidateCachedTitle() { CachedNodeTitle.MarkDirty(); }

private:
	FNodeTextCache CachedNodeTitle;
	
	/** Migration storage for pre-Bundle-Y assets. PostLoad copies this to the OutcomeTag pin's DefaultValue
	 *  if the pin's DefaultValue is empty, then leaves the field as-is (subsequent loads no-op since the pin
	 *  has the value). Could be removed in 0.5.x once all assets have been touched. */
	UPROPERTY()
	FGameplayTag OutcomeTag_DEPRECATED;

	
	/**
	 * Stable disambiguator for dynamic-without-PathName placements. INDEX_NONE = unallocated. Once assigned
	 * (via EnsureDynamicIndexAllocated), persists across the node's lifetime so the resolved path identity
	 * stays stable. ResolvePathIdentity formats this as "Dynamic <N+1>" — DynamicIndex 0 → "Dynamic 1",
	 * DynamicIndex 1 → "Dynamic 2", etc. Uniform numbering (no off-by-one for singletons) keeps the
	 * numbering legible regardless of how many sibling placements exist.
	 */
	UPROPERTY()
	int32 DynamicIndex = INDEX_NONE;
};