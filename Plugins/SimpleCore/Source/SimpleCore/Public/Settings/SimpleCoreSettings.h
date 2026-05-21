// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SimpleCoreSettings.generated.h"


/**
 * BP-friendly mirror of ELogVerbosity::Type for the log-category setting on USimpleCoreSettings. Maps 1:1 to UE's
 * underlying enum via ToELogVerbosity in SimpleCoreSettings.cpp; kept separate so the Project Settings dropdown
 * shows clean designer-facing labels without exposing the engine enum.
 */
UENUM(BlueprintType)
enum class ESimpleCoreLogVerbosity : uint8
{
	NoLogging	UMETA(DisplayName = "Off"),
	Fatal       UMETA(DisplayName = "Fatal"),
	Error       UMETA(DisplayName = "Error"),
	Warning     UMETA(DisplayName = "Warning"),
	Display     UMETA(DisplayName = "Display"),
	Log         UMETA(DisplayName = "Log"),
	Verbose     UMETA(DisplayName = "Verbose"),
	VeryVerbose UMETA(DisplayName = "Very Verbose"),
};


/**
 * Project Settings page for the SimpleCore foundation layer. Hosts the LogSimpleCore verbosity dial; future SimpleCore-
 * specific settings (signal-bus diagnostics, world-state thresholds, etc.) land here too as the layer grows.
 */
UCLASS(config=SimpleCore, DefaultConfig, meta=(DisplayName="Simple Core"))
class SIMPLECORE_API USimpleCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/**
	 * Verbosity for LogSimpleCore. The signal-bus and world-state subsystems' channel — covers subscribe / publish / dispatch
	 * traces and fact-store mutations. Raise to Verbose when debugging event delivery or fact lifecycle.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="SimpleCore"))
	ESimpleCoreLogVerbosity LogSimpleCoreVerbosity = ESimpleCoreLogVerbosity::Log;

	/**
	 * Pushes the verbosity value to LogSimpleCore. Called from PostEditChangeProperty for live-apply during editor sessions,
	 * and from FSimpleCoreModule::StartupModule on engine startup so settings take effect before any UE_LOG fires.
	 */
	void ApplyLogVerbosity() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};