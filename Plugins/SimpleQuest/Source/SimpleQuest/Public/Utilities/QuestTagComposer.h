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

private:
	// Helper for concatenating fully defined tag namespaces with the plugin prefix and relevant sub-prefix
	static FString MakeNamespace(const FString& SubPrefix)
	{
		return PluginPrefix + TEXT(".") + SubPrefix + TEXT(".");
	}
	
public:

	/**
	 * Wire-format string for a state-leaf enum value. Single source of truth: anywhere outside FQuestTagComposer
	 * that needs the leaf's string form (e.g. reserved-segment validators, INI emission, debug logging) routes
	 * through this rather than hardcoding "Live" / "Completed" / etc. literals. Adding a new EQuestStateLeaf entry
	 * means adding one switch arm here and every consumer picks it up automatically.
	 */
	static const FString& LeafToString(EQuestStateLeaf Leaf);
	
	// ------------------------------------------------------------------------------------------------
	// Namespace constants: every prefix string in one place. Renaming a namespace touches one line here
	// instead of every hand-rolled site that hard-coded the literal.
	// ------------------------------------------------------------------------------------------------

	inline static const FString PluginPrefix				= TEXT("SimpleQuest");
	inline static const FString QuestSubPrefix				= TEXT("Quest");
	inline static const FString StateSubPrefix				= TEXT("QuestState");
	inline static const FString OutcomeSubPrefix			= TEXT("QuestOutcome");
	inline static const FString PrereqRuleSubPrefix			= TEXT("QuestPrereqRule");
	inline static const FString ActivationGroupSubPrefix	= TEXT("QuestActivationGroup");
	// Pre-0.4.0 outcome tag prefix retained for defensive runtime support — assets compiled
	// before the namespace consolidation may still carry "Quest.Outcome.*" tags. Removal is
	// a 0.4.1 polish item, gated on every authored asset being recompiled at least once
	// (verifiable via Stale Quest Tags scanner). NOT part of item 2b cleanup.
	inline static const FString LegacyOutcomeSubPrefix		= TEXT("Quest.Outcome.");

	inline static const FString AllPrefixes[] = { PluginPrefix, QuestSubPrefix, StateSubPrefix, OutcomeSubPrefix,
		PrereqRuleSubPrefix, ActivationGroupSubPrefix, LegacyOutcomeSubPrefix };

	// ------------------------------------------------------------------------------------------------
	// Fully defined namespaces
	// ------------------------------------------------------------------------------------------------
	inline static const FString IdentityNamespace			= MakeNamespace(QuestSubPrefix);
	inline static const FString StateNamespace				= MakeNamespace(StateSubPrefix);
	inline static const FString OutcomeNamespace			= MakeNamespace(OutcomeSubPrefix);
	inline static const FString PrereqRuleNamespace			= MakeNamespace(PrereqRuleSubPrefix);
	inline static const FString ActivationGroupNamespace	= MakeNamespace(ActivationGroupSubPrefix);

	inline static const FString AllFullNamespaces[] = { IdentityNamespace, StateNamespace, OutcomeNamespace, PrereqRuleNamespace, ActivationGroupNamespace };

	// ------------------------------------------------------------------------------------------------
	// Suffixes - graph entry/exit paths
	// ------------------------------------------------------------------------------------------------
	inline static const FString PathSubSuffix				= TEXT("Path");       // used in MakeNodePathFact composition
	inline static const FString EntryPathSubSuffix			= TEXT("EntryPath");  // used in MakeEntryPathFact composition

	inline static const FString AllSuffixes[] = { PathSubSuffix, EntryPathSubSuffix };
	
	/** Iteration helper for "expand state facts on every identity tag" loops. */
	inline static constexpr EQuestStateLeaf AllStateLeaves[] = {
		EQuestStateLeaf::Live, EQuestStateLeaf::Completed, EQuestStateLeaf::PendingGiver,
		EQuestStateLeaf::Deactivated, EQuestStateLeaf::Blocked,
	};

	/**
	 * Comprehensive union of every tag token FQuestTagComposer manages: plugin prefix, sub-prefixes
	 * (including LegacyOutcomeSubPrefix), fully composed namespaces, suffixes (Path / EntryPath), and
	 * the wire-format leaf names (Live, Completed, …) sourced via LeafToString. Heterogeneous by design
	 * — single-segment entries mixed with dot-bearing prefix strings — so NOT suitable for tag-validity
	 * checks (would produce false positives). Primary use: reserved-segment validation in the compiler
	 * to block designer node labels from colliding with any internal-use token. Also suitable as a
	 * complete inventory for any future audit / inspection surface.
	 */
	inline static const TArray<FString> AllNamespaces = []
	{
		TArray<FString> Result;
		Result.Append(AllPrefixes, UE_ARRAY_COUNT(AllPrefixes));
		Result.Append(AllFullNamespaces, UE_ARRAY_COUNT(AllFullNamespaces));
		Result.Append(AllSuffixes, UE_ARRAY_COUNT(AllSuffixes));
		for (EQuestStateLeaf Leaf : AllStateLeaves)
		{
			Result.Add(LeafToString(Leaf));
		}
		return Result;
	}();
	
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

	/** Strips the PluginPrefix from a tag and returns the remainder for rendering surfaces (panel headers,
	 *  K2 node tag-picker chips, tooltips). Keeps the post-prefix segments verbatim — preserves the namespace
	 *  context that distinguishes `Quest.X` (identity) from `QuestState.X.Live` (state fact) without forcing
	 *  designers to mentally re-prepend the plugin name on every read. The data layer keeps the full tag;
	 *  only rendering surfaces use this shortened form. Copy-tag affordances and serialization continue to
	 *  use the raw tag.
	 *
	 *  Examples:
	 *    "SimpleQuest.Quest.MyAsset.Step1"               → "Quest.MyAsset.Step1"
	 *    "SimpleQuest.QuestState.MyAsset.Step1.Live"     → "QuestState.MyAsset.Step1.Live"
	 *    "SimpleQuest.QuestOutcome.Combat.BossDefeated"  → "QuestOutcome.Combat.BossDefeated"
	 *    "SimpleQuest.QuestPrereqRule.MyRule"            → "QuestPrereqRule.MyRule"
	 *    "Game.Foo.Bar" (foreign — no PluginPrefix)      → "Game.Foo.Bar"
	 *    NAME_None                                       → ""              (empty FText) */
	static FText FormatTagForDisplay(FName TagName);

	// ------------------------------------------------------------------------------------------------
	// Stale-tag utilities
	// ------------------------------------------------------------------------------------------------

	static bool IsTagRegisteredInRuntime(const FGameplayTag& Tag);
	static FGameplayTagContainer FilterToRegisteredTags(const FGameplayTagContainer& Container, const FString& ContextLabel);

private:
	FQuestTagComposer() = delete;


};