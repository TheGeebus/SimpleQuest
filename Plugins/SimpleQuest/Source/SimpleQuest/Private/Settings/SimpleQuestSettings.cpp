#include "Settings/SimpleQuestSettings.h"

#include "SimpleQuestLog.h"
#include "Subsystems/QuestManagerSubsystem.h"
#include "Utilities/SimpleCoreLog.h"

namespace
{
	ELogVerbosity::Type ToELogVerbosity(EQuestLogVerbosity Verbosity)
	{
		switch (Verbosity)
		{
		case EQuestLogVerbosity::NoLogging:   return ELogVerbosity::NoLogging;
		case EQuestLogVerbosity::Fatal:       return ELogVerbosity::Fatal;
		case EQuestLogVerbosity::Error:       return ELogVerbosity::Error;
		case EQuestLogVerbosity::Warning:     return ELogVerbosity::Warning;
		case EQuestLogVerbosity::Display:     return ELogVerbosity::Display;
		case EQuestLogVerbosity::Log:         return ELogVerbosity::Log;
		case EQuestLogVerbosity::Verbose:     return ELogVerbosity::Verbose;
		case EQuestLogVerbosity::VeryVerbose: return ELogVerbosity::VeryVerbose;
		default:                              return ELogVerbosity::Log;
		}
	}
}

void USimpleQuestSettings::ApplyLogVerbosity() const
{
	LogSimpleQuest.SetVerbosity(ToELogVerbosity(LogSimpleQuestVerbosity));
	LogSimpleQuestActivation.SetVerbosity(ToELogVerbosity(LogSimpleQuestActivationVerbosity));
	LogSimpleQuestCompiler.SetVerbosity(ToELogVerbosity(LogSimpleQuestCompilerVerbosity));
	LogSimpleQuestSubscription.SetVerbosity(ToELogVerbosity(LogSimpleQuestSubscriptionVerbosity));
	LogSimpleQuestState.SetVerbosity(ToELogVerbosity(LogSimpleQuestStateVerbosity));
	LogSimpleCore.SetVerbosity(ToELogVerbosity(LogSimpleCoreVerbosity));
}

#if WITH_EDITOR
void USimpleQuestSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyLogVerbosity();
}
#endif

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

	// Register the quest manager
	UE_LOG(LogSimpleQuest, Log, TEXT("Designated quest manager class: %s (creation gated by ShouldCreateSubsystem)"), *NewManagerClass->GetFullName());
	Collection.InitializeDependency(NewManagerClass);
}