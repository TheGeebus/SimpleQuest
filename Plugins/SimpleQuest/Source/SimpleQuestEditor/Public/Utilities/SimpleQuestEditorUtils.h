// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Settings/SimpleQuestSettings.h"

// ---- Wire colors ----
#define SQ_ED_WIRE_ACTIVATION		(GetDefault<USimpleQuestSettings>()->ActivationWireColor)
#define SQ_ED_WIRE_PREREQUISITE		(GetDefault<USimpleQuestSettings>()->PrerequisiteWireColor)
#define SQ_ED_WIRE_OUTCOME			(GetDefault<USimpleQuestSettings>()->OutcomeWireColor)
#define SQ_ED_WIRE_DEACTIVATION		(GetDefault<USimpleQuestSettings>()->DeactivationWireColor)
#define SQ_ED_WIRE_STALE			(GetDefault<USimpleQuestSettings>()->StaleWireColor)

// ---- Pin colors ----
#define SQ_ED_PIN_DEFAULT			(GetDefault<USimpleQuestSettings>()->DefaultPinColor)

// ---- Node title colors ----
#define SQ_ED_NODE_ENTRY			(GetDefault<USimpleQuestSettings>()->EntryNodeColor)
#define SQ_ED_NODE_EXIT_ACTIVE		(GetDefault<USimpleQuestSettings>()->ExitNodeActiveColor)
#define SQ_ED_NODE_EXIT_INACTIVE	(GetDefault<USimpleQuestSettings>()->ExitNodeInactiveColor)
#define SQ_ED_NODE_QUEST			(GetDefault<USimpleQuestSettings>()->QuestNodeColor)
#define SQ_ED_NODE_STEP				(GetDefault<USimpleQuestSettings>()->StepNodeColor)
#define SQ_ED_NODE_LINKED			(GetDefault<USimpleQuestSettings>()->LinkedQuestlineGraphNodeColor)
#define SQ_ED_NODE_ACTIVATE_GROUP   (GetDefault<USimpleQuestSettings>()->ActivateGroupNodeColor)
#define SQ_ED_NODE_PREREQ_GROUP		(GetDefault<USimpleQuestSettings>()->PrerequisiteGroupNodeColor)
#define SQ_ED_NODE_UTILITY			(GetDefault<USimpleQuestSettings>()->UtilityNodeColor)
#define SQ_ED_NODE_GRAPH_OUTCOME	(GetDefault<USimpleQuestSettings>()->GraphOutcomeNodeColor)

// ---- Group Examiner ----
#define SQ_ED_EXAMINER_GROUP_SETTER (GetDefault<USimpleQuestSettings>()->ExaminerGroupSetterColor)
#define SQ_ED_EXAMINER_GROUP_GETTER (GetDefault<USimpleQuestSettings>()->ExaminerGroupGetterColor)

#define SQ_ED_HOVER_HIGHLIGHT       (GetDefault<USimpleQuestSettings>()->HoverHighlightColor)


class UQuestObjective;
class UQuestlineNode_Step;
class UQuestlineNode_ContentBase;
class UK2Node_CompleteObjectiveWithOutcome; 
class FQuestlineGraphEditor;

struct FConnectionParams;
struct FGraphPanelPinConnectionFactory;
struct FToolMenuSection;
struct FGameplayTag;
struct FGroupExaminerTopology;
struct FPrereqExaminerTree;


class FSimpleQuestEditorUtilities
{
	
public:
	/**
	 * Sanitizes a designer-entered label into a valid Gameplay Tag segment. Trims whitespace, replaces any character that is not
	 * alphanumeric or underscore with an underscore.
	 */
	static FString SanitizeQuestlineTagSegment(const FString& InLabel);

	/**
	 * Collects unique OutcomeTags from all Exit nodes in a graph. Returns the tag names suitable for passing directly to SyncPinsByCategory.
	 */
	static TArray<FName> CollectExitOutcomeTagNames(const UEdGraph* Graph);

