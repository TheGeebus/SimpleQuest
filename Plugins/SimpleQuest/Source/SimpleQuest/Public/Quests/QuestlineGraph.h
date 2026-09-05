// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/QuestStepEnums.h"
#include "UObject/Object.h"
#include "QuestlineGraph.generated.h"

#if !WITH_EDITOR
class FNativeGameplayTag;
#endif

class URewardSetDataAsset;
class UEdGraph;
class UQuestDisplayData;
class UQuestNodeBase;
class UQuestRewardBase;

struct FGameplayTag;

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
 * Compiler-stamped contextual→alias pair. One entry per (ContextualTag, AssetScopedAliasTag) pair on a node - a node with N
 * aliases (i.e., N enclosing LinkedQuestline depths) produces N entries here. Top-level nodes (no aliases) produce zero entries.
 * Persisted alongside CompiledQuestTags and exposed via GetAssetRegistryTags so editor utilities can discriminate legitimate
 * cross-asset inlinings from coincidental leaf-name collisions when walking the asset registry.
 */
USTRUCT()
struct FQuestCompiledNodeAlias
{
    GENERATED_BODY()

    UPROPERTY()
    FName ContextualFName;

    UPROPERTY()
    FName AliasFName;
};

/**
 * A set of rewards granted together. Exists only because a TArray can't be a TMap value directly - it's the wrapper
 * that lets questline-level rewards be keyed by outcome tag in a map. Authoring-side twin of the runtime
 * FQuestReachableRewards; each entry is a configured UQuestRewardBase (C++/Blueprint subclass or Generic), edited inline.
 */
USTRUCT(BlueprintType)
struct FQuestRewardSet
{
    GENERATED_BODY()

    /** Rewards granted for this outcome, in order. Same Instanced adapters as a Grant Rewards node's array. */
    UPROPERTY(EditAnywhere, Instanced, Category = "Reward")
    TArray<TObjectPtr<UQuestRewardBase>> Rewards;

    /**
     * Shared reward compositions this outcome also grants. Compilation flattens each set's contents in alongside the
     * inline rewards above, referenced sets first in the order listed, so the runtime sees one flat list.
     *
     * Soft on purpose: the compiler resolves it and the runtime never needs the asset, so a hard reference would drag
     * it into the cook for nothing. Same reasoning as a loot reward's table.
     */
    UPROPERTY(EditAnywhere, Category = "Reward")
    TArray<TSoftObjectPtr<URewardSetDataAsset>> RewardSets;
};

/**
 * A questline's own reward sets, compiled to a form the runtime can read without loading the source asset. A linked
 * questline is erased at compile time - its nodes are inlined into the enclosing graph and its own UQuestlineGraph is
 * never loaded at runtime - so its authored QuestlineRewards (which live only on that asset) would otherwise be
 * unreachable when it completes while embedded. The compiler duplicates each reward onto the owning (enclosing) graph
 * and records it here, keyed by outcome, so completion delivery and advertisement queries resolve rewards uniformly
 * whether the questline runs standalone or embedded.
 */
USTRUCT()
struct FQuestCompiledQuestlineRewards
{
    GENERATED_BODY()

    /** Reward sets by the outcome the questline resolves with, matched against a resolution's OutcomeTag at delivery. */
    UPROPERTY()
    TMap<FGameplayTag , FQuestRewardSet> RewardsByOutcome;
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
     * This questline's own identity tag name - SimpleQuest.Questline.<sanitized EffectiveID>. Stamped by the compiler
     * alongside the node tags above, because runtime CANNOT recompose it: turning an ID into a tag segment needs
     * SanitizeQuestlineTagSegment, which lives in the editor module. Every consumer reads this rather than rebuilding
     * the string, which is how three manager sites ended up composing a tag the compiler never minted.
     * FName for the same reason CompiledQuestTags is - written at compile time, where FGameplayTag is unreliable.
     * NAME_None on an asset not compiled since this field existed; the next compile fills it in.
     */
    UPROPERTY()
    FName CompiledIdentityTag;
    
