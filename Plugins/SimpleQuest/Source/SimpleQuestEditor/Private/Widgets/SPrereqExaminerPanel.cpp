// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SPrereqExaminerPanel.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "GraphEditAction.h"
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
    const FLinearColor AndTint              = FLinearColor(FColor(150,  255,  150));    // green
    const FLinearColor OrTint               = FLinearColor(FColor(51,  178, 255));      // cyan
    const FLinearColor NotTint              = FLinearColor(FColor(255,  90,  90));      // red
    const FLinearColor RuleRefTint          = FLinearColor(FColor(242, 178,  51));      // amber
    const FLinearColor LeafTint             = FLinearColor(FColor(199, 199, 199));      // neutral text
    const FLinearColor LeafCategoryTint     = FLinearColor(FColor(130, 130, 130));      // muted — category prefix above leaf

    // Saturated variants applied when the corresponding operator's labels are hovered — boosts the parent operator + its
    // immediate children together so the visual group reads as one emphasized cluster.
    const FLinearColor AndTintSaturated     = FLinearColor(FColor(  0, 255,   0));
    const FLinearColor OrTintSaturated      = FLinearColor(FColor( 40,  40, 255));
    const FLinearColor NotTintSaturated     = FLinearColor(FColor(255,  13,  13));
    const FLinearColor RuleRefTintSaturated = FLinearColor(FColor(255, 230, 10));

    // Operator-box fill is near black; leaf fill is a shade lighter so content cells read as distinct from their container
    // frames even without contrasting outlines. Hover fill is shared — applied to leaves + pills on direct hover.
    const FLinearColor OperatorBoxFill = FLinearColor(FColor(28,28,28));
    const FLinearColor LeafBoxFill     = FLinearColor(FColor(42, 42, 42));
    const FLinearColor BoxHoverFill    = FLinearColor(FColor(56, 56, 56));
    const FLinearColor HeaderBoxFill   = FLinearColor(FColor(30, 22,  6));
    const FLinearColor HeaderHoverFill = FLinearColor(FColor(60, 44,  12));

    // Uniform layer padding — applied to every SVerticalBox/SHorizontalBox slot inside the expression tree so every
    // nested layer has the same spacing. Widget-intrinsic paddings (SPrereqExaminerBox::Padding,
    // SPrereqExaminerOperatorButton::ContentPadding) are 0; all layer padding flows from this one value.
    const FMargin LayerPadding = FMargin(4.f, 2.f);
    constexpr float BorderThickness = 1.f;
    constexpr float BoostedThickness = 2.0f;
    
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
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f, OrTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f, NotTint, BorderThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f); return &B; }
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
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f, AndTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::OrChild:
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f, OrTint, BorderThickness); return &B; }
            case EPrereqBoxOutline::NotChild:
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f, NotTint, BorderThickness); return &B; }
            default:
                { static const FSlateRoundedBoxBrush B(BoxHoverFill, 4.0f); return &B; }
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

