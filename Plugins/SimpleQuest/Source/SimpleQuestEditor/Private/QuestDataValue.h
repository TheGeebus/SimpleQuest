// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// PROTOTYPE — Resolver, Phase 3 Stage 1. The neutral, format-free cell value: a domain tagged-union that carries a leaf
// authored value STRUCTURALLY (a tag as an FGameplayTag, not the string "(TagName=..)") so a future format provider
// renders it in ITS representation without un-parsing UE-text. STRUCTURE (instanced objects, containers of them) stays as
// edges + child rows — NOT a cell value — so the union is leaf-only. See notes-07-phase3-stage1-code-spec.txt §1.
//
// Stage 1 carries BOTH the structured value (for Stage-2 providers) AND the canonical UE-text string the default TSV
// format emits (CanonicalText) — captured at build time straight from ExportTextItem, so the TSV round-trip is
// byte-identical to the pre-Stage-1 output by CONSTRUCTION (no reconstruction to verify). Header-only.

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

enum class EQuestDataValueKind : uint8
{
	Empty,          // absent / at-CDO-default — import skips, leaves the constructed default (Q6 rule)
	Tag,            // FGameplayTag           — single tag (GroupTag, OutcomeTag, FactTag, Currency, RewardType)
	TagContainer,   // FGameplayTagContainer  — Facts, TargetQuestTags
	Text,           // FText                  — NodeLabel, DisplayName, Description (loc-preserving)
	Scalar,         // int/int64/float/double/FName/FString/raw-uint8 — QuestlineID, Amount, ConditionPinCount, NumberOfElements
	Bool,           // bool                   — bShowDeactivationPins, bAlsoDeactivateTargets, bSuppressBroadcast
	Enum,           // uint8 UENUM            — ResettableReplay, PrerequisiteGateMode, BroadcastMode (token + numeric)
	Reference,      // soft object OR soft class — ObjectiveClass, LinkedGraph, LootTable (all soft), TargetActors/Classes elems
	Array,          // TArray OR TSet of the above — Elements hold the decomposed leaf values (container kind erased)
	StructLiteral,  // FInstancedStruct or any bare non-instanced FStructProperty — opaque ExportTextItem literal
};

// A single leaf cell value. Kind discriminates which structured field is meaningful (Tag / TagContainer / Text / bBool /
// EnumValue+token / Scalar-string / Elements). CanonicalText is the default-TSV-format string for this value, captured at
// build time from the exact ExportTextItem/FTextStringHelper call the pre-Stage-1 code used — so the TSV write path is
// byte-identical without reconstructing the string from the typed fields.
struct FQuestDataValue
{
	EQuestDataValueKind Kind = EQuestDataValueKind::Empty;

	FGameplayTag           Tag;             // Kind==Tag
	FGameplayTagContainer  TagContainer;    // Kind==TagContainer
	FText                  Text;            // Kind==Text (a real FText — carries loc namespace/key)
	FString                Scalar;          // Kind==Scalar; also the Reference path / Enum token / StructLiteral literal
	bool                   bBool = false;   // Kind==Bool
	int64                  EnumValue = 0;   // Kind==Enum numeric (paired with Scalar = the enum token string)
	TArray<FQuestDataValue> Elements;       // Kind==Array

	FString CanonicalText;                  // the default-TSV string form (empty for Kind==Empty)

	static FQuestDataValue MakeEmpty() { return FQuestDataValue{}; }
};