    /**
     * Parallel to CompiledQuestTags: each entry pairs a node's ContextualTag with one AssetScopedAliasTag it carries. Empty
     * for top-level nodes (no LinkedQuestline ancestors). Used by editor utilities to tell legitimate inlinings of this
     * asset's nodes (which carry an alias matching another asset's standalone-perspective compiled tag) apart from
     * coincidental leaf-name matches in unrelated graphs. Surfaced via GetAssetRegistryTags as "CompiledNodeAliases".
     */
    UPROPERTY()
    TArray<FQuestCompiledNodeAlias> CompiledNodeAliases;

    /**
     * Tags of all content nodes directly reachable from this questline's Entry node. Populated by the compiler. Used by the
     * subsystem to know which nodes to activate when starting this questline. This is created at graph compilation time, so we
     * use FName because FGameplayTag is unreliable in an editor context. 
     */
    UPROPERTY()
    TArray<FName> EntryNodeTags;
    
    /**
     * Checksum of the authoring input this asset's compiled data was built from - this graph plus every questline it
     * links, transitively. Compared against a freshly computed value when the editor opens the asset to decide whether
     * the compiled data is current. Zero on assets compiled before this existed; the next compile fills it in.
     */
    UPROPERTY()
    uint32 CompiledSourceHash = 0;

    /**
     * All compiled node instances, keyed by tag. Owned by this asset. Populated by the compiler - includes nodes inlined from
     * linked questline graphs. The subsystem looks up and activates nodes directly from this map. This is created at graph
     * compilation time, so we use FName because FGameplayTag is unreliable in an editor context.
     */
    UPROPERTY()
    TMap<FName, TObjectPtr<UQuestNodeBase>> CompiledNodes;

    /**
     * Questline-level rewards compiled to a runtime-reachable form, keyed by questline-asset identity tag name.
     * Populated by the compiler: one entry for this graph's own QuestlineRewards, plus one for each linked questline
     * inlined into it (whose source asset is never loaded at runtime). The manager flattens these into its own
     * by-identity lookup at registration; delivery and reward queries read that lookup by a resolution's GraphTag
     * rather than the live asset, so an embedded questline's rewards resolve the same as a standalone one's. Keyed by
     * FName for the same reason CompiledNodes is: this is built at compile time, where FGameplayTag is unreliable.
     */
    UPROPERTY()
    TMap<FName, FQuestCompiledQuestlineRewards> CompiledQuestlineRewards;
    
    /**
     * Destinations wired from THIS questline's own Entry node Deactivated pin, split the way a content node's
     * Deactivated pin is: wires into Activate inputs in the first set, into Deactivate inputs in the second.
     * A top-level graph has no runtime node at its identity tag - UQuestlineNode_Entry mints no instance, and the
     * asset-level lifecycle is manager-owned (ActivateQuestlineGraph publishes Activated/Started and writes Live;
     * PublishGraphResolutions writes Completed) - so this routing is filed on the asset and dispatched by identity,
     * the same shape CompiledQuestlineRewards uses for the same reason. A Quest container or LinkedQuestline
     * placement carries its inner graph's equivalent on the node instance instead.
     */
    UPROPERTY()
    TArray<FName> EntryDeactivatedActivateTags;

    UPROPERTY()
    TArray<FName> EntryDeactivatedDeactivateTags;
    
    /**
     * GroupTags this graph's UActivationGroupListenerNode instances subscribe to. Stamped by the compiler after
     * registration. Surfaced via GetAssetRegistryTags so the manager can build an inverted GroupTag→graphs index at
     * startup and async-load listener graphs reachable from any currently-loaded graph's setters. Inherits via
     * LinkedQuestline inlining: a wrapper asset whose linked inner contains a listener carries the inner's listener
     * tags too (the inner's listener instances end up in the wrapper's CompiledNodes with their original GroupTags
     * preserved - ActivationGroup tags are authored, not contextualized).
     */
    UPROPERTY()
    TArray<FGameplayTag> ListenerGroupTags;

