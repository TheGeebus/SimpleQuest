// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SPrereqExaminerPanel.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "SimpleQuestEditor.h"
#include "Debug/QuestPIEDebugChannel.h"
#include "Debug/QuestPrereqDebugState.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleEntry.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleExit.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Toolkit/QuestlineGraphEditor.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

// ---------------------------------------------------------------------------
// Per-kind tints + shared rounded-box brushes. Static-initialized brushes are safe because Slate reads the brush at paint time
// and never mutates it — a single shared instance backs any number of SBorders. Per-widget tint variation comes from
// BorderBackgroundColor attributes (multiplies the brush colors), not the brush itself.
// ---------------------------------------------------------------------------

namespace PrereqExaminer_Style
{
    // Default tints for the operators, rule reference header text and border, and text for content nodes 
    const FLinearColor AndTint              = FLinearColor(FColor(100,  170,  100));    // green
    const FLinearColor OrTint               = FLinearColor(FColor(44,  140, 190));      // cyan
    const FLinearColor NotTint              = FLinearColor(FColor(170,  60,  60));      // red
    const FLinearColor RuleRefTint          = FLinearColor(FColor(162, 122,  34));      // amber
    const FLinearColor LeafTint             = FLinearColor(FColor(199, 199, 199));      // neutral text
    const FLinearColor LeafCategoryTint     = FLinearColor(FColor(130, 130, 130));      // muted — category prefix above leaf

    // Debug-state fill colors — opaque, used by the two-layer fill SImage's ColorAndOpacity. These are full-alpha; the
    // previous semi-transparent wash values in PrereqDebug_Style can be removed once this refactor is clean.
    const FLinearColor DebugNotStartedTint  = FLinearColor(FColor( 42, 42, 42));      // muted neutral grey — barely differentiated
    const FLinearColor DebugInProgressTint  = FLinearColor(FColor( 54,  44,  23));      // muted amber
    const FLinearColor DebugUnsatisfiedTint = FLinearColor(FColor( 60,  42,  42));      // muted red / rust
    const FLinearColor DebugSatisfiedTint   = FLinearColor(FColor( 44, 52, 44));      // muted green

    // Saturated variants applied when the corresponding operator's labels are hovered — boosts the parent operator + its
    // immediate children together so the visual group reads as one emphasized cluster.
    const FLinearColor AndTintSaturated     = FLinearColor(FColor(  0, 180,   0));
    const FLinearColor OrTintSaturated      = FLinearColor(FColor( 38,  120, 220));
    const FLinearColor NotTintSaturated     = FLinearColor(FColor(190,  35,  35));
    const FLinearColor RuleRefTintSaturated = FLinearColor(FColor(180, 160, 6));

    // Operator-box fill is near black; leaf fill is a shade lighter so content cells read as distinct from their container
    // frames even without contrasting outlines. Hover fill is shared — applied to leaves + pills on direct hover.
    const FLinearColor OperatorBoxFill      = FLinearColor(FColor(26,26,26));
    const FLinearColor OperatorBoxHoverFill = FLinearColor(FColor(30, 30, 30));
    const FLinearColor LeafBoxFill          = FLinearColor(FColor(42, 42, 42));
    const FLinearColor LeafBoxHoverFill     = FLinearColor(FColor(56, 56, 56));
    const FLinearColor HeaderBoxFill        = FLinearColor(FColor(30, 22,  6));
    const FLinearColor HeaderHoverFill      = FLinearColor(FColor(60, 44,  12));

    // Uniform layer padding — applied to every SVerticalBox/SHorizontalBox slot inside the expression tree so every
    // nested layer has the same spacing. Widget-intrinsic paddings (SPrereqExaminerBox::Padding,
    // SPrereqExaminerOperatorButton::ContentPadding) are 0; all layer padding flows from this one value.
    const FMargin BorderLayerPadding = FMargin(4.f, 8.f);
    constexpr float BorderThickness = 1.5f;
    constexpr float BoostedThickness = 2.5f;

    const FMargin OperatorLabelPadding = FMargin(4.f, 2.f);
    
    // ---- Leaf brushes (4.f radius, leaf fill, kind outline) ----
    const FSlateBrush* GetLeafBrush(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, OrTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, NotTint, BorderThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f); return &B; }
        }
    }
    
    const FSlateBrush* GetLeafBrushDirectHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f, OrTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f, NotTint, BorderThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f); return &B; }
        }
    }
    
    const FSlateBrush* GetLeafBrushParentHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, AndTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, OrTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, NotTintSaturated, BoostedThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f); return &B; }
        }
    }

    // ---- Operator-outer brushes (4.f radius, operator fill, kind outline) ----
    // No direct-hover variant: outer wrappers are SelfHitTestInvisible so they never receive direct hover.
    const FSlateBrush* GetOperatorOuterBrush(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(OperatorBoxFill, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(OperatorBoxFill, 4.0f, OrTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(OperatorBoxFill, 4.0f, NotTint, BorderThickness); return &B; }
            default:
                // Default kind — outline already invisible; fill was OperatorBoxFill, which redundantly matches the panel
                // background AND occludes parent outline pixels when this wrapper lives inside a zero-padded container
                // (e.g., a RuleRef). Fully transparent avoids the occlusion without changing any visible fill behavior.
                { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }
    
    const FSlateBrush* GetOperatorOuterBrushParentHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(OperatorBoxFill, 4.0f, AndTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(OperatorBoxFill, 4.0f, OrTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(OperatorBoxFill, 4.0f, NotTintSaturated, BoostedThickness); return &B; }
            default:
                // Default kind has no parent operator to hover, so this should just match the Normal variant — also
                // fully transparent for the same occlusion reason (see GetOperatorOuterBrush).
                { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }

    // ---- RuleRef header stripe — top-rounded corners, amber identity preserved across hover + parent-hover states ----
    const FSlateBrush* GetRuleRefHeaderBrush()
    {
        static const FSlateRoundedBoxBrush Brush(
            PrereqExaminer_Style::HeaderBoxFill, FVector4(4.f, 4.f, 0.f, 0.f), RuleRefTint, BorderThickness);
        return &Brush;
    }
    
    const FSlateBrush* GetRuleRefHeaderBrushHover()
    {
        static const FSlateRoundedBoxBrush Brush(
            PrereqExaminer_Style::HeaderHoverFill, FVector4(4.f, 4.f, 0.f, 0.f), RuleRefTint, BorderThickness);
        return &Brush;
    }

    /** RuleRef header parent-hover variant — thicker saturated amber outline for emphasis when the RuleRef's parent
        operator is hovered. Mirrors the "thicker + saturated" pattern used by GetOperatorOuterBrushParentHover. */
    const FSlateBrush* GetRuleRefHeaderBrushParentHover()
    {
        static const FSlateRoundedBoxBrush Brush(
            PrereqExaminer_Style::HeaderBoxFill, FVector4(4.f, 4.f, 0.f, 0.f), RuleRefTintSaturated, BoostedThickness);
        return &Brush;
    }
    
    // ---- Pill brushes (4.f radius, operator fill, kind outline) — pills are collapsed operators ----
    const FSlateBrush* GetPillBrush(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, OrTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, NotTint, BorderThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f); return &B; }
        }
    }
    
    const FSlateBrush* GetPillBrushDirectHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f, OrTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f, NotTint, BorderThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(LeafBoxHoverFill, 4.0f); return &B; }
        }
    }
    
    const FSlateBrush* GetPillBrushParentHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, AndTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, OrTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f, NotTintSaturated, BoostedThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(LeafBoxFill, 4.0f); return &B; }
        }
    }
    
    /** Expression-area background — flat OperatorBoxFill extending to the panel's edges. Fills the region behind the
    scrollable expression tree so there's no visible gap between the last term's box and the panel's bottom edge. */
    const FSlateBrush* GetPanelBgBrush()
    {
        static const FSlateRoundedBoxBrush Brush(OperatorBoxFill, 0.0f);
        return &Brush;
    }

    /** Fully transparent rounded-box brush. Used as a "structural but invisible" wrapper where a widget needs a real
    FSlateBrush for Slate's layout path, but nothing should render. Prefer this over FAppStyle's "NoBorder" brush,
    which has DrawAs=NoDrawType and can get special-cased in layout/measurement. */
    const FSlateBrush* GetInvisibleBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

        // ---- Two-layer refactor (agenda item 7 Session B) -------------------------------------------------------------
    // Each widget now renders as stacked layers: fill (rounded-box SImage with dynamic ColorAndOpacity) + outline
    // (SBorder/SPrereqExaminerBox/SPrereqExaminerOperatorButton with transparent-fill + colored-outline brush). The
    // tint for PIE debug state lives on the fill layer; the outline stays parent-color-coded. Corner radii match so the
    // two layers align cleanly.

    // ---- Fill brushes (white, tinted via SImage.ColorAndOpacity). One per corner-radius shape. ----
    const FSlateBrush* GetFillBrush_Standard()
    {
        static const FSlateRoundedBoxBrush B(FLinearColor::White, 4.0f);
        return &B;
    }

    const FSlateBrush* GetFillBrush_RuleRefHeader()
    {
        static const FSlateRoundedBoxBrush B(FLinearColor::White, FVector4(4.f, 4.f, 0.f, 0.f));
        return &B;
    }

    // ---- Outline-only brushes (transparent fill + kind-colored outline). ----
    const FSlateBrush* GetLeafOutlineBrush(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:  { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, OrTint,  BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, NotTint, BorderThickness); return &B; }
            default:                          { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }

    const FSlateBrush* GetLeafOutlineBrushParentHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, AndTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::OrChild:  { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, OrTintSaturated,  BoostedThickness); return &B; }
            case EPrereqBoxOutline::NotChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, NotTintSaturated, BoostedThickness); return &B; }
            default:                          { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }

    const FSlateBrush* GetOperatorOuterOutlineBrush(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:  { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, OrTint,  BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, NotTint, BorderThickness); return &B; }
            default:                          { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }

    const FSlateBrush* GetOperatorOuterOutlineBrushParentHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
            case EPrereqBoxOutline::AndChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, AndTintSaturated, BoostedThickness); return &B; }
            case EPrereqBoxOutline::OrChild:  { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, OrTintSaturated,  BoostedThickness); return &B; }
            case EPrereqBoxOutline::NotChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, NotTintSaturated, BoostedThickness); return &B; }
            default:                          { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }

    const FSlateBrush* GetRuleRefHeaderOutlineBrush()
    {
        static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, FVector4(4.f, 4.f, 0.f, 0.f), RuleRefTint, BorderThickness);
        return &B;
    }

    const FSlateBrush* GetRuleRefHeaderOutlineBrushParentHover()
    {
        static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, FVector4(4.f, 4.f, 0.f, 0.f), RuleRefTintSaturated, BoostedThickness);
        return &B;
    }

    const FSlateBrush* GetPillOutlineBrush(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
        case EPrereqBoxOutline::AndChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, AndTint, BorderThickness); return &B; }
        case EPrereqBoxOutline::OrChild:  { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, OrTint,  BorderThickness); return &B; }
        case EPrereqBoxOutline::NotChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, NotTint, BorderThickness); return &B; }
        default:                          { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }

    const FSlateBrush* GetPillOutlineBrushParentHover(EPrereqBoxOutline Kind)
    {
        switch (Kind)
        {
        case EPrereqBoxOutline::AndChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, AndTintSaturated, BoostedThickness); return &B; }
        case EPrereqBoxOutline::OrChild:  { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, OrTintSaturated,  BoostedThickness); return &B; }
        case EPrereqBoxOutline::NotChild: { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f, NotTintSaturated, BoostedThickness); return &B; }
        default:                          { static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 4.0f); return &B; }
        }
    }

    // ---- Fill-color picker for the SImage.ColorAndOpacity lambda. PIE + resolved state returns the state color;
    // non-PIE / Unknown returns the caller-supplied default (the widget's original fill — OperatorBoxFill, LeafBoxFill,
    // HeaderBoxFill, pill fill, etc.). Hover-fill lightening outside PIE is out of scope for this pass — see agenda
    // note on future hover-emphasis-during-PIE state. ----
    FLinearColor GetFillColorForState(EPrereqDebugState State, const FLinearColor& DefaultFill)
    {
        switch (State)
        {
        case EPrereqDebugState::NotStarted:  return DebugNotStartedTint;
        case EPrereqDebugState::InProgress:  return DebugInProgressTint;
        case EPrereqDebugState::Unsatisfied: return DebugUnsatisfiedTint;
        case EPrereqDebugState::Satisfied:   return DebugSatisfiedTint;
        default:                             return DefaultFill;
        }
    }

    /**
     * Picks fill color considering both PIE debug state and hover state. State takes priority when the channel has
     * resolved a state; otherwise returns HoverFill when hovered (direct or parent-operator cascade), DefaultFill when
     * not. Non-PIE hover emphasis matches the pre-refactor behavior of DirectHoverBrush, with the addition that
     * parent-operator hover also cascades to children.
     */
    FLinearColor GetFillColorForStateAndHover(
        EPrereqDebugState State,
        bool bIsHovered,
        const FLinearColor& DefaultFill,
        const FLinearColor& HoverFill)
    {
        if (State != EPrereqDebugState::Unknown)
        {
            return GetFillColorForState(State, DefaultFill);
        }
        return bIsHovered ? HoverFill : DefaultFill;
    }
}

