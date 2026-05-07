// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/QuestNodeBase.h"
#include "Signals/SignalSubsystem.h"


/**
 * Multi-tag publish helper. Routes a quest event to every channel a node carries — ContextualTag (primary
 * parent-context channel) plus every entry in AssetScopedAliasTags (one per enclosing LinkedQuestline asset
 * perspective). Centralizes the multi-publish discipline at a single mutation site so subscribers reach via
 * any asset-scoped perspective without forcing every caller to hand-roll the iteration.
 *
 * Each publish gets its own copy of Event with Event.QuestTag set to the channel being published on;
 * subscribers' Event.GetQuestTag() reflects the channel they subscribed via. Event.Context (when present,
 * e.g. on FQuestStartedEvent) stays invariant across publishes — Context.NodeInfo.QuestTag is the canonical
 * ContextualTag, set during AssembleEventContext before this helper fires.
 *
 * Architectural note: a subscriber bound to a common ancestor across multiple publish channels (e.g. the
 * SimpleQuest.Quest root) will see one event per publish call. That's intentional — subscribers at narrower
 * asset-perspectives see exactly one event matching their natural perspective. Filter via Event.QuestTag if
 * root-level dedup is needed.
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

        const FGameplayTag ContextualTag = Node->GetContextualTag();
        if (ContextualTag.IsValid())
        {
            Event.QuestTag = ContextualTag;
            Signals->PublishMessage(ContextualTag, Event);
        }

        for (const FGameplayTag& AliasTag : Node->GetAssetScopedAliasTags())
        {
            if (AliasTag.IsValid())
            {
                Event.QuestTag = AliasTag;
                Signals->PublishMessage(AliasTag, Event);
            }
        }
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