// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "Quests/PrerequisiteExpression.h"

class UQuestlineGraph;
class UQuestlineNode_ContentBase;
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
	virtual TArray<FName> CompileGraph(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TMap<FGameplayTag, TArray<FName>>* OutEntryTagsByOutcome = nullptr);	

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

	/**
	 * Rules for moving between nodes. Subclass and register via ISimpleQuestEditorModule interface to override classification logic.
	 * 
	 * For creating new node types, prefer to subclass UQuestlineNodeBase and override internal classification methods such as IsExitNode, etc.
	 */
	TUniquePtr<FQuestlineGraphTraversalPolicy> TraversalPolicy;
	
	virtual void RegisterCompiledTags(UQuestlineGraph* InGraph);

	/** Renames detected during the most recent Compile(), keyed as FName (old → new). */
	TMap<FName, FName> DetectedTagRenames;
	
private:

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

	UQuestlineGraph* RootGraph = nullptr;

public:
	/** Accumulated compiler messages from the most recent Compile() call. */
	const TArray<TSharedRef<FTokenizedMessage>>& GetMessages() const { return Messages; }
	int32 GetNumErrors() const { return NumErrors; }
	int32 GetNumWarnings() const { return NumWarnings; }
	const TMap<FName, FName>& GetDetectedRenames() const { return DetectedTagRenames; }
	
};