	/**
	 * Discovers possible outcome tags for an objective class. Scans the class's Blueprint graphs for UK2Node_CompleteObjectiveWithOutcome
	 * instances; falls back to the CDO's GetPossibleOutcomes() virtual for classes where neither K2 nodes nor ObjectiveOutcome UPROPERTYs apply.
	 */
	static TArray<FGameplayTag> DiscoverObjectiveOutcomes(TSubclassOf<UQuestObjective> ObjectiveClass);

	/**
	 * Reconstructs the compiled gameplay tag for a step node by walking the graph hierarchy (Step → Quest → QuestlineGraph).
	 * Returns an invalid tag if the graph hasn't been compiled yet or the step label is empty.
	 */
	static FGameplayTag ReconstructStepTag(const UQuestlineNode_Step* StepNode);

	/**
	 * Finds actors in loaded editor worlds whose QuestTargetComponent watches the given step tag. Returns actor editor labels,
	 * sorted alphabetically.
	 */
	static TArray<FString> FindActorNamesWatchingTag(const FGameplayTag& StepTag);

	/**
	 * Reconstructs the compiled tag for the parent quest containing this step. Returns invalid if the step is at the top
	 * level (no parent quest node).
	 */
	static FGameplayTag ReconstructParentQuestTag(const UQuestlineNode_Step* StepNode);

	/**
	 * Finds actors in loaded editor worlds whose QuestGiverComponent gives the given quest tag. Returns actor editor labels,
	 * sorted alphabetically.
	 */
	static TArray<FString> FindActorNamesGivingTag(const FGameplayTag& QuestTag);

	/**
	 * Paired entry produced by the contextual-query helpers: an actor name + the display name of the outer questline
	 * asset whose contextual inlining of this node the actor is linked to. Applies to givers, watchers, or any future
	 * actor-per-tag contextual surface.
	 */
	struct FQuestContextualActor
	{
		FString ActorName;
		FText OuterAssetDisplayName;
	};

	/**
	 * Finds givers attached to this node's CONTEXTUAL inlined compiled tags, i.e., tags emitted by OUTER questline assets
	 * that LinkedQuestline-reference this node's home asset. Walks the Asset Registry's CompiledQuestTags AR tag on every
	 * questline asset except the home asset, matching by literal-dot-prefixed suffix on the node's relative path
	 * (everything past "Quest.<HomeQuestlineID>."). AR reads only, no sync-load. Home-asset skip avoids double-counting
	 * entries already surfaced via FindActorNamesGivingTag on the node's standalone compiled tag.
	 *
	 * Outer asset display name sources from the FriendlyName AR tag when present, falling back to the asset short name.
	 * Results sorted by (OuterAssetDisplayName, ActorName) and deduped on that pair.
	 */
	static TArray<FQuestContextualActor> FindContextualGiversForNode(const UQuestlineNode_ContentBase* ContentNode);

	/**
	 * Scans the Asset Registry for other questline assets whose CompiledQuestTags list contains an entry that ends with
	 * this node's relative path (post home-ID prefix strip). Returns the matching runtime tags — i.e., the contextual
	 * nested variants of this node's tag under every OUTER asset that LinkedQuestline-references the home. Empty when
	 * the node is used only in its own asset.
	 */
	static TArray<FGameplayTag> CollectContextualNodeTagsForEditorNode(const UQuestlineNode_ContentBase* ContentNode);
	
	/**
	 * Same walk as FindContextualGiversForNode, but resolves QuestTargetComponent watchers per contextual tag instead of
	 * givers. Surfaces target actors whose StepTagsToWatch include one of the node's contextual inlined tags — the
	 * equivalent of the standalone FindActorNamesWatchingTag path for the cross-graph case.
	 */
	static TArray<FQuestContextualActor> FindContextualWatchersForNode(const UQuestlineNode_ContentBase* ContentNode);

