// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/QuestNodeBase.h"
#include "Signals/SignalSubsystem.h"


/**
 * Multi-tag publish helper. Routes a quest event to every channel a node carries — ContextualTag (canonical
 * Stack[0], the deepest contextualization) plus every entry in AssetScopedAliasTags (one per enclosing
 * LinkedQuestline asset perspective). Centralizes the multi-publish discipline at a single mutation site so
 * subscribers reach via any asset-scoped perspective without forcing every caller to hand-roll the iteration.
 *
 * Channels route, payloads decide. The bus delivers identical payloads across all subscribers; one delivery per
 * subscription handle by default (deduplicate-on). Event.QuestTag is set once to ContextualTag (canonical) before
 * publishing — subscribers reading Event.QuestTag for "what fired" semantics see the canonical perspective uniformly.
 * The callback's first arg carries delivery metadata: which channel from the publishing set best matched this
 * subscription's bound tag. Subscribers bound at broad ancestors (e.g. SimpleQuest.Quest root) fire exactly once
 * per logical publish, with the callback arg set to the longest descendant channel in the publishing set; subscribers
 * bound at a specific asset perspective fire once with the callback arg set to that perspective's channel.
 *
 * Event.Context (when present, e.g. on FQuestStartedEvent) stays invariant across deliveries — Context.NodeInfo
 * .QuestTag is also the canonical ContextualTag, set during AssembleEventContext before this helper fires.
 *
 * Sibling to FQuestLifecycleQuery / FQuestActivationGuard / FQuestCatchUpFanout in Public/Utilities/.
 */
namespace FQuestPublish
{
    /**
     * Publishes Event on Node's ContextualTag and every AssetScopedAliasTag. No-ops if Signals or Node is null.
     * Skips publish on individual tags that fail IsValid().
     *
     * EventType must be a subclass of FQuestEventBase (i.e. carry a public FGameplayTag QuestTag field).
     * Templated so each event type instantiates its own copy semantics correctly; passed by value so per-channel
     * mutation of Event.QuestTag doesn't leak back to the caller.
     */
    template <typename EventType>
    void OnAllNodeTags(USignalSubsystem* Signals, const UQuestNodeBase* Node, EventType Event)
    {
        if (!Signals || !Node) return;

        TArray<FGameplayTag> Channels;
        Channels.Reserve(1 + Node->GetAssetScopedAliasTags().Num());

        const FGameplayTag ContextualTag = Node->GetContextualTag();
        if (ContextualTag.IsValid()) Channels.Add(ContextualTag);

        for (const FGameplayTag& AliasTag : Node->GetAssetScopedAliasTags())
        {
            if (AliasTag.IsValid()) Channels.Add(AliasTag);
        }

        if (Channels.IsEmpty()) return;

        // Set canonical identity once on the payload — Stack[0] / ContextualTag is the publisher's "what this event IS"
        // signal. Subscribers reading Event.QuestTag for branch logic see this canonical perspective uniformly across
        // every delivery. The bus's per-delivery best-match channel arrives separately as the callback's first arg
        // (delivery metadata; see SimpleCore signal subsystem's MULTI-CHANNEL PUBLISH CONTRACT).
        Event.QuestTag = Channels[0];
        Signals->PublishMessageOnChannels(MoveTemp(Channels), Event);
    }

    /**
     * Convenience overload for call sites that don't have a UQuestNodeBase pointer on hand (e.g. block-request
     * handlers receiving just a tag). Looks up the node from a ContextualTag-keyed map; falls back to a single
     * publish on FallbackTag if the lookup fails. The fallback path preserves current single-publish behavior
     * for degenerate cases (request fired against an unloaded tag) without losing the event entirely.
     */
    template <typename EventType, typename TLoadedNodeMap>
    void OnAllTagsForRequest(USignalSubsystem* Signals, FGameplayTag FallbackTag, const TLoadedNodeMap& LoadedNodeInstances, EventType Event)
    {
        if (!Signals) return;

        if (FallbackTag.IsValid())
        {
            if (const TObjectPtr<UQuestNodeBase>* InstancePtr = LoadedNodeInstances.Find(FallbackTag.GetTagName()))
            {
                if (UQuestNodeBase* Node = *InstancePtr)
                {
                    OnAllNodeTags(Signals, Node, Event);
                    return;
                }
            }

            // Fallback — node not loaded under this tag; single-publish on the request's tag preserves observability.
            Event.QuestTag = FallbackTag;
            Signals->PublishMessage(FallbackTag, Event);
        }
    }
}