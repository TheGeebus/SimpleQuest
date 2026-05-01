// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * Tag classifier. Every tag in the SimpleQuest namespace fits exactly one of these kinds.
 * LegacyOutcome covers the transitional Quest.Outcome.* prefix retained as a defensive guard
 * during the 0.4.0 namespace migration; will retire once every authored asset has been recompiled.
 */
enum class EQuestTagKind : uint8
{
	Unknown,
	Identity,           // SimpleQuest.Quest.*           — designer-authored or compiler-emitted node identity
	State,              // SimpleQuest.QuestState.*      — runtime-managed state facts
	Outcome,            // SimpleQuest.QuestOutcome.*    — designer-authored outcome identifiers
	PrereqRule,         // SimpleQuest.QuestPrereqRule.* — designer-authored prereq rule group identity
	ActivationGroup,    // SimpleQuest.QuestActivationGroup.*
	LegacyOutcome,      // Quest.Outcome.*               — pre-0.4.0 outcome (transitional)
};

/**
 * Per-quest lifecycle leaf, written under SimpleQuest.QuestState.<Path>.<Leaf>. Replaces the prior FString-keyed
 * Leaf_X constants: typo-proof, switch-exhaustive, iterates cleanly via FQuestTagComposer::AllStateLeaves.
 */
enum class EQuestStateLeaf : uint8
{
	Live,
	Completed,
	PendingGiver,
	Deactivated,
	Blocked,
};

/**
 * Single source of truth for every tag composition and decomposition operation in SimpleQuest. Every site that
 * builds a tag from segments OR parses one back into segments routes through here. Hand-rolled compose /
 * decompose at call sites is forbidden by convention - adding a new tag kind means adding one method here and
 * one EQuestTagKind entry, not "find every place tags get built from scratch."
 */
class SIMPLEQUEST_API FQuestTagComposer
{
public:
	// ------------------------------------------------------------------------------------------------
	// Namespace constants: every prefix string in one place. Renaming a namespace touches one line here
	// instead of every hand-rolled site that hard-coded the literal.
	// ------------------------------------------------------------------------------------------------
	inline static const FString IdentityNamespace        = TEXT("SimpleQuest.Quest.");
	inline static const FString StateNamespace           = TEXT("SimpleQuest.QuestState.");
	inline static const FString OutcomeNamespace         = TEXT("SimpleQuest.QuestOutcome.");
	inline static const FString PrereqRuleNamespace      = TEXT("SimpleQuest.QuestPrereqRule.");
	inline static const FString ActivationGroupNamespace = TEXT("SimpleQuest.QuestActivationGroup.");
	inline static const FString LegacyOutcomePrefix      = TEXT("Quest.Outcome.");

	// ------------------------------------------------------------------------------------------------
	// BACKWARDS COMPAT — preserved during the FQuestStateTagUtils → FQuestTagComposer migration so the
	// codebase compiles incrementally. Prefer the modern surface at new sites:
	//   FString constants  → EQuestStateLeaf enum
	//   Namespace          → StateNamespace
	//   MakeStateFact(...) FString-overloads → EQuestStateLeaf-overloads
	// Remove this block once every call site has migrated. Tracked under item 2b cleanup follow-up.
	// ------------------------------------------------------------------------------------------------
	inline static const FString Namespace        = TEXT("SimpleQuest.QuestState.");
	inline static const FString Leaf_Live        = TEXT("Live");
	inline static const FString Leaf_Completed   = TEXT("Completed");
	inline static const FString Leaf_PendingGiver = TEXT("PendingGiver");
	inline static const FString Leaf_Deactivated = TEXT("Deactivated");
	inline static const FString Leaf_Blocked     = TEXT("Blocked");

	static FName MakeStateFact(FName IdentityTagName, const FString& Leaf);
	static FName MakeStateFact(FGameplayTag IdentityTag, const FString& Leaf);
	
	/** Iteration helper for "expand state facts on every identity tag" loops. */
	inline static constexpr EQuestStateLeaf AllStateLeaves[] = {
		EQuestStateLeaf::Live, EQuestStateLeaf::Completed, EQuestStateLeaf::PendingGiver,
		EQuestStateLeaf::Deactivated, EQuestStateLeaf::Blocked,
	};

