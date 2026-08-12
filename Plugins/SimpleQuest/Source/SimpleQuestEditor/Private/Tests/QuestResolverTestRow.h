// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// A row struct that exists only so a test can build a REAL UDataTable. Every resolver test to date hand-builds its
// FQuestDataBundle, which pins bundle -> plan -> apply and says nothing about the step before it. A hand-built bundle
// encodes what its author BELIEVED the reader produces; only a real read can contradict that belief.
//
// The fields are chosen to exercise the contract: 'type' is a discriminator column, 'label' a plain string, 'amount' a
// number left at its default in one row, and 'outcome' a tag left unset — the last two pinning that a DataTable row
// yields cells that are PRESENT with Kind::Empty rather than absent, a distinction no text source can express.
//
// NATIVE, not Blueprint, so GetAuthoredNameForField() == GetFName() here. That is a LIMIT of this fixture rather than a
// property of the system: those two diverge only for UserDefinedStructs, and that divergence needs its own test.

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "QuestResolverTestRow.generated.h"

USTRUCT()
struct FQuestResolverTestRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY()
	FString type;

	UPROPERTY()
	FString label;

	UPROPERTY()
	int32 amount = 0;

	UPROPERTY()
	FGameplayTag outcome;
};

