// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "QuestPlayerComponent.generated.h"


class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestPlayerComponent : public UQuestComponentBase
{
	GENERATED_BODY()

public:	
	UQuestPlayerComponent();

protected:
	virtual void BeginPlay() override;
	
	
};
