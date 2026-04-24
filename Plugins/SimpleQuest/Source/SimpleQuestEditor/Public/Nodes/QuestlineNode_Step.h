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
	
	/** The objective that defines how this step is completed. Required for compilation. */
	UPROPERTY(EditAnywhere, Category = "Step")
	TSoftClassPtr<UQuestObjective> ObjectiveClass;

	/** Optional reward granted on step completion. */
	UPROPERTY(EditAnywhere, Category = "Step")
	TSoftClassPtr<UQuestReward> RewardClass;

	UPROPERTY(EditAnywhere, Category = "Step")
	TArray<TSoftObjectPtr<AActor>> TargetActors;

	UPROPERTY(EditAnywhere, Category = "Step")
	TSet<TSoftClassPtr<AActor>> TargetClasses;
	
	UPROPERTY(EditAnywhere, Category = "Step")
	int32 NumberOfElements = 0;

	UPROPERTY(EditAnywhere, Category = "Step")
	EPrerequisiteGateMode PrerequisiteGateMode = EPrerequisiteGateMode::GatesProgression;

	/** Transient widget state — preserves expanded/collapsed detail view across widget rebuilds. Not serialized. */
	UPROPERTY(Transient)
	bool bStepDetailExpanded = false;

	UPROPERTY(Transient)
	bool bTargetActorsExpanded = false;

	UPROPERTY(Transient)
	bool bTargetClassesExpanded = false;
};
