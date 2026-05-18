#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/QuestManagerSubsystem.h"
#include "SimpleQuestSettings.generated.h"


/**
 * BP-friendly mirror of ELogVerbosity::Type for the log-category settings on USimpleQuestSettings. Maps 1:1 to UE's
 * underlying enum via ToELogVerbosity in SimpleQuestSettings.cpp; kept separate so the Project Settings dropdown
 * shows clean designer-facing labels without exposing the engine enum.
 */
UENUM(BlueprintType)
enum class EQuestLogVerbosity : uint8
{
	NoLogging   UMETA(DisplayName = "Off"),
	Fatal       UMETA(DisplayName = "Fatal"),
	Error       UMETA(DisplayName = "Error"),
	Warning     UMETA(DisplayName = "Warning"),
	Display     UMETA(DisplayName = "Display"),
	Log         UMETA(DisplayName = "Log"),
	Verbose     UMETA(DisplayName = "Verbose"),
	VeryVerbose UMETA(DisplayName = "Very Verbose"),
};


UCLASS(config=SimpleQuest, DefaultConfig, meta=(DisplayName="Simple Quest"))
class SIMPLEQUEST_API USimpleQuestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/**
	 * Quest manager class loaded when the game starts. Set to a Blueprint or C++ subclass of UQuestManagerSubsystem
	 * to customize manager behavior; defaults to the native UQuestManagerSubsystem.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Initialization", meta=(DisplayName="Quest Manager"))
	TSoftClassPtr<UQuestManagerSubsystem> QuestManagerClass = TSoftClassPtr<UQuestManagerSubsystem>(UQuestManagerSubsystem::StaticClass());

	/**
	 * Manager activation flow — cascade entry, chain advancement, live-state writers, container Live derivation.
	 * Raise to Verbose when debugging "why isn't my quest going Live" or chain-advancement issues.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Activation"))
	EQuestLogVerbosity LogSimpleQuestActivationVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Subscriber wiring — Observer/Trigger/Giver registration, K2 node subscriptions, catch-up fanout, prereq-leaf
	 * enablement watches. Raise to Verbose when debugging "bound but never fires" or catch-up replay behavior.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Subscription"))
	EQuestLogVerbosity LogSimpleQuestSubscriptionVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Compile-time output — graph compile, native tag registration, rename-redirect machinery, stale-tag warnings.
	 * Bulk of the historical Verbose noise; raise selectively when investigating compile or rename behavior.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Compiler"))
	EQuestLogVerbosity LogSimpleQuestCompilerVerbosity = EQuestLogVerbosity::Log;

	/**
	 * UQuestStateSubsystem registry mutations — resolutions, entries, tag/alias registrations. The durable record of
	 * what happened (becomes load-bearing once save/load lands in 0.5.0). Raise to Verbose when debugging "what's
	 * persisted vs ephemeral."
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="State"))
	EQuestLogVerbosity LogSimpleQuestStateVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Umbrella channel — module startup, settings, debug overlay, and anything not covered by the specialized channels
	 * above. Live-applied: changes take effect immediately without restart.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Module"))
	EQuestLogVerbosity LogSimpleQuestVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Pushes every verbosity value to its log category. Called from PostEditChangeProperty for live-apply during editor
	 * sessions, and from FSimpleQuest::StartupModule on engine startup so settings take effect before any UE_LOG fires.
	 */
	void ApplyLogVerbosity() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};


UCLASS()
class SIMPLEQUEST_API UGameInstanceSubsystemInitializer : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	virtual void Initialize(FSubsystemCollectionBase& Collection) override final;
};