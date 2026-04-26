// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_PortalEntryBase.h"
#include "QuestlineNode_ActivationGroupEntry.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_ActivationGroupEntry : public UQuestlineNode_PortalEntryBase
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	
	virtual FGameplayTag GetGroupTag() const override { return GroupTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { GroupTag = NewTag; }
	virtual FString GetTagFilterString() const override { return TEXT("SimpleQuest.QuestActivationGroup"); }

	/** Single-input setter: no dynamic-pin UI. Designers merge multiple upstream signals via a knot. */
	virtual bool CanAddInputPin() const override { return false; }

	UPROPERTY(EditAnywhere, Category="Activation Group", meta=(Categories="SimpleQuest.QuestActivationGroup"))
	FGameplayTag GroupTag;
};