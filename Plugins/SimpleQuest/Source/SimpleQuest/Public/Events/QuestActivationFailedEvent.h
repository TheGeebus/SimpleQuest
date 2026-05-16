// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "QuestActivationFailedEvent.generated.h"

/**
 * Fired when an activation attempt against a quest is refused. Covers the four refusal paths in
 * UQuestManagerSubsystem::ActivateNodeByTag:
 *   - UnknownQuest: tag has no loaded UQuestNodeBase instance (stale tag, unloaded asset, or never-compiled name).
 *   - AlreadyLive: a Step is already Live and a re-activation cascade reached it; the diamond guard refused the
 *     redundant fire.
 *   - AlreadyPendingGiver: a Step is already in PendingGiver state; the diamond guard refused the redundant
 *     gate-fire.
 *   - Blocked: the quest's Blocked fact is set; the gate refused the actual SetQuestLive transition (giver-gated
 *     UI stays interactive on top of Block; only the Live transition is gated).
 *
 * Also fires at request-level when an FQuestActivationRequestEvent resolves to canonical tags that all fail the
 * LoadedNodeInstances lookup — covers the state-vs-cache desync case where the request was received but no node
 * could service it.
 *
 * Distinct from FQuestGiveBlockedEvent: that surfaces the give-time blocker check refusal (the Giver flow's
 * pre-ActivateNodeByTag gate). FQuestActivationFailedEvent covers the cascade-time refusals downstream of any
 * entry point — direct manager calls, request events, and the post-blocker-check Give flow all route through
 * ActivateNodeByTag and surface failures here.
 *
 * Subscription: bind per-tag for any specific quest, or at any registered ancestor tag for broader scope ("all
 * failures under this Questline"). The framework guarantees publishes always land on a registered channel: the
 * AlreadyLive / AlreadyPendingGiver / Blocked paths publish on the refused node's perspective set (registered by
 * definition since the node is loaded); the UnknownQuest path publishes on the failed tag itself when registered,
 * and otherwise walks up the tag's FName to find the first registered ancestor and publishes there (with that
 * ancestor's alias perspectives included if it's a loaded node). Adopters who try to bind exactly to a stale tag
 * receive RegisterQuestObserver's "stale tag — skipping subscribe" warning, which is the correct surface for that
 * authoring mistake; observability for the failed activation flows through the registered ancestor instead.
 *
 * When Reason == UnknownQuest: Event.QuestTag is either the registered failed tag (state-vs-cache desync) or
 * strict-invalid (stale tag — registry lookup failed). The framework normalizes loose-valid inputs (FName
 * preserved across BP serialization but tag absent from the registry) to strict-invalid, so adopters never see
 * the loose-valid intermediate state. AttemptedTagName carries the raw FName regardless — read that for "what
 * was attempted" when Event.QuestTag may be invalid. Other Reason values always carry valid registered tags.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestActivationFailedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestActivationFailedEvent() = default;

	FQuestActivationFailedEvent(const FGameplayTag InQuestTag, const FName InAttemptedTagName, const EQuestActivationBlocker InReason)
		: FQuestEventBase(InQuestTag), Reason(InReason), AttemptedTagName(InAttemptedTagName) {}

	FQuestActivationFailedEvent(const FGameplayTag InQuestTag, const FName InAttemptedTagName, const EQuestActivationBlocker InReason, const FQuestEventPayload& InPayload)
		: FQuestEventBase(InQuestTag, InPayload), Reason(InReason), AttemptedTagName(InAttemptedTagName) {}

	/**
	 * Why the activation was refused. One reason per event — the manager's gate is mutually exclusive (a single
	 * refusal branch fires per ActivateNodeByTag call). Designers branch on this to dispatch reason-specific
	 * reaction (locked-quest fanfare on Blocked, debug inspector entry on UnknownQuest, etc.).
	 */
	UPROPERTY(BlueprintReadOnly)
	EQuestActivationBlocker Reason = EQuestActivationBlocker::AlreadyLive;

	/**
	 * Raw FName of the attempted tag. Always populated, even when Event.QuestTag is invalid (stale-tag
	 * UnknownQuest case — FName preserved across BP serialization but the registry lookup fails). For all
	 * other paths this matches Event.QuestTag.GetTagName(). Use this for the consistent "what was attempted"
	 * identity regardless of registration state — debug overlays, telemetry, refusal-fanfare display.
	 */
	UPROPERTY(BlueprintReadOnly)
	FName AttemptedTagName;
};