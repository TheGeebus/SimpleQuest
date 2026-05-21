#include "SignalBusUnsubscribeTest.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NativeGameplayTags.h"
#include "Signals/SignalSubsystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(Tag_Bus_Test_Foo, "Test.Bus.Foo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(Tag_Bus_Test_Bar, "Test.Bus.Bar");
UE_DEFINE_GAMEPLAY_TAG_STATIC(Tag_Bus_Test_Baz, "Test.Bus.Baz");

namespace
{
	USignalSubsystem* GetSignals(const UObject* Ctx)
	{
		if (!Ctx) return nullptr;
		if (UWorld* World = Ctx->GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				return GI->GetSubsystem<USignalSubsystem>();
			}
		}
		return nullptr;
	}

	void Toast(const UObject* Ctx, const FString& Text, FLinearColor Color = FLinearColor::White)
	{
		UKismetSystemLibrary::PrintString(Ctx, Text, true, true, Color, 6.f);
	}
}

void ASignalBusUnsubscribeTest::BeginPlay()
{
	Super::BeginPlay();

	USignalSubsystem* Signals = GetSignals(this);
	if (!Signals)
	{
		Toast(this, TEXT("BusTest: SignalSubsystem unavailable — aborting"), FLinearColor::Red);
		return;
	}

	Signals->SubscribeMessage<FBusTestPayload>(Tag_Bus_Test_Foo, this, &ASignalBusUnsubscribeTest::HandleFoo);
	Signals->SubscribeMessage<FBusTestPayload>(Tag_Bus_Test_Bar, this, &ASignalBusUnsubscribeTest::HandleBar);
	Signals->SubscribeMessage<FBusTestPayload>(Tag_Bus_Test_Baz, this, &ASignalBusUnsubscribeTest::HandleBaz);
	Toast(this, TEXT("BusTest: subscribed to Test.Bus.Foo / Bar / Baz — End PIE to test EndPlay teardown"),
		FLinearColor(0.3f, 1.f, 0.3f));

	// Round 1 — three HandleX toasts confirm the subscriptions are live before the EndPlay test path runs.
	Signals->PublishMessage(Tag_Bus_Test_Foo, FBusTestPayload{1});
	Signals->PublishMessage(Tag_Bus_Test_Bar, FBusTestPayload{2});
	Signals->PublishMessage(Tag_Bus_Test_Baz, FBusTestPayload{3});
}

void ASignalBusUnsubscribeTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Mirrors the new UQuestObserverComponent::EndPlay teardown pattern — call UnsubscribeListener(this) so the bus
	// drops every record this actor registered, instead of leaving stale TWeakObjectPtr records to accumulate.
	// Verification: watch the Output Log for "Signal::UnsubscribeListener: listener='...' removed=3 compacted=3"
	// after pressing Stop in PIE. The yellow on-screen toast may flash briefly before the level finishes tearing
	// down, but the log line is the canonical confirmation.
	if (USignalSubsystem* Signals = GetSignals(this))
	{
		Signals->UnsubscribeListener(this);
		Toast(this, TEXT("BusTest: EndPlay called UnsubscribeListener(this)"), FLinearColor(1.f, 0.9f, 0.2f));
	}
	Super::EndPlay(EndPlayReason);
}

void ASignalBusUnsubscribeTest::HandleFoo(FGameplayTag Channel, const FBusTestPayload& Payload)
{
	Toast(this, FString::Printf(TEXT("BusTest: Foo received (value=%d)"), Payload.Value));
}

void ASignalBusUnsubscribeTest::HandleBar(FGameplayTag Channel, const FBusTestPayload& Payload)
{
	Toast(this, FString::Printf(TEXT("BusTest: Bar received (value=%d)"), Payload.Value));
}

void ASignalBusUnsubscribeTest::HandleBaz(FGameplayTag Channel, const FBusTestPayload& Payload)
{
	Toast(this, FString::Printf(TEXT("BusTest: Baz received (value=%d)"), Payload.Value));
}