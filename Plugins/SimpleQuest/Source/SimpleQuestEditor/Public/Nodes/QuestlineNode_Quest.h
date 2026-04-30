// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_ContentBase.h"
#include "Misc/Guid.h"
#include "QuestlineNode_Quest.generated.h"


UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_Quest : public UQuestlineNode_ContentBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	void RebuildOutcomePinsFromInnerGraph();

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PostPlacedNewNode() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostPasteNode() override;
	virtual void PostLoad() override;
	virtual FString GetDefaultNodeBaseName() const override { return TEXT("NewQuest"); }
	
protected:
	virtual void NotifyInnerGraphsOfRename() override;
	
private:
	void CreateInnerGraph();
	void SubscribeToInnerGraphChanges();
	void OnInnerGraphChanged(const FEdGraphEditAction& Action);

	UPROPERTY()
	TObjectPtr<UEdGraph> InnerGraph;

	FDelegateHandle InnerGraphChangedHandle;

public:
	FORCEINLINE UEdGraph* GetInnerGraph() const { return InnerGraph; }

	/**
	 * Walks Graph and regenerates identity on every inner content node (UEdGraphNode::NodeGuid +
	 * UQuestlineNode_ContentBase::QuestGuid). Recurses into nested Quest inner graphs and re-establishes
	 * their OnGraphChanged subscription + outcome-pin rebuild. Static so it can access SubscribeToInnerGraphChanges
	 * on nested Quest instances without promoting that method's access level.
	 *
	 * Called from PostPasteNode after paste deserializes the source's inner graph into this Quest — the deep-copied
	 * inner nodes still carry the source's identities, which the compiler would reject as duplicate-tag collisions.
	 */
	static void RegenerateInnerGraphIdentitiesRecursive(UEdGraph* Graph);
};
