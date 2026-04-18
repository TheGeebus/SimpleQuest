// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "Quests/PrerequisiteExpression.h"

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
 * A single recursive CompileGraph call (do not call directly, prefer Compile) handles all linked Quest and Step node objects.
 * LinkedQuestline graph nodes are compiler-only scaffolding: the compiler inlines their wiring into the parent graph's tag
 * relationships and the nodes themselves are erased with no corresponding runtime class.
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
	 * @param Graph					The questline graph asset to compile.
	 * @param TagPrefix				Sanitized questline ID used as the tag namespace for this graph's nodes.
	 * @param BoundaryTagsByOutcome	Tags injected when an Exit_Success node is reached (empty at top level).
	 * @param VisitedAssetPaths		Stack of asset paths currently open in the recursion, used for cycle detection.
	 * @param OutEntryTagsByOutcome Tags from input pins connected to optional Outcome graph entry pins on Quest or Linked
	 *								Questline child graphs 
	 * @return						Returns the tags connected to an Any Outcome graph entry pin
	 */
	virtual TArray<FName> CompileGraph(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TMap<FGameplayTag, FQuestEntryRouteList>* OutEntryTagsByOutcome = nullptr);	

	/**
	 * Follows an output pin through knots, exit nodes, and linked questline nodes, collecting the gameplay tags of all terminal
	 * content nodes. Exit nodes return the appropriate boundary tag set. LinkedQuestline nodes are compiled recursively and
	 * their entry tags are returned in their place.
	 *
	 * @param FromPin				The output pin to trace.
	 * @param TagPrefix				Tag namespace of the currently compiling graph. Used to resolve the linked node's own downstream
	 *								connections before recursing into the linked asset.
	 * @param BoundaryTagsByOutcome	Forwarded to Exit_Success resolution.
	 * @param VisitedAssetPaths		Cycle detection stack, shared with CompileGraph.
	 * @param OutTags				Accumulates the resolved tags.
	 */
	virtual void ResolvePinToTags(UEdGraphPin* FromPin, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TArray<FName>& OutTags);
	
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
	 * Given a spec's (SourceNodeGuid, ParentAsset), resolves the compiled QuestTag of the source content node. Used as the
	 * SourceFilter on entry destinations so runtime routing can discriminate per-source. Returns NAME_None when the source
	 * cannot be located (unresolvable asset, missing node, etc.) — caller emits a warning and skips the spec.
	 */
	FName ResolveSourceFilterTag(const FIncomingSignalPinSpec& Spec, const UQuestlineGraph* ChildAsset) const;

	/**
	 * Walks the Outer chain from a content node up to its containing asset, collecting sanitized ancestor labels, then composes
	 * the compiled QuestTag: Quest.<QuestlineID>.<AncestorLabel>...<NodeLabel>. Independent of compile pass ordering — uses
	 * editor-time data only.
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
	 * Given a Success or Failure output pin on a content node, returns the corresponding WorldState state fact FName (Quest.State.<Tag>.Succeeded / Failed).
	 * Returns NAME_None for Any Outcome (caller builds the OR node) or non-content-node pins.
	 */
	FName ResolveOutputPinToStateFact(UEdGraphPin* OutputPin, const FString& TagPrefix) const;

	/**
	 * Follows the Deactivated output pin and splits resolved node tags by destination pin category: connections to Activate inputs
	 * populate OutActivateTags (NextNodesOnDeactivation); connections to Deactivate inputs populate OutDeactivateTags
	 * (NextNodesToDeactivateOnDeactivation).
	 */
	void ResolveDeactivatedPinToTags(UEdGraphPin* FromPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, TArray<FName>& OutActivateTags, TArray<FName>& OutDeactivateTags);

	/** Appends a clickable action token that navigates to the given node in the graph editor. */
	void AddNodeNavigationToken(TSharedRef<FTokenizedMessage>& Msg, const UEdGraphNode* Node);

	/** Pass 1: iterate content nodes, validate labels, create runtime instances, assign tags. */
	void CompileNodeRegistration(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TArray<UQuestlineNode_ContentBase*>& OutContentNodes, TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& OutNodeInstanceMap);

	/** Pass 1b: compile all group nodes — prereq setters (merged), activation setters, activation getters. */
	void CompileGroupSetters(UEdGraph* Graph, const FString& TagPrefix, TArray<FName>& OutMonitorTags, TArray<FName>& OutGetterEntryTags);

	/** Pass 1c: create runtime instances for utility nodes (SetBlocked, ClearBlocked, GroupSignal). */
	void CompileUtilityNodes(UEdGraph* Graph, TArray<UQuestlineNode_UtilityBase*>& OutUtilityEdNodes);

	/** Pass 2: route each content node's output pins into the runtime routing sets. */
	void CompileOutputWiring(const TArray<UQuestlineNode_ContentBase*>& ContentNodes, const TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& NodeInstanceMap, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths);

	/** Resolve entry tags from the graph's Entry node, splitting per-outcome when applicable. */
	TArray<FName> ResolveEntryTags(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TMap<FGameplayTag, FQuestEntryRouteList>* OutEntryTagsByOutcome);

	/** GUID-bridge rename detection: chain-collapse existing ledger, add new renames, prune identities. */
	void DetectAndRecordTagRenames(UQuestlineGraph* InGraph, const TMap<FGuid, FName>& OldTagsByGuid);

	/** Shared handler for AND/OR combinator nodes — creates expression node and recurses into all input pins. */
	int32 CompileCombinatorNode(EPrerequisiteExpressionType Type, UEdGraphNode* Node, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths,	FPrerequisiteExpression& OutExpression);
	
	UQuestlineGraph* RootGraph = nullptr;

	/**
	 * Accumulates linked-placement GUIDs as compilation recurses through LinkedQuestline nodes. Combined with each content
	 * node's own GUID at instance assignment time so that multiple placements of the same linked asset produce runtime
	 * instances with distinct QuestContentGuids — required for both rename detection and per-instance save state.
	 * Zero at top-level; push/pop save-restore around each LinkedQuestline recursion.
	 */
	FGuid CurrentOuterGuidChain;

	/** Deterministic compound of two GUIDs; asymmetric so (Outer, Inner) differs from (Inner, Outer). */
	static FGuid CombineGuids(const FGuid& Outer, const FGuid& Inner);

public:
	/** Accumulated compiler messages from the most recent Compile() call. */
	const TArray<TSharedRef<FTokenizedMessage>>& GetMessages() const { return Messages; }
	int32 GetNumErrors() const { return NumErrors; }
	int32 GetNumWarnings() const { return NumWarnings; }
	const TMap<FName, FName>& GetDetectedRenames() const { return DetectedTagRenames; }
	
};
