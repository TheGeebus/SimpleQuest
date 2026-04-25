// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "K2Nodes/K2Node_BindToQuestEvent.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintAsync/QuestEventSubscription.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_CallFunction.h"
#include "K2Node_TemporaryVariable.h"
#include "KismetCompiler.h"
#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"
#include "UObject/WeakObjectPtr.h"


void UK2Node_BindToQuestEvent::AllocateDefaultPins()
{
    Super::AllocateDefaultPins();

    // Defensive: ensure the OutcomeTag data pin exists. Some UE 5.6 paths drop unique-to-one-delegate
    // params during the unified pin pass.
    if (!FindPin(TEXT("OutcomeTag")))
    {
        FCreatePinParams PinParams;
        UEdGraphPin* OutcomePin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct,
            FGameplayTag::StaticStruct(), TEXT("OutcomeTag"), PinParams);
        if (OutcomePin)
        {
            OutcomePin->PinFriendlyName = NSLOCTEXT("SimpleQuestEditor", "OutcomeTagLabel", "Outcome Tag");
        }
    }

    // Defensive: ensure the proxy reference output exists, named "AsyncTask" per the
    // meta=(ExposedAsyncProxy=AsyncTask) on UBlueprintAsyncActionBase.
    if (!FindPin(TEXT("AsyncTask")))
    {
        UEdGraphPin* ProxyPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object,
            UQuestEventSubscription::StaticClass(), TEXT("AsyncTask"));
        if (ProxyPin)
        {
            ProxyPin->PinFriendlyName = NSLOCTEXT("SimpleQuestEditor", "AsyncTaskLabel", "Async Task");
        }
    }
}

void UK2Node_BindToQuestEvent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
    Super::ExpandNode(CompilerContext, SourceGraph);

    // Base UK2Node_AsyncAction creates: factory call, AsyncTask temp variable, delegate handlers, delegate-param
    // assignment statements. What it does NOT create — at least when the factory function lives on a separate
    // class from the proxy class, as we're set up — is the assignment that copies the factory's return value into
    // the AsyncTask temp variable. Result: AsyncTask temp stays null, any designer wire from AsyncTask reads null
    // at runtime, Cancel crashes. Splice the missing assignment in here.

    // 1. Find the AsyncTask temp variable. The four temp variables base creates are typed FGameplayTag (×2),
    //    FQuestEventContext, and UQuestEventSubscription* — only the last is object-typed, so filter by
    //    PinSubCategoryObject pointing at our proxy class.
    UK2Node_TemporaryVariable* AsyncTaskTemp = nullptr;
    for (UEdGraphNode* Node : SourceGraph->Nodes)
    {
        UK2Node_TemporaryVariable* TempVar = Cast<UK2Node_TemporaryVariable>(Node);
        if (!TempVar) continue;
        if (TempVar->VariableType.PinSubCategoryObject == UQuestEventSubscription::StaticClass())
        {
            AsyncTaskTemp = TempVar;
            break;
        }
    }
    if (!AsyncTaskTemp)
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("[BindToQuestEvent::ExpandNode] AsyncTask temp variable not found — base didn't generate one"));
        return;
    }

    // 2. Find the factory call (return type matches UQuestEventSubscription)
    UK2Node_CallFunction* FactoryCall = nullptr;
    for (UEdGraphNode* Node : SourceGraph->Nodes)
    {
        UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
        if (!Call) continue;
        UEdGraphPin* Ret = Call->GetReturnValuePin();
        if (Ret && Ret->PinType.PinSubCategoryObject == UQuestEventSubscription::StaticClass())
        {
            FactoryCall = Call;
            break;
        }
    }
    if (!FactoryCall) return;

    UEdGraphPin* FactoryThen = FactoryCall->GetThenPin();
    UEdGraphPin* FactoryReturn = FactoryCall->GetReturnValuePin();
    UEdGraphPin* TempVarPin = AsyncTaskTemp->FindPin(TEXT("Variable"));
    if (!FactoryThen || !FactoryReturn || !TempVarPin) return;

    // 3. Spawn an assignment statement: AsyncTaskTemp = Factory return value
    UK2Node_AssignmentStatement* Assignment = CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
    Assignment->AllocateDefaultPins();

    UEdGraphPin* AssignExec = Assignment->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* AssignThen = Assignment->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* AssignVariable = Assignment->GetVariablePin();
    UEdGraphPin* AssignValue = Assignment->GetValuePin();
    if (!AssignExec || !AssignThen || !AssignVariable || !AssignValue) return;

    // 4. Splice the assignment between Factory.Then and whatever followed (typically the IsValid → bind-delegates chain).
    //    Capture existing successors → break the link → re-route Factory.Then → Assignment.Exec → Assignment.Then → original successors.
    TArray<UEdGraphPin*> OriginalSuccessors = FactoryThen->LinkedTo;
    FactoryThen->BreakAllPinLinks();
    FactoryThen->MakeLinkTo(AssignExec);
    for (UEdGraphPin* Successor : OriginalSuccessors)
    {
        AssignThen->MakeLinkTo(Successor);
    }

    // 5. Wire the assignment's Variable input to AsyncTaskTemp's variable pin (assigning into the temp's storage)
    //    and the Value input to Factory.Return.
    AssignVariable->MakeLinkTo(TempVarPin);
    AssignValue->MakeLinkTo(FactoryReturn);

    // 6. Notify type propagation so the wildcard pins on the Assignment node resolve to UQuestEventSubscription*.
    Assignment->NotifyPinConnectionListChanged(AssignVariable);
    Assignment->NotifyPinConnectionListChanged(AssignValue);

    UE_LOG(LogSimpleQuest, Log, TEXT("[BindToQuestEvent::ExpandNode] Inserted AsyncTask = factory-return assignment"));
}

