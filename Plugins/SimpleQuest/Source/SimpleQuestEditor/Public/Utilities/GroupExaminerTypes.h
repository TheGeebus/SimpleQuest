// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UEdGraphNode;
class UQuestlineGraph;

/**
 * One concrete reference within a group-topology endpoint. For a setter endpoint, an upstream content-node source feeding
 * the setter's Activate input; for a getter endpoint, a downstream destination reached by the getter's Forward output.
 * Weak refs so examiner widgets survive asset unloads gracefully.
 */
struct FGroupExaminerReference
{
	TWeakObjectPtr<UEdGraphNode>     Node;
	TWeakObjectPtr<UQuestlineGraph>  Asset;
	FString                          PinLabel;
};

/**
 * One setter or getter endpoint in the group topology. For activation groups (v1), References is a flat list of sources
 * or destinations from the appropriate directional walker. Prereq extension will layer richer per-condition structure on
 * top — a future FGroupExaminerPrereqEndpoint subclass or parallel type can replace References with a nested condition
 * representation while keeping the topology shape (Setters + Getters lists) unchanged.
 */
struct FGroupExaminerEndpoint
{
	TWeakObjectPtr<UEdGraphNode>        Node;
	TWeakObjectPtr<UQuestlineGraph>     Asset;
	TArray<FGroupExaminerReference>     References;
};

/**
 * Full topology for one GroupTag across all Questline assets in the project. Populated by an AR-scan pass that sync-loads
 * matching assets, iterates their graphs for nodes with matching GroupTag, and walks each via CollectEffectiveSources /
 * CollectActivationTerminals. Scope is cross-project (not compile-tree-scoped like Surface D) because the examiner's job
 * is an authoring-time "where does this group come from anywhere" answer.
 */
struct FGroupExaminerTopology
{
	FGameplayTag                        GroupTag;
	TArray<FGroupExaminerEndpoint>      Setters;
	TArray<FGroupExaminerEndpoint>      Getters;
};