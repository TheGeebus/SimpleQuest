// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

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

	/**
	 * Take ownership of an inner graph that arrived by COPY rather than by creation - paste deserialized it, or object
	 * duplication carried it across. Re-establishes the things a copy cannot bring with it: the schema, the change
	 * subscription, the outcome pins derived from the inner Exits, and fresh identity on every descendant.
	 *
	 * bClearImportKeys is what separates the two callers. A PASTED node is a new node and must not keep answering to the
	 * source row its original was imported under - two nodes in one asset claiming one row is unresolvable on re-import.
	 * A DUPLICATED ASSET is a copy of the whole provenance and keeps it.
	 */
	void AdoptCopiedInnerGraph(bool bClearImportKeys);

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
	static void RegenerateInnerGraphIdentitiesRecursive(UEdGraph* Graph, bool bClearImportKeys);
};