namespace PrereqDebug_Style
{
    // Leaf fills — full 5-state range. Alpha ~0.35 so the tint reads as a wash over the existing leaf visuals without
    // drowning out the Source/Outcome text or the colored outline.
    static const FLinearColor Unknown      = FLinearColor(0.f, 0.f, 0.f, 0.f);                      // transparent — no tint
    static const FLinearColor NotStarted   = FLinearColor(FColor(120, 120, 120, 90));               // grey
    static const FLinearColor InProgress   = FLinearColor(FColor(250, 200,  60, 90));               // amber
    static const FLinearColor Unsatisfied  = FLinearColor(FColor(230,  60,  60, 90));               // red
    static const FLinearColor Satisfied    = FLinearColor(FColor( 90, 210, 110, 90));               // green

    // Combinator tint — same colors as leaves but binary (Unknown / Unsatisfied / Satisfied only). InProgress and
    // NotStarted aren't produced for combinators; the runtime boolean eval collapses those to Unsatisfied.
    const FLinearColor& ColorForLeafState(EPrereqDebugState State)
    {
        switch (State)
        {
        case EPrereqDebugState::NotStarted:   return NotStarted;
        case EPrereqDebugState::InProgress:   return InProgress;
        case EPrereqDebugState::Unsatisfied:  return Unsatisfied;
        case EPrereqDebugState::Satisfied:    return Satisfied;
        default:                              return Unknown;
        }
    }

    const FLinearColor& ColorForCombinatorState(EPrereqDebugState State)
    {
        switch (State)
        {
        case EPrereqDebugState::Satisfied:    return Satisfied;
        case EPrereqDebugState::Unsatisfied:  return Unsatisfied;
        default:                              return Unknown;
        }
    }
}


// ---------------------------------------------------------------------------
// SPrereqExaminerBox — non-clickable hover/double-click wrapper for leaf cells.
//
// Double-clicking navigates to NavigateNode; hovering triggers the Tier 2 cross-editor highlight (HighlightNodesInViewport /
// ClearNodeHighlight) on HighlightNode's editor. No single-click binding — leaves don't toggle. Implementation detail of this
// TU; no header exposure.
// ---------------------------------------------------------------------------

class SPrereqExaminerBox : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPrereqExaminerBox)
        : _NormalBrush(nullptr)
        , _DirectHoverBrush(nullptr)
        , _ParentHoverBrush(nullptr)
        , _Padding(FMargin(3.f, 2.f))
    {}
        SLATE_ARGUMENT(const FSlateBrush*, NormalBrush)
        SLATE_ARGUMENT(const FSlateBrush*, DirectHoverBrush)
        SLATE_ARGUMENT(const FSlateBrush*, ParentHoverBrush)
        SLATE_ATTRIBUTE(FMargin, Padding)
        SLATE_ARGUMENT(TWeakObjectPtr<UEdGraphNode>, HighlightNode)
        SLATE_ARGUMENT(TWeakObjectPtr<UEdGraphNode>, NavigateNode)
        SLATE_ARGUMENT(TWeakPtr<SPrereqExaminerPanel>, PanelWeak)
        SLATE_ARGUMENT(FGuid, ParentOperatorGuid)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        HighlightNode = InArgs._HighlightNode;
        NavigateNode = InArgs._NavigateNode;
        PanelWeak = InArgs._PanelWeak;
        ParentOperatorGuid = InArgs._ParentOperatorGuid;

        NormalBrush = InArgs._NormalBrush ? InArgs._NormalBrush : PrereqExaminer_Style::GetLeafBrush(EPrereqBoxOutline::Default);
        DirectHoverBrush = InArgs._DirectHoverBrush ? InArgs._DirectHoverBrush : PrereqExaminer_Style::GetLeafBrushDirectHover(EPrereqBoxOutline::Default);
        ParentHoverBrush = InArgs._ParentHoverBrush ? InArgs._ParentHoverBrush : NormalBrush;

        ChildSlot
        [
            SNew(SBorder)
                .BorderImage_Lambda([this]() { return GetCurrentBrush(); })
                .Padding(InArgs._Padding)
                [
                    InArgs._Content.Widget
                ]
        ];
    }

protected:
    virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
    {
        SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
        bDirectHover = true;
        if (UEdGraphNode* Node = HighlightNode.Get())
        {
            if (FQuestlineGraphEditor* Editor = FSimpleQuestEditorUtilities::GetEditorForNode(Node))
            {
                Editor->HighlightNodesInViewport({ Node });
            }
        }
    }

    virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
    {
        SCompoundWidget::OnMouseLeave(MouseEvent);
        bDirectHover = false;
        if (UEdGraphNode* Node = HighlightNode.Get())
        {
            if (FQuestlineGraphEditor* Editor = FSimpleQuestEditorUtilities::GetEditorForNode(Node))
            {
                Editor->ClearNodeHighlight();
            }
        }
    }

    virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
    {
        if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
        {
            if (UEdGraphNode* Node = NavigateNode.Get())
            {
                FSimpleQuestEditorUtilities::NavigateToEdGraphNode(Node);
                return FReply::Handled();
            }
        }
        return FReply::Unhandled();
    }

