// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNodeBase.h"
#include "QuestlineNode_Entry.generated.h"

UCLASS()
class UQuestlineNode_Entry : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }

	/**
	 * Outcome tags this entry node can receive from a parent graph. Each tag gets its own output pin. Populate manually or
	 * via right-click > Import Outcome Pins.
	 */
	UPROPERTY(EditAnywhere, Category = "Quest", meta = (Categories = "Quest.Outcome"))
	TArray<FGameplayTag> IncomingOutcomeTags;

	UPROPERTY(EditAnywhere, Category = "Quest")
	bool bShowDeactivationPins = false;

	/** Surgically adds/removes outcome output pins to match IncomingOutcomeTags without disturbing existing wiring. */
	void RefreshOutcomePins();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	
private:
	void ImportOutcomePinsFromParent();
};