    /**
     * GroupTags this graph's UActivationGroupSetterNode instances publish on. Stamped by the compiler post-registration.
     * Surfaced via GetAssetRegistryTags so the manager can match this graph's outward signal surface against the global
     * listener-graph index when this graph registers - driving the reachability-walk that async-loads any listener graph
     * reachable from one of these tags. Inherits via LinkedQuestline inlining for the same reason as ListenerGroupTags.
     */
    UPROPERTY()
    TArray<FGameplayTag> OutwardSetterGroupTags;
    
    /**
     * Identifier used as the Gameplay Tag scope for all quests in this questline. Must be unique across the project. Defaults
     * to the asset name if left empty. Override this when you need a stable tag namespace independent of the asset name,
     * or to disambiguate duplicate assets.
     *
     * Format: SimpleQuest.Questline.<QuestlineID>.<QuestNodeLabel>
     */
    UPROPERTY(EditAnywhere)
    FString QuestlineID;

    /**
     * Designer-facing display name shown in editor surfaces that need a human-readable label - LinkedQuestline node
     * titles, outliner roots, asset tooltips. Falls back to the asset short name when empty. Unlike QuestlineID, this
     * is purely presentational - changing it never affects compiled tag identity, so designers can rename freely.
     */
    UPROPERTY(EditAnywhere)
    FText DisplayName;
    
    /**
     * Flavor / context blurb for the questline as a whole. Shown alongside DisplayName in quest log overview, journal
     * entries, and asset tooltips. Empty by default. Multi-line authoring.
     */
    UPROPERTY(EditAnywhere, meta = (MultiLine = true))
    FText Description;

    /**
     * Optional richer UI metadata for the questline (overview icon, hero image, chapter art, etc.). Adopter UI casts to
     * its expected UQuestDisplayData subclass at consumption time. Queried at runtime via UQuestStateSubsystem::
     * GetDisplayData using a questline-level tag.
     */
    UPROPERTY(EditAnywhere)
    TObjectPtr<UQuestDisplayData> DisplayData;
    
    /**
     * Asset-level default for per-run resettability - the root rung of this questline's inherit walk. Inherit
     * (default) defers to the nearest ancestor: the host LinkedQuestline node when this graph is embedded, otherwise
     * Off (permanent) at the top level. On / Off overrides that, opting the whole questline's content in / out.
     * Content nodes and inner containers override this further (nearest wins). The compiler folds it into the
     * per-node effective bResettableReplay it stamps onto the runtime instances.
     */
    UPROPERTY(EditAnywhere)
    EResettableReplay ResettableReplay = EResettableReplay::Inherit;

    virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

#if !WITH_EDITOR
    TArray<TUniquePtr<FNativeGameplayTag>> RegisteredNativeTags;
#endif

public:
    virtual void PostLoad() override;
    virtual void PostDuplicate(bool bDuplicateForPIE) override;
    const TArray<FName>& GetEntryNodeTags() const { return EntryNodeTags; }
    const TMap<FName, TObjectPtr<UQuestNodeBase>>& GetCompiledNodes() const { return CompiledNodes; }
    const TArray<FName>& GetCompiledQuestTags() const { return CompiledQuestTags; }
    const TArray<FQuestCompiledNodeAlias>& GetCompiledNodeAliases() const { return CompiledNodeAliases; }
    const FString& GetQuestlineID() const { return QuestlineID; }
    const TArray<FGameplayTag>& GetOutwardSetterGroupTags() const { return OutwardSetterGroupTags; }
    const TArray<FGameplayTag>& GetListenerGroupTags() const { return ListenerGroupTags; }
    const TMap<FName, FQuestCompiledQuestlineRewards>& GetCompiledQuestlineRewards() const { return CompiledQuestlineRewards; }
    uint32 GetCompiledSourceHash() const { return CompiledSourceHash; }
    const TArray<FName>& GetEntryDeactivatedActivateTags() const { return EntryDeactivatedActivateTags; }
    const TArray<FName>& GetEntryDeactivatedDeactivateTags() const { return EntryDeactivatedDeactivateTags; }

