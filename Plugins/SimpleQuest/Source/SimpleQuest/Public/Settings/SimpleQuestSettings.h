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
	 * Quest manager to load when the game starts. Defaults to the native UQuestManagerSubsystem class; set to a
	 * Blueprint or C++ subclass derived from UQuestManagerSubsystem if you need to customize manager behavior.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Initialization")
	TSoftClassPtr<UQuestManagerSubsystem> QuestManagerClass = TSoftClassPtr<UQuestManagerSubsystem>(UQuestManagerSubsystem::StaticClass());

	/**
	 * Verbosity for LogSimpleQuest. Live-applied — changing this in Project Settings updates the log filter
	 * immediately without restart. Persists via UDeveloperSettings's Config save. Equivalent to setting
	 * `LogSimpleQuest=<verbosity>` in DefaultEngine.ini's [Core.Log] section, but designer-facing.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging")
	EQuestLogVerbosity LogSimpleQuestVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Verbosity for LogSimpleCore. Same live-apply behavior as LogSimpleQuestVerbosity. SimpleCore covers the
	 * underlying signal bus and world-state subsystems; raise to Verbose or VeryVerbose for diagnostic runs.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging")
	EQuestLogVerbosity LogSimpleCoreVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Pushes both verbosity values to their log categories. Called from PostEditChangeProperty for live-apply
	 * during editor sessions, and from FSimpleQuest::StartupModule on engine startup so settings take effect
	 * before any UE_LOG fires.
	 */
	void ApplyLogVerbosity() const;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA

	UPROPERTY(Config, EditAnywhere, Category="Colors|Wires")
	FLinearColor ActivationWireColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Colors|Wires")
	FLinearColor PrerequisiteWireColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Colors|Wires")
	FLinearColor OutcomeWireColor = FLinearColor::White;
	
	UPROPERTY(Config, EditAnywhere, Category="Colors|Wires")
	FLinearColor DeactivationWireColor = FLinearColor(0.9f, 0.4f, 0.3f);
	
	UPROPERTY(Config, EditAnywhere, Category="Colors|Wires")
	FLinearColor StaleWireColor = FLinearColor(1.f, 0.f, 0.f);

	UPROPERTY(Config, EditAnywhere, Category="Colors|Pins")
	FLinearColor DefaultPinColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor EntryNodeColor = FLinearColor(0.7f, 0.15f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor ExitNodeActiveColor = FLinearColor(0.9f, 0.7f, 0.1f); 

	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor ExitNodeInactiveColor = FLinearColor(0.6f, 0.6f, 0.6f);
	
	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor QuestNodeColor = FLinearColor(0.2f, 0.4f, 0.7f);

	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor StepNodeColor = FLinearColor(0.1f, 0.2f, 0.35f);
	
	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor LinkedQuestlineGraphNodeColor = FLinearColor(0.2f, 0.6f, 0.9f);
	
	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor ActivateGroupNodeColor = FLinearColor(0.9f, 0.7f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor PrerequisiteGroupNodeColor = FLinearColor(0.65f, 0.05f, 0.65f);

	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor UtilityNodeColor = FLinearColor(0.2f, 0.2f, 0.8f);
	
	UPROPERTY(Config, EditAnywhere, Category="Colors|Nodes")
	FLinearColor GraphOutcomeNodeColor = FLinearColor(0.3f, 0.7f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="Colors|Debug|Examiners|ActivationGroup")
	FLinearColor ExaminerGroupSetterColor = FLinearColor(0.85f, 0.65f, 0.15f);

	UPROPERTY(Config, EditAnywhere, Category="Colors|Debug|Examiners|ActivationGroup")
	FLinearColor ExaminerGroupGetterColor = FLinearColor(0.25f, 0.60f, 0.90f);

	/**
	 * Color used by the Group Examiner (and future examiners) to draw a hover-highlight border around a graph node when
	 * the designer hovers a corresponding row in the panel. Cross-editor — if the target node lives in another open editor,
	 * the highlight draws there too. Bright, distinctive colors read best since they need to contrast with arbitrary node
	 * backgrounds.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Colors|Debug|Examiners")
	FLinearColor HoverHighlightColor = FLinearColor(0.3f, 0.95f, 0.95f);
	
#endif
};

UCLASS()
class SIMPLEQUEST_API UGameInstanceSubsystemInitializer : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	virtual void Initialize(FSubsystemCollectionBase& Collection) override final;
};