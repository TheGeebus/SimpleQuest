// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PrereqExaminerTypes.generated.h"

UENUM()
enum class EPrereqExaminerNodeType : uint8
{
    Leaf,     // WorldState fact reference — content outcome, Entry terminal "Entered" sentinel, Any Outcome sentinel
    And,
    Or,
    Not,
    RuleRef,  // cross-graph reference to a named prerequisite rule via a Rule Exit
};

/**
 * One node of the prerequisite expression tree as rendered by SPrereqExaminerPanel. Structurally mirrors
 * FPrerequisiteExpressionNode (flat index layout, child indices into the containing tree's Nodes array) but carries
 * editor-side metadata — display labels, weak node references for navigation, rule-entry backreferences — that the
 * runtime expression doesn't need.
 */
USTRUCT()
struct FPrereqExaminerNode
{
    GENERATED_BODY()

    UPROPERTY() EPrereqExaminerNodeType Type = EPrereqExaminerNodeType::Leaf;

    /** Human-readable row text, e.g., "Step: Reached", "Rule: BossDefeated", "AND". */
    UPROPERTY() FText DisplayLabel;

    /** Leaf-only: content node's display title (row-1 value in the leaf's two-row display). Will transparently pick up
        a FriendlyName override once that field lands on the content-node display. */
    UPROPERTY() FText LeafSourceLabel;

    /** Leaf-only: path's tag-picker category prefix for static-OutcomeTag-derived path identities (everything after
        "SimpleQuest.QuestOutcome." up to and including the last dot, e.g., "Combat." for
        SimpleQuest.QuestOutcome.Combat.BossDefeated). Empty for sentinels ("Any Outcome"), for outcomes that are
        direct children of SimpleQuest.QuestOutcome, and for dynamic path identities (bare designer-authored or
        "Dynamic N" auto-numbered names). Rendered de-emphasized above LeafPathLabel in the leaf widget. */
    UPROPERTY() FText LeafPathCategory;

    /** Leaf-only: path pin label, the leaf segment of a named outcome tag for static placements, the bare path
        identity for dynamic placements, the "Any Outcome" sentinel, or "Entered". Row-2 primary value in the
        leaf's two-row display. */
    UPROPERTY() FText LeafPathLabel;

    /** WorldState fact tag the leaf/RuleRef reads at runtime. For leaves: matches the compiler's per-leaf fact output
        (MakeNodePathFact for named paths, MakeStateFact(Completed) for Any Outcome). For RuleRef: the rule's
        group tag. Invalid on combinator nodes and on leaves whose source pin role isn't covered by
        ResolveLeafFactForOutputPin (rare. Entry outcome leaves, for example). Drives PIE leaf-state coloring. */
    UPROPERTY() FGameplayTag LeafTag;

    /** Source content node's compiled runtime tag (e.g., "SimpleQuest.Quest.Demo.Step1"). Paired with LeafTag so the debug channel
        can classify NotStarted / InProgress / Satisfied / Unsatisfied by cross-checking the source node's state facts.
        Leaf-only; invalid on other node types. */
    UPROPERTY() FGameplayTag LeafSourceTag;

    /** Navigation target on double-click - the editor node that sources this leaf or owns this rule reference. */
    UPROPERTY() TWeakObjectPtr<UEdGraphNode> SourceNode;

    /** RuleRef only: the Prerequisite Rule Entry that defines the referenced rule. Enables drill-down into the rule's
        own Enter expression (walked eagerly into ChildIndices when the RuleRef is emitted). */
    UPROPERTY() TWeakObjectPtr<UEdGraphNode> RuleEntryNode;

    /** Indices into FPrereqExaminerTree::Nodes — combinator operands, or a RuleRef's expanded inner expression. */
    UPROPERTY() TArray<int32> ChildIndices;
};

/**
 * The full examinable expression pinned to SPrereqExaminerPanel. ContextNode is the editor node that was right-clicked
 * to build this tree. RuleTag + RuleEntryNode are populated only when ContextNode is a Rule Entry or Exit — they feed
 * the panel's rule-info header; otherwise the header is suppressed.
 */
USTRUCT()
struct FPrereqExaminerTree
{
    GENERATED_BODY()

    /** The node whose right-click triggered this pinning. */
    UPROPERTY() TWeakObjectPtr<UEdGraphNode> ContextNode;

    /** Populated when ContextNode is a Prerequisite Rule Entry or Exit; invalid tag + null node otherwise. */
    UPROPERTY() FGameplayTag RuleTag;
    UPROPERTY() TWeakObjectPtr<UEdGraphNode> RuleEntryNode;

    UPROPERTY() TArray<FPrereqExaminerNode> Nodes;

    /** Index into Nodes of the top-level expression node, or INDEX_NONE when the context has no wired expression. */
    UPROPERTY() int32 RootIndex = INDEX_NONE;

    /** Convenience — an empty tree signals "render the rule-info header (if populated) + an empty-state row". */
    bool IsEmpty() const { return RootIndex == INDEX_NONE; }
};