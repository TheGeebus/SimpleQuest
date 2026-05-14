// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Utilities/QuestActivationGuard.h"


namespace
{
    constexpr EAutomationTestFlags ActivationGuardTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}


// Step diamond - refuses on Live or PendingGiver; ordering is Live wins when both set.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_StepDiamond,
    "SimpleQuest.ActivationGuard.StepDiamond", ActivationGuardTestFlags)
bool FQuestActivationGuard_StepDiamond::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bIsContainer = false;

    In.bIsLive = true;
    In.bIsPendingGiver = false;
    TestEqual(TEXT("Step Live alone"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::RefuseStepAlreadyLive);

    In.bIsLive = false;
    In.bIsPendingGiver = true;
    TestEqual(TEXT("Step PendingGiver alone"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::RefuseStepAlreadyPendingGiver);

    In.bIsLive = true;
    In.bIsPendingGiver = true;
    TestEqual(TEXT("Step Live wins over PendingGiver"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::RefuseStepAlreadyLive);

    return true;
}


// Container reentry - Live or PendingGiver on a container falls through to giver/Block
// check rather than refusing. Returned at the end when no giver fire and not blocked.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_ContainerReentry,
    "SimpleQuest.ActivationGuard.ContainerReentry", ActivationGuardTestFlags)
bool FQuestActivationGuard_ContainerReentry::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bIsContainer = true;

    In.bIsLive = true;
    TestEqual(TEXT("Container Live, no giver, not blocked"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::ContainerReentry);

    In.bIsLive = false;
    In.bIsPendingGiver = true;
    TestEqual(TEXT("Container PendingGiver, no giver, not blocked"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::ContainerReentry);

    In.bIsLive = true;
    In.bIsPendingGiver = true;
    TestEqual(TEXT("Container Live + PendingGiver"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::ContainerReentry);

    return true;
}


// Container reentry + giver - giver fire / path-aware skip preempt the ContainerReentry decision.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_ContainerReentryWithGiver,
    "SimpleQuest.ActivationGuard.ContainerReentryWithGiver", ActivationGuardTestFlags)
bool FQuestActivationGuard_ContainerReentryWithGiver::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bIsContainer = true;
    In.bIsLive = true;
    In.bHasRegisteredGiver = true;

    In.bAllReachableStepsAlreadyLive = false;
    TestEqual(TEXT("Container Live, has giver, not all live"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::GiverGateFire);

    In.bAllReachableStepsAlreadyLive = true;
    TestEqual(TEXT("Container Live, has giver, all reachable Steps live"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::GiverGateSkipPathAware);

    In.bBypassGiverGate = true;
    TestEqual(TEXT("Container Live, has giver, bypassed -> ContainerReentry"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::ContainerReentry);

    return true;
}


// Container reentry + Block - Block still refuses on reentry (giver gate didn't fire).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_ContainerReentryBlocked,
    "SimpleQuest.ActivationGuard.ContainerReentryBlocked", ActivationGuardTestFlags)
bool FQuestActivationGuard_ContainerReentryBlocked::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bIsContainer = true;
    In.bIsLive = true;
    In.bIsBlocked = true;

    TestEqual(TEXT("Container Live + Blocked, no giver"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::RefuseBlocked);

    return true;
}


// Giver gate fire - registered giver, not bypassed, path-aware skip not applicable.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_GiverGateFire,
    "SimpleQuest.ActivationGuard.GiverGateFire", ActivationGuardTestFlags)
bool FQuestActivationGuard_GiverGateFire::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bHasRegisteredGiver = true;

    // Step with registered giver - fires gate.
    In.bIsContainer = false;
    TestEqual(TEXT("Step + registered giver"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::GiverGateFire);

    // Container with registered giver, not all reachable Steps live - fires gate.
    In.bIsContainer = true;
    In.bAllReachableStepsAlreadyLive = false;
    TestEqual(TEXT("Container + registered giver, not all live"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::GiverGateFire);

    return true;
}


// Path-aware skip - Container + registered giver + all reachable Steps live -> skip.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_GiverGateSkipPathAware,
    "SimpleQuest.ActivationGuard.GiverGateSkipPathAware", ActivationGuardTestFlags)
bool FQuestActivationGuard_GiverGateSkipPathAware::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bIsContainer = true;
    In.bHasRegisteredGiver = true;
    In.bAllReachableStepsAlreadyLive = true;

    TestEqual(TEXT("All reachable Steps already live"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::GiverGateSkipPathAware);

    return true;
}


// Bypass giver gate - bBypassGiverGate=true skips the entire giver section.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_BypassGiverGate,
    "SimpleQuest.ActivationGuard.BypassGiverGate", ActivationGuardTestFlags)
bool FQuestActivationGuard_BypassGiverGate::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bHasRegisteredGiver = true;
    In.bBypassGiverGate = true;

    // Step + bypass - skips giver section, no diamond, not blocked -> Proceed.
    In.bIsContainer = false;
    TestEqual(TEXT("Step + bypass + clean state"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::Proceed);

    // Container + bypass + Live -> ContainerReentry (bypass means giver doesn't fire).
    In.bIsContainer = true;
    In.bIsLive = true;
    TestEqual(TEXT("Container + bypass + Live"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::ContainerReentry);

    return true;
}


// Block refusal - Blocked, no giver gate fire, no diamond hit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_RefuseBlocked,
    "SimpleQuest.ActivationGuard.RefuseBlocked", ActivationGuardTestFlags)
bool FQuestActivationGuard_RefuseBlocked::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bIsBlocked = true;

    In.bIsContainer = false;
    TestEqual(TEXT("Step + Blocked"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::RefuseBlocked);

    In.bIsContainer = true;
    TestEqual(TEXT("Container + Blocked, no diamond hit, no giver"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::RefuseBlocked);

    return true;
}


// Plain Proceed - no diamond, no giver, no Block.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_Proceed,
    "SimpleQuest.ActivationGuard.Proceed", ActivationGuardTestFlags)
bool FQuestActivationGuard_Proceed::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;

    In.bIsContainer = false;
    TestEqual(TEXT("Step clean"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::Proceed);

    In.bIsContainer = true;
    TestEqual(TEXT("Container clean"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::Proceed);

    return true;
}


// Giver gate preempts Block - registered giver + Blocked = GiverGateFire (intentional).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestActivationGuard_GiverPreemptsBlock,
    "SimpleQuest.ActivationGuard.GiverPreemptsBlock", ActivationGuardTestFlags)
bool FQuestActivationGuard_GiverPreemptsBlock::RunTest(const FString& Parameters)
{
    FQuestActivationGuardInputs In;
    In.bHasRegisteredGiver = true;
    In.bIsBlocked = true;

    In.bIsContainer = false;
    TestEqual(TEXT("Step + giver + Blocked -> GiverGateFire (Block doesn't preempt)"),
        FQuestActivationGuard::DecideFromInputs(In),
        EQuestActivationGuardDecision::GiverGateFire);

    return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS

