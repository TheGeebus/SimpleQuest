// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNodeBase.h"
#include "Types/IncomingSignalPinSpec.h"
#include "QuestlineNode_Entry.generated.h"

UCLASS()
class UQuestlineNode_Entry : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual void PostLoad() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }

	/**
	 * New-model incoming signal specs. Each spec with bExposed=true generates an output pin. Session 19: raw EditAnywhere
	 * array for interim editing until Session 20's custom details panel tree lands. Migrated from IncomingOutcomeTags on
	 * first load post-upgrade.
	 */
	UPROPERTY(EditAnywhere, Category = "Quest")
	TArray<FIncomingSignalPinSpec> IncomingSignals;

	/**
	 * DEPRECATED — migrated to IncomingSignals on PostLoad. Do not modify directly. This property is kept as a hidden field
	 * solely for deserialization of pre-migration assets; once PostLoad runs, it is cleared and stays empty.
	 */
	UPROPERTY()
	TArray<FGameplayTag> IncomingOutcomeTags_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = "Quest")
	bool bShowDeactivationPins = false;

	/**
	 * Surgically adds/removes outcome output pins to match IncomingSignals (exposed entries only) without disturbing existing wiring.
	 */
	void RefreshOutcomePins();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

private:
	void ImportOutcomePinsFromParent();
};