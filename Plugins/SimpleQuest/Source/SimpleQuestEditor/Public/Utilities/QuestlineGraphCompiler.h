// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "Quests/Types/PrerequisiteExpression.h"

struct FQuestBoundaryCompletion;
struct FIncomingSignalPinSpec;
struct FQuestEntryRouteList;
class UQuestlineGraph;
class UQuestlineNode_ContentBase;
class UQuestlineNode_UtilityBase;
class UQuestNodeBase;
class UQuest;
class UEdGraphPin;
class FQuestlineGraphTraversalPolicy;


/**
 * Compiles a UQuestlineGraph asset, walking the graph structure and writing derived data to each quest/step CDO.
 *
 * A single recursive CompileGraph call (do not call directly, prefer Compile) handles all linked Quest and Step node
 * objects. LinkedQuestline graph nodes compile to UQuest runtime instances under a nested tag namespace
 * (SimpleQuest.Quest.<ParentID>.<NodeLabel>.<InnerNode>) — the linked asset's content is inlined as the UQuest's inner
 * routing but the LinkedQuestline itself retains a first-class compiled tag, lifecycle events, and save identity.
 *
 * - Call Compile as the entry point to both validate the questline graph asset and initiate recursive compilation.
 *
 * Use ISimpleQuestEditorModule::RegisterCompilerFactory to provide a custom subclass.
 * @see ISimpleQuestEditorModule::RegisterCompilerFactory
 */
class SIMPLEQUESTEDITOR_API FQuestlineGraphCompiler
{
public:
	FQuestlineGraphCompiler();
	virtual ~FQuestlineGraphCompiler();

    /**
     * Entry point. Validates the asset, then kicks off recursive graph compilation. Collects and registers all nested and
     * linked quest Gameplay Tags. Returns true if there were no errors.
     */
	virtual bool Compile(UQuestlineGraph* InGraph);

protected:
	/**
	 * Compiles one graph level. Assigns QuestContentGuid and QuestTag to all content node CDOs, then resolves output pin
	 * wiring into NextNodesOnSuccess / NextNodesOnFailure. Recurses into linked questline graph assets.
	 *
	 * @param Graph							The questline graph asset to compile.
	 * @param TagPrefix						Sanitized questline ID used as the tag namespace for this graph's nodes.
	 * @param BoundaryTagsByPath			Tags injected when an Exit_Success node is reached (empty at top level).
 	 * @param BoundaryCompletionsByPath		Inherited boundary completions keyed by Exit OutcomeTag (NAME_None for Any-Outcome catch-all).
	 *										Mirrors BoundaryTagsByPath: consumed by ResolvePinToTags when crossing an Exit.
	 * @param VisitedAssetPaths				Stack of asset paths currently open in the recursion, used for cycle detection.
	 * @param OutEntryTagsByPath			Tags from input pins connected to optional Outcome graph entry pins on Quest or Linked
	 *										Questline child graphs 
	 * @return								Returns the tags connected to an Any Outcome graph entry pin
	 */
	virtual TArray<FName> CompileGraph(
		UEdGraph* Graph,
		const FString& TagPrefix,
		const TMap<FName, TArray<FName>>& BoundaryTagsByPath,
		const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
		TArray<FString>& VisitedAssetPaths,
		TMap<FName, FQuestEntryRouteList>* OutEntryTagsByPath = nullptr);	

