// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/Types/QuestEventTypes.h"
#include "QuestObserverDedupeTestTypes.generated.h"

/**
 * Counts what an observer broadcasts. The component's delegates are dynamic multicasts, so a sink has to be a UObject
 * with a UFUNCTION - which is why this lives in a header rather than beside the tests, and why it is not wrapped in
 * WITH_DEV_AUTOMATION_TESTS: UnrealHeaderTool does not evaluate that symbol.
 */
UCLASS()
class UQuestObserverDedupeSink : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnAny(FQuestLifecycleEventReport Report)
	{
		++Total;
		Reports.Add(Report);
	}

	/** How many times one tag was reported with one event type - the number the duplicate bug inflated. */
	int32 CountFor(const FGameplayTag Tag, const EQuestLifecycleEventType Type) const
	{
		int32 Count = 0;
		for (const FQuestLifecycleEventReport& Report : Reports)
		{
			if (Report.QuestTag == Tag && Report.EventType == Type) { ++Count; }
		}
		return Count;
	}

	int32 Total = 0;
	TArray<FQuestLifecycleEventReport> Reports;
};

