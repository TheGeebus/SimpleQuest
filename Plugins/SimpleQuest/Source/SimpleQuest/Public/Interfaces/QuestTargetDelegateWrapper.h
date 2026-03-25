#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "QuestTargetDelegateWrapper.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQuestTriggerDelegate, UObject*, InTargetObject);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FQuestTargetKilledDelegate, AActor*, KilledActor, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestTargetInteractedDelegate, AActor*, TargetActor, AActor*, InstigatingActor);

UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuestTargetDelegateWrapper : public UObject
{
	GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FQuestTriggerDelegate OnQuestTargetTriggered;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FQuestTargetKilledDelegate OnQuestTargetKilled;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestTargetInteractedDelegate OnQuestTargetInteracted;
};