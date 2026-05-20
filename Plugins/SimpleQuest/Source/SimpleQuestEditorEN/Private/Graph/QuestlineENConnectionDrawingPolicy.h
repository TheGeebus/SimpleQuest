#pragma once

#if WITH_ELECTRONIC_NODES

#include "ENConnectionDrawingPolicy.h"
#include "Graph/QuestlineDrawingPolicyMixin.h"

/**
 * Layers a dashed-rendering effect onto Electronic Nodes' wire drawing for prerequisite wires only. Reads
 * Params.bUserFlag1 (set by the Mixin's DetermineWiringStyle for direct prereq pins AND knot-traversed
 * prereq chains via LeadsOnlyToPrereqInputs) to decide whether the current connection should dash.
 *
 * Wire rendering hooks into EN's MakeDrawSpline virtual (introduced in EN's 5.7 release alongside UE 5.7).
 * EN's internal path drawer accumulates a list of (Start, StartDir, End, EndDir) Bezier tuples per
 * connection and calls MakeDrawSpline once per tuple via FENPathDrawer::DoDraw. We override that single
 * hook, subdivide each Bezier ourselves, and run a dash state machine across the subdivision. Dash state
 * persists across multiple MakeDrawSpline calls within a single connection so Manhattan / Subway wire
 * styles (which emit multiple splines per connection) stay continuously dashed across their corners.
 */
class FQuestlineENConnectionDrawingPolicy : public TQuestlineDrawingPolicyMixin<FENConnectionDrawingPolicy>
{
    using Super = TQuestlineDrawingPolicyMixin<FENConnectionDrawingPolicy>;
    using FENBase = FENConnectionDrawingPolicy;

public:
    FQuestlineENConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect,
        FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
       : Super(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj)
    {}

    virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params) override;

    virtual void MakeDrawSpline(FSlateWindowElementList& ElementList, uint32 InLayer,
        const FVector2D InStart, const FVector2D InStartDir,
        const FVector2D InEnd, const FVector2D InEndDir,
        float InThickness, ESlateDrawEffect InDrawEffects,
        const FLinearColor& InTint) override;

private:
    /**
     * Accumulates dashing across a single straight line segment fed from MakeDrawSpline's Bezier
     * subdivision. Maintains dash state (DashAccumulated, bDashDrawing, CurrentDash) across calls so the
     * dash pattern stays continuous across both Bezier subdivisions AND multi-spline EN wire styles.
     */
    void AccumulateDashSegment(const FVector2f& Start, const FVector2f& End, int32 LayerId,
        const FLinearColor& Color, float Thickness);

    // Dash state — reset per connection in DrawConnection
    bool  bIsPrereqWire = false;
    float DashAccumulated = 0.f;
    bool  bDashDrawing = true;
    TArray<FVector2f> CurrentDash;

    static constexpr float DashLength = 10.f;
    static constexpr float GapLength = 5.f;
    static constexpr int32 SplineSubdivisions = 32;
};

#endif