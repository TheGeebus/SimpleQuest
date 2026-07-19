// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNodeBase.h"
#include "Quests/Types/QuestStepEnums.h"
#include "QuestlineNode_ContentBase.generated.h"

class UQuestDisplayData;

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_ContentBase : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	UQuestlineNode_ContentBase();
	virtual bool IsContentNode() const override { return true; }
	
	virtual void AllocateDefaultPins() override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PostPlacedNewNode() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostPasteNode() override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual void EnsureDeactivationPinsForAutowire() override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// Display name set by the designer in the graph
	UPROPERTY(EditAnywhere, Category = "Quest")
	FText NodeLabel;

	/**
	 * UI-friendly title for this node (quest log entries, HUD labels, journal). Compiler copies this onto the
	 * corresponding runtime UQuestNodeBase at compile time; runtime queries read it via
	 * UQuestStateSubsystem::GetDisplayName.
	 *
	 * Empty by default. Empty means "don't pipeline anything into the display" — designers who don't want this
	 * node's tag to surface in UI leave it blank. The editor-visible NodeLabel is NOT substituted; it stays an
	 * organizational identity (graph editor, outliner, compile logs) separate from UI text.
	 */
	UPROPERTY(EditAnywhere, Category = "Display")
	FText DisplayName;

	/**
	 * Flavor / context blurb for this node. Multi-line. Empty by default. Compiler copies onto the runtime node;
	 * runtime queries read via UQuestStateSubsystem::GetDisplayDescription.
	 */
	UPROPERTY(EditAnywhere, Category = "Display", meta = (MultiLine = true))
	FText Description;

	/**
	 * Optional reference to a UQuestDisplayData asset for richer UI metadata (icon, hints, callouts, parametrized text,
	 * etc.). Adopters subclass UQuestDisplayData and instance the .uasset per node that needs more than DisplayName /
	 * Description. Compiler copies the reference onto the runtime node; UI consumers retrieve via
	 * UQuestStateSubsystem::GetDisplayData and cast to their expected subclass.
	 */
	UPROPERTY(EditAnywhere, Category = "Display")
	TObjectPtr<UQuestDisplayData> DisplayData;
	
	/**
	 * Returns true if ProposedLabel is available for this node — i.e., no other content node on the same direct graph
	 * currently displays it as a live label AND no other content node on the same graph still holds it as its last-
	 * compiled identity. The second check is what blocks the in-batch collision case: a designer renames Node 1 from
	 * X to Y, then before recompiling tries to rename Node 2 to X. Live state says X is free (Node 1 displays Y now),
	 * but Node 1's compiled identity is still X until the next compile rewrites the redirect map, so X is effectively
	 * pinned. OutError carries a human-readable explanation when the function returns false.
	 */
	bool IsLabelAvailable(const FString& ProposedLabel, FText& OutError) const;

	UPROPERTY(EditAnywhere, Category = "Quest")
	bool bShowDeactivationPins = false;
	
	/**
	 * Per-run resettability for replayable content. Inherit (default) takes the nearest ancestor container's
	 * setting (root = permanent); On opts this node and inheriting descendants into per-run resettable gating; Off
	 * overrides out to permanent. The compiler resolves the effective value through the alias (host-scoped)
	 * hierarchy and stamps it onto the runtime node. Applies to this node's structural resolution (Path /
	 * Resolution / Entry); no effect on Outcome or raw Fact gates.
	 */
	UPROPERTY(EditAnywhere, Category = "Quest")
	EResettableReplay ResettableReplay = EResettableReplay::Inherit;

	/**
	 * Persistent expanded/collapsed state for the "Givers" list shown by content-node Slate widgets. Lifted to
	 * the base so every content-node widget that surfaces givers shares one storage location — avoids duplicating
	 * the flag on every subclass that ends up with a giver affordance.
	 */
	UPROPERTY(Transient)
	bool bGiversExpanded = false;

	/** Persistent expanded/collapsed state for the "Grants" list on a linked-questline node's Slate widget. */
	UPROPERTY(Transient)
	bool bRewardsExpanded = false;

	/**
	 * Set by content-node Slate widgets when the reconstructed tag from current labels diverges from the last
	 * compiled tag. Lifts Step's original widget-local flag to the base so every content-node widget can display the
	 * same "Recompile to update tags" affordance. Cleared on first matching refresh post-compile.
	 */
	UPROPERTY(Transient)
	bool bTagStale = false;

protected:
	virtual FString GetDefaultNodeBaseName() const { return TEXT("Node"); }

	/**
	 * Regenerate QuestGuid and sweep NodeLabel for uniqueness within the current graph. Shared body used by
	 * PostPlacedNewNode (fresh placement), PostDuplicate (programmatic StaticDuplicateObject paths), and
	 * PostPasteNode (user Ctrl-D / copy-paste via SGraphEditor's ExportText/ImportText round-trip).
	 *
	 * Strips any trailing "_N" counter on the current label to get a root name, then sweeps from counter+1
	 * upward — so "GetBook_1" re-duplicated becomes "GetBook_2", not "GetBook_1_1". Fresh placements where
	 * NodeLabel is empty fall back to GetDefaultNodeBaseName().
	 */
	void EnsureUniqueLabel();
	
	/**
	 * Override to insert custom output pins between Prerequisites and Any Outcome. Do NOT override AllocateDefaultPins — the
	 * base class controls pin ordering to guarantee Deactivate always appears at the bottom.
	 */
	virtual void AllocateOutcomePins() {}

	/** Notify graph + owned inner graphs recursively so widgets re-evaluate stale tag state. Container subclasses
	(Quest) override to walk their inner graph; default is no-op. */
	virtual void NotifyInnerGraphsOfRename() {}

	/** Shared side-effect for both inline rename (OnRenameNode) and Details-panel NodeLabel edit
	(PostEditChangeProperty). Fires NotifyGraphChanged on this node's graph plus any owned inner graphs. */
	void NotifyRenameSideEffects();

private:
	/** Snapshot of NodeLabel taken in PreEditChange so PostEditChangeProperty can revert if the new value fails validation. */
	UPROPERTY(Transient)
	FText PreEditNodeLabelSnapshot;
};