private:
    const FSlateBrush* GetCurrentBrush() const
    {
        if (bDirectHover) return DirectHoverBrush;
        if (ParentOperatorGuid.IsValid())
        {
            if (TSharedPtr<SPrereqExaminerPanel> Panel = PanelWeak.Pin())
            {
                if (Panel->GetHoveredOperatorGuid() == ParentOperatorGuid)
                {
                    return ParentHoverBrush;
                }
            }
        }
        return NormalBrush;
    }

    TWeakObjectPtr<UEdGraphNode> HighlightNode;
    TWeakObjectPtr<UEdGraphNode> NavigateNode;
    TWeakPtr<SPrereqExaminerPanel> PanelWeak;
    FGuid ParentOperatorGuid;
    const FSlateBrush* NormalBrush = nullptr;
    const FSlateBrush* DirectHoverBrush = nullptr;
    const FSlateBrush* ParentHoverBrush = nullptr;
    bool bDirectHover = false;
};

// ---------------------------------------------------------------------------
// SPrereqExaminerOperatorButton — SButton subclass for clickable control surfaces (operator labels, RuleRef header stripes,
// collapsed pills). OnClicked fires the toggle; OnMouseButtonDoubleClick navigates; hover invokes the highlight channel.
//
// Double-click detection note: Slate dispatches a double-click as (Down → Up → DoubleClickDown → Up). SButton's normal click
// processing fires OnClicked on each Up, so a double-click fires the toggle twice (net: back to original state) plus navigation
// once. Acceptable UX — the net visual after a double-click is a one-frame flicker plus the navigation landing.
// ---------------------------------------------------------------------------

class SPrereqExaminerOperatorButton : public SButton
{
public:
    SLATE_BEGIN_ARGS(SPrereqExaminerOperatorButton)
        : _NormalBrush(nullptr)
        , _DirectHoverBrush(nullptr)
        , _ParentHoverBrush(nullptr)
        , _ContentPadding(FMargin(2.f))
        , _bShowChevron(false)
        , _bChevronExpanded(true)
        , _bChevronBalanceContent(false)
    {}
        SLATE_ARGUMENT(const FSlateBrush*, NormalBrush)
        SLATE_ARGUMENT(const FSlateBrush*, DirectHoverBrush)
        SLATE_ARGUMENT(const FSlateBrush*, ParentHoverBrush)
        SLATE_EVENT(FOnClicked, OnClicked)
        SLATE_ATTRIBUTE(FMargin, ContentPadding)
        SLATE_ARGUMENT(TWeakObjectPtr<UEdGraphNode>, NavigateNode)
        SLATE_ARGUMENT(TWeakObjectPtr<UEdGraphNode>, HighlightNode)
        SLATE_ARGUMENT(TWeakPtr<SPrereqExaminerPanel>, PanelWeak)
        SLATE_ARGUMENT(FGuid, OperatorGuid)            // broadcast this GUID to the panel on hover (operator labels only)
        SLATE_ARGUMENT(FGuid, ParentOperatorGuid)      // react to panel state == this GUID for parent-hover tinting
        SLATE_ARGUMENT(bool, bShowChevron)             // render a leading hover-reveal chevron that handles collapse toggle
        SLATE_ARGUMENT(bool, bChevronExpanded)         // true → ▾ (collapse on click); false → ▸ (expand on click)
        SLATE_ARGUMENT(bool, bChevronBalanceContent)   // mirror chevron footprint on the right + center content, so a panel-centered label stays centered
        SLATE_ATTRIBUTE(FSlateColor, ChevronColor)     // chevron image tint — usually bound to the same attr as the label text
        SLATE_EVENT(FOnClicked, OnChevronClicked)      // fires on chevron click (not on button-body click)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        NavigateNode = InArgs._NavigateNode;
        HighlightNode = InArgs._HighlightNode;
        PanelWeak = InArgs._PanelWeak;
        OperatorGuid = InArgs._OperatorGuid;
        ParentOperatorGuid = InArgs._ParentOperatorGuid;
        NormalBrush = InArgs._NormalBrush;
        DirectHoverBrush = InArgs._DirectHoverBrush ? InArgs._DirectHoverBrush : NormalBrush;
        ParentHoverBrush = InArgs._ParentHoverBrush ? InArgs._ParentHoverBrush : NormalBrush;

        // Invisible FButtonStyle — the visible brush comes from our internal SBorder wrapper. Decouples the click behavior
        // (SButton's state machine) from the visual (our 3-state brush selection via panel-state lookup).
        Style = FButtonStyle();
        const FSlateBrush NoBrush = *FAppStyle::GetBrush("NoBorder");
        Style.SetNormal(NoBrush);
        Style.SetHovered(NoBrush);
        Style.SetPressed(NoBrush);
        Style.SetDisabled(NoBrush);
        Style.SetNormalPadding(FMargin(0.f));
        Style.SetPressedPadding(FMargin(0.f));

        // Optional hover-reveal chevron on the left. When bShowChevron is true, the chevron owns the collapse toggle;
        // the outer button's OnClicked / OnMouseButtonDoubleClick stay free for navigation. Visibility is tied to
        // IsHovered() (Hidden when not hovered, so layout stays stable — no label shift when the user mouses in).
        TSharedRef<SWidget> InnerContent = InArgs._Content.Widget;
        if (InArgs._bShowChevron)
        {
            // Engine tree-arrow brushes — the same glyphs STreeView's expander uses. Font-independent, so no risk of
            // fallback glyphs like the Unicode triangles produced. Tinted via SImage's ColorAndOpacity so the brush
            // still inherits the label's color.
            const FSlateBrush* ChevronBrush = FAppStyle::Get().GetBrush(
                InArgs._bChevronExpanded ? TEXT("TreeArrow_Expanded") : TEXT("TreeArrow_Collapsed"));
            const TAttribute<FSlateColor> ChevronColorAttr = InArgs._ChevronColor;
            const FOnClicked OnChevronClickedEvt = InArgs._OnChevronClicked;

            TSharedRef<SWidget> Chevron =
                SNew(SButton)
                    .ButtonStyle(&Style)                         // reuse the invisible style — chevron image paints directly
                    .ContentPadding(FMargin(0.f))
                    .OnClicked(OnChevronClickedEvt)
                    .Visibility_Lambda([this]() { return IsHovered() ? EVisibility::Visible : EVisibility::Hidden; })
                    [
                        SNew(SImage)
                            .Image(ChevronBrush)
                            .ColorAndOpacity(ChevronColorAttr)
                    ];

            // Chevron + content layout. When bChevronBalanceContent is set, the content slot centers its child and a
            // Hidden-visibility mirror of the chevron is appended on the right — keeps a panel-centered label (the
            // AND/OR/NOT case) from shifting sideways once the chevron reserves its space. Pills / RuleRef headers
            // leave the flag off so their natural left-anchored flow is preserved.
            const EHorizontalAlignment ContentHAlign = InArgs._bChevronBalanceContent ? HAlign_Center : HAlign_Fill;
            TSharedRef<SHorizontalBox> ContentRow = SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 3.f, 0.f))
                    [ Chevron ]
                + SHorizontalBox::Slot().FillWidth(1.f).HAlign(ContentHAlign).VAlign(VAlign_Center)
                    [ InArgs._Content.Widget ];

            if (InArgs._bChevronBalanceContent)
            {
                ContentRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(3.f, 0.f, 0.f, 0.f))
                    [
                        SNew(SImage)
                            .Image(ChevronBrush)
                            .Visibility(EVisibility::Hidden)   // reserves the same footprint; never paints
                    ];
            }

            InnerContent = ContentRow;
        }

        SButton::Construct(
            SButton::FArguments()
                .ButtonStyle(&Style)
                .OnClicked(InArgs._OnClicked)
                .ContentPadding(FMargin(0.f))
                [
                    SNew(SBorder)
                        .BorderImage_Lambda([this]() { return GetCurrentBrush(); })
                        .Padding(InArgs._ContentPadding)
                        [
                            InnerContent
                        ]
                ]);
    }

protected:
    virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
    {
        SButton::OnMouseEnter(MyGeometry, MouseEvent);
        if (OperatorGuid.IsValid())
        {
            if (TSharedPtr<SPrereqExaminerPanel> Panel = PanelWeak.Pin())
            {
                Panel->SetHoveredOperator(OperatorGuid);
            }
        }
        if (UEdGraphNode* Node = HighlightNode.Get())
        {
            if (FQuestlineGraphEditor* Editor = FSimpleQuestEditorUtilities::GetEditorForNode(Node))
            {
                Editor->HighlightNodesInViewport({ Node });
            }
        }
    }

    virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
    {
        SButton::OnMouseLeave(MouseEvent);
        if (OperatorGuid.IsValid())
        {
            if (TSharedPtr<SPrereqExaminerPanel> Panel = PanelWeak.Pin())
            {
                Panel->SetHoveredOperator(FGuid());
            }
        }
        if (UEdGraphNode* Node = HighlightNode.Get())
        {
            if (FQuestlineGraphEditor* Editor = FSimpleQuestEditorUtilities::GetEditorForNode(Node))
            {
                Editor->ClearNodeHighlight();
            }
        }
    }

    virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
    {
        if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
        {
            if (UEdGraphNode* Node = NavigateNode.Get())
            {
                FSimpleQuestEditorUtilities::NavigateToEdGraphNode(Node);
                return FReply::Handled();
            }
        }
        return SButton::OnMouseButtonDoubleClick(MyGeometry, MouseEvent);
    }