	/**
	 * Applies tag renames to all quest components in loaded editor worlds via the virtual UQuestComponentBase::ApplyTagRenames.
	 * Returns the number of actors modified.
	 */
	static int32 ApplyTagRenamesToLoadedWorlds(const TMap<FName, FName>& Renames);

	/**
	 * Walks the node's Outer chain (through any Quest container graphs) to the owning UQuestlineGraph asset, then
	 * matches the node's QuestGuid against CompiledNodes to resolve its compiled runtime tag. Works for any
	 * UQuestlineNode_ContentBase descendant (Quest, Step, LinkedQuestline). Returns an invalid tag when the node
	 * hasn't been compiled yet or the graph Outer chain is broken.
	 */
	static FGameplayTag FindCompiledTagForNode(const UQuestlineNode_ContentBase* ContentNode);

	/**
	 * Compiler-adjacent resolver — returns the WorldState fact tag a prereq-expression leaf reading OutputPin would check
	 * at runtime, plus the source node's compiled runtime tag (OutSourceTag). Mirrors the content-node branches of
	 * FQuestlineGraphCompiler::CompilePrerequisiteFromOutputPin: AnyOutcomeOut resolves to QuestState.<src>.Completed;
	 * NamedOutcomeOut resolves to QuestState.<src>.Outcome.<leaf>. Returns invalid tags for pin roles the examiner treats
	 * as drill-through (Rule Entry Forward) or RuleRef (Rule Exit); those code paths don't flatten to leaves in the
	 * examiner tree. Used by the Prereq Examiner for PIE leaf coloring.
	 */
	static FGameplayTag ResolveLeafFactForOutputPin(const UEdGraphPin* OutputPin, FGameplayTag& OutSourceTag);
	
	/**
	 * Returns true if the content node's reconstructed tag (from current labels) matches its last compiled tag. False when
	 * renamed since last compile, never compiled, or the Outer chain is broken. Works for any UQuestlineNode_ContentBase
	 * descendant — widgets displaying a "Recompile to update tags" warning use this as the source of truth.
	 */
	static bool IsContentNodeTagCurrent(const UQuestlineNode_ContentBase* ContentNode);

	/** Back-compat thin wrapper — prefer IsContentNodeTagCurrent. Same semantics, Step-typed argument. */
	static bool IsStepTagCurrent(const UQuestlineNode_Step* StepNode);

	/**
	 * Syncs a node's pins of a given category to match a desired set of pin names. Pins not in DesiredPinNames are orphaned
	 * (if wired) or removed (if unwired). Missing names are created. After the add/remove pass, pins of the category are
	 * reordered in the node's Pins array so their positions match DesiredPinNames order — this means toggle-off-then-on returns
	 * a pin to its original position rather than the bottom of the category. Orphaned pins (present but not in DesiredPinNames)
	 * are placed after desired pins, preserving their relative order. Calls Modify() and NotifyGraphChanged() internally.
	 *
	 * Callers control ordering by the order of DesiredPinNames — sort alphabetically for stable display, leave in index order
	 * for ordinal-named pins (e.g., "Condition_0", "Condition_1", ...).
	 */
	static void SyncPinsByCategory(UEdGraphNode* Node,	EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories = {});

	/**
	 * In-place alphabetical sort for pin-name arrays passed to SyncPinsByCategory. Use when the pin category should display
	 * in stable alphabetical order regardless of the source-data traversal order that produced the array (e.g., outcome pins
	 * derived from objective UPROPERTY order, Exit tag collection order, or IncomingSignals array order).
	 */
	static void SortPinNamesAlphabetical(TArray<FName>& PinNames);
	
	/**
	 * Scans all UQuestlineGraph assets in the project (AR scan + sync load) for ActivationGroup setters and getters whose
	 * GroupTag matches InGroupTag, and builds a full topology: for each setter, content-node sources feeding its Activate
	 * input; for each getter, content-node destinations reached by its Forward output. Walkers delegate to the existing
	 * CollectEffectiveSources / CollectActivationTerminals primitives. Intended for authoring-time inspection widgets;
	 * sync-loading is accepted overhead for on-select invocation.
	 */
	static void CollectActivationGroupTopology(const FGameplayTag& InGroupTag, FGroupExaminerTopology& OutTopology);