	/**
	 * Follows an output pin through knots, exit nodes, and linked questline nodes, collecting the gameplay tags of all terminal
	 * content nodes. Exit nodes return the appropriate boundary tag set. LinkedQuestline nodes are compiled recursively and
	 * their entry tags are returned in their place.
	 *
	 * @param FromPin						The output pin to trace.
	 * @param TagPrefix						Tag namespace of the currently compiling graph. Used to resolve the linked node's own downstream
	 *										connections before recursing into the linked asset.
	 * @param BoundaryTagsByPath			Forwarded to Exit_Success resolution.
     * @param BoundaryCompletionsByPath	    Inherited boundary completions for this compile context, keyed by Exit OutcomeTag. Looked up by
	 *										the Exit-crossing branch of the walk and accumulated into OutBoundaryCompletions.
	 * @param VisitedAssetPaths				Cycle detection stack, shared with CompileGraph.
	 * @param OutTags						Accumulates the resolved tags.
	 * @param OutBoundaryCompletions		Out-accumulator for boundary completions picked up as the walk crosses Exits. Caller appends
	 *										these to the corresponding routing table so ChainToNextNodes can fire them at runtime.
	 * @param OutVisitedExitsByPath			Outcome deduplication detection stack.
	 */
	virtual void ResolvePinToTags(
		UEdGraphPin* FromPin,
		const FString& TagPrefix,
		const TMap<FName, TArray<FName>>& BoundaryTagsByPath,
		const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
		TArray<FString>& VisitedAssetPaths,
		TArray<FName>& OutTags,
		TArray<FQuestBoundaryCompletion>& OutBoundaryCompletions,
		TMap<FName, TArray<TWeakObjectPtr<const UEdGraphNode>>>* OutVisitedExitsByPath = nullptr);
	
	/**
	 * Sanitizes a designer-entered node label into a valid Gameplay Tag segment. Replaces spaces and invalid characters with
	 * underscores, removes leading/trailing whitespace. May be overriden to change the default behavior, which is to simply
	 * call SimpleQuestEditorUtilities::SanitizeQuestlineTagSegment.
	 */
	virtual FString SanitizeTagSegment(const FString& InLabel) const;
	
	/**
     * Entry point for prerequisite expression compilation. Called from Pass 2 for each content node with something wired to its
     * Prerequisites pin. Returns a trivially-true expression if the pin is empty.
     */
    virtual FPrerequisiteExpression CompilePrerequisiteExpression(UEdGraphPin* PrerequisiteInputPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths);
	
	/** Constructs the FName used to register and look up a node's gameplay tag. */
	virtual FName MakeNodeTagName(const FString& TagPrefix, const FString& SanitizedLabel) const;

	/**
	 * Rules for moving between nodes. Subclass and register via ISimpleQuestEditorModule interface to override classification logic.
	 * 
	 * For creating new node types, prefer to subclass UQuestlineNodeBase and override internal classification methods such as IsExitNode, etc.
	 */
	TUniquePtr<FQuestlineGraphTraversalPolicy> TraversalPolicy;
	
	virtual void RegisterCompiledTags(UQuestlineGraph* InGraph);
	
private:

	/**
	 * Parallel-path warning data structures. Populated during the compile pass, analyzed at the end of Compile(). All keyed
	 * by compiled tag names (FName) so LinkedQuestline boundary crossings work via the same compiled-tag naming the rest of
	 * the compiler uses. Cleared at Compile() start.
	 */
	struct FSourcePathKey
	{
		FName SourceTag;	// compiled tag of the source content node
		FName Path;			// path identity (outcome tag's full FName for static placements; sanitized PathName for dynamic
							// placements). NAME_None = "any path from this source".

		bool operator==(const FSourcePathKey& Other) const
		{
			return SourceTag == Other.SourceTag && Path == Other.Path;
		}
		friend uint32 GetTypeHash(const FSourcePathKey& Key)
		{
			return HashCombine(GetTypeHash(Key.SourceTag), GetTypeHash(Key.Path));
		}
	};
	
	/**
	 * Given a spec's (SourceNodeGuid, ParentAsset), resolves the compiled QuestTag of the source content node. Used as the
	 * SourceFilter on entry destinations so runtime routing can discriminate per-source. Returns NAME_None when the source
	 * cannot be located (unresolvable asset, missing node, etc.) — caller emits a warning and skips the spec.
	 */
	FName ResolveSourceFilterTag(const FIncomingSignalPinSpec& Spec, const UQuestlineGraph* ChildAsset) const;

