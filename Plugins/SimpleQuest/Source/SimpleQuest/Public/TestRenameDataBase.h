// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "TestRenameDataBase.generated.h"

/**
 * 
 */
UCLASS()
class SIMPLEQUEST_API UTestRenameDataBase : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FGameplayTag RenameTestTag;
};
