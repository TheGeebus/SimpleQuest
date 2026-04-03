#pragma once

#if WITH_ELECTRONIC_NODES

#include "ENConnectionDrawingPolicy.h"
#include "Graph/QuestlineDrawingPolicyMixin.h"

class FQuestlineENConnectionDrawingPolicy : public TQuestlineDrawingPolicyMixin<FENConnectionDrawingPolicy>
{
    using Super = TQuestlineDrawingPolicyMixin<FENConnectionDrawingPolicy>;
    using FENBase = FENConnectionDrawingPolicy;
    using FKismetBase = FKismetConnectionDrawingPolicy;

public:
    FQuestlineENConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect,
        FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
       : Super(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj)
    {}

    virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params) override;
     
    virtual void DrawWireSegment(const FVector2f& Start, const FVector2f& End,	int32 LayerId, const FLinearColor& Color, float Thickness) override;
    virtual void DrawArcSegment(const FVector2f& Start, const FVector2f& StartTangent, const FVector2f& End, const FVector2f& EndTangent, int32 LayerId, const FLinearColor& Color, float Thickness) override;
    virtual bool IsIdenticalRibbonSegment(float RibbonMin, float RibbonMax, float CurrentMin, float CurrentMax) override;
    
private:
    // Dash state — reset per connection in DrawConnection
    bool  bIsPrereqWire = false;
    float DashAccumulated = 0.f;
    bool  bDashDrawing = true;
    TArray<FVector2f> CurrentDash;

    static constexpr float DashLength = 10.f;
    static constexpr float GapLength = 5.f;
};

#endif
