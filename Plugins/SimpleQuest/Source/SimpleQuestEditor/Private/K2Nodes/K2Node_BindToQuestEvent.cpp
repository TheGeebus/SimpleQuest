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
    if (bExposeOnCompleted && !FindPin(TEXT("OutcomeTag")))
    {
        FCreatePinParams PinParams;
        UEdGraphPin* OutcomePin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct,
            FGameplayTag::StaticStruct(), TEXT("OutcomeTag"), PinParams);
        if (OutcomePin)
        {
            OutcomePin->PinFriendlyName = NSLOCTEXT("SimpleQuestEditor", "OutcomeTagLabel", "Outcome Tag");
        }
    }
    
    /*
    // Defensive: ensure the proxy reference output exists, named "Subscription" per the
    // meta=(ExposedAsyncProxy=Subscription) on UQuestEventSubscription.
    if (!FindPin(TEXT("Subscription")))
    {
        UEdGraphPin* ProxyPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object,
            UQuestEventSubscription::StaticClass(), TEXT("Subscription"));
        if (ProxyPin)
        {
            ProxyPin->PinFriendlyName = NSLOCTEXT("SimpleQuestEditor", "SubscriptionLabel", "Subscription");
        }
    }
    */
    
    // Strip exec output pins for events the designer hasn't exposed via the bExpose* properties. The proxy class
    // delegate properties still exist (Super generated a pin for each), so we surgically remove the ones we don't
    // want to surface. The corresponding subscription is gated on the proxy side via the ExposedEvents bitmask.
    auto StripIfHidden = [this](const TCHAR* PinName, bool bExpose)
    {
        if (!bExpose)
        {
            if (UEdGraphPin* Pin = FindPin(PinName))
            {
                RemovePin(Pin);
            }
        }
    };
    StripIfHidden(TEXT("OnActivated"),   bExposeOnActivated);
    StripIfHidden(TEXT("OnStarted"),     bExposeOnStarted);
    StripIfHidden(TEXT("OnCompleted"),   bExposeOnCompleted);
    StripIfHidden(TEXT("OnDeactivated"), bExposeOnDeactivated);
    StripIfHidden(TEXT("OnGiven"),       bExposeOnGiven);
    StripIfHidden(TEXT("OnProgress"),    bExposeOnProgress);
    StripIfHidden(TEXT("OnBlocked"),     bExposeOnBlocked);
    
    // Strip exec-unique data pins:
    //   OutcomeTag → only meaningful on OnCompleted
    if (!bExposeOnCompleted)
    {
        if (UEdGraphPin* OutcomePin = FindPin(TEXT("OutcomeTag")))
        {
            RemovePin(OutcomePin);
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

    // 7. Stuff the computed exposure mask into the factory call's ExposedEvents parameter pin. The pin is hidden
    //    via the factory function's HidePin meta — designers configure exposure via the K2 node's bExpose* bools
    //    instead. Setting the per-instance DefaultValue is what makes those bools take effect at runtime.
    const int32 Mask = ComputeExposureMask();
    if (UEdGraphPin* MaskPin = FactoryCall->FindPin(TEXT("ExposedEvents")))
    {
        MaskPin->DefaultValue = FString::FromInt(Mask);
    }
    else
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("[BindToQuestEvent::ExpandNode] ExposedEvents pin not found on factory call — exposure mask not applied"));
    }

    if (Mask == 0)
    {
        CompilerContext.MessageLog.Warning(*FString::Printf(TEXT(
            "@@: Bind To Quest Event has no exposed event pins — subscription will be a no-op. ")
            TEXT("Enable at least one event under Pins | <phase> in the Details panel.")), this);
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("[BindToQuestEvent::ExpandNode] Inserted AsyncTask = factory-return assignment; ExposedEvents mask = %d"), Mask);
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

