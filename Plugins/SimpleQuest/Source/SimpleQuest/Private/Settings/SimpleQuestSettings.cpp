#include "Settings/SimpleQuestSettings.h"

#include "SimpleQuestLog.h"
#include "Subsystems/QuestManagerSubsystem.h"

void UGameInstanceSubsystemInitializer::Initialize(FSubsystemCollectionBase& Collection)
{
	const USimpleQuestSettings* SimpleQuestSettings = GetDefault<USimpleQuestSettings>();

	// check if the quest manager is invalid
	UClass* NewManagerClass = SimpleQuestSettings->QuestManagerClass.LoadSynchronous();
	if (!NewManagerClass)
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UGameInstanceSubsystemInitializer::Initialize : QuestManagerClass is not valid, %s"), *GetFullName())
		return;
	}

	// initialize the quest manager
	UE_LOG(LogSimpleQuest, Log, TEXT("UGameInstanceSubsystemInitializer::Initialize : Initializing quest manager: %s"), *NewManagerClass->GetFullName());
	Collection.InitializeDependency(NewManagerClass);
}
