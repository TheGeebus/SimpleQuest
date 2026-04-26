// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestResolutionSubsystem.h"

#include "SimpleQuestLog.h"

const FQuestResolutionRecord* UQuestResolutionSubsystem::GetQuestResolution(FGameplayTag QuestTag) const
{
	return QuestResolutions.Find(QuestTag);
}

bool UQuestResolutionSubsystem::HasResolved(FGameplayTag QuestTag) const
{
	return QuestResolutions.Contains(QuestTag);
}

int32 UQuestResolutionSubsystem::GetResolutionCount(FGameplayTag QuestTag) const
{
	const FQuestResolutionRecord* Record = QuestResolutions.Find(QuestTag);
	return Record ? Record->ResolutionCount : 0;
}

void UQuestResolutionSubsystem::RecordResolution(FGameplayTag QuestTag, FGameplayTag OutcomeTag, double ResolutionTime)
{
	if (!QuestTag.IsValid()) return;

	FQuestResolutionRecord& Record = QuestResolutions.FindOrAdd(QuestTag);
	Record.OutcomeTag = OutcomeTag;
	Record.ResolutionTime = ResolutionTime;
	Record.ResolutionCount++;
	
	UE_LOG(LogSimpleQuest, Log, TEXT("QuestResolutions: recorded '%s' outcome='%s' (resolution #%d at t=%.2fs)"),
		*QuestTag.ToString(), *OutcomeTag.ToString(), Record.ResolutionCount, Record.ResolutionTime);
}