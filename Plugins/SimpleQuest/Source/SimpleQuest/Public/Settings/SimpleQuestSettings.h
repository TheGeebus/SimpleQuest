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

#if WITH_EDITORONLY_DATA
	UPROPERTY(Config, EditAnywhere, Category="Wire Colors")
	FLinearColor ActivationWireColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Wire Colors")
	FLinearColor PrerequisiteWireColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Wire Colors")
	FLinearColor OutcomeWireColor = FLinearColor::White;
	
	UPROPERTY(Config, EditAnywhere, Category="Wire Colors")
	FLinearColor DeactivationWireColor = FLinearColor(0.9f, 0.4f, 0.3f);
	
	UPROPERTY(Config, EditAnywhere, Category="Wire Colors")
	FLinearColor StaleWireColor = FLinearColor(1.f, 0.f, 0.f);

	UPROPERTY(Config, EditAnywhere, Category="Pin Colors")
	FLinearColor DefaultPinColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor EntryNodeColor = FLinearColor(0.7f, 0.15f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor ExitNodeActiveColor = FLinearColor(0.9f, 0.7f, 0.1f); 

	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor ExitNodeInactiveColor = FLinearColor(0.6f, 0.6f, 0.6f);
	
	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor QuestNodeColor = FLinearColor(0.2f, 0.4f, 0.7f);

	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor StepNodeColor = FLinearColor(0.1f, 0.2f, 0.35f);
	
	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor LinkedQuestlineGraphNodeColor = FLinearColor(0.2f, 0.6f, 0.9f);
	
	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor ActivateGroupNodeColor = FLinearColor(0.9f, 0.7f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor PrerequisiteGroupNodeColor = FLinearColor(0.65f, 0.05f, 0.65f);

	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor UtilityNodeColor = FLinearColor(0.2f, 0.2f, 0.8f);
	
	UPROPERTY(Config, EditAnywhere, Category="Node Colors")
	FLinearColor GraphOutcomeNodeColor = FLinearColor(0.3f, 0.7f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="ExaminerWidgets|ActivationGroup")
	FLinearColor ExaminerGroupSetterColor = FLinearColor(0.85f, 0.65f, 0.15f);

	UPROPERTY(Config, EditAnywhere, Category="ExaminerWidgets|ActivationGroup")
	FLinearColor ExaminerGroupGetterColor = FLinearColor(0.25f, 0.60f, 0.90f);

	/**
	 * Color used by the Group Examiner (and future examiners) to draw a hover-highlight border around a graph node when
	 * the designer hovers a corresponding row in the panel. Cross-editor — if the target node lives in another open editor,
	 * the highlight draws there too. Bright, distinctive colors read best since they need to contrast with arbitrary node
	 * backgrounds.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Simple Quest|Colors")
	FLinearColor HoverHighlightColor = FLinearColor(0.3f, 0.95f, 0.95f);
	
#endif
};

UCLASS()
class SIMPLEQUEST_API UGameInstanceSubsystemInitializer : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	virtual void Initialize(FSubsystemCollectionBase& Collection) override final;
};