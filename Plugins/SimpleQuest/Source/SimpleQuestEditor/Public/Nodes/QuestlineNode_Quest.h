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


	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void PostPlacedNewNode() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual FString GetDefaultNodeBaseName() const override { return TEXT("Quest"); }

private:
	void CreateInnerGraph();
	
	UPROPERTY()
	TObjectPtr<UEdGraph> InnerGraph;

public:
	FORCEINLINE UEdGraph* GetInnerGraph() const { return InnerGraph; }
};