private:
    const FSlateBrush* GetCurrentBrush() const
    {
        if (IsHovered() && DirectHoverBrush) return DirectHoverBrush;
        if (ParentOperatorGuid.IsValid() && ParentHoverBrush)
        {
            if (TSharedPtr<SPrereqExaminerPanel> Panel = PanelWeak.Pin())
            {
                if (Panel->GetHoveredOperatorGuid() == ParentOperatorGuid)
                {
                    return ParentHoverBrush;
                }
            }
        }
        return NormalBrush;
    }

    TWeakObjectPtr<UEdGraphNode> NavigateNode;
    TWeakObjectPtr<UEdGraphNode> HighlightNode;
    TWeakPtr<SPrereqExaminerPanel> PanelWeak;
    FGuid OperatorGuid;
    FGuid ParentOperatorGuid;
    const FSlateBrush* NormalBrush = nullptr;
    const FSlateBrush* DirectHoverBrush = nullptr;
    const FSlateBrush* ParentHoverBrush = nullptr;
    FButtonStyle Style;
};

// ---------------------------------------------------------------------------
// SPrereqExaminerPanel
// ---------------------------------------------------------------------------

void SPrereqExaminerPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
            [ SAssignNew(HeaderSlot, SBox) ]
        + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SBorder)
                    .BorderImage(PrereqExaminer_Style::GetPanelBgBrush())
                    .Padding(0.f)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                            [ SAssignNew(ExpressionSlot, SBox) ]
                    ]
            ]
    ];

    RebuildAll();
}

SPrereqExaminerPanel::~SPrereqExaminerPanel()
{
    UnsubscribeFromContextGraph();
}

void SPrereqExaminerPanel::PinContextNode(UEdGraphNode* ContextNode)
{
    UnsubscribeFromContextGraph();
    Tree = FSimpleQuestEditorUtilities::CollectPrereqExpressionTopology(ContextNode);
    CollapsedByNodeGuid.Reset();   // fresh pin starts fully expanded
    RebuildAll();
    SubscribeToContextGraph();
}

void SPrereqExaminerPanel::Refresh()
{
    UEdGraphNode* Ctx = Tree.ContextNode.Get();
    Tree = FSimpleQuestEditorUtilities::CollectPrereqExpressionTopology(Ctx);
    RebuildAll();                  // collapse state keyed by NodeGuid — survives topology re-walks
}

