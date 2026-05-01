// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/QuestPrereqDebugState.h"
#include "Widgets/SCompoundWidget.h"
#include "Types/PrereqExaminerTypes.h"

class UEdGraph;
class UEdGraphNode;
class SBox;
struct FEdGraphEditAction;
struct FSlateBrush;

/**
 * Identifies the parent operator that a child box renders inside. Drives the box's outline color — AND children inherit
 * green, OR children cyan, NOT child red, and Default (top-level, or inside a RuleRef's inner expression) falls back to
 * the near-black panel outline.
 */
enum class EPrereqBoxOutline : uint8
{
    Default,
    AndChild,
    OrChild,
    NotChild,
};

/**
 * Panel rendering the pinned prerequisite expression as a nested composite of labeled rounded boxes. Each combinator level
 * places its operator label between its operands (vertical algebraic layout); the NOT operator label sits above its single
 * operand and the operand itself gains a red outline (the inversion signal lives on the negated child, not on the NOT frame).
 * Combinators and rule references are individually collapsible — collapsed boxes render as compact summary pills.
 *
 * Hover coordination: when a designer hovers an operator label, the panel stores that combinator's GUID in HoveredOperatorGuid.
 * Every operator label at the same level (n-input combinators render n-1 labels for one combinator node, all sharing a GUID)
 * saturates its text color, and every immediate child box saturates its outline color — via TAttribute lambdas that read the
 * panel state each paint. Leaves still gain a brighter fill on direct hover; the two hover visuals are mutually exclusive in
 * practice since the cursor can only be in one place.
 */
class SPrereqExaminerPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPrereqExaminerPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SPrereqExaminerPanel();

    /** Pin a new context node (or nullptr to clear). Resets collapse state, unsubscribes prior graph, rewalks, rebuilds. */
    void PinContextNode(UEdGraphNode* ContextNode);

    /** Re-walk the currently-pinned context without changing graph subscriptions. Collapse state survives. */
    void Refresh();

    /** Invoked by an SPrereqExaminerOperatorButton on mouse enter/leave - stores the hovered combinator's GUID for
        downstream saturation lookups, and triggers a paint invalidation so TAttribute lambdas re-evaluate. */
    void SetHoveredOperator(const FGuid& Guid);
    const FGuid& GetHoveredOperatorGuid() const { return HoveredOperatorGuid; }

    /**
     * Routes a graph-node halo on behalf of a hovered leaf/operator widget. Tracks the highlighted node so the panel
     * can clear it on rebuild - destroyed widgets don't fire OnMouseLeave to clean up after themselves, so without
     * panel-side tracking the cross-editor halo persists on the destination node after a collapse/expand.
     */
    void SetGraphHighlight(UEdGraphNode* Node);

    /** Clears the active graph-node halo set via SetGraphHighlight. */
    void ClearGraphHighlight();

private:
    // ---- Composition ----
    TSharedRef<SWidget> BuildHeader();
    TSharedRef<SWidget> BuildExpressionWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer = false);
    TSharedRef<SWidget> BuildLeafWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer = false);
    TSharedRef<SWidget> BuildCombinatorWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer = false);
    TSharedRef<SWidget> BuildNotWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer = false);
    TSharedRef<SWidget> BuildRuleRefWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer = false);
    TSharedRef<SWidget> BuildCollapsedPill(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer = false);
    TSharedRef<SWidget> BuildOperatorLabel(int32 NodeIndex, FText OperatorText, FSlateColor NormalColor, FSlateColor SaturatedColor);

    /** Shared brush-attribute builder for outer SBorder wrappers (combinator, NOT, RuleRef). Returns an attribute that
        picks the kind-parameterized normal brush, or its parent-hover saturated variant when the hovered operator matches
        ParentOperatorGuid. */
    TAttribute<const FSlateBrush*> MakeOuterBrushAttribute(const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind);

    void RebuildAll();

    // ---- Collapse state ----
    bool IsCollapsed(const FGuid& NodeGuid) const;
    void SetCollapsed(const FGuid& NodeGuid, bool bCollapsed);
    void ToggleCollapsed(const FGuid& NodeGuid);
    void ExpandAll();
    void CollapseAll();

    // ---- Graph subscription ----
    void SubscribeToContextGraph();
    void UnsubscribeFromContextGraph();
    void HandleGraphChanged(const FEdGraphEditAction& Action);

    // ---- Header actions ----
    FReply HandleExpandAllClicked();
    FReply HandleCollapseAllClicked();
    FReply HandleRefreshClicked();
    FReply HandleHeaderEntryNavigationClicked();

    // ---- Data ----
    FPrereqExaminerTree Tree;
    TMap<FGuid, bool> CollapsedByNodeGuid;
    FGuid HoveredOperatorGuid;
    
    /** Last node passed to SetGraphHighlight; tracked so ClearGraphHighlight can hit the right editor when the
    originating widget is destroyed (RebuildAll mid-hover) and no longer fires its OnMouseLeave. */
    TWeakObjectPtr<UEdGraphNode> LastHighlightedNode;

    // ---- Widgets ----
    TSharedPtr<SBox> HeaderSlot;
    TSharedPtr<SBox> ExpressionSlot;

    // ---- Subscription ----
    TWeakObjectPtr<UEdGraph> SubscribedGraph;
    FDelegateHandle GraphChangedHandle;

    // ---- Debug ----

    /** While PIE debug channel is active, invalidates paint every tick so leaf/combinator tints reflect live fact changes. */
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
    
    /** Recursively computes a debug state for the given tree node. Leaves query FQuestPIEDebugChannel directly;
    combinators / RuleRefs roll up from their children. Returns Unknown when not in PIE. */
    EPrereqDebugState ComputeDebugState(int32 NodeIndex) const;
};