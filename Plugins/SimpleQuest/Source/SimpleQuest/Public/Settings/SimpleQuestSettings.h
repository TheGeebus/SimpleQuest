#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SimpleQuestSettings.generated.h"

class UQuestManagerSubsystem;

UCLASS(config=SimpleQuest, DefaultConfig, meta=(DisplayName="Simple Quest"))
class SIMPLEQUEST_API USimpleQuestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	
	/** Quest manager to load when the game starts. Blueprints or C++ class derived from UQuestManagerSubsystem. */
	UPROPERTY(Config, EditAnywhere, Category="Initialization")
	TSoftClassPtr<UQuestManagerSubsystem> QuestManagerClass = TSoftClassPtr<UQuestManagerSubsystem>(FSoftObjectPath(TEXT("/SimpleQuest/BP_QuestManager.BP_QuestManager_C")));
};

UCLASS()
class SIMPLEQUEST_API UGameInstanceSubsystemInitializer : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	virtual void Initialize(FSubsystemCollectionBase& Collection) override final;
};