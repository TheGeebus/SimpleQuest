// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_ContentBase.h"
#include "QuestlineNode_LinkedQuestline.generated.h"

class UQuestlineGraph;

/**
 * References an external UQuestlineGraph asset. Compiler-only — erased at compile time via bidirectional wiring pass.
 * Has no corresponding runtime class.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_LinkedQuestline : public UQuestlineNode_ContentBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
	virtual void PostPlacedNewNode() override;

	/** LinkedQuestline identity is the referenced asset's FriendlyName (or short name). No per-instance label — the
		title is fully derived, so inline rename is disabled to prevent designer confusion from edits that have no
		visible effect on GetNodeTitle. To change the displayed name, edit FriendlyName on the referenced asset. */
	virtual bool GetCanRenameNode() const override { return false; }
	
	/** The external questline graph asset this node references. */
	UPROPERTY(EditAnywhere, Category = "Quest")
	TSoftObjectPtr<UQuestlineGraph> LinkedGraph;

	/** Scans the linked graph's Exit nodes and syncs QuestOutcome output pins to match. */
	void RebuildOutcomePinsFromLinkedGraph();
};