void UK2Node_BindToQuestEvent::PostPlacedNewNode()
{
    Super::PostPlacedNewNode();

    // Pre-populate proxy fields BEFORE AllocateDefaultPins runs. UE 5.6's spawner invokes the order:
    //   PostPlacedNewNode → AllocateDefaultPins → CustomizeNodeDelegate
    // Our CustomizeNodeDelegate sets these same fields, but it runs AFTER AllocateDefaultPins, by which
    // point the base's pin generation has already given up on AsyncTask + delegate-unique data pins
    // (it iterates GetProxyClass(), which is still null at that moment).
    //
    // Setting the proxy fields here means base's AllocateDefaultPins sees the correct proxy reference
    // and generates the full pin set naturally — no defensive overrides needed downstream.
    if (UFunction* FactoryFunc = USimpleQuestBlueprintLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(USimpleQuestBlueprintLibrary, BindToQuestEvent)))
    {
        FObjectProperty* ReturnProperty = CastField<FObjectProperty>(FactoryFunc->GetReturnProperty());
        ProxyFactoryFunctionName = FactoryFunc->GetFName();
        ProxyFactoryClass = FactoryFunc->GetOuterUClass();
        ProxyClass = ReturnProperty ? ReturnProperty->PropertyClass : nullptr;
    }
}

void UK2Node_BindToQuestEvent::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
    const FName PinName = Pin.PinName;
    const EEdGraphPinDirection Direction = Pin.Direction;
    const bool bIsExec = Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;

    // ── Inputs ─────────────────────────────────────────────────────
    if (Direction == EGPD_Input && PinName == TEXT("QuestTag"))
    {
        HoverTextOut = TEXT(
            "The quest tag to subscribe to. Pass a leaf tag (e.g. Quest.MyLine.Step1) to watch a specific quest, "
            "or a parent tag (e.g. Quest.MyLine) to receive events from every descendant quest under it.");
        return;
    }

    // ── Output exec pins (per-delegate fire) ───────────────────────
    if (Direction == EGPD_Output && bIsExec)
    {
        if (PinName == TEXT("OnActivated"))
        {
            HoverTextOut = TEXT(
                "Fires when the subscribed quest becomes enabled — it's ready to start but may be waiting on a "
                "giver. Typical wiring: spawn quest-log entries, place world markers, queue intro UI.");
            return;
        }
        if (PinName == TEXT("OnStarted"))
        {
            HoverTextOut = TEXT(
                "Fires when the subscribed quest actually begins — its objectives are live. Typical wiring: "
                "music swap, target activation, gameplay-state changes, anything that should run only once the "
                "quest is truly underway.");
            return;
        }
        if (PinName == TEXT("OnCompleted"))
        {
            HoverTextOut = TEXT(
                "Fires when the subscribed quest resolves. Outcome Tag tells you which outcome fired — switch "
                "on it to branch by Victory / Defeat / Negotiated / etc. The subscription stays bound after this "
                "event; if you only want to react once, wire Cancel into the Async Task pin.");
            return;
        }
        if (PinName == TEXT("OnDeactivated"))
        {
            HoverTextOut = TEXT(
                "Fires when the subscribed quest is blocked or torn down without completing. Useful for cleanup "
                "(dismiss UI, deactivate markers, revert music) on quest failure or interruption.");
            return;
        }
    }

    // ── Output data pins ───────────────────────────────────────────
    if (Direction == EGPD_Output && !bIsExec)
    {
        if (PinName == TEXT("QuestTag"))
        {
            HoverTextOut = TEXT(
                "Which quest this event fired for. When subscribing on a parent tag, this is the specific "
                "descendant quest that triggered — use it to identify which child of the watched line is "
                "active right now.");
            return;
        }
        if (PinName == TEXT("OutcomeTag"))
        {
            HoverTextOut = TEXT(
                "The outcome the quest resolved with — only meaningful on the On Completed pin. Empty (no tag) "
                "for catch-up notifications when the quest had already completed before binding (the original "
                "outcome can't be recovered from world state alone).");
            return;
        }
        if (PinName == TEXT("Context"))
        {
            HoverTextOut = TEXT(
                "The full event payload — Triggered Actor, Instigator, Node Info (quest tag + display name), "
                "and Custom Data. Break the struct (right-click → Split Struct Pin) or use member-access nodes "
                "to read individual fields. Lets you build messages like 'Player X completed Quest Y' without "
                "extra lookups.");
            return;
        }
        if (PinName == TEXT("AsyncTask"))
        {
            HoverTextOut = TEXT(
                "Reference to this subscription instance. Wire directly to a Cancel call when an exec pin fires "
                "(e.g. cancel-after-first-completion), or promote to a variable to hold the reference for later "
                "cancellation from elsewhere (e.g. End Play, player death). The subscription is otherwise "
                "GameInstance-scoped — it stays bound until manually cancelled or the game tears down.");
            return;
        }
    }

    // Fallback to base — covers Begin exec input, hidden WorldContext, and anything else we haven't named.
    Super::GetPinHoverText(Pin, HoverTextOut);
}

