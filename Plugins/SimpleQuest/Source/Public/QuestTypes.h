#pragma once
#include "CoreMinimal.h"
#include "SimpleQuestLog.h"
#include "QuestTypes.generated.h"


class AQuestTarget;
/**
 * Displayed via the CommsWidget when starting or ending a quest step. Intended to advance the plotline or story. 
 */
USTRUCT(BlueprintType)
struct FCommsEvent
{
	GENERATED_BODY()

	// The sound to play during a comms event. Possibly a voice line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<USoundBase> Sound;
	// An image to show in the comms panel. Typically the portrait of the speaker.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UTexture2D> Texture;
	// The text to display while playing the voice line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayText = FText();
	// How long to display the text if there is no sound file specified.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ViewDuration = 13.f;
	// Whether to use the specified duration even if a sound file is specified.
	// Otherwise text is displayed for the duration of the sound plus a settable
	// delay on the UQuestManagerSubsystem class: CommsEventSubtitleDelay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bOverrideDuration = false;
};

USTRUCT(BlueprintType)
struct FQuestHUDEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<USoundBase> Sound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UTexture2D> Texture;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayText = FText();
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ViewDuration = 13.f;
};

/**
 * Displayed via the QuestWidget while a given quest step is active. Intended to guide and remind the player of
 * their current task.
 */
USTRUCT(BlueprintType)
struct FQuestText
{
	GENERATED_BODY()

	// The title of the quest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Title = FText();
	// A description of this quest step. Displayed below the title. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Info = FText();
	// Whether to display the quest goal counter during this quest step.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseCounter = false;
	// The text to display alongside the quest goal counter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText CounterLabel = FText();
	// The maximum amount to display on the quest goal counter.
	UPROPERTY(BlueprintReadWrite)
	int32 CounterMaxValue = 0;
	// The current amount to display on the counter. Does not have to start at 0.
	UPROPERTY(BlueprintReadWrite)
	int32 CounterCurrentValue = 0;
};