TSharedRef<SWidget> SPrereqExaminerPanel::BuildExpressionWidget(
    int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind)
{
    if (!Tree.Nodes.IsValidIndex(NodeIndex))
    {
        return SNew(STextBlock).Text(LOCTEXT("InvalidExpressionNode", "(invalid)"));
    }
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    if (Node.Type == EPrereqExaminerNodeType::Leaf)
    {
        return BuildLeafWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind);
    }

    UEdGraphNode* SourceNode = Node.SourceNode.Get();
    if (SourceNode && IsCollapsed(SourceNode->NodeGuid))
    {
        return BuildCollapsedPill(NodeIndex, ParentOperatorGuid, ParentOutlineKind);
    }

    switch (Node.Type)
    {
        case EPrereqExaminerNodeType::Not:      return BuildNotWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind);
        case EPrereqExaminerNodeType::RuleRef:  return BuildRuleRefWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind);
        case EPrereqExaminerNodeType::And:
        case EPrereqExaminerNodeType::Or:       return BuildCombinatorWidget(NodeIndex, ParentOperatorGuid, ParentOutlineKind);
        default:                                return SNullWidget::NullWidget;
    }
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildLeafWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    const FSlateColor LeafColor = FSlateColor(PrereqExaminer_Style::LeafTint);
    const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
    const FSlateFontInfo ValueFont  = FCoreStyle::GetDefaultFontStyle("Regular", 8);

    return SNew(SPrereqExaminerBox)
        .NormalBrush(PrereqExaminer_Style::GetLeafBrush(ParentOutlineKind))
        .DirectHoverBrush(PrereqExaminer_Style::GetLeafBrushDirectHover(ParentOutlineKind))
        .ParentHoverBrush(PrereqExaminer_Style::GetLeafBrushParentHover(ParentOutlineKind))
        .PanelWeak(SharedThis(this))
        .ParentOperatorGuid(ParentOperatorGuid)
        .HighlightNode(Node.SourceNode)
        .NavigateNode(Node.SourceNode)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
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
        ];
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildCombinatorWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind)
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
        Body->AddSlot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
            [ BuildExpressionWidget(Node.ChildIndices[i], MyOperatorGuid, ChildKind) ];
        if (i < ChildCount - 1)
        {
            Body->AddSlot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
                [ BuildOperatorLabel(NodeIndex, OperatorText, FSlateColor(NormalTextColor), FSlateColor(SaturatedTextColor)) ];
        }
    }

    return SNew(SBorder)
        .BorderImage(MakeOuterBrushAttribute(ParentOperatorGuid, ParentOutlineKind))
        .Padding(PrereqExaminer_Style::LayerPadding)
        .Visibility(EVisibility::SelfHitTestInvisible)
        [ Body ];
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildNotWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    UEdGraphNode* MyNode = Node.SourceNode.Get();
    const FGuid MyOperatorGuid = MyNode ? MyNode->NodeGuid : FGuid();

    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
    Body->AddSlot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
        [ BuildOperatorLabel(NodeIndex, LOCTEXT("NotLabel", "NOT"),
            FSlateColor(PrereqExaminer_Style::NotTint),
            FSlateColor(PrereqExaminer_Style::NotTintSaturated)) ];
    if (Node.ChildIndices.Num() > 0)
    {
        // Child gets NotChild outline — the red border on the operand IS the negation signal, so NOT doesn't need its
        // own outer frame to mark itself visually.
        Body->AddSlot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
            [ BuildExpressionWidget(Node.ChildIndices[0], MyOperatorGuid, EPrereqBoxOutline::NotChild) ];
    }

    // Outer SBorder matches the structure AND/OR/RuleRef use — kind-colored outline via MakeOuterBrushAttribute, so a
    // NOT child of AND/OR renders like any other kind-colored child. Padding(0) keeps the outline tight against the
    // VBox, minimizing the gap between NOT's outline and its contents. The red NotChild outline on the operand below
    // is what signals negation; this outer frame only reflects NOT's relationship to its parent.
    return SNew(SBorder)
        .BorderImage(MakeOuterBrushAttribute(ParentOperatorGuid, ParentOutlineKind))
        .Padding(PrereqExaminer_Style::LayerPadding)
        .Visibility(EVisibility::SelfHitTestInvisible)
        [ Body ];
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildRuleRefWidget(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind)
{
    const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
    UEdGraphNode* ExitNode = Node.SourceNode.Get();
    UEdGraphNode* EntryNode = Node.RuleEntryNode.Get();
    const FGuid NodeGuid = ExitNode ? ExitNode->NodeGuid : FGuid();
    const TWeakObjectPtr<UEdGraphNode> EntryWeak = EntryNode;

    // Header text color saturates when the RuleRef's parent operator is hovered — mirrors the saturation the outer
    // outline already picks up via MakeOuterBrushAttribute, so the whole RuleRef reads as "emphasized" together.
    TWeakPtr<SPrereqExaminerPanel> WeakPanel = SharedThis(this);
    TAttribute<FSlateColor> HeaderTextColorAttr = TAttribute<FSlateColor>::CreateLambda(
        [WeakPanel, ParentOperatorGuid]() -> FSlateColor
        {
            if (ParentOperatorGuid.IsValid())
            {
                if (TSharedPtr<SPrereqExaminerPanel> Panel = WeakPanel.Pin())
                {
                    if (Panel->GetHoveredOperatorGuid() == ParentOperatorGuid)
                    {
                        return FSlateColor(PrereqExaminer_Style::RuleRefTintSaturated);
                    }
                }
            }
            return FSlateColor(PrereqExaminer_Style::RuleRefTint);
        });
    
    TSharedRef<SWidget> Header = SNew(SPrereqExaminerOperatorButton)
        .NormalBrush(PrereqExaminer_Style::GetRuleRefHeaderBrush())
        .DirectHoverBrush(PrereqExaminer_Style::GetRuleRefHeaderBrushHover())
        .ParentHoverBrush(PrereqExaminer_Style::GetRuleRefHeaderBrushParentHover())   // thicker + saturated on parent-hover
        .PanelWeak(SharedThis(this))
        .ParentOperatorGuid(ParentOperatorGuid)                                       // enables parent-hover brush swap
        .NavigateNode(Node.SourceNode)
        .HighlightNode(Node.SourceNode)
        .bShowChevron(true)
        .bChevronExpanded(true)                                                       // this widget only builds when NOT collapsed
        .ChevronColor(HeaderTextColorAttr)                                            // amber, saturates on parent-operator hover
        .OnChevronClicked_Lambda([this, NodeGuid]() -> FReply
        {
            if (NodeGuid.IsValid()) ToggleCollapsed(NodeGuid);
            return FReply::Handled();
        })
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
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
        ];

    TSharedRef<SWidget> Inner = (Node.ChildIndices.Num() > 0)
        ? BuildExpressionWidget(Node.ChildIndices[0], FGuid(), EPrereqBoxOutline::Default)   // rule boundary — no propagation
        : StaticCastSharedRef<SWidget>(
            SNew(STextBlock)
                .Text(LOCTEXT("RuleRefNoEnterExpr", "(rule has no Enter expression wired)"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .Margin(FMargin(8.f, 4.f)));

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
        ? PrereqExaminer_Style::LayerPadding
        : FMargin(0.f);
    
    return SNew(SBorder)
        .BorderImage(MakeOuterBrushAttribute(ParentOperatorGuid, ParentOutlineKind))
        .Padding(FMargin(0.f))
        .Visibility(EVisibility::SelfHitTestInvisible)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f))
                [ Header ]
            + SVerticalBox::Slot().AutoHeight().Padding(InnerSlotPadding)
                [ Inner ]
        ];
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
                    + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
                    [
                        SNew(STextBlock)
                            .Text(OperatorText)
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                            .ColorAndOpacity(TextColorAttr)
                    ]
                ]
        ];
}

TSharedRef<SWidget> SPrereqExaminerPanel::BuildCollapsedPill(int32 NodeIndex, const FGuid& ParentOperatorGuid, EPrereqBoxOutline ParentOutlineKind)
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

    return SNew(SPrereqExaminerOperatorButton)
        .NormalBrush(PrereqExaminer_Style::GetPillBrush(ParentOutlineKind))
        .DirectHoverBrush(PrereqExaminer_Style::GetPillBrushDirectHover(ParentOutlineKind))
        .ParentHoverBrush(PrereqExaminer_Style::GetPillBrushParentHover(ParentOutlineKind))
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
            + SVerticalBox::Slot().AutoHeight().Padding(PrereqExaminer_Style::LayerPadding)
            [
                SNew(STextBlock)
                    .Text(Label)
                    .ColorAndOpacity(LabelColor)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            ]
        ];
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
    const FSlateBrush* Normal = PrereqExaminer_Style::GetOperatorOuterBrush(ParentOutlineKind);
    const FSlateBrush* Hover  = PrereqExaminer_Style::GetOperatorOuterBrushParentHover(ParentOutlineKind);
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

#undef LOCTEXT_NAMESPACE