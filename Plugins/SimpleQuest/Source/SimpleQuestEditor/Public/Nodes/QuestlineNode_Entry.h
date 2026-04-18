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
	 * Source-qualified incoming signal specs. Each spec with bExposed=true generates an output pin whose name is
	 * disambiguated against sibling specs. Session 20: raw EditAnywhere array for interim editing; Session 20b
	 * replaces with a custom details panel tree grouped by source.
	 */
	UPROPERTY(EditAnywhere, Category = "Quest")
	TArray<FIncomingSignalPinSpec> IncomingSignals;

	UPROPERTY(EditAnywhere, Category = "Quest")
	bool bShowDeactivationPins = false;

	/**
	 * Surgically adds/removes outcome output pins to match IncomingSignals (exposed specs only) without disturbing
	 * existing wiring.
	 */
	void RefreshOutcomePins();

	/**
	 * Computes the disambiguated output pin name for an exposed spec given all exposed specs on this Entry. Public so the compiler
	 * can resolve pins deterministically. See implementation for the graded disambiguation rules.
	 */
	static FName BuildDisambiguatedPinName(const FIncomingSignalPinSpec& Spec, const TArray<FIncomingSignalPinSpec>& AllSpecs);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

private:
	void ImportOutcomePinsFromParent();
};