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
 * Self-testing actor for various SimpleCore and SimpleQuest features. Reconfigured regularly.
 */
UCLASS()
class SIMPLEQUESTDEMO_API ASignalBusUnsubscribeTest : public AActor
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleFoo(FGameplayTag Channel, const FBusTestPayload& Payload);
	void HandleBar(FGameplayTag Channel, const FBusTestPayload& Payload);
	void HandleBaz(FGameplayTag Channel, const FBusTestPayload& Payload);
};