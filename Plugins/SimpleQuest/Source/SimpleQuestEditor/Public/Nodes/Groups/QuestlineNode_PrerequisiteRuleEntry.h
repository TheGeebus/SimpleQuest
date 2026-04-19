// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_PortalEntryBase.h"
#include "QuestlineNode_PrerequisiteRuleEntry.generated.h"


UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteRuleEntry : public UQuestlineNode_PortalEntryBase
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	virtual void PostLoad() override;

	virtual FGameplayTag GetGroupTag() const override { return GroupTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { GroupTag = NewTag; }
	virtual FString GetTagFilterString() const override { return TEXT("QuestPrereqRule"); }

	/** Single-input Rule Entry: no multi-condition UI. Compose with AND/OR/NOT combinators into the Enter pin. */
	virtual bool CanAddInputPin() const override { return false; }

	/** Identifies this rule. Referenced by Rule Exits and prerequisite expressions anywhere in the project. */
	UPROPERTY(EditAnywhere, Category="Prerequisite Rule", meta=(Categories="QuestPrereqRule"))
	FGameplayTag GroupTag;
};