FSlateIcon UK2Node_BindToQuestEvent::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	// Reuses the existing Questline class icon registered by FSimpleQuestEditor::StartupModule — same brush
	// UK2Node_CompleteObjectiveWithOutcome uses. No new style-set entry needed.
	return FSlateIcon(TEXT("SimpleQuestStyle"), TEXT("ClassIcon.QuestlineGraph"));
}

void UK2Node_BindToQuestEvent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    const UClass* ActionKey = GetClass();
    if (!ActionRegistrar.IsOpenForRegistration(ActionKey)) return;

    // Factory lives on USimpleQuestBlueprintLibrary (not on UQuestEventSubscription) so UK2Node_AsyncAction's
    // base iteration doesn't scan it — this K2 node is the only entry point for the Bind To Quest Event action.
    UFunction* FactoryFunc = USimpleQuestBlueprintLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(USimpleQuestBlueprintLibrary, BindToQuestEvent));
    if (!FactoryFunc) return;

    UBlueprintNodeSpawner* Spawner = UBlueprintFunctionNodeSpawner::Create(FactoryFunc);
    if (!Spawner) return;

    Spawner->NodeClass = GetClass();

    // Palette-row icon. The palette uses DefaultMenuSignature.Icon rather than calling GetIconAndTint on
    // a template node, so we have to set it explicitly — otherwise the right-click search shows the default
    // F (function) glyph regardless of what GetIconAndTint returns.
    Spawner->DefaultMenuSignature.Icon = FSlateIcon(TEXT("SimpleQuestStyle"), TEXT("ClassIcon.QuestlineGraph"));
    Spawner->DefaultMenuSignature.IconTint = FLinearColor::White;

    // CustomizeNodeDelegate — post-spawn hook that populates ProxyFactoryFunctionName / ProxyFactoryClass /
    // ProxyClass on the node. Without it, the base UK2Node_AsyncAction machinery has no idea what proxy class
    // this node wraps and can't auto-generate input + output pins. Mirrors what the base's GetMenuActions does.
    TWeakObjectPtr<UFunction> FactoryFuncWeak = MakeWeakObjectPtr(FactoryFunc);
    Spawner->CustomizeNodeDelegate = UBlueprintFunctionNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
        [FactoryFuncWeak](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
        {
            UK2Node_BindToQuestEvent* AsyncNode = CastChecked<UK2Node_BindToQuestEvent>(NewNode);
            if (UFunction* Func = FactoryFuncWeak.Get())
            {
                FObjectProperty* ReturnProperty = CastField<FObjectProperty>(Func->GetReturnProperty());
                AsyncNode->ProxyFactoryFunctionName = Func->GetFName();
                AsyncNode->ProxyFactoryClass = Func->GetOuterUClass();
                AsyncNode->ProxyClass = ReturnProperty ? ReturnProperty->PropertyClass : nullptr;
            }
        });

    ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
}