    /**
     * This questline's own identity tag - what resolutions of its root-scope Exits attribute to, and the key its
     * questline-level rewards are filed under. Invalid until the asset has been compiled; callers branch on validity.
     */
    FGameplayTag GetIdentityTag() const;

    /**
     * Sets the tag-namespace identity for this questline, applying the same normalization the details panel does - a
     * whitespace-only ID is not empty, so it would defeat the asset-name fallback and compose a tag with an empty leaf.
     *
     * The ID must be unique across the project: the compiler refuses a graph whose ID is already claimed, so callers
     * creating questlines programmatically are responsible for choosing one that isn't taken.
     */
    void SetQuestlineID(const FString& InQuestlineID);
    
    const FText& GetDescription() const { return Description; }
    UQuestDisplayData* GetDisplayData() const { return DisplayData; }
    EResettableReplay GetResettableReplay() const { return ResettableReplay; }
    const TMap<FGameplayTag, FQuestRewardSet>& GetQuestlineRewards() const { return QuestlineRewards; }
    
    /** Identity used as both the QuestlineEffectiveID AR tag and this graph's section key in the compiled display ini. */
    FString GetEffectiveID() const { return QuestlineID.IsEmpty() ? GetName() : QuestlineID; }

    /**
     * Rewards granted on THIS QUESTLINE's completion, keyed by its top-level Exit outcome tags - first-class "the whole
     * questline pays this," distinct from a reward node before the final step (that's the final step's reward) and from
     * rewards wired on a linked-node's pins in a linking asset (that's per-placement as a step in another questline). Fires
     * on EVERY instantiation, standalone or linked, with no container required. Optional - leave empty for a questline
     * with no inherent completion reward that is independent of its placement in other graphs. This authored map IS the
     * runtime home - read directly at questline resolution (the manager resolves the questline's identity tag to this
     * graph and grants these rewards for the resolved outcome). Whole-questline data on the whole-questline object.
     */
    UPROPERTY(EditAnywhere, Category = "Rewards")
    TMap<FGameplayTag, FQuestRewardSet> QuestlineRewards;

#if WITH_EDITOR
    /**
     * "Tag=Name|Description|DisplayDataPath" records for the questline-self identity plus every compiled node carrying
     * a display payload, sorted by tag. Name/Description use the loc-preserving, quote-wrapped FText export form so they
     * survive a delimited round-trip. Accumulated by the editor's AccumulateCompiledDisplay, coalesced to the ini in EndCompileBatch.
     */
    TArray<FString> GetCompiledDisplayRecords() const;
    
    /** Normalizes QuestlineID on edit - see the implementation for why surrounding whitespace can't be allowed to survive. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    /**
     * DisplayName if set, otherwise the asset's short name. The single entry point for any editor surface that wants a human-readable
     * label for this questline - avoids scattered "is DisplayName empty?" checks.
     */
    FText GetDisplayName() const;

    /**
     * Returns the raw authored DisplayName field WITHOUT the asset-short-name fallback. Use when empty
     * carries semantic meaning - e.g., runtime registry writes where adopter sidebars rely on "empty
     * means drop this entry from the UI." GetDisplayName() above is for editor surfaces (outliner,
     * tooltips, asset chips) that always want a renderable label and don't carry the empty-as-meaningful
     * convention.
     */
    const FText& GetAuthoredDisplayName() const { return DisplayName; }

    // Editor-only: the actual UEdGraph object is only needed in the editor. The data it represents is compiled in-editor for use at runtime
#if WITH_EDITORONLY_DATA
public:	
    /**
     * The questline graph object. Contains Quest nodes and the wiring between them.
     */
    UPROPERTY()
    TObjectPtr<UEdGraph> QuestlineEdGraph;

    /**
     * Compiled editor nodes, keyed by tag - mirrors CompiledNodes but holds the UEdGraphNode* for navigation.
     * Populated by the compiler alongside CompiledNodes. Serialized so navigation works without recompiling after reload.
     */
    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UEdGraphNode>> CompiledEditorNodes;
#endif

};