void SPrereqExaminerPanel::RebuildAll()
{
    // Old widgets are about to be discarded — any hovered-operator state they broadcast is stale. Clear it so freshly
    // built widgets don't re-emerge already emphasized (e.g., expanding a previously hovered operator should read as
    // un-hovered until the cursor actually lands on a label again).
    SetHoveredOperator(FGuid());

    if (HeaderSlot.IsValid())
    {
        HeaderSlot->SetContent(BuildHeader());
    }
    if (ExpressionSlot.IsValid())
    {
        if (Tree.RootIndex == INDEX_NONE)
        {
            const FText Empty = Tree.ContextNode.IsValid()
                ? LOCTEXT("PrereqExaminerNoExpression", "No prerequisite expression wired.")
                : LOCTEXT("PrereqExaminerEmpty", "Right-click a node and select Examine Prerequisite Expression to inspect.");
            ExpressionSlot->SetContent(
                SNew(SBorder).Padding(FMargin(8.f))
                [
                    SNew(STextBlock).Text(Empty).ColorAndOpacity(FSlateColor::UseSubduedForeground()).AutoWrapText(true)
                ]);
        }
        else
        {
            ExpressionSlot->SetContent(BuildExpressionWidget(Tree.RootIndex, FGuid(), EPrereqBoxOutline::Default));
        }
    }
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildHeader()
{
    const bool bHasContext = Tree.ContextNode.IsValid();
    const bool bHasExpression = Tree.RootIndex != INDEX_NONE;

    const FText ContextLabel = bHasContext
        ? FText::Format(LOCTEXT("PinnedCtxFmt", "Pinned: {0}"), Tree.ContextNode->GetNodeTitle(ENodeTitleType::ListView))
        : LOCTEXT("PrereqExaminerHeaderEmpty", "Prerequisite Examiner");

    TSharedRef<SVerticalBox> Header = SNew(SVerticalBox);
    Header->AddSlot().AutoHeight().Padding(FMargin(4.f, 4.f, 4.f, 2.f))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(ContextLabel)
                    .Font(FAppStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
            ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f))
            [
                SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .Visibility(bHasExpression ? EVisibility::Visible : EVisibility::Collapsed)
                    .ToolTipText(LOCTEXT("ExpandAllTooltip", "Expand all collapsed combinators and rule references."))
                    .OnClicked(this, &SPrereqExaminerPanel::HandleExpandAllClicked)
                    .ContentPadding(FMargin(4.f, 2.f))
                    [ SNew(STextBlock).Text(LOCTEXT("ExpandAll", "Expand")) ]
            ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f))
            [
                SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .Visibility(bHasExpression ? EVisibility::Visible : EVisibility::Collapsed)
                    .ToolTipText(LOCTEXT("CollapseAllTooltip", "Collapse every combinator and rule reference to a summary pill."))
                    .OnClicked(this, &SPrereqExaminerPanel::HandleCollapseAllClicked)
                    .ContentPadding(FMargin(4.f, 2.f))
                    [ SNew(STextBlock).Text(LOCTEXT("CollapseAll", "Collapse")) ]
            ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f))
            [
                SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .Visibility(bHasContext ? EVisibility::Visible : EVisibility::Collapsed)
                    .ToolTipText(LOCTEXT("RefreshTooltip", "Re-walk the pinned expression (useful for cross-asset rule edits)."))
                    .OnClicked(this, &SPrereqExaminerPanel::HandleRefreshClicked)
                    .ContentPadding(FMargin(4.f, 2.f))
                    [ SNew(STextBlock).Text(LOCTEXT("Refresh", "Refresh")) ]
            ]
    ];

    // Rule-info header shown whenever the pinned context IS a Rule Entry/Exit (regardless of whether its tag is set) —
    // designers can see "no tag set" and jump back to the node to fix it. Tag-valid non-rule paths (content node with a
    // Rule Exit buried in its prereq expression) fall through the bIsRuleContext branch but still render via RuleTag.
    UEdGraphNode* Ctx = Tree.ContextNode.Get();
    UQuestlineNode_PrerequisiteRuleEntry* CtxAsEntry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(Ctx);
    UQuestlineNode_PrerequisiteRuleExit* CtxAsExit = Cast<UQuestlineNode_PrerequisiteRuleExit>(Ctx);
    const bool bIsRuleContext = CtxAsEntry || CtxAsExit;

    if (Tree.RuleTag.IsValid() || bIsRuleContext)
    {
        const FText RuleHeaderText = Tree.RuleTag.IsValid()
            ? FText::Format(LOCTEXT("RuleHeaderFmt", "Rule: {0}"), FText::FromString(Tree.RuleTag.GetTagName().ToString()))
            : LOCTEXT("RuleHeaderNoTag", "Rule: (no tag set)");

        // "Go to Exit" when the pinned context is a Rule Exit whose tag didn't resolve to a defining Entry (no-tag case,
        // or tag set but no matching Entry anywhere in the project); otherwise "Go to Entry".
        const FText ButtonLabel = (CtxAsExit && !Tree.RuleEntryNode.IsValid())
            ? LOCTEXT("PrereqExaminerGotoExit", "Go to Exit")
            : LOCTEXT("PrereqExaminerGotoEntry", "Go to Entry");

        Header->AddSlot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 4.f))
        [
            SNew(SBorder).Padding(FMargin(6.f, 3.f)).BorderImage(PrereqExaminer_Style::GetPillBrush(EPrereqBoxOutline::Default))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                            .Text(RuleHeaderText)
                            .ColorAndOpacity(FSlateColor(PrereqExaminer_Style::RuleRefTint))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                    ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f, 0.f, 0.f))
                    [
                        SNew(SButton)
                            .Text(ButtonLabel)
                            .OnClicked(this, &SPrereqExaminerPanel::HandleHeaderEntryNavigationClicked)
                    ]
            ]
        ];
    }

    return Header;
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildExpressionWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer)
{
    if (!Tree.Nodes.IsValidIndex(NodeIndex))
    {
        return SNew(STextBlock).Text(LOCTEXT("InvalidExpressionNode", "(invalid)"));
    }
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    if (Node.Type == EPrereqExaminerNodeType::Leaf)
    {
        return BuildLeafWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind, bSuppressFillLayer);
    }

    UEdGraphNode* SourceNode = Node.SourceNode.Get();
    if (SourceNode && IsCollapsed(SourceNode->NodeGuid))
    {
        return BuildCollapsedPill(NodeIndex, ParentOperatorGuid, ParentOutlineKind, bSuppressFillLayer);
    }

    switch (Node.Type)
    {
        case EPrereqExaminerNodeType::Not:      return BuildNotWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind, bSuppressFillLayer);
        case EPrereqExaminerNodeType::RuleRef:  return BuildRuleRefWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind, bSuppressFillLayer);
        case EPrereqExaminerNodeType::And:
        case EPrereqExaminerNodeType::Or:       return BuildCombinatorWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind, bSuppressFillLayer);
        default:                                return SNullWidget::NullWidget;
    }
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildLeafWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    const FSlateColor LeafColor = FSlateColor(PrereqExaminer_Style::LeafTint);
    const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
    const FSlateFontInfo ValueFont  = FCoreStyle::GetDefaultFontStyle("Regular", 8);

    // Two-layer rendering — fill SImage with rounded-box brush at matching radii sits behind the outline-only
    // SPrereqExaminerBox. Fill color is state-tinted during PIE; LeafBoxFill otherwise. Content sits inside the
    // SPrereqExaminerBox's Padding, on top of both layers. Outline stays parent-color-coded via kind-specific brush.
    TSharedRef<SOverlay> Root = SNew(SOverlay);
    TWeakPtr<SOverlay> RootWeak(Root);

    Root->AddSlot()
    [
        SNew(SImage)
            .Image(PrereqExaminer_Style::GetFillBrush_Standard())
            .ColorAndOpacity_Lambda([this, NodeIndex, ParentOperatorGuid, RootWeak]() -> FSlateColor
            {
                const bool bHovered = RootWeak.IsValid() && RootWeak.Pin()->IsHovered();
                return FSlateColor(PrereqExaminer_Style::GetFillColorForStateAndHover(
                    ComputeDebugState(NodeIndex),
                    bHovered,
                    PrereqExaminer_Style::LeafBoxFill,
                    PrereqExaminer_Style::LeafBoxHoverFill));
            })
            .Visibility(EVisibility::HitTestInvisible)
    ];
    Root->AddSlot()
    [
        SNew(SPrereqExaminerBox)
            .NormalBrush(PrereqExaminer_Style::GetLeafOutlineBrush(ParentOutlineKind))
            .DirectHoverBrush(PrereqExaminer_Style::GetLeafOutlineBrush(ParentOutlineKind))   // outline-only; direct-hover fill effect deferred
            .ParentHoverBrush(PrereqExaminer_Style::GetLeafOutlineBrushParentHover(ParentOutlineKind))
            .PanelWeak(SharedThis(this))
            .ParentOperatorGuid(ParentOperatorGuid)
            .HighlightNode(Node.SourceNode)
            .NavigateNode(Node.SourceNode)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::BorderLayerPadding)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                    .Text(LOCTEXT("LeafSourceHeader", "Source: "))
                                    .Font(HeaderFont)
                                    .ColorAndOpacity(LeafColor)
                            ]
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                    .Text(Node.LeafSourceLabel)
                                    .Font(ValueFont)
                                    .ColorAndOpacity(LeafColor)
                            ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                    .Text(LOCTEXT("LeafOutcomeHeader", "Outcome: "))
                                    .Font(HeaderFont)
                                    .ColorAndOpacity(LeafColor)
                            ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                    .Text(Node.LeafOutcomeCategory)
                                    .Font(ValueFont)
                                    .ColorAndOpacity(FSlateColor(PrereqExaminer_Style::LeafCategoryTint))
                                    .Visibility(Node.LeafOutcomeCategory.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
                            ]
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                    .Text(Node.LeafOutcomeLabel)
                                    .Font(ValueFont)
                                    .ColorAndOpacity(LeafColor)
                            ]
                    ]
                ]
            ]
    ];
    return Root;
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildCombinatorWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    const bool bIsAnd = (Node.Type == EPrereqExaminerNodeType::And);
    const FLinearColor NormalTextColor    = bIsAnd ? PrereqExaminer_Style::AndTint          : PrereqExaminer_Style::OrTint;
    const FLinearColor SaturatedTextColor = bIsAnd ? PrereqExaminer_Style::AndTintSaturated : PrereqExaminer_Style::OrTintSaturated;
    const FText OperatorText = bIsAnd ? LOCTEXT("AndLabel", "AND") : LOCTEXT("OrLabel", "OR");
    const EPrereqBoxOutline ChildKind = bIsAnd ? EPrereqBoxOutline::AndChild : EPrereqBoxOutline::OrChild;

    UEdGraphNode* MyNode = Node.SourceNode.Get();
    const FGuid MyOperatorGuid = MyNode ? MyNode->NodeGuid : FGuid();

    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
    const int32 ChildCount = Node.ChildIndices.Num();
    for (int32 i = 0; i < ChildCount; ++i)
    {
        FMargin Padding = FMargin(PrereqExaminer_Style::BorderLayerPadding.Left, PrereqExaminer_Style::OperatorLabelPadding.Top);
        if (i == 0) Padding.Top = PrereqExaminer_Style::BorderLayerPadding.Top;
        if (i == ChildCount - 1) Padding.Bottom = PrereqExaminer_Style::BorderLayerPadding.Bottom;
        Body->AddSlot().AutoHeight().Padding(Padding)
            [ BuildExpressionWidget(Node.ChildIndices[i], MyOperatorGuid, ChildKind) ];
        if (i < ChildCount - 1)
        {
            Body->AddSlot().AutoHeight().Padding(PrereqExaminer_Style::OperatorLabelPadding)
                [ BuildOperatorLabel(NodeIndex, OperatorText, FSlateColor(NormalTextColor), FSlateColor(SaturatedTextColor)) ];
        }
    }

    // Two-layer rendering — fill SImage behind, outline-only SBorder + Body content on top. Fill color is state-tinted
    // during PIE (combinator's rolled-up state via ComputeDebugState), OperatorBoxFill otherwise.
    TSharedRef<SOverlay> Root = SNew(SOverlay);
    if (!bSuppressFillLayer)
    {
        Root->AddSlot()
        [
            SNew(SImage)
                .Image(PrereqExaminer_Style::GetFillBrush_Standard())
                .ColorAndOpacity_Lambda([this, NodeIndex, MyOperatorGuid]() -> FSlateColor
                {
                    const bool bHovered = MyOperatorGuid.IsValid() && GetHoveredOperatorGuid() == MyOperatorGuid;
                    return FSlateColor(PrereqExaminer_Style::GetFillColorForStateAndHover(
                        ComputeDebugState(NodeIndex),
                        bHovered,
                        PrereqExaminer_Style::OperatorBoxFill,
                        PrereqExaminer_Style::OperatorBoxHoverFill));
                })
                .Visibility(EVisibility::HitTestInvisible)
        ];
    }
    Root->AddSlot()
    [
        SNew(SBorder)
            .BorderImage(MakeOuterBrushAttribute(ParentOperatorGuid, ParentOutlineKind))
            .Padding(PrereqExaminer_Style::BorderLayerPadding)
            .Visibility(EVisibility::SelfHitTestInvisible)
            [ Body ]
    ];
    return Root;
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildNotWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    UEdGraphNode* MyNode = Node.SourceNode.Get();
    const FGuid MyOperatorGuid = MyNode ? MyNode->NodeGuid : FGuid();

    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
    Body->AddSlot().AutoHeight().Padding(PrereqExaminer_Style::OperatorLabelPadding)
        [ BuildOperatorLabel(NodeIndex, LOCTEXT("NotLabel", "NOT"),
            FSlateColor(PrereqExaminer_Style::NotTint),
            FSlateColor(PrereqExaminer_Style::NotTintSaturated)) ];
    if (Node.ChildIndices.Num() > 0)
    {
        // Child gets NotChild outline — the red border on the operand IS the negation signal, so NOT doesn't need its
        // own outer frame to mark itself visually. Fill-suppression flag does NOT propagate to NOT's operand — the
        // operand is a regular expression child and paints its own fill normally.
        Body->AddSlot().AutoHeight().Padding(PrereqExaminer_Style::BorderLayerPadding.Left, PrereqExaminer_Style::OperatorLabelPadding.Top, PrereqExaminer_Style::BorderLayerPadding.Right, PrereqExaminer_Style::BorderLayerPadding.Bottom)
            [ BuildExpressionWidget(Node.ChildIndices[0], MyOperatorGuid, EPrereqBoxOutline::NotChild) ];
    }

    // Two-layer rendering — fill SImage behind, outline-only SBorder + Body on top. Fill color reflects NOT's rolled-up
    // (inverted) debug state during PIE, OperatorBoxFill otherwise. Fill slot is skipped when this NOT is itself the
    // Inner of a RuleRef (bSuppressFillLayer=true) so the RuleRef's outer color-coded outline stays unobscured at its
    // rounded corners.
    TSharedRef<SOverlay> Root = SNew(SOverlay);
    if (!bSuppressFillLayer)
    {
        Root->AddSlot()
        [
            SNew(SImage)
                .Image(PrereqExaminer_Style::GetFillBrush_Standard())
                .ColorAndOpacity_Lambda([this, NodeIndex, MyOperatorGuid]() -> FSlateColor
                {
                    const bool bHovered = MyOperatorGuid.IsValid() && GetHoveredOperatorGuid() == MyOperatorGuid;
                    return FSlateColor(PrereqExaminer_Style::GetFillColorForStateAndHover(
                        ComputeDebugState(NodeIndex),
                        bHovered,
                        PrereqExaminer_Style::OperatorBoxFill,
                        PrereqExaminer_Style::OperatorBoxHoverFill));
                })
                .Visibility(EVisibility::HitTestInvisible)
        ];
    }
    Root->AddSlot()
    [
        SNew(SBorder)
            .BorderImage(MakeOuterBrushAttribute(ParentOperatorGuid, ParentOutlineKind))
            .Padding(PrereqExaminer_Style::BorderLayerPadding)
            .Visibility(EVisibility::SelfHitTestInvisible)
            [ Body ]
    ];
    return Root;
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildRuleRefWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    UEdGraphNode* ExitNode = Node.SourceNode.Get();
    UEdGraphNode* EntryNode = Node.RuleEntryNode.Get();
    const FGuid NodeGuid = ExitNode ? ExitNode->NodeGuid : FGuid();
    const TWeakObjectPtr<UEdGraphNode> EntryWeak = EntryNode;

    // Header text + chevron color saturate on self-hover (NodeGuid match) — when the header itself is hovered, the
    // label and expansion arrow brighten together. The header's amber outline (HeaderAmberOutlineAttr below) is
    // intentionally driven off parent-hover instead, so the border and text decouple: cursor on the header brightens
    // the text; cursor on the RuleRef's parent operator brightens the border.
    TWeakPtr<SPrereqExaminerPanel> WeakPanel = SharedThis(this);
    TAttribute<FSlateColor> HeaderTextColorAttr = TAttribute<FSlateColor>::CreateLambda(
        [WeakPanel, NodeGuid]() -> FSlateColor
        {
            if (NodeGuid.IsValid())
            {
                if (TSharedPtr<SPrereqExaminerPanel> Panel = WeakPanel.Pin())
                {
                    if (Panel->GetHoveredOperatorGuid() == NodeGuid)
                    {
                        return FSlateColor(PrereqExaminer_Style::RuleRefTintSaturated);
                    }
                }
            }
            return FSlateColor(PrereqExaminer_Style::RuleRefTint);
        });
    
    // Two-layer header — fill SImage with top-rounded brush (matching header radii) behind the outline-only
    // SPrereqExaminerOperatorButton. Fill color tints to state during PIE, HeaderBoxFill (dark amber) otherwise.
    // Three-layer Header: fill (state-tinted SImage) → button with TRANSPARENT brush carrying content and click/chevron
    // handling (no outline painted by the button itself) → amber outline SImage as the top slot. This ordering means the
    // amber outline is the LAST thing painted in the Header, so it sits on top of the outer widget's parent-color-coded
    // outline (drawn by the outer SBorder before content in the 2-layer outer structure). Net visual at the widget top:
    // outer outline → header fill covers it → header content → amber outline wins.
    const FSlateBrush* TransparentBrush = FAppStyle::Get().GetBrush("NoBorder");

    TAttribute<const FSlateBrush*> HeaderAmberOutlineAttr = TAttribute<const FSlateBrush*>::CreateLambda(
        [WeakPanel, ParentOperatorGuid]() -> const FSlateBrush*
        {
            if (ParentOperatorGuid.IsValid())
            {
                if (TSharedPtr<SPrereqExaminerPanel> Panel = WeakPanel.Pin())
                {
                    if (Panel->GetHoveredOperatorGuid() == ParentOperatorGuid)
                    {
                        return PrereqExaminer_Style::GetRuleRefHeaderOutlineBrushParentHover();
                    }
                }
            }
            return PrereqExaminer_Style::GetRuleRefHeaderOutlineBrush();
        });

    TSharedRef<SOverlay> HeaderOverlay = SNew(SOverlay);
    TWeakPtr<SOverlay> HeaderWeak(HeaderOverlay);

    HeaderOverlay->AddSlot()
    [
        SNew(SImage)
            .Image(PrereqExaminer_Style::GetFillBrush_RuleRefHeader())
            .ColorAndOpacity_Lambda([this, NodeIndex, NodeGuid]() -> FSlateColor
            {
                const bool bSelf = NodeGuid.IsValid() && GetHoveredOperatorGuid() == NodeGuid;
                return FSlateColor(PrereqExaminer_Style::GetFillColorForStateAndHover(
                    ComputeDebugState(NodeIndex),
                    bSelf,
                    PrereqExaminer_Style::HeaderBoxFill,
                    PrereqExaminer_Style::HeaderHoverFill));
            })
            .Visibility(EVisibility::HitTestInvisible)
    ];
    HeaderOverlay->AddSlot()
    [
        SNew(SPrereqExaminerOperatorButton)
            .NormalBrush(TransparentBrush)
            .DirectHoverBrush(TransparentBrush)
            .ParentHoverBrush(TransparentBrush)
            .PanelWeak(SharedThis(this))
            .OperatorGuid(NodeGuid)                          // RuleRef header acts as the RuleRef's "operator label" — publish on hover so self-hover tinting fires
            .ParentOperatorGuid(ParentOperatorGuid)
            .NavigateNode(Node.SourceNode)
            .HighlightNode(Node.SourceNode)
            .bShowChevron(true)
            .bChevronExpanded(true)
            .ChevronColor(HeaderTextColorAttr)
            .OnChevronClicked_Lambda([this, NodeGuid]() -> FReply
            {
                if (NodeGuid.IsValid()) ToggleCollapsed(NodeGuid);
                return FReply::Handled();
            })
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::BorderLayerPadding)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                                .Text(Node.DisplayLabel)
                                .ColorAndOpacity(HeaderTextColorAttr)
                                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                        ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("GotoRuleEntryTooltip", "Navigate to this rule's defining Entry node."))
                                .IsEnabled(EntryNode != nullptr)
                                .OnClicked_Lambda([EntryWeak]() -> FReply
                                {
                                    if (UEdGraphNode* Target = EntryWeak.Get())
                                    {
                                        FSimpleQuestEditorUtilities::NavigateToEdGraphNode(Target);
                                    }
                                    return FReply::Handled();
                                })
                                .ContentPadding(FMargin(3.f, 1.f))
                                [ SNew(STextBlock).Text(FText::FromString(TEXT("→"))) ]
                        ]
                ]
            ]
    ];
    HeaderOverlay->AddSlot()
    [
        // Amber outline on top — paints AFTER the button's content, and (combined with the 2-layer outer below)
        // after the outer's parent-color-coded outline too. Always sits at the Header's bounds since SOverlay
        // slots default to Fill/Fill.
        SNew(SImage)
            .Image(HeaderAmberOutlineAttr)
            .Visibility(EVisibility::HitTestInvisible)
    ];

    TSharedRef<SWidget> Header = HeaderOverlay;

    TSharedRef<SWidget> Inner = (Node.ChildIndices.Num() > 0)
        ? BuildExpressionWidget(Node.ChildIndices[0], FGuid(), EPrereqBoxOutline::Default, true)   // rule boundary — no propagation; suppress Inner's fill so its rounded-corner pull-away doesn't expose gaps over the RuleRef's outer color-coded outline
        : StaticCastSharedRef<SWidget>(
            SNew(STextBlock)
                .Text(LOCTEXT("RuleRefNoEnterExpr", "(rule has no Enter expression wired)"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .Margin(FMargin(8.f, 4.f)));

    // Direct-child operator GUID — the outer/content-area fill saturates only when the child's operator label publishes its
    // own hover (AND/OR, NOT, nested RuleRef header). Leaves and collapsed pills don't publish, so they deliberately don't
    // trigger the emphasis — matching the "fill emphasis follows the halo'd node on the graph" mental model. Border boost
    // on the child's outline still handles its own inputs' emphasis separately.
    FGuid ChildOperatorGuid;
    if (Node.ChildIndices.Num() > 0)
    {
        const FPrereqExaminerNode& ChildNode = Tree.Nodes[Node.ChildIndices[0]];
        if (UEdGraphNode* ChildSourceNode = ChildNode.SourceNode.Get())
        {
            ChildOperatorGuid = ChildSourceNode->NodeGuid;
        }
    }

    // Inner-slot padding depends on whether the Inner will render as a collapsed pill. A pill has no visible outer
    // frame of its own, so sitting flush against the Header stripe looks squashed; give it LayerPadding breathing
    // room in that case. Expanded widgets (leaf, combinator, RuleRef with its own amber header) already carry visible
    // outer frames and read correctly when flush with the Header.
    bool bInnerIsCollapsedPill = false;
    if (Node.ChildIndices.Num() > 0)
    {
        const FPrereqExaminerNode& InnerNode = Tree.Nodes[Node.ChildIndices[0]];
        if (InnerNode.Type != EPrereqExaminerNodeType::Leaf)
        {
            if (UEdGraphNode* InnerSource = InnerNode.SourceNode.Get())
            {
                bInnerIsCollapsedPill = IsCollapsed(InnerSource->NodeGuid);
            }
        }
    }
    const FMargin InnerSlotPadding = bInnerIsCollapsedPill
        ? PrereqExaminer_Style::BorderLayerPadding
        : FMargin(0.f);
    
    // Two-layer outer: fill SImage behind + SBorder-with-outline wrapping content. Paint sequence at widget top:
    //   1. Outer fill (OperatorBoxFill, no PIE tint on outer — RuleRef state lives on the Header)
    //   2. Outer SBorder brush (parent-color-coded outline)
    //   3. Header fill (covers outer outline at top edge — state-tinted during PIE, HeaderBoxFill otherwise)
    //   4. Header content (text, chevron, → button)
    //   5. Header amber outline (Header's third SOverlay slot, painted last — sits on top of the outer outline)
    //   6. Inner expression widget (below, inset by InnerSlotPadding from the outer outline)
    // Amber wins at the header bounds; parent-color wins everywhere else.
    TSharedRef<SOverlay> Root = SNew(SOverlay);
    if (!bSuppressFillLayer)
    {
        Root->AddSlot()
        [
            SNew(SImage)
                .Image(PrereqExaminer_Style::GetFillBrush_Standard())
                .ColorAndOpacity_Lambda([this, NodeIndex, ChildOperatorGuid]() -> FSlateColor
                {
                    const bool bChild = ChildOperatorGuid.IsValid() && GetHoveredOperatorGuid() == ChildOperatorGuid;
                    return FSlateColor(PrereqExaminer_Style::GetFillColorForStateAndHover(
                        ComputeDebugState(NodeIndex),
                        bChild,
                        PrereqExaminer_Style::OperatorBoxFill,
                        PrereqExaminer_Style::OperatorBoxHoverFill));
                })
                .Visibility(EVisibility::HitTestInvisible)
        ];
    }
    Root->AddSlot()
    [
        SNew(SBorder)
            .BorderImage(MakeOuterBrushAttribute(ParentOperatorGuid, ParentOutlineKind))
            .Padding(FMargin(0.f))
            .Visibility(EVisibility::SelfHitTestInvisible)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f))
                    [ Header ]
                + SVerticalBox::Slot().AutoHeight().Padding(InnerSlotPadding)
                    [ Inner ]
            ]
    ];
    return Root;
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildOperatorLabel(int32 NodeIndex, FText OperatorText, FSlateColor NormalColor, FSlateColor SaturatedColor)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    const FGuid NodeGuid = Node.SourceNode.IsValid() ? Node.SourceNode->NodeGuid : FGuid();
    TWeakPtr<SPrereqExaminerPanel> WeakPanel = SharedThis(this);

    TAttribute<FSlateColor> TextColorAttr = TAttribute<FSlateColor>::CreateLambda(
        [WeakPanel, NodeGuid, NormalColor, SaturatedColor]() -> FSlateColor
        {
            if (NodeGuid.IsValid())
            {
                if (TSharedPtr<SPrereqExaminerPanel> Panel = WeakPanel.Pin())
                {
                    if (Panel->GetHoveredOperatorGuid() == NodeGuid)
                    {
                        return SaturatedColor;
                    }
                }
            }
            return NormalColor;
        });

    const FSlateBrush* Transparent = FAppStyle::GetBrush("NoBorder");

    // HAlign-center SBox sizes the button to its content + padding, matching the header buttons' compact footprint.
    // bChevronExpanded=true because this widget only builds when the operator is NOT collapsed (collapsed state routes
    // through BuildCollapsedPill). The hover-reveal chevron glyph is therefore ▾ (collapse on click).
    return SNew(SBox)
        .HAlign(HAlign_Center)
        [
            SNew(SPrereqExaminerOperatorButton)
                .NormalBrush(Transparent)
                .DirectHoverBrush(Transparent)
                .ParentHoverBrush(Transparent)
                .PanelWeak(SharedThis(this))
                .OperatorGuid(NodeGuid)
                .NavigateNode(Node.SourceNode)
                .HighlightNode(Node.SourceNode)
                .bShowChevron(true)
                .bChevronExpanded(true)
                .bChevronBalanceContent(true)                              // mirror chevron on the right so AND/OR/NOT stays centered under the outer SBox(HAlign_Center)
                .ChevronColor(TextColorAttr)                               // chevron inherits the operator's tint (and saturates with it)
                .OnChevronClicked_Lambda([this, NodeGuid]() -> FReply
                {
                    if (NodeGuid.IsValid()) ToggleCollapsed(NodeGuid);
                    return FReply::Handled();
                })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::OperatorLabelPadding)
                    [
                        SNew(STextBlock)
                            .Text(OperatorText)
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                            .ColorAndOpacity(TextColorAttr)
                    ]
                ]
        ];
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildCollapsedPill(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind, bool bSuppressFillLayer)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    const FGuid NodeGuid = Node.SourceNode.IsValid() ? Node.SourceNode->NodeGuid : FGuid();

    // Count non-leaf children for the AND/OR pill summaries — "complex" terms are combinators or rule references that
    // themselves carry nested structure, as opposed to flat leaf reads of a single fact tag. Lets the designer gauge
    // how much logic hides behind a collapsed pill at a glance.
    int32 ComplexChildCount = 0;
    for (int32 ChildIdx : Node.ChildIndices)
    {
        if (Tree.Nodes.IsValidIndex(ChildIdx) && Tree.Nodes[ChildIdx].Type != EPrereqExaminerNodeType::Leaf)
        {
            ++ComplexChildCount;
        }
    }

    FText Label;
    FSlateColor LabelColor;
    switch (Node.Type)
    {
    case EPrereqExaminerNodeType::And:
        Label = (ComplexChildCount > 0)
            ? FText::Format(LOCTEXT("AndPillFmtComplex", "[AND: {0} terms, {1} complex]"),
                FText::AsNumber(Node.ChildIndices.Num()), FText::AsNumber(ComplexChildCount))
            : FText::Format(LOCTEXT("AndPillFmtSimple", "[AND: {0} terms]"),
                FText::AsNumber(Node.ChildIndices.Num()));
        LabelColor = FSlateColor(PrereqExaminer_Style::AndTint);
        break;
    case EPrereqExaminerNodeType::Or:
        Label = (ComplexChildCount > 0)
            ? FText::Format(LOCTEXT("OrPillFmtComplex", "[OR: {0} terms, {1} complex]"),
                FText::AsNumber(Node.ChildIndices.Num()), FText::AsNumber(ComplexChildCount))
            : FText::Format(LOCTEXT("OrPillFmtSimple", "[OR: {0} terms]"),
                FText::AsNumber(Node.ChildIndices.Num()));
        LabelColor = FSlateColor(PrereqExaminer_Style::OrTint);
        break;
    case EPrereqExaminerNodeType::Not:
        Label = LOCTEXT("NotPill", "[NOT]");
        LabelColor = FSlateColor(PrereqExaminer_Style::NotTint);
        break;
    case EPrereqExaminerNodeType::RuleRef:
        Label = FText::Format(LOCTEXT("RulePillFmt", "[{0}]"), Node.DisplayLabel);
        LabelColor = FSlateColor(PrereqExaminer_Style::RuleRefTint);
        break;
    default:
        Label = Node.DisplayLabel;
        LabelColor = FSlateColor::UseForeground();
        break;
    }

    // Two-layer rendering — fill SImage behind, outline-only SPrereqExaminerOperatorButton on top. Fill color is
    // state-tinted during PIE (pill represents the collapsed combinator / RuleRef; its state is the rolled-up state
    // of the collapsed subtree via ComputeDebugState), LeafBoxFill otherwise. Fill slot is skipped when this pill is
    // itself the Inner of a RuleRef (bSuppressFillLayer=true) so the RuleRef's outer color-coded outline stays
    // unobscured at its rounded corners.
    TSharedRef<SOverlay> Root = SNew(SOverlay);
    TWeakPtr<SOverlay> RootWeak(Root);
    if (!bSuppressFillLayer)
    {
        Root->AddSlot()
        [
            SNew(SImage)
                .Image(PrereqExaminer_Style::GetFillBrush_Standard())
                .ColorAndOpacity_Lambda([this, NodeIndex, RootWeak]() -> FSlateColor
                {
                    const bool bSelf = RootWeak.IsValid() && RootWeak.Pin()->IsHovered();
                    return FSlateColor(PrereqExaminer_Style::GetFillColorForStateAndHover(
                        ComputeDebugState(NodeIndex),
                        bSelf,
                        PrereqExaminer_Style::LeafBoxFill,
                        PrereqExaminer_Style::LeafBoxHoverFill));
                })
                .Visibility(EVisibility::HitTestInvisible)
        ];
    }
    Root->AddSlot()
    [
        SNew(SPrereqExaminerOperatorButton)
            .NormalBrush(PrereqExaminer_Style::GetPillOutlineBrush(ParentOutlineKind))
            .DirectHoverBrush(PrereqExaminer_Style::GetPillOutlineBrush(ParentOutlineKind))   // outline-only; direct-hover fill deferred
            .ParentHoverBrush(PrereqExaminer_Style::GetPillOutlineBrushParentHover(ParentOutlineKind))
            .PanelWeak(SharedThis(this))
            .ParentOperatorGuid(ParentOperatorGuid)      // pill is a child of parent operator — track for parent-hover
            .NavigateNode(Node.SourceNode)
            .HighlightNode(Node.SourceNode)
            .bShowChevron(true)
            .bChevronExpanded(false)                     // pill is the collapsed state — chevron is ▸ (expand on click)
            .ChevronColor(LabelColor)                    // chevron tint matches the pill text tint (AND green / OR cyan / NOT red / RuleRef amber)
            .OnChevronClicked_Lambda([this, NodeGuid]() -> FReply
            {
                if (NodeGuid.IsValid()) ToggleCollapsed(NodeGuid);
                return FReply::Handled();
            })
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::BorderLayerPadding)
                [
                    SNew(STextBlock)
                        .Text(Label)
                        .ColorAndOpacity(LabelColor)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                ]
            ]
    ];
    return Root;
}

