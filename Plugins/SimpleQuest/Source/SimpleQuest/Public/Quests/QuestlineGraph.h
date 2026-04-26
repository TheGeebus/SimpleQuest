// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "QuestlineGraph.generated.h"

#if !WITH_EDITOR
class FNativeGameplayTag;
#endif

class UQuestNodeBase;
class UEdGraph;

USTRUCT()
struct FQuestTagRename
{
    GENERATED_BODY()
    
    UPROPERTY()
    FName OldTag;
    
    UPROPERTY()
    FName NewTag;
};

/**
 * Authoring container for a questline, a directed graph of quest and step nodes. Owns a UEdGraph (QuestlineEdGraph) containing
 * the visual layout, and holds the compiler output used at runtime: entry node tags and the full node tag-to-class registry
 * (including nodes from linked questline assets inlined at compile time). This is the asset type the designer creates and
 * opens in the graph editor.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestlineGraph : public UObject
{
    GENERATED_BODY()

    friend class FQuestlineGraphCompiler;
    friend class UQuestManagerSubsystem;

private:
    /**
     * The tags representing the quests on this graph. Used for runtime lookups and event dispatch. Uses FName because we will
     * need to register these on game start.
     */
    UPROPERTY()
    TArray<FName> CompiledQuestTags;

    /**
     * Tag renames detected during compilation, persisted for deferred propagation to unloaded actors. Chain-collapsed across compiles.
     */
    UPROPERTY()
    TArray<FQuestTagRename> PendingTagRenames;

    /**
     * Tags of all content nodes directly reachable from this questline's Entry node. Populated by the compiler. Used by the
     * subsystem to know which nodes to activate when starting this questline. This is created at graph compilation time, so we
     * use FName because FGameplayTag is unreliable in an editor context. 
     */
    UPROPERTY()
    TArray<FName> EntryNodeTags;

    /**
     * All compiled node instances, keyed by tag. Owned by this asset. Populated by the compiler — includes nodes inlined from
     * linked questline graphs. The subsystem looks up and activates nodes directly from this map. This is created at graph
     * compilation time, so we use FName because FGameplayTag is unreliable in an editor context.
     */
    UPROPERTY()
    TMap<FName, TObjectPtr<UQuestNodeBase>> CompiledNodes;

    /**
     * Identifier used as the Gameplay Tag scope for all quests in this questline. Must be unique across the project. Defaults
     * to the asset name if left empty. Override this when you need a stable tag namespace independent of the asset name,
     * or to disambiguate duplicate assets.
     *
     * Format: SimpleQuest.Quest.<QuestlineID>.<QuestNodeLabel>
     */
    UPROPERTY(EditAnywhere)
    FString QuestlineID;

    /**
     * Designer-facing display name shown in editor surfaces that need a human-readable label — LinkedQuestline node
     * titles, outliner roots, asset tooltips. Falls back to the asset short name when empty. Unlike QuestlineID, this
     * is purely presentational — changing it never affects compiled tag identity, so designers can rename freely.
     */
    UPROPERTY(EditAnywhere)
    FText FriendlyName;

    virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

#if !WITH_EDITOR
    TArray<TUniquePtr<FNativeGameplayTag>> RegisteredNativeTags;
#endif

public:
    virtual void PostLoad() override;
    const TArray<FName>& GetEntryNodeTags() const { return EntryNodeTags; }
    const TMap<FName, TObjectPtr<UQuestNodeBase>>& GetCompiledNodes() const { return CompiledNodes; }
    const TArray<FName>& GetCompiledQuestTags() const { return CompiledQuestTags; }
    const FString& GetQuestlineID() const { return QuestlineID; }
    const TArray<FQuestTagRename>& GetPendingTagRenames() const { return PendingTagRenames; }
    void ClearPendingTagRenames() { PendingTagRenames.Empty(); }

    /**
     * FriendlyName if set, otherwise the asset's short name. The single entry point for any editor surface that wants a human-readable
     * label for this questline — avoids scattered "is FriendlyName empty?" checks.
     */
    FText GetDisplayName() const;

    // Editor-only: the actual UEdGraph object is only needed in the editor. The data it represents is compiled in-editor for use at runtime
#if WITH_EDITORONLY_DATA
public:	
    /**
     * The questline graph object. Contains Quest nodes and the wiring between them.
     */
    UPROPERTY()
    TObjectPtr<UEdGraph> QuestlineEdGraph;

    /**
     * Compiled editor nodes, keyed by tag — mirrors CompiledNodes but holds the UEdGraphNode* for navigation.
     * Populated by the compiler alongside CompiledNodes. Serialized so navigation works without recompiling after reload.
     */
    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UEdGraphNode>> CompiledEditorNodes;
#endif

};