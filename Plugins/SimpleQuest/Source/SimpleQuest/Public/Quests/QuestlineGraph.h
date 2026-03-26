// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "QuestlineGraph.generated.h"

class UEdGraph;

/**
 * A QuestlineGraph is the top-level authoring container for a series of linked quests.
 * It holds the outer Questline graph, which contains Quest nodes that can be
 * double-clicked to navigate into each Quest's inner Step graph.
 * This is the asset type the designer creates and opens in the visual graph editor.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestlineGraph : public UObject
{
	GENERATED_BODY()

public:

#if WITH_EDITORONLY_DATA
	/**
	 * The outer questline graph object. Contains Quest nodes and the wiring between them
	 */
	UPROPERTY()
	TObjectPtr<UEdGraph> QuestlineEdGraph;
#endif
};
