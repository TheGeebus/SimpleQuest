#pragma once

#include "CoreMinimal.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "QuestlineNode_Step.generated.h"

class UQuestObjective;
class UQuestReward;

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_Step : public UQuestlineNode_ContentBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FString GetDefaultNodeBaseName() const override { return TEXT("Step"); }

	/** The objective that defines how this step is completed. Required for compilation. */
	UPROPERTY(EditAnywhere, Category = "Step")
	TSubclassOf<UQuestObjective> ObjectiveClass;

	/** Optional reward granted on step completion. */
	UPROPERTY(EditAnywhere, Category = "Step")
	TSubclassOf<UQuestReward> RewardClass;

	UPROPERTY(EditAnywhere, Category = "Step")
	TArray<TSoftObjectPtr<AActor>> TargetActors;

	UPROPERTY(EditAnywhere, Category = "Step")
	TSubclassOf<AActor> TargetClass;

	UPROPERTY(EditAnywhere, Category = "Step")
	int32 NumberOfElements = 0;

	UPROPERTY(EditAnywhere, Category = "Step")
	FVector TargetVector = FVector::ZeroVector;
};
