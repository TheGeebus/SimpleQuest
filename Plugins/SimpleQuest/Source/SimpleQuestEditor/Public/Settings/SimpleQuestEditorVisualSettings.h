// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SimpleQuestEditorVisualSettings.generated.h"


/**
 * Editor Preferences page for the SimpleQuest plugin's editor-time visual palette — wire colors, pin colors, node title
 * colors, and debug-highlight colors used by the Questline graph editor. Per-developer (saved to the user's local
 * EditorPerProjectUserSettings.ini, not source-controlled); class-header defaults are the canonical palette that ships
 * with the plugin. Lives in Editor Preferences rather than Project Settings because these knobs aren't meant for adopter
 * tweaking — the palette is a categorization vocabulary the plugin standardizes on.
 *
 * TO BE REMOVED ONCE THE COLOR SCHEME IS FINALIZED
 */
UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Simple Quest Visuals"))
class SIMPLEQUESTEDITOR_API USimpleQuestEditorVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return FName("Editor"); }
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	// ----- Wires -----

	UPROPERTY(Config, EditAnywhere, Category="Wires")
	FLinearColor ActivationWireColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Wires")
	FLinearColor PrerequisiteWireColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="Wires")
	FLinearColor OutcomeWireColor = FLinearColor(1.000000f, 0.734210f, 0.061106f);

	UPROPERTY(Config, EditAnywhere, Category="Wires")
	FLinearColor DeactivationWireColor = FLinearColor(0.900000f, 0.176436f, 0.095931f);

	UPROPERTY(Config, EditAnywhere, Category="Wires")
	FLinearColor StaleWireColor = FLinearColor(1.f, 0.f, 0.f);

	// ----- Pins -----

	UPROPERTY(Config, EditAnywhere, Category="Pins")
	FLinearColor DefaultPinColor = FLinearColor::White;

	// ----- Nodes -----

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor EntryNodeColor = FLinearColor(0.7f, 0.15f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor ExitNodeActiveColor = FLinearColor(0.900000f, 0.249375f, 0.000000f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor ExitNodeInactiveColor = FLinearColor(0.6f, 0.6f, 0.6f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor QuestNodeColor = FLinearColor(0.114586f, 0.069608f, 0.700000f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor StepNodeColor = FLinearColor(0.036534f, 0.135833f, 0.350000f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor LinkedQuestlineGraphNodeColor = FLinearColor(0.000000f, 0.772680f, 0.900000f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor ActivateGroupNodeColor = FLinearColor(0.090496f, 0.900000f, 0.011218f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor PrerequisiteGroupNodeColor = FLinearColor(0.65f, 0.05f, 0.65f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor GraphOutcomeNodeColor = FLinearColor(0.3f, 0.7f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="Nodes")
	FLinearColor UtilityNodeColor = FLinearColor(0.800000f, 0.302498f, 0.005179f);

	// ----- Debug Highlights -----

	UPROPERTY(Config, EditAnywhere, Category="Debug Highlights")
	FLinearColor ExaminerGroupSetterColor = FLinearColor(0.85f, 0.65f, 0.15f);

	UPROPERTY(Config, EditAnywhere, Category="Debug Highlights")
	FLinearColor ExaminerGroupGetterColor = FLinearColor(0.25f, 0.60f, 0.90f);

	UPROPERTY(Config, EditAnywhere, Category="Debug Highlights")
	FLinearColor HoverHighlightColor = FLinearColor(0.3f, 0.95f, 0.95f);
};