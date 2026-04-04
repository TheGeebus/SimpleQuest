// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UQuestlineGraph;
class UQuestlineNode_ContentBase;
class UQuestNodeBase;
class UEdGraphPin;

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
	virtual ~FQuestlineGraphCompiler() = default;

    /** Entry point. Validates the asset, then kicks off recursive graph compilation. Returns true if there were no errors. */
	virtual bool Compile(UQuestlineGraph* InGraph);

protected:
	/**
	 * Compiles one graph level. Assigns QuestContentGuid and QuestTag to all content node CDOs, then resolves output pin
	 * wiring into NextNodesOnSuccess / NextNodesOnFailure. Recurses into linked questline graph assets.
	 *
	 * @param Graph					The questline graph asset to compile.
	 * @param TagPrefix				Sanitized questline ID used as the tag namespace for this graph's nodes.
	 * @param SuccessBoundaryTags	Tags injected when an Exit_Success node is reached (empty at top level).
	 * @param FailureBoundaryTags	Tags injected when an Exit_Failure node is reached (empty at top level).
	 * @param VisitedAssetPaths		Stack of asset paths currently open in the recursion, used for cycle detection.
	 * @return						Tags of the content nodes directly reachable from this graph's Entry node.
	 */
	virtual TArray<FGameplayTag> CompileGraph(UQuestlineGraph* Graph, const FString& TagPrefix,	const TArray<FGameplayTag>& SuccessBoundaryTags,
		const TArray<FGameplayTag>& FailureBoundaryTags, TArray<FString>& VisitedAssetPaths);
	
	/**
	 * Follows an output pin through knots, exit nodes, and linked questline nodes, collecting the gameplay tags of all terminal
	 * content nodes. Exit nodes return the appropriate boundary tag set. LinkedQuestline nodes are compiled recursively and
	 * their entry tags are returned in their place.
	 *
	 * @param FromPin				The output pin to trace.
	 * @param TagPrefix				Tag namespace of the currently compiling graph. Used to resolve the linked node's own downstream
	 *								connections before recursing into the linked asset.
	 * @param SuccessBoundaryTags	Forwarded to Exit_Success resolution.
	 * @param FailureBoundaryTags	Forwarded to Exit_Failure resolution.
	 * @param VisitedAssetPaths		Cycle detection stack, shared with CompileGraph.
	 * @param OutTags				Accumulates the resolved tags.
	 */
	virtual void ResolvePinToTags(UEdGraphPin* FromPin,	const FString& TagPrefix, const TArray<FGameplayTag>& SuccessBoundaryTags,
		const TArray<FGameplayTag>& FailureBoundaryTags, TArray<FString>& VisitedAssetPaths, TArray<FGameplayTag>& OutTags);
	
	/**
	 * Sanitizes a designer-entered node label into a valid Gameplay Tag segment. Replaces spaces and invalid characters with
	 * underscores, removes leading/trailing whitespace.
	 */
	virtual FString SanitizeTagSegment(const FString& InLabel) const;

	/** Logs a compile error and sets the internal error flag. */
	void AddError(const FString& Message);

	/** Logs a compile warning without setting the internal error flag. */
	void AddWarning(const FString& Message);

	/** Internal questline compiler error flag. Returned by the main Compile function. */
	bool bHasErrors = false;

	/** Accumulates all compiled node classes across the full recursive compilation run. Written to the top-level graph by Compile(). */
	TMap<FGameplayTag, TSubclassOf<UQuestNodeBase>> AllCompiledNodeClasses;

private:
	/**
	 * Returns the UQuestNodeBase CDO for a Quest or Leaf content node. Returns null for LinkedQuestline nodes (compiler-only,
	 * no runtime class) and for Quest/Leaf nodes with no class assigned.
	 */
	UQuestNodeBase* GetCDOForContentNode(UQuestlineNode_ContentBase* ContentNode) const;

	/**
	 * Returns true if following the output pin through any knot chain leads directly to an Exit node. Used to determine whether
	 * bCompletesParentGraph should be set on a content node's CDO.
	 */
	static bool PinLeadsToExit(UEdGraphPin* FromPin);
};
