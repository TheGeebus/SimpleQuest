// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestStepEnums.generated.h"

UENUM(BlueprintType)
enum class EPrerequisiteGateMode : uint8
{
	/** Trigger events are silently ignored while prerequisites are unmet. Player must interact again after prerequisites satisfy. */
	GatesProgression	UMETA(DisplayName = "Gate Progression"),

	/** Trigger events count normally. If completion conditions are already met when prerequisites satisfy, the step completes immediately. */
	GatesCompletion		UMETA(DisplayName = "Gate Completion"),
};