	/**
	 * Builds a prerequisite expression tree for SPrereqExaminerPanel, dispatched by ContextNode's type. Handles four
	 * entry points:
	 *   Content node (Quest / Step / LinkedQuestline / Outcome terminal) — walks the node's Prerequisites input pin.
	 *   Combinator (AND / OR / NOT) — emits the combinator as the root, walks each condition input.
	 *   Rule Entry — walks the Entry's Enter input; tree's RuleTag + RuleEntryNode populated for the header.
	 *   Rule Exit — resolves the Exit's tag to its defining Entry (local graph first, cross-asset AR fallback), walks that
	 *     Entry's Enter input; tree's RuleTag + RuleEntryNode populated.
	 * Knots are traversed transparently. Rule Exit references encountered during the walk emit RuleRef nodes with the
	 * referenced Entry's Enter expression eagerly attached as children (cycle-guarded against mutually referenced rules).
	 * Rule Entry Forward pins are inlined directly (direct-eval — no RuleRef boundary). Returns a tree with
	 * RootIndex == INDEX_NONE when the context has no wired expression.
	 */
	static FPrereqExaminerTree CollectPrereqExpressionTopology(UEdGraphNode* ContextNode);

	/**
	 * Appends an "Examine Prerequisite Expression" entry to a right-click context menu section. Resolves the owning editor
	 * via GetEditorForNode and calls PinPrereqExaminer.
	 */
	static void AddExaminePrereqExpressionEntry(FToolMenuSection& Section, UEdGraphNode* ContextNode);
	
	/**
	 * Opens the node's containing UQuestlineGraph asset in the Graph Editor and focuses the node. No-op if the node has no
	 * resolvable UQuestlineGraph outer or if GEditor is unavailable. Extracted from the compiler's AddNodeNavigationToken
	 * inner lambda so live UI (detail-panel hyperlinks, future examiner widgets) can share a single navigation entry point.
	 */
	static void NavigateToEdGraphNode(const UEdGraphNode* Node);

	/**
	 * Returns the FQuestlineGraphEditor toolkit instance currently editing the node's containing UQuestlineGraph, or nullptr
	 * if no editor is open for that asset. Walks the node's outer chain to locate the containing graph, then queries
	 * UAssetEditorSubsystem. Used by UI actions that need to dispatch to the editor (e.g. Group Examiner context-menu pinning).
	 */
	static FQuestlineGraphEditor* GetEditorForNode(const UEdGraphNode* Node);

	/**
	 * Appends an "Examine Group Connections" menu entry that pins the given node's GroupTag in the Group Examiner panel.
	 * Extracted to utilities because the action is identical for activation setter and getter nodes. Action is disabled when
	 * the GroupTag is invalid. Node captured as TWeakObjectPtr so stale references after GC no-op cleanly.
	 */
	static void AddExamineGroupConnectionsEntry(FToolMenuSection& Section, UEdGraphNode* Node, FGameplayTag GroupTag);
	
private:
	/** Walks the graph outer chain from any content node to reconstruct its compiled tag. */
	static FGameplayTag ReconstructNodeTagInternal(const UQuestlineNode_ContentBase* ContentNode);

	/**
	 * Shared contextual-query body. Walks the Asset Registry for non-home questline packages, suffix-matches on the node's
	 * relative path, and invokes TagToActorNames() per contextual tag to resolve the actor list. Both
	 * FindContextualGiversForNode and FindContextualWatchersForNode are thin wrappers around this. LogLabel is emitted in
	 * the Verbose diagnostic line for trace clarity.
	 */
	static TArray<FQuestContextualActor> CollectContextualActorEntries(const UQuestlineNode_ContentBase* ContentNode,
		TFunctionRef<TArray<FString>(const FGameplayTag&)> TagToActorNames, const TCHAR* LogLabel);
};