bool SPrereqExaminerPanel::IsCollapsed(const FGuid& NodeGuid) const
{
    if (!NodeGuid.IsValid()) return false;
    const bool* Found = CollapsedByNodeGuid.Find(NodeGuid);
    return Found ? *Found : false;
}

void SPrereqExaminerPanel::SetCollapsed(const FGuid& NodeGuid, bool bCollapsed)
{
    if (!NodeGuid.IsValid()) return;
    CollapsedByNodeGuid.Add(NodeGuid, bCollapsed);
    RebuildAll();
}

void SPrereqExaminerPanel::ToggleCollapsed(const FGuid& NodeGuid)
{
    SetCollapsed(NodeGuid, !IsCollapsed(NodeGuid));
}

void SPrereqExaminerPanel::ExpandAll()
{
    for (const FPrereqExaminerNode& Node : Tree.Nodes)
    {
        if (Node.Type == EPrereqExaminerNodeType::Leaf) continue;
        if (UEdGraphNode* SourceNode = Node.SourceNode.Get())
        {
            CollapsedByNodeGuid.Add(SourceNode->NodeGuid, false);
        }
    }
    RebuildAll();
}

void SPrereqExaminerPanel::CollapseAll()
{
    for (const FPrereqExaminerNode& Node : Tree.Nodes)
    {
        if (Node.Type == EPrereqExaminerNodeType::Leaf) continue;
        if (UEdGraphNode* SourceNode = Node.SourceNode.Get())
        {
            CollapsedByNodeGuid.Add(SourceNode->NodeGuid, true);
        }
    }
    RebuildAll();
}