	/**
	 * Recursively collects content-node (source, outcome) pairs that transitively reach a graph's boundary via any combination
	 * of direct wires, Entered-pin passthroughs, and Entry-spec-pin passthroughs. Extends the one-level `CollectEntryReachingSources`
	 * by continuing the walk up through Entry indirections until a concrete content-node source is found or the walk escapes
	 * the compile tree (filtered by VisitedAssetPaths). Cycle-guarded via VisitedGraphs — a graph visited once doesn't re-emit.
	 */
	void CollectTransitiveParentSources(UEdGraph* InGraph, const TArray<FString>& VisitedAssetPaths, TSet<FSourcePathKey>& OutKeys, TSet<UEdGraph*>& VisitedGraphs);
	
	/**
	 * Walks the Outer chain from a content node up to its containing asset, collecting sanitized ancestor labels, then composes
	 * the compiled QuestTag: SimpleQuest.Quest.<QuestlineID>.<AncestorLabel>...<NodeLabel>. Independent of compile pass
	 * ordering — uses editor-time data only.
	 */
	FName ComputeCompiledTagForContentNode(const UQuestlineNode_ContentBase* SourceNode, const UQuestlineGraph* ContainingAsset) const;

	/** Logs a compile error and sets the internal error flag. */
	void AddError(const FString& Message, const UEdGraphNode* Node = nullptr);

	/** Logs a compile warning without setting the internal error flag. */
	void AddWarning(const FString& Message, const UEdGraphNode* Node = nullptr);

	/** Internal questline compiler error flag. Returned by the main Compile function. */
	bool bHasErrors = false;

	/** FMessageLog error logging messages accumulated during compilation */	
	TArray<TSharedRef<FTokenizedMessage>> Messages;
	int32 NumErrors = 0;
	int32 NumWarnings = 0;
	
	/**
	 * Accumulates all compiled node classes across the full recursive compilation run. This is the runtime data set, which inlines
	 * linked questline graph nodes, effectively erasing them. Written to the top-level graph by Compile().
	 */
	TMap<FName, TObjectPtr<UQuestNodeBase>> AllCompiledNodes;

	/**
	 * Accumulates all compiled node classes across the full recursive compilation run. Includes linked graph nodes, which are
	 * inlined in the runtime data set. This set is used for in-editor navigation. Written to the top-level graph by Compile().
	 */
	TMap<FName, TObjectPtr<UEdGraphNode>> AllCompiledEditorNodes;
	
	/** Accumulates all compiled quest tags across the full recursive compilation run. Written to the top-level graph by Compile(). */
	TArray<FName> AllCompiledQuestTags;	

	/**
	 * Maps utility editor nodes to their compile-time FName keys. Keyed by editor node pointer; values are GUID-derived FNames
	 * (Util_<NodeGuid>) used as AllCompiledNodes lookup keys. Not gameplay tags — utility nodes are internal routing only,
	 * not tracked quest states.
	 */
	TMap<UEdGraphNode*, FName> UtilityNodeKeyMap;
	
	/** Renames detected during the most recent Compile(), keyed as FName (old → new). */
	TMap<FName, FName> DetectedTagRenames;

	/**
	 * Recursive helper. Walks one output pin in the prerequisite expression sub-graph and appends the corresponding node(s) to 
	 * OutExpression. Returns the index of the root node added, or INDEX_NONE if the pin could not be resolved.
	 */
	int32 CompilePrerequisiteFromOutputPin(UEdGraphPin* OutputPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, FPrerequisiteExpression& OutExpression);

	/**
	 * Follows the Deactivated output pin and splits resolved node tags by destination pin category: connections to Activate inputs
	 * populate OutActivateTags (NextNodesOnDeactivation); connections to Deactivate inputs populate OutDeactivateTags
	 * (NextNodesToDeactivateOnDeactivation).
	 */
	void ResolveDeactivatedPinToTags(UEdGraphPin* FromPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, TArray<FName>& OutActivateTags, TArray<FName>& OutDeactivateTags);

	/**
	 * Emits a tokenized compile-time warning when a single content-node output pin reaches two or more distinct Outcome
	 * terminals sharing the same OutcomeTag. The compiler accepts the union of reached destinations, but the authoring
	 * is ambiguous — one outcome should route through one terminal. Called from the outcome-routing pass after
	 * ResolvePinToTags returns the visited-exits collection.
	 */
	void EmitDuplicateOutcomeRoutingWarning(
		const UEdGraphNode* SourceNode,
		const UEdGraphPin* SourcePin,
		const FName& DuplicatedPathIdentity,
		const TArray<TWeakObjectPtr<const UEdGraphNode>>& DuplicateExits,
		const FString& TagPrefix);
	
