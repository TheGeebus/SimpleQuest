// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The adopter's translation config: how a studio's own-shaped source data (usually a flat table whose rows are different
// node kinds distinguished by a "type" column) maps onto questline node properties. Two things: (1) which node kind each
// row is — a discriminator column + a value->class table; (2) which source column fills which node property — a flat
// binding list, bound once, each binding applying to every node kind that actually has a property by that name.

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Nodes/QuestlineNodeBase.h"
#include "QuestImportMapping.generated.h"

// On re-import into an existing asset, whether an absent/unmapped cell keeps the graph's current value or resets it.
// Teeth only in the in-place round-trip (a fresh import has no prior value); on a fresh import Preserve == Reset.
UENUM()
enum class EQuestAbsentFieldPolicy : uint8
{
	Preserve UMETA(DisplayName = "Preserve graph value"),		// default: absent -> leave the current value untouched
	Reset    UMETA(DisplayName = "Reset to default"),			// absent -> write the property's class default
	Require  UMETA(DisplayName = "Require (error if absent)")	// absent -> refuse the import (no partial asset)
};

// One source column -> one node property. Applies to a row only when the row's resolved node class has a property of
// this name (bind once, land where it fits). Every field is a stable identifier the picker populates.
USTRUCT()
struct FQuestColumnBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Binding")
	FName SourceColumn;

	UPROPERTY(EditAnywhere, Category = "Binding")
	FName TargetProperty;

	UPROPERTY(EditAnywhere, Category = "Binding")
	EQuestAbsentFieldPolicy AbsentPolicy = EQuestAbsentFieldPolicy::Preserve;
};

// One node kind: the class rows of this kind become, plus every discriminator value that routes to it. A studio's source may
// use several synonyms for one kind (e.g. "objective" and "task" both meaning Step); all live in Values. PrimaryValue is the
// one written back on reverse-export (the inverse of the many-to-one value->class map) — it MUST be one of Values.
USTRUCT()
struct FQuestDiscriminatorClass
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Row Kind")
	TSoftClassPtr<UQuestlineNodeBase> NodeClass;

	// Every discriminator value that routes to NodeClass. Usually one; more when a studio uses synonyms for one kind.
	UPROPERTY(EditAnywhere, Category = "Row Kind")
	TArray<FString> Values;

	// The value written back for this class on reverse-export (must be one of Values). Empty -> the first value is used.
	UPROPERTY(EditAnywhere, Category = "Row Kind")
	FString PrimaryValue;
};

UCLASS(BlueprintType)
class SIMPLEQUESTEDITOR_API UQuestImportMapping : public UDataAsset
{
	GENERATED_BODY()

public:
	// ── Which node kind is each row? ─────────────────────────────────────────────────────────────────
	// The source column whose value names each row's node kind (the studio's "type"/"kind" column).
	UPROPERTY(EditAnywhere, Category = "Row Kind")
	FName DiscriminatorColumn;

	// Each node kind + the discriminator value(s) that route to it. One class may claim several values (studio synonyms).
	// A source discriminator value not covered by any entry here is an error at import (rows can't be silently dropped).
	UPROPERTY(EditAnywhere, Category = "Row Kind")
	TArray<FQuestDiscriminatorClass> DiscriminatorClasses;

	// The source column holding each row's semantic key (the studio's "id"/"quest_key" column). Optional. On import the key
	// is read from column 0 by the provider regardless; this records WHICH column that is so reverse-export writes the
	// preserved source key back into the studio's own key column. Empty -> reverse-export keys default to the exported GUID.
	UPROPERTY(EditAnywhere, Category = "Row Kind")
	FName KeyColumn;

	// ── Which source column fills which property? ────────────────────────────────────────────────────
	// Flat binding list, bound once. Each applies to every resolved class that has a matching property.
	UPROPERTY(EditAnywhere, Category = "Bindings")
	TArray<FQuestColumnBinding> Bindings;

	// Absent-case policy for a property that has no binding at all.
	UPROPERTY(EditAnywhere, Category = "Bindings")
	EQuestAbsentFieldPolicy DefaultAbsentPolicy = EQuestAbsentFieldPolicy::Preserve;

	// In-place re-import: keep nodes that exist in the asset but have no source row (the default keeps them).
	UPROPERTY(EditAnywhere, Category = "In-Place")
	bool bDeleteOrphanedNodes = false;
};