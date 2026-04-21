#include "Graph/QuestlineENConnectionDrawingPolicy.h"

#if WITH_ELECTRONIC_NODES

void FQuestlineENConnectionDrawingPolicy::DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End,
                                                         const FConnectionParams& Params)
{
    bIsPrereqWire = (Params.AssociatedPin2 != nullptr
                  && Params.AssociatedPin2->PinName == TEXT("Prerequisites"));

    DashAccumulated = 0.f;
    bDashDrawing = true;
    CurrentDash.Reset();

    FENBase::DrawConnection(LayerId, Start, End, Params);

    if (bIsPrereqWire && bDashDrawing && CurrentDash.Num() >= 2)
        FSlateDrawElement::MakeLines(DrawElementsList, LayerId,
            FPaintGeometry(), CurrentDash, ESlateDrawEffect::None,
            Params.WireColor, true, Params.WireThickness);
}

void FQuestlineENConnectionDrawingPolicy::DrawWireSegment(const FVector2f& Start, const FVector2f& End, int32 LayerId,
    const FLinearColor& Color, float Thickness)
{
    if (!bIsPrereqWire)
    {
        FENBase::DrawWireSegment(Start, End, LayerId, Color, Thickness);
        return;
    }

    // Dash this segment, continuing state from previous segments on this connection
    const float SegLen = FVector2f::Distance(Start, End);
    if (SegLen < KINDA_SMALL_NUMBER) return;
    
    // If a DrawRadius arc was drawn since the last DrawWireSegment call, CurrentDash ends at the pre-arc position while
    // this segment starts at the post-arc position. Flush the partial dash now to prevent a diagonal jump across the
    // arc in MakeLines.
    if (CurrentDash.Num() > 0 && FVector2f::Distance(CurrentDash.Last(), Start) > 0.5f)
    {
        if (CurrentDash.Num() >= 2)
        {
            FSlateDrawElement::MakeLines(DrawElementsList, LayerId, FPaintGeometry(), CurrentDash, ESlateDrawEffect::None,
                Color, true, Thickness);
        }
        CurrentDash.Reset();
        // DashAccumulated and bDashDrawing are preserved — the dash pattern continues across the arc without accounting
        // for its length (arc is solid).
    }
    
    if (CurrentDash.Num() == 0) CurrentDash.Add(Start);

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
                FSlateDrawElement::MakeLines(DrawElementsList, LayerId, FPaintGeometry(), CurrentDash, ESlateDrawEffect::None,
                    Color, true, Thickness);
            }
            CurrentDash.Reset();
        }

        Consumed += ToThreshold;
        DashAccumulated  = 0.f;
        bDashDrawing = !bDashDrawing;
        if (bDashDrawing) CurrentDash.Add(Start + Dir * Consumed);
    }
}

void FQuestlineENConnectionDrawingPolicy::DrawArcSegment(const FVector2f& Start, const FVector2f& StartTangent,const FVector2f& End,
    const FVector2f& EndTangent, int32 LayerId, const FLinearColor& Color, float Thickness)
{
    if (!bIsPrereqWire)
    {
        FENBase::DrawArcSegment(Start, StartTangent, End, EndTangent, LayerId, Color, Thickness);
        return;
    }

    // Subdivide the cubic Bezier arc into straight segments so the dash state machine runs continuously through corners
    // without a seam or jump.
    static constexpr int32 NumSubdivisions = 32;
    FVector2f Prev = Start;
    for (int32 i = 1; i <= NumSubdivisions; ++i)
    {
        const float T = static_cast<float>(i) / static_cast<float>(NumSubdivisions);
        const FVector2f Point = FMath::CubicInterp(Start, StartTangent, End, EndTangent, T);
        DrawWireSegment(Prev, Point, LayerId, Color, Thickness);
        Prev = Point;
    }
}

bool FQuestlineENConnectionDrawingPolicy::IsIdenticalRibbonSegment(float RibbonMin, float RibbonMax, float CurrentMin, float CurrentMax)
{
    return  (FMath::IsNearlyEqual(RibbonMin, CurrentMin, KINDA_SMALL_NUMBER) && FMath::IsNearlyEqual(RibbonMax, CurrentMax, KINDA_SMALL_NUMBER)) ||
            (FMath::IsNearlyEqual(RibbonMin, CurrentMax, KINDA_SMALL_NUMBER) && FMath::IsNearlyEqual(RibbonMax, CurrentMin, KINDA_SMALL_NUMBER));
}

#endif