	/** Appends a clickable action token that navigates to the given node in the graph editor. */
	void AddNodeNavigationToken(TSharedRef<FTokenizedMessage>& Msg, const UEdGraphNode* Node);

	/** Pass 1: iterate content nodes, validate labels, create runtime instances, assign tags. */
	void CompileNodeRegistration(
		UEdGraph* Graph,
		const FString& TagPrefix,
		const TMap<FName, TArray<FName>>& BoundaryTagsByPath,
		const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
		TArray<FString>& VisitedAssetPaths, TArray<UQuestlineNode_ContentBase*>& OutContentNodes,
		TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& OutNodeInstanceMap);

	/** Pass 1b: compile all group nodes — prereq setters (merged), activation setters, activation getters. */
	void CompileGroupSetters(
		UEdGraph* Graph,
		const FString& TagPrefix,
		TArray<FString>& VisitedAssetPaths,
		TArray<FName>& OutMonitorTags,
		TArray<FName>& OutGetterEntryTags);

	/** Pass 1c: create runtime instances for utility nodes (SetBlocked, ClearBlocked, GroupSignal). */
	void CompileUtilityNodes(UEdGraph* Graph, TArray<UQuestlineNode_UtilityBase*>& OutUtilityEdNodes);

	/** Pass 2: route each content node's output pins into the runtime routing sets. */
	void CompileOutputWiring(
		const TArray<UQuestlineNode_ContentBase*>& ContentNodes,
		const TMap<UQuestlineNode_ContentBase*,
		UQuestNodeBase*>& NodeInstanceMap,
		const FString& TagPrefix,
		const TMap<FName, TArray<FName>>& BoundaryTagsByPath,
		const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
		TArray<FString>& VisitedAssetPaths);

	/** Resolve entry tags from the graph's Entry node, splitting per-path when applicable. */
	TArray<FName> ResolveEntryTags(
		UEdGraph* Graph,
		const FString& TagPrefix,
		const TMap<FName, TArray<FName>>& BoundaryTagsByPath,
		const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
		TArray<FString>& VisitedAssetPaths,
		TMap<FName, FQuestEntryRouteList>* OutEntryTagsByPath);

	/** GUID-bridge rename detection: chain-collapse existing ledger, add new renames, prune identities. */
	void DetectAndRecordTagRenames(UQuestlineGraph* InGraph, const TMap<FGuid, FName>& OldTagsByGuid);

	/** Shared handler for AND/OR combinator nodes — creates expression node and recurses into all input pins. */
	int32 CompileCombinatorNode(EPrerequisiteExpressionType Type, UEdGraphNode* Node, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths,	FPrerequisiteExpression& OutExpression);

	/**
	 * Walks a container content node's (Quest or LinkedQuestline) output pins and builds the per-path
	 * boundary maps for the recursive inner CompileGraph call. For each named outcome pin (and the
	 * Any-Outcome pin), computes outer-side destination tags via ResolvePinToTags and accumulates the
	 * container's own wrapper boundary completion (inserted at the front of each list for innermost-
	 * first cascade order through nested containers).
	 */
	void ComputeInnerBoundaryMaps(
		UQuestlineNode_ContentBase* ContentNode,
		const FString& TagPrefix,
		const FString& Label,
		const TMap<FName, TArray<FName>>& BoundaryTagsByPath,
		const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
		TArray<FString>& VisitedAssetPaths,
		TMap<FName, TArray<FName>>& OutBoundaryByPath,
		TMap<FName, TArray<FQuestBoundaryCompletion>>& OutBoundaryCompletionsByPath);
	
	UQuestlineGraph* RootGraph = nullptr;

