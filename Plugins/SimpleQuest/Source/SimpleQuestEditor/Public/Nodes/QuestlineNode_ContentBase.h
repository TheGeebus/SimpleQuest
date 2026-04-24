// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNodeBase.h"
#include "QuestlineNode_ContentBase.generated.h"

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
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual void EnsureDeactivationPinsForAutowire() override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// Display name set by the designer in the graph
	UPROPERTY(EditAnywhere, Category = "Quest")
	FText NodeLabel;

	// Stable identity for this node. Used to derive QuestID at compile time. Generated once on placement and regenerated
	// on duplication. Never hand-edited.
	UPROPERTY(VisibleAnywhere, Category = "Quest")
	FGuid QuestGuid;

	UPROPERTY(EditAnywhere, Category = "Quest")
	bool bShowDeactivationPins = false;

	/** Persistent expanded/collapsed state for the "Givers" list shown by content-node Slate widgets. Lifted to
	the base so every content-node widget that surfaces givers shares one storage location — avoids duplicating
	the flag on every subclass that ends up with a giver affordance. */
	UPROPERTY(Transient)
	bool bGiversExpanded = false;

	/** Set by content-node Slate widgets when the reconstructed tag from current labels diverges from the last
	compiled tag. Lifts Step's original widget-local flag to the base so every content-node widget can display the
	same "Recompile to update tags" affordance. Cleared on first matching refresh post-compile. */
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
	void EnsureFreshIdentityAndUniqueLabel();
	
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
	

};