void SPrereqExaminerPanel::SetHoveredOperator(const FGuid& Guid)
{
    if (HoveredOperatorGuid != Guid)
    {
        HoveredOperatorGuid = Guid;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

TAttribute<const FSlateBrush*> SPrereqExaminerPanel::MakeOuterBrushAttribute(const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind)
{
    // Two-layer refactor: outer SBorders now use outline-only brushes (transparent fill + kind-colored outline). Fill
    // lives on a separate SImage layer behind the SBorder, colored dynamically per PIE debug state.
    const FSlateBrush* Normal = PrereqExaminer_Style::GetOperatorOuterOutlineBrush(ParentOutlineKind);
    const FSlateBrush* Hover  = PrereqExaminer_Style::GetOperatorOuterOutlineBrushParentHover(ParentOutlineKind);
    TWeakPtr<SPrereqExaminerPanel> WeakPanel = SharedThis(this);

    return TAttribute<const FSlateBrush*>::CreateLambda(
        [WeakPanel, ParentOperatorGuid, Normal, Hover]() -> const FSlateBrush*
        {
            if (ParentOperatorGuid.IsValid())
            {
                if (TSharedPtr<SPrereqExaminerPanel> Panel = WeakPanel.Pin())
                {
                    if (Panel->GetHoveredOperatorGuid() == ParentOperatorGuid)
                    {
                        return Hover;
                    }
                }
            }
            return Normal;
        });
}

void SPrereqExaminerPanel::SubscribeToContextGraph()
{
    UEdGraphNode* Ctx = Tree.ContextNode.Get();
    if (!Ctx) return;
    UEdGraph* Graph = Ctx->GetGraph();
    if (!Graph) return;
    SubscribedGraph = Graph;
    GraphChangedHandle = Graph->AddOnGraphChangedHandler(
        FOnGraphChanged::FDelegate::CreateSP(this, &SPrereqExaminerPanel::HandleGraphChanged));
}

void SPrereqExaminerPanel::UnsubscribeFromContextGraph()
{
    if (UEdGraph* Graph = SubscribedGraph.Get())
    {
        if (GraphChangedHandle.IsValid())
        {
            Graph->RemoveOnGraphChangedHandler(GraphChangedHandle);
        }
    }
    GraphChangedHandle.Reset();
    SubscribedGraph.Reset();
}

void SPrereqExaminerPanel::HandleGraphChanged(const FEdGraphEditAction& Action)
{
    Refresh();
}

FReply SPrereqExaminerPanel::HandleExpandAllClicked()
{
    ExpandAll();
    return FReply::Handled();
}

FReply SPrereqExaminerPanel::HandleCollapseAllClicked()
{
    CollapseAll();
    return FReply::Handled();
}

FReply SPrereqExaminerPanel::HandleRefreshClicked()
{
    Refresh();
    return FReply::Handled();
}

FReply SPrereqExaminerPanel::HandleHeaderEntryNavigationClicked()
{
    // Prefer the resolved defining Rule Entry; fall back to the pinned context (covers Rule Entry with any tag state,
    // and Rule Exit whose tag didn't resolve — including the no-tag case).
    UEdGraphNode* Target = Tree.RuleEntryNode.Get();
    if (!Target)
    {
        Target = Tree.ContextNode.Get();
    }
    if (Target)
    {
        FSimpleQuestEditorUtilities::NavigateToEdGraphNode(Target);
    }
    return FReply::Handled();
}

void SPrereqExaminerPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    if (FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel())
    {
        if (Channel->IsActive())
        {
            Invalidate(EInvalidateWidget::Paint);
        }
    }
}

EPrereqDebugState SPrereqExaminerPanel::ComputeDebugState(int32 NodeIndex) const
{
    if (!Tree.Nodes.IsValidIndex(NodeIndex)) return EPrereqDebugState::Unknown;

    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    if (!Channel || !Channel->IsActive()) return EPrereqDebugState::Unknown;

    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    switch (Node.Type)
    {
    case EPrereqExaminerNodeType::Leaf:
        return Channel->QueryLeafState(Node.LeafTag, Node.LeafSourceTag);

    case EPrereqExaminerNodeType::And:
    {
        // True iff every child Satisfied. Any Unknown propagates up (we can't confidently paint the combinator).
        bool bAllSatisfied = true;
        for (int32 ChildIdx : Node.ChildIndices)
        {
            const EPrereqDebugState ChildState = ComputeDebugState(ChildIdx);
            if (ChildState == EPrereqDebugState::Unknown) return EPrereqDebugState::Unknown;
            if (ChildState != EPrereqDebugState::Satisfied) bAllSatisfied = false;
        }
        return (Node.ChildIndices.Num() > 0 && bAllSatisfied) ? EPrereqDebugState::Satisfied : EPrereqDebugState::Unsatisfied;
    }

    case EPrereqExaminerNodeType::Or:
    {
        // True iff any child Satisfied.
        for (int32 ChildIdx : Node.ChildIndices)
        {
            const EPrereqDebugState ChildState = ComputeDebugState(ChildIdx);
            if (ChildState == EPrereqDebugState::Unknown) return EPrereqDebugState::Unknown;
            if (ChildState == EPrereqDebugState::Satisfied) return EPrereqDebugState::Satisfied;
        }
        return EPrereqDebugState::Unsatisfied;
    }

    case EPrereqExaminerNodeType::Not:
    {
        if (Node.ChildIndices.Num() == 0) return EPrereqDebugState::Unknown;
        const EPrereqDebugState ChildState = ComputeDebugState(Node.ChildIndices[0]);
        if (ChildState == EPrereqDebugState::Unknown) return EPrereqDebugState::Unknown;
        return (ChildState == EPrereqDebugState::Satisfied) ? EPrereqDebugState::Unsatisfied : EPrereqDebugState::Satisfied;
    }

    case EPrereqExaminerNodeType::RuleRef:
    {
        // Option B: recurse into the drilled-in child subtree. Single child carrying the rule's expression; its state
        // IS the RuleRef's state.
        if (Node.ChildIndices.Num() == 0) return EPrereqDebugState::Unknown;
        return ComputeDebugState(Node.ChildIndices[0]);
    }

    default:
        return EPrereqDebugState::Unknown;
    }
}

#undef LOCTEXT_NAMESPACE