	/**
	 * Accumulates linked-placement GUIDs as compilation recurses through LinkedQuestline nodes. Combined with each content
	 * node's own GUID at instance assignment time so that multiple placements of the same linked asset produce runtime
	 * instances with distinct QuestContentGuids — required for both rename detection and per-instance save state.
	 * Zero at top-level; push/pop save-restore around each LinkedQuestline recursion.
	 */
	FGuid CurrentOuterGuidChain;

	/** (source, outcome) pairs that reach each destination via direct outcome→Activate wiring (through utilities and Setter.Forward chains). */
	TMap<FName, TSet<FSourcePathKey>> DirectReachesByDest;

	/** (source, outcome) pairs feeding ActivationGroupSetters of each group tag. */
	TMap<FGameplayTag, TSet<FSourcePathKey>> GroupSetterSourcesByTag;

	/** Destinations reached by ActivationGroupGetters of each group tag (via the getter's Forward output chain). */
	TMap<FGameplayTag, TSet<FName>> GroupGetterDestsByTag;

	/**
	 * Setter editor-node lookup keyed by group tag and then by the specific (source, outcome) pair the setter contributes.
	 * Lets the warning emitter identify the setter that actually contributed THIS collision's source rather than picking an
	 * arbitrary setter with a matching tag. Nested TMap avoids a custom hash pair-key.
	 */
	TMap<FGameplayTag, TMap<FSourcePathKey, UEdGraphNode*>> SetterEdNodeByGroupAndSource;

	/**
	 * Getter editor-node lookup keyed by group tag and then by destination tag. Lets the warning emitter identify the specific
	 * getter that reaches THIS collision's destination rather than picking an arbitrary getter with a matching tag — important
	 * when multiple linked assets each contribute a getter for the same group but reach different destinations.
	 */
	TMap<FGameplayTag, TMap<FName, UEdGraphNode*>> GetterEdNodeByGroupAndDest;
	
	/**
	 * Collision test with AnyOutcome absorption: two keys collide when their sources match AND their outcomes match OR either
	 * outcome is invalid (the "any outcome" sentinel). Mirrors UQuestlineGraphSchema::PinsRepresentSameSignal semantics.
	 */
	static bool ParallelPathKeysCollide(const FSourcePathKey& A, const FSourcePathKey& B);

	/** Runs at the end of Compile() to analyze collected data and emit parallel-path warnings. */
	void EmitParallelPathWarnings();
	
	/**
	 * Emits one tokenized Warning-severity message per parallel-path collision. Message format: plain-text preamble with inline
	 * clickable tokens for source, destination, setter, and (first) getter so designers can navigate directly to each offending
	 * node from the Quest Compiler message log. Falls back to plain text for any node ref that didn't resolve.
	 */
	void EmitParallelPathCollisionWarning(const FGameplayTag& GroupTag, const FSourcePathKey& SetterSource, const FSourcePathKey& DirectSource, const FName& DestTag);
	
	/**
	 * Activation Group metadata collection pass — iterates graph nodes for ActivationGroupSetters and ActivationGroupGetters,
	 * walks their input/output chains via the traversal policy, and records setter source-outcome pairs and getter destinations
	 * keyed by group tag. Called from CompileGraph per graph (including recursive linked compiles), so the compiler-level maps
	 * accumulate entries from the entire compile tree.
	 */
	void CollectActivationGroupMetadata(UEdGraph* Graph, const FString& TagPrefix);
	
public:
	/** Accumulated compiler messages from the most recent Compile() call. */
	const TArray<TSharedRef<FTokenizedMessage>>& GetMessages() const { return Messages; }
	int32 GetNumErrors() const { return NumErrors; }
	int32 GetNumWarnings() const { return NumWarnings; }
	const TMap<FName, FName>& GetDetectedRenames() const { return DetectedTagRenames; }

	/**
	 * Deterministic compound of two GUIDs; asymmetric so (Outer, Inner) differs from (Inner, Outer). Public so editor
	 * navigation code can mirror the compiler's GUID-chain logic when looking up the editor node corresponding to a
	 * runtime instance whose QuestContentGuid was combined through one or more LinkedQuestline placements.
	 */
	static FGuid CombineGuids(const FGuid& Outer, const FGuid& Inner);
	
};
