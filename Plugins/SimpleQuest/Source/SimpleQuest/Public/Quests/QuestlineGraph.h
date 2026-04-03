// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "QuestlineGraph.generated.h"

class UEdGraph;
class UQuest;

/**
 * A QuestlineGraph is the top-level authoring container for a series of linked quests. It holds the outer Questline graph,
 * which contains Quest nodes that can be double-clicked to navigate into each Quest's inner Step graph. This is the asset
 * type the designer creates and opens in the visual graph editor.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestlineGraph : public UObject
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler; 

public:
	virtual void PostLoad() override;

private:
	
	/**
	 * The tags representing the quests on this graph. Used for runtime lookups and event dispatch. Uses FName because we
	 * will need to register these on PostLoad.
	 */
	UPROPERTY()
	TArray<FName> CompiledQuestTags;

	/**
	 * The root quest classes for this questline — Quest nodes with no incoming connections. Populated by the compiler.
	 * Pass this questline asset to UQuestManagerSubsystem::InitialQuestlines instead of manually tracking individual quest classes.
	 */
	UPROPERTY()
	TArray<TSoftClassPtr<UQuest>> EntryQuests;
	
	/**
	 * Identifier used as the Gameplay Tag scope for all quests in this questline. Must be unique across the project. Defaults
	 * to the asset name if left empty. Override this when you need a stable tag namespace independent of the asset name,
	 * or to disambiguate duplicate assets.
	 *
	 * Format: Quest.<QuestlineID>.<QuestNodeLabel>
	 */
	UPROPERTY(EditAnywhere)
	FString QuestlineID;
	
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

#if WITH_EDITORONLY_DATA
public:	
	/**
	 * The questline graph object. Contains Quest nodes and the wiring between them.
	 */
	UPROPERTY()
	TObjectPtr<UEdGraph> QuestlineEdGraph;

#endif
};
