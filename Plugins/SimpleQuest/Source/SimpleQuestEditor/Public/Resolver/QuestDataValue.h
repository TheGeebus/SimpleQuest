// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// PROTOTYPE — Resolver, Phase 3 Stage 1. The neutral, format-free cell value: a domain tagged-union that carries a leaf
// authored value STRUCTURALLY (a tag as an FGameplayTag, not the string "(TagName=..)") so a future format provider
// renders it in ITS representation without un-parsing UE-text. STRUCTURE (instanced objects, containers of them) stays as
// edges + child rows — NOT a cell value — so the union is leaf-only. See notes-07-phase3-stage1-code-spec.txt §1.
//
// The value carries ONLY the structured form — every provider (TSV, JSON, an adopter's) renders the on-disk string from
// the typed fields on write and reconstructs the typed field from its format on read. No format-specific string is
// cached on the value; the neutral type is genuinely format-free. Header-only.

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

enum class EQuestDataValueKind : uint8
{
	Empty,          // absent / at-CDO-default — import skips, leaves the constructed default
	Tag,            // FGameplayTag           — single tag (GroupTag, OutcomeTag, FactTag, Currency, RewardType)
	TagContainer,   // FGameplayTagContainer  — Facts, TargetQuestTags
	Text,           // FText                  — NodeLabel, DisplayName, Description (localization-preserving)
	String,         // FString/FName plain string — QuestlineID (+ structural class/graph cells)
	Number,         // int/int64/float/double/raw-uint8 numeric — Amount, ConditionPinCount, NumberOfElements
	Bool,           // bool                   — bShowDeactivationPins, bAlsoDeactivateTargets, bSuppressBroadcast
	Enum,           // uint8 UENUM            — ResettableReplay, PrerequisiteGateMode, BroadcastMode (token + numeric)
	Reference,      // soft object OR soft class — ObjectiveClass, LinkedGraph, LootTable (all soft), TargetActors/Classes elems
	Array,          // TArray OR TSet of the above — Elements hold the decomposed leaf values (container kind erased)
	StructLiteral,  // FInstancedStruct or any bare non-instanced FStructProperty — opaque ExportTextItem literal
};

// A single leaf cell value. Kind discriminates which structured field is meaningful (Tag / TagContainer / Text / bBool /
// EnumValue+token / StringForm / Elements). StringForm holds the string form for the string-carrying Kinds (String,
// Number, Enum token, Reference path, StructLiteral literal); the other Kinds carry a typed field a provider renders from.
struct FQuestDataValue
{
	EQuestDataValueKind Kind = EQuestDataValueKind::Empty;

	FGameplayTag           Tag;             // Kind==Tag
	FGameplayTagContainer  TagContainer;    // Kind==TagContainer
	FText                  Text;            // Kind==Text (a real FText — carries loc namespace/key)
	FString                StringForm;      // Kind==String/Number; also the Reference path / Enum token / StructLiteral literal
	bool                   bBool = false;   // Kind==Bool
	int64                  EnumValue = 0;   // Kind==Enum numeric (paired with StringForm = the enum token string)
	TArray<FQuestDataValue> Elements;       // Kind==Array

	// Value equality, for change detection. In-place re-import compares an incoming cell against a node's CURRENT value to
	// decide whether anything would actually change; without this, every re-import rewrites every property and reports a
	// diff for rows nobody touched. Only the field the Kind makes meaningful participates.
	// Text compares by its source string, NOT by localization identity: two FTexts with the same words but different keys
	// carry the same authored content, and treating a key difference as a content change would churn every localized field.
	// Array compares element-wise IN ORDER, which is correct for TArray. A TSet arrives here with its ordering already
	// erased, so a reordered set can read as changed — a false positive that costs a redundant write, never a missed one.
	bool operator==(const FQuestDataValue& Other) const
	{
		if (Kind != Other.Kind) return false;
		switch (Kind)
		{
		case EQuestDataValueKind::Empty:        return true;
		case EQuestDataValueKind::Tag:          return Tag == Other.Tag;
		case EQuestDataValueKind::TagContainer: return TagContainer == Other.TagContainer;
		case EQuestDataValueKind::Text:         return Text.ToString() == Other.Text.ToString();
		case EQuestDataValueKind::Bool:         return bBool == Other.bBool;
		case EQuestDataValueKind::Enum:         return EnumValue == Other.EnumValue && StringForm == Other.StringForm;
		case EQuestDataValueKind::Array:
			if (Elements.Num() != Other.Elements.Num()) return false;
			for (int32 Idx = 0; Idx < Elements.Num(); ++Idx) { if (!(Elements[Idx] == Other.Elements[Idx])) return false; }
			return true;
		default: return StringForm == Other.StringForm;   // String / Number / Reference / StructLiteral all carry StringForm
		}
	}
	bool operator!=(const FQuestDataValue& Other) const { return !(*this == Other); }

	static FQuestDataValue MakeEmpty() { return FQuestDataValue{}; }

	// Convenience constructors for the string-carrying Kinds — the routing core synthesizes cells (flow conventions)
	// and providers build them inline; both want the Kind + StringForm set in one expression.
	static FQuestDataValue MakeString(const FString& In) { FQuestDataValue V; V.Kind = EQuestDataValueKind::String; V.StringForm = In; return V; }
	static FQuestDataValue MakeNumber(const FString& In) { FQuestDataValue V; V.Kind = EQuestDataValueKind::Number; V.StringForm = In; return V; }
};