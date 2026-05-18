#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SignalBusUnsubscribeTest.generated.h"


/** Tiny payload used to confirm subscribers receive the exact bytes published. */
USTRUCT(BlueprintType)
struct FBusTestPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Value = 0;
};


/**
 * Self-testing actor for USignalSubsystem::UnsubscribeListener. BeginPlay subscribes to three channels, publishes a
 * payload on each (round 1 — three toasts expected), calls UnsubscribeListener(this), then publishes on each channel
 * again (round 2 — zero toasts expected). Drop in any test level; press Play; observe the screen toasts and the
 * Output Log line "Signal::UnsubscribeListener: listener='...' removed=3 compacted=3". Delete the actor + this class
 * after verifying.
 */
UCLASS()
class SIMPLEQUESTDEMO_API ASignalBusUnsubscribeTest : public AActor
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

private:
	void HandleFoo(FGameplayTag Channel, const FBusTestPayload& Payload);
	void HandleBar(FGameplayTag Channel, const FBusTestPayload& Payload);
	void HandleBaz(FGameplayTag Channel, const FBusTestPayload& Payload);
};