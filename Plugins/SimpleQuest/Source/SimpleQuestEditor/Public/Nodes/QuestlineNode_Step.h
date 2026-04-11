#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Quests/Types/QuestStepEnums.h"
#include "QuestlineNode_Step.generated.h"

class UQuestObjective;
class UQuestReward;

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_Step : public UQuestlineNode_ContentBase
{
	GENERATED_BODY()

public:
	virtual void AllocateOutcomePins() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void RefreshOutcomePins();

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FString GetDefaultNodeBaseName() const override { return TEXT("Step"); }
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;
	
	/** The objective that defines how this step is completed. Required for compilation. */
	UPROPERTY(EditAnywhere, Category = "Step")
	TSubclassOf<UQuestObjective> ObjectiveClass;

	/** Optional reward granted on step completion. */
	UPROPERTY(EditAnywhere, Category = "Step")
	TSubclassOf<UQuestReward> RewardClass;

	UPROPERTY(EditAnywhere, Category = "Step")
	TArray<TSoftObjectPtr<AActor>> TargetActors;

	UPROPERTY(EditAnywhere, Category = "Step")
	TSet<TSubclassOf<AActor>> TargetClasses;

	UPROPERTY(EditAnywhere, Category = "Step")
	int32 NumberOfElements = 0;

	UPROPERTY(EditAnywhere, Category = "Step")
	FVector TargetVector = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Step")
	EPrerequisiteGateMode PrerequisiteGateMode = EPrerequisiteGateMode::GatesProgression;
	
private:
	static FText MakeOutcomePinLabel(const FGameplayTag& Tag);

};