void UK2Node_BindToQuestEvent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // ReconstructNode on any bExpose* toggle — the pin set is keyed off these flags. ReconstructNode preserves
    // wires to surviving pins automatically; wires to pins that disappear get reported as orphaned during the
    // next compile, which is the standard UE behavior for configurable K2 nodes.
    if (PropertyChangedEvent.Property)
    {
        const FName PropertyName = PropertyChangedEvent.Property->GetFName();
        if (PropertyName.ToString().StartsWith(TEXT("bExpose")))
        {
            ReconstructNode();
        }
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
            "Quest Tag\n"
            "Gameplay Tag Structure\n\n"
            "The quest tag to subscribe to. Pass a leaf tag (e.g. SimpleQuest.Quest.MyLine.Step1) to watch a "
            "specific quest, or a parent tag (e.g. SimpleQuest.Quest.MyLine) to receive events from every "
            "descendant quest under it.");
        return;
    }

    // ── Output exec pins (per-delegate fire) ───────────────────────
    if (Direction == EGPD_Output && bIsExec)
    {
        if (PinName == TEXT("OnActivated"))
        {
            HoverTextOut = TEXT(
                "On Activated\n"
                "Exec\n\n"
                "Fires when execution reaches a giver-gated quest. Always fires on first wire arrival, "
                "regardless of prereq state — read Context for prereq info if you need to decorate UI based "
                "on whether the quest is actually accept-ready. Typical wiring: spawn quest-log entries, place "
                "world markers, queue intro UI.");
            return;
        }
        if (PinName == TEXT("OnGiven"))
        {
            HoverTextOut = TEXT(
                "On Given\n"
                "Exec\n\n"
                "Fires when a player accepts the quest from a giver — between Activation and Started. Useful "
                "for analytics, audio stings, or any one-shot reaction to acceptance specifically. Transient "
                "event: no catch-up. If you bind after the give already happened, this pin won't fire.");
            return;
        }
        if (PinName == TEXT("OnStarted"))
        {
            HoverTextOut = TEXT(
                "On Started\n"
                "Exec\n\n"
                "Fires when the subscribed quest enters the Live state — its objectives are bound and ticking. "
                "Typical wiring: music swap, target activation, gameplay-state changes, anything that should run "
                "only once the quest is truly underway.");
            return;
        }
        if (PinName == TEXT("OnProgress"))
        {
            HoverTextOut = TEXT(
                "On Progress\n"
                "Exec\n\n"
                "Fires on objective progress ticks during the Live phase. Context.CompletionData carries "
                "CurrentCount / RequiredCount / TriggeredActor / Instigator — break the struct to read them. "
                "Fires once per progress tick, not just on milestones.");
            return;
        }
        if (PinName == TEXT("OnCompleted"))
        {
            HoverTextOut = TEXT(
                "On Completed\n"
                "Exec\n\n"
                "Fires when the subscribed quest resolves. Outcome Tag tells you which outcome fired — switch "
                "on it to branch by Victory / Defeat / Negotiated / etc. The subscription stays bound after this "
                "event; if you only want to react once, wire Cancel into the Async Task pin.");
            return;
        }
        if (PinName == TEXT("OnDeactivated"))
        {
            HoverTextOut = TEXT(
                "On Deactivated\n"
                "Exec\n\n"
                "Fires when the subscribed quest is interrupted before completing (abandon, faction shift, "
                "external Deactivate request). Useful for cleanup — dismiss UI, deactivate markers, revert "
                "music. For the specific Blocked-state case, also expose On Blocked if you need to distinguish.");
            return;
        }
        if (PinName == TEXT("OnBlocked"))
        {
            HoverTextOut = TEXT(
                "On Blocked\n"
                "Exec\n\n"
                "Fires when the quest enters Blocked state via a SetBlocked utility node. Co-fires with "
                "On Deactivated when both pins are exposed (Blocked is a kind of deactivation). Read this when "
                "the Blocked-vs-other-deactivation distinction matters — e.g. to surface a 'this quest is "
                "locked out' UI distinct from 'this quest was abandoned'.");
            return;
        }
    }

    // ── Output data pins ───────────────────────────────────────────
    if (Direction == EGPD_Output && !bIsExec)
    {
        if (PinName == TEXT("QuestTag"))
        {
            HoverTextOut = TEXT(
                "Quest Tag\n"
                "Gameplay Tag Structure\n\n"
                "Which quest this event fired for. When subscribing on a parent tag, this is the specific "
                "descendant quest that triggered — use it to identify which child of the watched line is "
                "active right now.");
            return;
        }
        if (PinName == TEXT("OutcomeTag"))
        {
            HoverTextOut = TEXT(
                "Outcome Tag\n"
                "Gameplay Tag Structure\n\n"
                "The outcome the quest resolved with — only meaningful on the On Completed pin. For catch-up "
                "notifications (quest already resolved before binding), the outcome is recovered from this "
                "session's resolution registry. Empty (no tag) only when no record exists for this quest "
                "(e.g., it never resolved this session, or registry state was reset).");
            return;
        }
        if (PinName == TEXT("Context"))
        {
            HoverTextOut = TEXT(
                "Context\n"
                "Quest Event Context Structure\n\n"
                "The full event payload — Triggered Actor, Instigator, Node Info (quest tag + display name), "
                "and Custom Data. Break the struct (right-click → Split Struct Pin) or use member-access nodes "
                "to read individual fields. On the On Progress pin, Context.CompletionData carries the progress "
                "counters.");
            return;
        }
        if (PinName == TEXT("Subscription"))
        {
            HoverTextOut = TEXT(
                "Subscription\n"
                "Quest Event Subscription Object Reference\n\n"
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

void UK2Node_BindToQuestEvent::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
    Super::GetNodeContextMenuActions(Menu, Context);
    if (Context->Pin) return;  // pin context, not node context

    TWeakObjectPtr<UK2Node_BindToQuestEvent> WeakThis(const_cast<UK2Node_BindToQuestEvent*>(this));
    auto AddToggle = [WeakThis](FToolMenuSection& Section, const FText& Label, bool* PropPtr)
    {
        Section.AddMenuEntry(
            FName(*Label.ToString()),
            Label,
            FText::Format(NSLOCTEXT("BindToQuestEvent", "ToggleTooltip",
                "Show or hide the {0} exec pin on this node."), Label),
            FSlateIcon(),
            FUIAction(
            FExecuteAction::CreateLambda([WeakThis, PropPtr]()
                {
                    if (UK2Node_BindToQuestEvent* Node = WeakThis.Get())
                    {
                        const FScopedTransaction Transaction(NSLOCTEXT("BindToQuestEvent", "ToggleEventTransaction", "Toggle Quest Event Pin"));
                        Node->Modify();
                        *PropPtr = !*PropPtr;
                        Node->ReconstructNode();
                    }
                }),
                FCanExecuteAction(),
                FIsActionChecked::CreateLambda([PropPtr]() { return *PropPtr; })),
            EUserInterfaceActionType::Check);
    };

    {
        FToolMenuSection& Section = Menu->AddSection("BindToQuestEvent_Offer",
            NSLOCTEXT("BindToQuestEvent", "OfferPhase", "Exposed Events — Offer Phase"));
        AddToggle(Section, NSLOCTEXT("BindToQuestEvent", "OnActivated", "On Activated"),
            const_cast<bool*>(&bExposeOnActivated));
        AddToggle(Section, NSLOCTEXT("BindToQuestEvent", "OnGiven", "On Given"),
            const_cast<bool*>(&bExposeOnGiven));
    }
    {
        FToolMenuSection& Section = Menu->AddSection("BindToQuestEvent_Run",
            NSLOCTEXT("BindToQuestEvent", "RunPhase", "Exposed Events — Run Phase"));
        AddToggle(Section, NSLOCTEXT("BindToQuestEvent", "OnStarted", "On Started"),
            const_cast<bool*>(&bExposeOnStarted));
        AddToggle(Section, NSLOCTEXT("BindToQuestEvent", "OnProgress", "On Progress"),
            const_cast<bool*>(&bExposeOnProgress));
    }
    {
        FToolMenuSection& Section = Menu->AddSection("BindToQuestEvent_End",
            NSLOCTEXT("BindToQuestEvent", "EndPhase", "Exposed Events — End Phase"));
        AddToggle(Section, NSLOCTEXT("BindToQuestEvent", "OnCompleted", "On Completed"),
            const_cast<bool*>(&bExposeOnCompleted));
        AddToggle(Section, NSLOCTEXT("BindToQuestEvent", "OnDeactivated", "On Deactivated"),
            const_cast<bool*>(&bExposeOnDeactivated));
        AddToggle(Section, NSLOCTEXT("BindToQuestEvent", "OnBlocked", "On Blocked"),
            const_cast<bool*>(&bExposeOnBlocked));
    }
}

int32 UK2Node_BindToQuestEvent::ComputeExposureMask() const
{
    int32 Mask = 0;
    if (bExposeOnActivated)   Mask |= static_cast<int32>(EQuestEventTypes::Activated);
    if (bExposeOnStarted)     Mask |= static_cast<int32>(EQuestEventTypes::Started);
    if (bExposeOnCompleted)   Mask |= static_cast<int32>(EQuestEventTypes::Completed);
    if (bExposeOnDeactivated) Mask |= static_cast<int32>(EQuestEventTypes::Deactivated);
    if (bExposeOnGiven)       Mask |= static_cast<int32>(EQuestEventTypes::Given);
    if (bExposeOnProgress)    Mask |= static_cast<int32>(EQuestEventTypes::Progress);
    if (bExposeOnBlocked)     Mask |= static_cast<int32>(EQuestEventTypes::Blocked);
    return Mask;
}
