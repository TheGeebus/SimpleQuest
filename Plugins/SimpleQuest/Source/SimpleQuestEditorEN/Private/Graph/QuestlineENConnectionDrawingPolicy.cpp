#include "Graph/QuestlineENConnectionDrawingPolicy.h"

#if WITH_ELECTRONIC_NODES

void FQuestlineENConnectionDrawingPolicy::DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End,
                                                         const FConnectionParams& Params)
{
    // Prereq-wire detection now reads Params.bUserFlag1, set by the Mixin's DetermineWiringStyle. Inherits
    // the Mixin's knot-traversal logic (LeadsOnlyToPrereqInputs) so wires passing through knot chains into
    // prereq inputs dash correctly without a second topology walk here.
    bIsPrereqWire = Params.bUserFlag1;

    DashAccumulated = 0.f;
    bDashDrawing = true;
    CurrentDash.Reset();

    FENBase::DrawConnection(LayerId, Start, End, Params);

    // Trailing flush — any partial dash still open in CurrentDash after the connection's final MakeDrawSpline
    // call gets emitted here. Uses Params.WireColor / WireThickness as the trailing-render attributes;
    // EN's internal MakeDrawSpline calls within this connection share the same color and thickness so the
    // approximation matches the actual last-rendered spline.
    if (bIsPrereqWire && bDashDrawing && CurrentDash.Num() >= 2)
    {
        FSlateDrawElement::MakeLines(DrawElementsList, LayerId, FPaintGeometry(), CurrentDash,
            ESlateDrawEffect::None, Params.WireColor, true, Params.WireThickness);
    }
}

void FQuestlineENConnectionDrawingPolicy::MakeDrawSpline(FSlateWindowElementList& ElementList, uint32 InLayer,
    const FVector2D InStart, const FVector2D InStartDir, const FVector2D InEnd, const FVector2D InEndDir,
    float InThickness, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint)
{
    if (!bIsPrereqWire)
    {
        FENBase::MakeDrawSpline(ElementList, InLayer, InStart, InStartDir, InEnd, InEndDir, InThickness, InDrawEffects, InTint);
        return;
    }

    // Subdivide the cubic Hermite spline into straight line segments and feed each through the dash state
    // machine. Dash state (DashAccumulated, bDashDrawing, CurrentDash) is INSTANCE state that persists
    // across this entire connection — Manhattan and Subway wire styles emit multiple MakeDrawSpline calls
    // per connection, and the state machine maintains a continuous dash pattern across all of them.
    const FVector2f Start2f(InStart);
    const FVector2f StartTan(InStartDir);
    const FVector2f End2f(InEnd);
    const FVector2f EndTan(InEndDir);

    FVector2f Prev = Start2f;
    for (int32 i = 1; i <= SplineSubdivisions; ++i)
    {
        const float T = static_cast<float>(i) / static_cast<float>(SplineSubdivisions);
        const FVector2f Point = FMath::CubicInterp(Start2f, StartTan, End2f, EndTan, T);
        AccumulateDashSegment(Prev, Point, static_cast<int32>(InLayer), InTint, InThickness);
        Prev = Point;
    }
}

void FQuestlineENConnectionDrawingPolicy::AccumulateDashSegment(const FVector2f& Start, const FVector2f& End,
    int32 LayerId, const FLinearColor& Color, float Thickness)
{
    const float SegLen = FVector2f::Distance(Start, End);
    if (SegLen < KINDA_SMALL_NUMBER) return;

    // EN's tuples in DrawList have two categories of position discrepancy at boundaries:
    //   • small numerical noise (a few units) — tangent-computation differences between EN helpers leave
    //     adjacent tuple endpoints not quite matching, but the wire is visually continuous.
    //   • large genuine jumps (tens of units) — real routing discontinuities in Manhattan/Subway paths
    //     where the rendered wire takes a discrete step.
    //
    // The dash polyline should flow through the first category (line caps stay aligned, no visible artifact)
    // and break cleanly across the second (otherwise MakeLines draws a diagonal across the gap). DashFlush-
    // Threshold sits high enough that small noise stays under it, but well below the magnitude of genuine
    // jumps EN produces. DashAccumulated and bDashDrawing are preserved across the flush so the dash phase
    // continues correctly past the break.
    static constexpr float DashFlushThreshold = 8.0f;
    if (CurrentDash.Num() > 0 && FVector2f::Distance(CurrentDash.Last(), Start) > DashFlushThreshold)
    {
        if (CurrentDash.Num() >= 2)
        {
            FSlateDrawElement::MakeLines(DrawElementsList, LayerId, FPaintGeometry(), CurrentDash,
                ESlateDrawEffect::None, Color, true, Thickness);
        }
        CurrentDash.Reset();
    }

    // Only seed CurrentDash with Start when we're actively drawing a dash. In gap mode, CurrentDash must
    // stay empty until the next gap→dash transition seeds it with the proper dash-start point. Adding
    // Start unconditionally pollutes CurrentDash during gap mode and produces a phantom line across the
    // gap when the next dash eventually renders via MakeLines.
    if (CurrentDash.Num() == 0 && bDashDrawing) CurrentDash.Add(Start);
    
    const FVector2f Dir = (End - Start) / SegLen;
    float Consumed = 0.f;

    while (Consumed < SegLen)
    {
        const float Threshold = bDashDrawing ? DashLength : GapLength;
        const float ToThreshold = Threshold - DashAccumulated;
        const float Available = SegLen - Consumed;

        if (Available < ToThreshold)
        {
            if (bDashDrawing) CurrentDash.Add(End);
            DashAccumulated += Available;
            break;
        }

        const FVector2f Transition = Start + Dir * (Consumed + ToThreshold);
        if (bDashDrawing)
        {
            CurrentDash.Add(Transition);
            if (CurrentDash.Num() >= 2)
            {
                FSlateDrawElement::MakeLines(DrawElementsList, LayerId, FPaintGeometry(), CurrentDash,
                    ESlateDrawEffect::None, Color, true, Thickness);
            }
            CurrentDash.Reset();
        }

        Consumed += ToThreshold;
        DashAccumulated = 0.f;
        bDashDrawing = !bDashDrawing;
        if (bDashDrawing) CurrentDash.Add(Start + Dir * Consumed);
    }
}

#endif