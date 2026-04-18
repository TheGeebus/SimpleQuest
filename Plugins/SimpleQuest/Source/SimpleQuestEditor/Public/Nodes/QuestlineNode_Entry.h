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
	
	/**
	 * True if this pin is a spec-generated incoming signal pin on the Entry node. Spec pins are the QuestOutcome category outputs;
	 * the "Entered" sentinel is QuestActivation and the "Deactivated" pin is QuestDeactivated, so a simple category+direction check
	 * distinguishes them without consulting the IncomingSignals array.
	 */
	static bool IsIncomingSignalPin(const UEdGraphPin* Pin);
	
	/**
	 * Context-menu handler for "Remove Incoming Pin" on a specific spec pin. Finds the matching IncomingSignals entry by recomputing
	 * the disambiguated pin name and flips bExposed to false; the spec entry persists so future Import recognizes it. Orphaned
	 * pins (no matching spec) are removed directly. No-op if PinName doesn't match any spec or orphan pin.
	 */
	void RemoveIncomingPinByName(FName PinName);

	/**
	 * Context-menu handler for "Clear Unused Incoming Pins" on the Entry node body. Sweeps every exposed spec whose generated pin
	 * has zero LinkedTo connections and flips bExposed to false on each. Toast message reports the count cleared (or "nothing to
	 * clear" if all exposed pins are wired).
	 */
	void ClearUnusedIncomingPins();

};