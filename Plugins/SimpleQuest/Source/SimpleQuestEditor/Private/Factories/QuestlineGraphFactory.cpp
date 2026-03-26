// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Factories/QuestlineGraphFactory.h"
#include "Quests/QuestlineGraph.h"

#include "Graph/QuestlineGraphSchema.h"

UQuestlineGraphFactory::UQuestlineGraphFactory()
{
	SupportedClass = UQuestlineGraph::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UQuestlineGraphFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UQuestlineGraph* NewAsset = NewObject<UQuestlineGraph>(InParent, InClass, InName, Flags);

#if WITH_EDITOR
	NewAsset->QuestlineEdGraph = NewObject<UEdGraph>(NewAsset, NAME_None, RF_Transactional);
	NewAsset->QuestlineEdGraph->Schema = UQuestlineGraphSchema::StaticClass();
	NewAsset->QuestlineEdGraph->GetSchema()->CreateDefaultNodesForGraph(*NewAsset->QuestlineEdGraph);
#endif

	return NewAsset;
}