	// ------------------------------------------------------------------------------------------------
	// Classifier: every "is this kind of tag?" question routes here
	// ------------------------------------------------------------------------------------------------
	static EQuestTagKind ClassifyTag(FName TagName);
	static bool IsIdentityTag(FName TagName);          // == eligible for state-fact expansion
	static bool IsStateTag(FName TagName);
	static bool IsOutcomeTag(FName TagName);           // covers both modern and Quest.Outcome.* legacy
	static bool IsPrereqRuleTag(FName TagName);
	static bool IsActivationGroupTag(FName TagName);

	// ------------------------------------------------------------------------------------------------
	// Compose: build tag names from structured inputs. Always emit canonical format.
	// ------------------------------------------------------------------------------------------------

	/** Builds an identity tag from an asset prefix + ordered child segments. */
	static FName MakeIdentityTag(const FString& AssetPrefix, TArrayView<const FString> Segments);

	/** Per-node lifecycle state fact: SimpleQuest.QuestState.<NodePath>.<Leaf>. */
	static FName MakeStateFact(FName IdentityTagName, EQuestStateLeaf Leaf);
	static FName MakeStateFact(FGameplayTag IdentityTag, EQuestStateLeaf Leaf);

	/** Per-node path resolution fact: SimpleQuest.QuestState.<NodePath>.Path.<Outcome>. */
	static FName MakeNodePathFact(FName IdentityTagName, FName PathIdentity);

	/** Per-quest entry path fact: SimpleQuest.QuestState.<NodePath>.EntryPath.<Outcome>. */
	static FName MakeEntryPathFact(FName IdentityTagName, FName PathIdentity);

	/** Resolves a state-fact name to a registered FGameplayTag. Returns invalid tag if not registered. */
	static FGameplayTag ResolveStateFactTag(FGameplayTag IdentityTag, EQuestStateLeaf Leaf);

	/** Swap one namespace prefix for another. Returns InTag unchanged if InTag doesn't start with From. */
	static FName SwapNamespacePrefix(FName InTagName, const FString& From, const FString& To);

	// ------------------------------------------------------------------------------------------------
	// Decompose: extract structured pieces from tag names.
	// ------------------------------------------------------------------------------------------------

	/** Returns the trailing dot-delimited segment ("SimpleQuest.Quest.A.B" → "B"). */
	static FString GetLeafSegment(FName TagName);

	/** Strips the trailing dot-delimited segment ("SimpleQuest.Quest.A.B" → "SimpleQuest.Quest.A").
	 *  Returns false if TagName has no '.' separator (already at root). */
	static bool TryGetParentTag(FName TagName, FName& OutParentTag);

	/** Walks ancestors leaf → root, invoking Visitor per ancestor. Visitor returns false to short-circuit.
	 *  Stops before reaching the root prefix (caller's responsibility to define the stop boundary). */
	static void EnumerateAncestors(FName TagName, TFunctionRef<bool(FName)> Visitor);

	/** Strips the outcome namespace prefix from PathString in-place if present. Handles BOTH the modern
	 *  SimpleQuest.QuestOutcome. namespace AND the legacy Quest.Outcome. prefix. Returns true if a strip occurred. */
	static bool TryStripOutcomePrefix(FString& InOutPathString);

	/** Canonical "Combat: Boss Defeated" formatter for outcome-tag display. Strips the outcome prefix, splits
	 *  remaining segments on '.', NameToDisplayString per segment, joins with ": ". Single source so K2Node
	 *  display, QuestlineNodeBase::GetOutcomeLabel, and the Prereq Examiner tree all render identically. */
	static FText FormatOutcomeForDisplay(FName OutcomeTagName);

	// ------------------------------------------------------------------------------------------------
	// Stale-tag utilities
	// ------------------------------------------------------------------------------------------------

	static bool IsTagRegisteredInRuntime(const FGameplayTag& Tag);
	static FGameplayTagContainer FilterToRegisteredTags(const FGameplayTagContainer& Container, const FString& ContextLabel);

private:
	FQuestTagComposer() = delete;

	/** Internal: enum → wire-format string for the tag name. Not public, leaf identity is enum-typed at the boundary. */
	static const FString& LeafToString(EQuestStateLeaf Leaf);
};