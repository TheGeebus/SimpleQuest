// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestlineGraphCompiler.h"

#include "GameplayTagsManager.h"
#include "ISimpleQuestEditorModule.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestStep.h"
#include "Quests/Quest.h"
#include "Quests/PrerequisiteExpression.h"
#include "Quests/QuestPrereqRuleNode.h"
#include "Quests/SetBlockedNode.h"
#include "Quests/ClearBlockedNode.h"
#include "Quests/ActivationGroupExitNode.h"
#include "Quests/ActivationGroupEntryNode.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleEntry.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleExit.h"
#include "Nodes/Utility/QuestlineNode_SetBlocked.h"
#include "Nodes/Utility/QuestlineNode_ClearBlocked.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupEntry.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupExit.h"
#include "Utilities/QuestStateTagUtils.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Objectives/QuestObjective.h"
#include "Rewards/QuestReward.h"
#include "Toolkit/QuestlineGraphEditor.h"
#include "Types/QuestPinRole.h"
#include "Utilities/SimpleQuestEditorUtils.h"



FQuestlineGraphCompiler::FQuestlineGraphCompiler()
    : TraversalPolicy(MakeUnique<FQuestlineGraphTraversalPolicy>())
{
}

FQuestlineGraphCompiler::~FQuestlineGraphCompiler() = default;


// -------------------------------------------------------------------------------------------------
// Entry point
// -------------------------------------------------------------------------------------------------

bool FQuestlineGraphCompiler::Compile(UQuestlineGraph* InGraph)
{
    if (!InGraph || !InGraph->QuestlineEdGraph)
    {
        AddError(TEXT("Invalid graph asset. QuestlineEdGraph is null."));
        return false;
    }

    bHasErrors = false;
    Messages.Empty();
    NumErrors = 0;
    NumWarnings = 0;
    RootGraph = InGraph;

    // Derive the effective questline ID; designer override takes priority, asset name is the fallback
    const FString TagPrefix = SanitizeTagSegment(InGraph->QuestlineID.IsEmpty() ? InGraph->GetName() : InGraph->QuestlineID);

    // Validate that no other questline asset shares this effective ID
    IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
    TArray<FAssetData> AllQuestlineGraphs;
    FARFilter Filter;
    Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    AssetRegistry.GetAssets(Filter, AllQuestlineGraphs);

    for (const FAssetData& Asset : AllQuestlineGraphs)
    {
        if (Asset.GetObjectPathString() == InGraph->GetPathName()) continue;
        FAssetTagValueRef TagValue = Asset.TagsAndValues.FindTag(TEXT("QuestlineEffectiveID"));
        if (TagValue.IsSet() && SanitizeTagSegment(TagValue.GetValue()) == TagPrefix)
        {
            AddError(FString::Printf(
                TEXT("QuestlineID '%s' is already used by '%s'. Set a unique QuestlineID on one of these assets to resolve the conflict."),
                *TagPrefix,
                *Asset.GetObjectPathString()));
            return false;
        }
    }
    
    UE_LOG(LogSimpleQuest, Log, TEXT("Compile: starting '%s' (prefix='%s')"),
        *InGraph->GetName(),
        *TagPrefix);

    // ── Snapshot old GUID→Tag mapping for rename detection ────────
    TMap<FGuid, FName> OldTagsByGuid;
    for (const auto& [TagName, NodeInstance] : InGraph->CompiledNodes)
    {
        if (NodeInstance && NodeInstance->GetQuestGuid().IsValid())
        {
            OldTagsByGuid.Add(NodeInstance->GetQuestGuid(), TagName);
        }
    }
	CurrentOuterGuidChain = FGuid();
    DetectedTagRenames.Empty();

	// Clear parallel-path tracking so a fresh compile doesn't inherit stale data from a prior compile.
	DirectReachesByDest.Empty();
	GroupSetterSourcesByTag.Empty();
	GroupGetterDestsByTag.Empty();
	SetterEdNodeByGroupAndSource.Empty();
	GetterEdNodeByGroupAndDest.Empty();
	
    InGraph->Modify();
    InGraph->CompiledNodes.Empty(); 
    InGraph->EntryNodeTags.Empty();
    InGraph->CompiledQuestTags.Empty();
    AllCompiledNodes.Empty();
    UtilityNodeKeyMap.Empty();
    RootGraph = InGraph;

    // Refresh outcome pins on all step nodes so that changes to outcomes on an objective class are reflected without
    // the designer having to touch ObjectiveClass again.
    for (UEdGraphNode* Node : InGraph->QuestlineEdGraph->Nodes)
    {
        if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(Node)) StepNode->RefreshOutcomePins();
    }

    // The graphs that have already been compiled. Provided to CompileGraph, which forwards it to all recursive calls.
    TArray<FString> VisitedAssetPaths;
    VisitedAssetPaths.Add(InGraph->GetPathName());

    // Start recursive compilation, working forward from the Start node.
    TArray<FName> EntryTags = CompileGraph(InGraph->QuestlineEdGraph, TagPrefix, {}, VisitedAssetPaths);
    InGraph->EntryNodeTags = EntryTags;
    InGraph->CompiledNodes = MoveTemp(AllCompiledNodes);
    InGraph->CompiledEditorNodes = MoveTemp(AllCompiledEditorNodes);
    InGraph->CompiledQuestTags = MoveTemp(AllCompiledQuestTags);

    // Detect renames via GUID bridge
    DetectAndRecordTagRenames(InGraph, OldTagsByGuid);

    RegisterCompiledTags(InGraph);

    UE_LOG(LogSimpleQuest, Log, TEXT("Compile: '%s' finished — %d node(s), %d tag(s), %d error(s), %d warning(s)"),
        *InGraph->GetName(),
        InGraph->CompiledNodes.Num(),
        InGraph->CompiledQuestTags.Num(),
        NumErrors,
        NumWarnings);

	EmitParallelPathWarnings();
	
    return !bHasErrors;
}


// -------------------------------------------------------------------------------------------------
// CompileGraph — recursive
// -------------------------------------------------------------------------------------------------

TArray<FName> FQuestlineGraphCompiler::CompileGraph(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TMap<FGameplayTag, FQuestEntryRouteList>* OutEntryTagsByOutcome)	
{
    if (!Graph) return {};

    // ---- Pass 1: label uniqueness, GUID write, tag assignment ----
    TArray<UQuestlineNode_ContentBase*> ContentNodes;
    TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*> NodeInstanceMap;
    CompileNodeRegistration(Graph, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, ContentNodes, NodeInstanceMap);

    // ---- Pass 1b: setter nodes — create UQuestPrereqRuleNode monitors ----
    TArray<FName> MonitorTags;
    TArray<FName> GetterEntryTags;
    CompileGroupSetters(Graph, TagPrefix, VisitedAssetPaths, MonitorTags, GetterEntryTags);

    // ---- Pass 1c: utility nodes ----
    TArray<UQuestlineNode_UtilityBase*> UtilityEdNodes;
    CompileUtilityNodes(Graph, UtilityEdNodes);

    UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGraph: [%s] %d content, %d group setter(s), %d utility node(s)"),
        *TagPrefix,
        ContentNodes.Num(),
        MonitorTags.Num(),
        UtilityEdNodes.Num());
    
    if (bHasErrors) return {};

    // ---- Stale pin diagnostic ----
    for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
    {
        for (UEdGraphPin* Pin : ContentNode->Pins)
        {
            if (Pin->bOrphanedPin && Pin->LinkedTo.Num() > 0)
            {
                const FString Label = ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
                AddWarning(FString::Printf(
                    TEXT("[%s] Node '%s' has a stale pin '%s' with %d active connection(s). These wires will be ignored at runtime. Right-click the node to remove stale pins."),
                    *TagPrefix, *Label, *Pin->PinName.ToString(), Pin->LinkedTo.Num()), ContentNode);
            }
        }
    }

    // ---- Pass 2: output pin wiring ----
    CompileOutputWiring(ContentNodes, NodeInstanceMap, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths);

	// ---- Collect activation group metadata for parallel-path analysis ----
	CollectActivationGroupMetadata(Graph, TagPrefix);

	// ---- Pass 2b: Forward output wiring for utility-keyed nodes that live in THIS graph ----
	// UtilityNodeKeyMap is a compiler-wide map accumulated across recursion; iterating it unconditionally would
	// rewrite nested utility nodes with the outer graph's TagPrefix each time recursion unwinds. Scope the loop
	// to this graph's nodes so each utility node's forward wiring is resolved against the prefix it was born with.
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		const FName* UtilKey = UtilityNodeKeyMap.Find(Node);
		if (!UtilKey) continue;

		UQuestNodeBase* Inst = AllCompiledNodes.FindRef(*UtilKey);
		if (!Inst) continue;

		Inst->NextNodesOnForward.Empty();

		if (UEdGraphPin* ForwardPin = UQuestlineNodeBase::FindPinByRole(Node, EQuestPinRole::ExecForwardOut))
		{
			TArray<FName> ForwardTags;
			ResolvePinToTags(ForwardPin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, ForwardTags);
			for (const FName& Tag : ForwardTags) Inst->NextNodesOnForward.Add(Tag);
		}
	}

    // ---- Resolve entry tags from the graph's Entry node ----
    TArray<FName> EntryTags = ResolveEntryTags(Graph, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, OutEntryTagsByOutcome);
    EntryTags.Append(MonitorTags);
    EntryTags.Append(GetterEntryTags);
    return EntryTags;
}

void FQuestlineGraphCompiler::CompileNodeRegistration(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TArray<UQuestlineNode_ContentBase*>& OutContentNodes, TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& OutNodeInstanceMap)
{
    TMap<FString, UQuestlineNode_ContentBase*> LabelMap;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node);
        if (!ContentNode) continue;
        OutContentNodes.Add(ContentNode);
    	
    	/**
		 * LinkedQuestline's GetNodeTitle is driven by the referenced asset's name (so multiple placements of the same asset share
		 * a title); use NodeLabel directly to guarantee per-placement uniqueness. Other content nodes' GetNodeTitle already reflects
		 * NodeLabel via the base ContentBase path, so behavior is unchanged for them.
		 */
    	const FString Label = Cast<UQuestlineNode_LinkedQuestline>(ContentNode)
			? SanitizeTagSegment(ContentNode->NodeLabel.ToString())
			: SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    	
        if (Label.IsEmpty())
        {
            AddError(FString::Printf(TEXT("[%s] A content node has an empty label. All Quest and Step nodes must have a label before compiling."), *TagPrefix), ContentNode);
            continue;
        }
        if (LabelMap.Contains(Label))
        {
            AddError(FString::Printf(TEXT("[%s] Duplicate node label '%s'. Labels must be unique within a graph."), *TagPrefix, *Label), ContentNode);
            continue;
        }
        LabelMap.Add(Label, ContentNode);

        // Create the appropriate runtime instance
        UQuestNodeBase* Instance = nullptr;

        if (UQuestlineNode_Quest* QuestEdNode = Cast<UQuestlineNode_Quest>(ContentNode))
        {
            UQuest* QuestInstance = NewObject<UQuest>(RootGraph);
            if (QuestEdNode->GetInnerGraph())
            {
                const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
                TMap<FGameplayTag, FQuestEntryRouteList> InnerEntryByOutcome;
                QuestInstance->EntryStepTags = CompileGraph(QuestEdNode->GetInnerGraph(), InnerPrefix, {}, VisitedAssetPaths, &InnerEntryByOutcome);
                QuestInstance->EntryStepTagsByOutcome = MoveTemp(InnerEntryByOutcome);
                
                // Register entry outcome fact tags for prerequisite expressions within the inner graph.
                for (UEdGraphNode* InnerNode : QuestEdNode->GetInnerGraph()->Nodes)
                {
                    if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(InnerNode))
                    {
                        const FName QuestTagName = MakeNodeTagName(TagPrefix, Label);
                        for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
                        {
                            if (!Spec.bExposed) continue;
                            if (!Spec.Outcome.IsValid()) continue;
                            AllCompiledQuestTags.AddUnique(FQuestStateTagUtils::MakeEntryOutcomeFact(QuestTagName, Spec.Outcome));
                        }
                        break;
                    }
                }
            }            
            Instance = QuestInstance;
        }
        else if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(ContentNode))
        {
            if (StepNode->ObjectiveClass.IsNull())
            {
                AddError(FString::Printf(TEXT("[%s] Step node '%s' has no Objective Class assigned."), *TagPrefix, *Label), ContentNode);
                continue;
            }
            UQuestStep* StepInstance = NewObject<UQuestStep>(RootGraph);
            StepInstance->QuestObjective = StepNode->ObjectiveClass;
            StepInstance->Reward = StepNode->RewardClass;
            StepInstance->TargetClasses = StepNode->TargetClasses;
            StepInstance->NumberOfElements = StepNode->NumberOfElements;
            StepInstance->TargetActors.Append(StepNode->TargetActors);
            StepInstance->PrerequisiteGateMode = StepNode->PrerequisiteGateMode;
            Instance = StepInstance;
        }
		else if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(ContentNode))
		{
			if (LinkedNode->LinkedGraph.IsNull())
			{
				// Null LinkedGraph is valid — emit a UQuest instance with the node's own compiled tag so designers can
				// attach givers and reference the LinkedQuestline by tag before picking an asset. No inner routing
				// populates (empty EntryStepTags / NextNodesByOutcome); the instance behaves as an empty container
				// until an asset is picked and the graph recompiled. Warning is still issued so designers know the
				// compile is effectively incomplete.
				AddWarning(FString::Printf(TEXT("[%s] LinkedQuestline node '%s' has no asset assigned — runtime instance emitted with no inner routing; pick an asset to populate."),
					*TagPrefix,
					*Label),
					LinkedNode);
				Instance = NewObject<UQuest>(RootGraph);
			}
			else
			{
				UQuestlineGraph* LinkedGraph = LinkedNode->LinkedGraph.LoadSynchronous();
				if (!LinkedGraph || !LinkedGraph->QuestlineEdGraph)
				{
					AddError(FString::Printf(TEXT("[%s] LinkedQuestline '%s' failed to load asset '%s'."),
						*TagPrefix,
						*Label,
						*LinkedNode->LinkedGraph.ToString()),
						LinkedNode);
					continue;
				}

				// Refresh outcome pins before reading them — the linked graph's Exit tags may have changed since this node
				// was last edited or loaded, without triggering PostLoad/PostEditChangeProperty on the parent. Runs once
				// per compile, per placement, which is cheap.
				LinkedNode->RebuildOutcomePinsFromLinkedGraph();

				const FString LinkedPath = LinkedGraph->GetPathName();
				if (VisitedAssetPaths.Contains(LinkedPath))
				{
					/**
					 * Reconstruct the cycle path for the error: slice VisitedAssetPaths from the cycling asset's prior entry to
					 * the end, then close with the cycling asset name again. The cycle is a property of the chain as a whole —
					 * this link is not uniquely at fault, it just happens to be the one that closes the loop during recursion.
					 */
					const int32 CycleStart = VisitedAssetPaths.IndexOfByKey(LinkedPath);
					FString CyclePath;
					for (int32 i = CycleStart; i < VisitedAssetPaths.Num(); ++i)
					{
						CyclePath += FPackageName::ObjectPathToObjectName(VisitedAssetPaths[i]);
						CyclePath += TEXT(" → ");
					}
					CyclePath += FPackageName::ObjectPathToObjectName(LinkedPath);

					AddError(FString::Printf(
						TEXT("LinkedQuestline cycle detected: compile chain [%s]. This link closes the cycle; it is valid in isolation, "
						"but any link in this chain must be removed for compilation to succeed. Use activation group setter/getter pairs for runtime "
						"loops across assets."),
						*CyclePath),
						LinkedNode);
					continue;
				}

				UQuest* QuestInstance = NewObject<UQuest>(RootGraph);

				/**
				 * Build the boundary map: each LinkedQuestline output pin represents an exit outcome of the linked asset, and its
				 * downstream wires in THIS (parent) graph are the destinations the linked Exit nodes should route to. Named outcome
				 * pins keyed by outcome tag; "Any Outcome" pin stored under the invalid tag as a catch-all.
				 */
				TMap<FGameplayTag, TArray<FName>> LinkedBoundaryByOutcome;
				for (UEdGraphPin* OutputPin : LinkedNode->Pins)
				{
					if (OutputPin->Direction != EGPD_Output) continue;

					TArray<FName> PinTags;
					ResolvePinToTags(OutputPin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, PinTags);
					if (PinTags.IsEmpty()) continue;

					if (OutputPin->PinType.PinCategory == TEXT("QuestOutcome"))
					{
						const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutputPin->PinName, false);
						if (OutcomeTag.IsValid()) for (const FName& Tag : PinTags) LinkedBoundaryByOutcome.FindOrAdd(OutcomeTag).AddUnique(Tag);
					}
					else if (UQuestlineNodeBase::GetPinRoleOf(OutputPin) == EQuestPinRole::AnyOutcomeOut)
					{
						for (const FName& Tag : PinTags) LinkedBoundaryByOutcome.FindOrAdd(FGameplayTag()).AddUnique(Tag);
					}
				}

				/**
				 * Compile the linked asset's graph as the UQuest's inner graph. TagPrefix for the inner compile is the
				 * LinkedQuestline's own compiled path — same pattern as inline Quest. Linked content nodes' compiled tags
				 * thus nest under this LinkedQuestline's tag (Quest.<ParentID>.<NodeLabel>.<InnerNodeLabel>), keeping a
				 * stable per-parent namespace when the same linked asset is referenced from multiple places.
				 */
				VisitedAssetPaths.Add(LinkedPath);

				/**
				 * Push the linked placement's GUID onto the chain so inner content nodes produce placement-unique compound
				 * GUIDs. Save/restore with local so nested LinkedQuestlines accumulate correctly through multiple levels.
				 */
				const FGuid PreviousGuidChain = CurrentOuterGuidChain;
				CurrentOuterGuidChain = CombineGuids(CurrentOuterGuidChain, LinkedNode->QuestGuid);

				const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
				TMap<FGameplayTag, FQuestEntryRouteList> InnerEntryByOutcome;
				QuestInstance->EntryStepTags = CompileGraph(LinkedGraph->QuestlineEdGraph, InnerPrefix, LinkedBoundaryByOutcome, VisitedAssetPaths, &InnerEntryByOutcome);
				QuestInstance->EntryStepTagsByOutcome = MoveTemp(InnerEntryByOutcome);

				CurrentOuterGuidChain = PreviousGuidChain;
				VisitedAssetPaths.RemoveSingleSwap(LinkedPath);

				/**
				 * Register entry outcome fact tags for prerequisite expressions inside the linked graph — same pattern as the
				 * inline Quest branch. Iterates the linked graph's Entry node specs.
				 */
				for (UEdGraphNode* InnerNode : LinkedGraph->QuestlineEdGraph->Nodes)
				{
					if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(InnerNode))
					{
						const FName QuestTagName = MakeNodeTagName(TagPrefix, Label);
						for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
						{
							if (!Spec.bExposed) continue;
							if (!Spec.Outcome.IsValid()) continue;
							AllCompiledQuestTags.AddUnique(FQuestStateTagUtils::MakeEntryOutcomeFact(QuestTagName, Spec.Outcome));
						}
						break;
					}
				}

				Instance = QuestInstance;
			}
		}
        
        if (!Instance) continue;
        
    	Instance->QuestContentGuid = CombineGuids(CurrentOuterGuidChain, ContentNode->QuestGuid);
        Instance->NodeInfo.DisplayName = ContentNode->NodeLabel;
        const FName TagName = MakeNodeTagName(TagPrefix, Label);
        AllCompiledQuestTags.Add(TagName);

        // Register outcome tags: both the raw Quest.Outcome.* tag and the per-node fact tag
        if (const UQuestlineNode_Step* QuestStepNode = Cast<UQuestlineNode_Step>(ContentNode))
        {
            if (!QuestStepNode->ObjectiveClass.IsNull())
            {
                TArray<FGameplayTag> Outcomes = FSimpleQuestEditorUtilities::DiscoverObjectiveOutcomes(QuestStepNode->ObjectiveClass.LoadSynchronous());
                for (const FGameplayTag& OutcomeTag : Outcomes)
                {
                    AllCompiledQuestTags.AddUnique(OutcomeTag.GetTagName());
                    AllCompiledQuestTags.AddUnique(FQuestStateTagUtils::MakeNodeOutcomeFact(TagName, OutcomeTag));
                }
            }
        }
        
        AllCompiledNodes.Add(TagName, Instance);
        AllCompiledEditorNodes.Add(TagName, ContentNode);
        OutNodeInstanceMap.Add(ContentNode, Instance);
    }
}

void FQuestlineGraphCompiler::CompileGroupSetters(UEdGraph* Graph, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, TArray<FName>& OutMonitorTags, TArray<FName>& OutGetterEntryTags)
{
    // ---- Prerequisite Rule Entries: compile each Entry's Enter-pin expression subtree into a runtime Monitor ----
	TMap<FGameplayTag, TArray<UQuestlineNode_PrerequisiteRuleEntry*>> PrereqEntriesByTag;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
	    if (UQuestlineNode_PrerequisiteRuleEntry* Entry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(Node))
	    {
	        if (!Entry->GroupTag.IsValid())
	        {
	            AddWarning(FString::Printf(TEXT("[%s] A Prerequisite Rule Entry has no rule tag set and will be skipped."), *TagPrefix), Entry);
	            continue;
	        }
	        PrereqEntriesByTag.FindOrAdd(Entry->GroupTag).Add(Entry);
	    }
	}

	for (auto& [RuleTag, Entries] : PrereqEntriesByTag)
	{
		// Duplicate-tag detection: one Entry per tag is the contract. Multiple Entries would create a silent race where only
		// the first-compiled definition takes effect. Emit a tokenized error with clickable navigation to each offending Entry.
		if (Entries.Num() > 1)
		{
			TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Error);
			Msg->AddToken(FTextToken::Create(FText::FromString(
				FString::Printf(TEXT("[%s] Prerequisite Rule tag '%s' is defined by %d Entries — rule names must be unique. Offending Entries:"),
					*TagPrefix, *RuleTag.GetTagName().ToString(), Entries.Num()))));

			for (UQuestlineNode_PrerequisiteRuleEntry* OffendingEntry : Entries)
			{
				AddNodeNavigationToken(Msg, OffendingEntry);
			}

			Messages.Add(Msg);
			bHasErrors = true;
			NumErrors++;
			UE_LOG(LogSimpleQuest, Error,
				TEXT("QuestlineGraphCompiler: Prerequisite Rule tag '%s' has %d Entries — rule names must be unique."),
				*RuleTag.GetTagName().ToString(), Entries.Num());

			continue;  // Skip Monitor creation for this tag — asset is in error state.
		}
		
	    UQuestPrereqRuleNode* Monitor = NewObject<UQuestPrereqRuleNode>(RootGraph);
	    Monitor->GroupTag = RuleTag;

	    UQuestlineNode_PrerequisiteRuleEntry* PrimaryEntry = Entries[0];
	    if (Entries.Num() > 1)
	    {
	        UE_LOG(LogSimpleQuest, Verbose,
	            TEXT("CompileGroupSetters: [%s] rule tag '%s' has %d Entries — using first; duplicate detection pass will error in 4.c."),
	            *TagPrefix, *RuleTag.GetTagName().ToString(), Entries.Num());
	    }

	    if (UEdGraphPin* EnterPin = PrimaryEntry->GetPinByRole(EQuestPinRole::PrereqIn))
	    {
	        if (EnterPin->LinkedTo.Num() > 0)
	        {
	            Monitor->Expression = CompilePrerequisiteExpression(EnterPin, TagPrefix, VisitedAssetPaths);
	        }
	    }

	    const FName RuleTagName = RuleTag.GetTagName();
	    AllCompiledNodes.Add(RuleTagName, Monitor);
	    OutMonitorTags.Add(RuleTagName);

	    TArray<FGameplayTag> LeafTags;
	    Monitor->Expression.CollectLeafTags(LeafTags);
	    UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGroupSetters: [%s] prereq rule '%s' — expression with %d leaf(s)"),
	        *TagPrefix, *RuleTagName.ToString(), LeafTags.Num());
	}

    // ---- Activation Group Entries: each Entry publishes a tag when its Activate input arrives; compile to runtime instance ----
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ActivationGroupEntry* Setter = Cast<UQuestlineNode_ActivationGroupEntry>(Node);
        if (!Setter) continue;

        if (!Setter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] An Activation Group Setter has no GroupTag set and will be skipped."), *TagPrefix), Setter);
            continue;
        }

        UActivationGroupExitNode* Inst = NewObject<UActivationGroupExitNode>(RootGraph);
        Inst->GroupTag = Setter->GroupTag;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s"), *Node->NodeGuid.ToString()));
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Inst);
        AllCompiledEditorNodes.Add(UtilKey, Node);

        UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGroupSetters: [%s] activation setter '%s'"),
            *TagPrefix, *Setter->GroupTag.GetTagName().ToString());
    }

    // ---- Activation Group Exits: source nodes — subscribe to the group tag, add to entry tags for graph-start activation ----
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ActivationGroupExit* Getter = Cast<UQuestlineNode_ActivationGroupExit>(Node);
        if (!Getter) continue;

        if (!Getter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] An Activation Group Getter has no GroupTag set and will be skipped."), *TagPrefix), Getter);
            continue;
        }

        UActivationGroupEntryNode* Inst = NewObject<UActivationGroupEntryNode>(RootGraph);
        Inst->GroupTag = Getter->GroupTag;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s"), *Node->NodeGuid.ToString()));
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Inst);
        AllCompiledEditorNodes.Add(UtilKey, Node);
        OutGetterEntryTags.Add(UtilKey);

        UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGroupSetters: [%s] activation getter '%s' (entry tag)"),
            *TagPrefix, *Getter->GroupTag.GetTagName().ToString());
    }
}

void FQuestlineGraphCompiler::CompileUtilityNodes(UEdGraph* Graph, TArray<UQuestlineNode_UtilityBase*>& OutUtilityEdNodes)
{
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_UtilityBase* UtilEdNode = Cast<UQuestlineNode_UtilityBase>(Node);
        if (!UtilEdNode) continue;

        UQuestNodeBase* Instance = nullptr;

        if (UQuestlineNode_SetBlocked* BlockNode = Cast<UQuestlineNode_SetBlocked>(UtilEdNode))
        {
            USetBlockedNode* Inst = NewObject<USetBlockedNode>(RootGraph);
            Inst->TargetQuestTags = BlockNode->TargetQuestTags;
            Instance = Inst;
        }
        else if (UQuestlineNode_ClearBlocked* ClearBlockNode = Cast<UQuestlineNode_ClearBlocked>(UtilEdNode))
        {
            UClearBlockedNode* Inst = NewObject<UClearBlockedNode>(RootGraph);
            Inst->TargetQuestTags = ClearBlockNode->TargetQuestTags;
            Instance = Inst;
        }

        if (!Instance) continue;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s"), *Node->NodeGuid.ToString()));
        OutUtilityEdNodes.Add(UtilEdNode);
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Instance);
        AllCompiledEditorNodes.Add(UtilKey, Node);
    }
}

void FQuestlineGraphCompiler::CompileOutputWiring(const TArray<UQuestlineNode_ContentBase*>& ContentNodes, const TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& NodeInstanceMap, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths)
{
    for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
    {
        if (Cast<UQuestlineNode_LinkedQuestline>(ContentNode)) continue;

        UQuestNodeBase* Instance = NodeInstanceMap.FindRef(ContentNode);
        if (!Instance) continue;

        Instance->NextNodesByOutcome.Empty();
        Instance->NextNodesOnAnyOutcome.Empty();
        Instance->NextNodesOnDeactivation.Empty();
        Instance->NextNodesToDeactivateOnDeactivation.Empty();

    	/**
		 * Source tag for this content node, reconstructed from the compile-time label formula. LinkedQuestlines
		 * are already `continue`d past at the top of this loop, so GetNodeTitle-based labeling is the right choice for
		 * everything that reaches here (Quest, Step, etc.).
		 */
    	const FName SourceTag = MakeNodeTagName(TagPrefix, SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
    	
        // Route each output pin into the correct runtime routing set
        for (UEdGraphPin* Pin : ContentNode->Pins)
        {
            if (Pin->Direction != EGPD_Output) continue;
            if (Pin->bOrphanedPin) continue; 

            // Deactivated pin: split routing by destination pin category
            if (Pin->PinType.PinCategory == TEXT("QuestDeactivated"))
            {
                TArray<FName> ActivateTags, DeactivateTags;
                ResolveDeactivatedPinToTags(Pin, TagPrefix, VisitedAssetPaths, ActivateTags, DeactivateTags);
                for (const FName& Tag : ActivateTags)  Instance->NextNodesOnDeactivation.Add(Tag);
                for (const FName& Tag : DeactivateTags) Instance->NextNodesToDeactivateOnDeactivation.Add(Tag);
                continue;
            }

        	TArray<FName> ResolvedTags;
        	TMap<FGameplayTag, TArray<TWeakObjectPtr<const UEdGraphNode>>> VisitedExitsByOutcome;
        	ResolvePinToTags(Pin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, ResolvedTags, &VisitedExitsByOutcome);

        	// Duplicate-Outcome-routing check: one outcome pin reaching multiple distinct Outcome terminals that share
        	// an OutcomeTag is almost always an authoring mistake. The compiler accepts the union of their destinations
        	// (each Exit's BoundaryTags are independently merged into ResolvedTags), but the authoring intent is
        	// ambiguous — each outcome should route through exactly one terminal.
        	for (const auto& Pair : VisitedExitsByOutcome)
        	{
        		if (Pair.Value.Num() > 1)
        		{
        			EmitDuplicateOutcomeRoutingWarning(ContentNode, Pin, Pair.Key, Pair.Value, TagPrefix);
        		}
        	}

        	if (ResolvedTags.IsEmpty()) continue;

            if (Pin->PinType.PinCategory == TEXT("QuestOutcome"))
            {
                const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(Pin->PinName, false);
                if (OutcomeTag.IsValid())
                {
	                FQuestOutcomeNodeList& List = Instance->NextNodesByOutcome.FindOrAdd(OutcomeTag);
                	for (const FName& Tag : ResolvedTags)
                	{
                		List.NodeTags.AddUnique(Tag);
                	}
                	// Record per-destination direct reach for (source, specific-outcome).
                	const FSourceOutcomeKey Key{ SourceTag, OutcomeTag };
                	for (const FName& Tag : ResolvedTags)
                	{
                		DirectReachesByDest.FindOrAdd(Tag).Add(Key);
                	}
                }
            }
			else if (UQuestlineNodeBase::GetPinRoleOf(Pin) == EQuestPinRole::AnyOutcomeOut)
            {
                for (const FName& Tag : ResolvedTags)
                {
	                Instance->NextNodesOnAnyOutcome.Add(Tag);
                }
            	// Surface D: record per-destination direct reach for (source, any-outcome). Invalid FGameplayTag encodes
            	// "any outcome from this source" — collision test absorbs specific-outcome keys from the same source.
            	const FSourceOutcomeKey Key{ SourceTag, FGameplayTag() };
            	for (const FName& Tag : ResolvedTags) DirectReachesByDest.FindOrAdd(Tag).Add(Key);
            }
        	
        }

        // Entry Deactivated pin: merge inner Entry node's deactivation routing into this Quest instance
        if (UQuestlineNode_Quest* QuestEdNode = Cast<UQuestlineNode_Quest>(ContentNode))
        {
            if (UEdGraph* InnerGraph = QuestEdNode->GetInnerGraph())
            {
                for (UEdGraphNode* InnerNode : InnerGraph->Nodes)
                {
                    if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(InnerNode))
                    {
						if (UEdGraphPin* DeactivatedPin = EntryNode->GetPinByRole(EQuestPinRole::DeactivatedOut))
                        {
                            if (!DeactivatedPin->bOrphanedPin && DeactivatedPin->LinkedTo.Num() > 0)
                            {
                                const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                                const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
                                TArray<FName> ActivateTags, DeactivateTags;
                                ResolveDeactivatedPinToTags(DeactivatedPin, InnerPrefix, VisitedAssetPaths, ActivateTags, DeactivateTags);
                                for (const FName& Tag : ActivateTags)  Instance->NextNodesOnDeactivation.Add(Tag);
                                for (const FName& Tag : DeactivateTags) Instance->NextNodesToDeactivateOnDeactivation.Add(Tag);
                            }
                        }
                        break;
                    }
                }
            }
        }
        
        // Mark nodes whose output chain reaches an exit — they complete their parent graph
        {
            auto CheckExit = [this](UEdGraphPin* Pin) -> bool
            {
                TSet<const UEdGraphNode*> V;
                return TraversalPolicy->HasDownstreamExit(Pin, V);
            };
            bool bCompletesParent = false;
            for (UEdGraphPin* Pin : ContentNode->Pins)
            {
                if (Pin->Direction == EGPD_Output && !Pin->bOrphanedPin && CheckExit(Pin))
                {
                    bCompletesParent = true;
                    break;
                }
            }
            Instance->bCompletesParentGraph = bCompletesParent;
        }
        
    	if (UEdGraphPin* PrereqPin = ContentNode->GetPinByRole(EQuestPinRole::PrereqIn))
        {
            if (PrereqPin->LinkedTo.Num() > 0)
            {
                Instance->PrerequisiteExpression = CompilePrerequisiteExpression(PrereqPin, TagPrefix, VisitedAssetPaths);
            }
        }
    }
}

TArray<FName> FQuestlineGraphCompiler::ResolveEntryTags(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TMap<FGameplayTag, FQuestEntryRouteList>* OutEntryTagsByOutcome)
{
	TArray<FName> EntryTags;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(Node);
		if (!EntryNode) continue;

		/**
		 * Non-outcome output pins (Entered sentinel, Deactivated) produce unconditional entry tags — no per-outcome or per-source
		 * routing, just "fire when this graph enters regardless of context."
		 */
		for (UEdGraphPin* Pin : Node->Pins)
		{
		    if (Pin->Direction != EGPD_Output) continue;
		    if (Pin->bOrphanedPin) continue;
		    if (Pin->PinType.PinCategory == TEXT("QuestOutcome")) continue;

		    TArray<FName> PinDests;
		    ResolvePinToTags(Pin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, PinDests);
		    EntryTags.Append(PinDests);

		    /**
		     * Surface D: the Entered pin represents "any parent source that activates this Entry's containing boundary." Semantically
		     * source-abstracted — symmetric to content-node AnyOutcome which is outcome-abstracted. Enumerate each parent source
		     * reaching this graph's boundary and record (sourceTag, outcomeTag) → destTag as a direct reach. ParallelPathKeysCollide
		     * handles AnyOutcome absorption on each enumerated source. Filter by VisitedAssetPaths so AR-scan results from outside
		     * the current compile tree (unrelated top-level assets that happen to link this graph) don't contaminate the analysis.
		     */
			if (UQuestlineNodeBase::GetPinRoleOf(Pin) == EQuestPinRole::AnyOutcomeOut && PinDests.Num() > 0)
		    {
		        TSet<FQuestEffectiveSource> ReachingSources;
		        FQuestlineGraphTraversalPolicy GraphTraversalPolicy;
		        GraphTraversalPolicy.CollectEntryReachingSources(Graph, ReachingSources);

		        for (const FQuestEffectiveSource& Source : ReachingSources)
		        {
		            if (!Source.Pin) continue;

		            const FString SourceAssetPath = Source.Asset ? Source.Asset->GetPathName() : FString();
		            if (!VisitedAssetPaths.Contains(SourceAssetPath)) continue;

		            const UQuestlineNode_ContentBase* SourceContent = Cast<UQuestlineNode_ContentBase>(Source.Pin->GetOwningNode());
		            if (!SourceContent) continue;

		            const FName SourceTag = ComputeCompiledTagForContentNode(SourceContent, Source.Asset);
		            if (SourceTag.IsNone()) continue;

		            FGameplayTag OutcomeTag;
		            if (Source.Pin->PinType.PinCategory == TEXT("QuestOutcome"))
		            {
		                OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(Source.Pin->PinName, false);
		            }
		            // QuestActivation "Any Outcome" from parent leaves OutcomeTag invalid — absorption handled by the collision test.

		            const FSourceOutcomeKey Key{ SourceTag, OutcomeTag };
		            for (const FName& DestTag : PinDests)
		            {
		                DirectReachesByDest.FindOrAdd(DestTag).Add(Key);
		            }
		        }
		    }
		}

		/**
		 * Per-spec routing for QuestOutcome pins. Iterate IncomingSignals directly — pin names are disambiguated and not
		 * parseable as gameplay tags. Each exposed spec produces one FQuestEntryDestination per resolved downstream tag, each
		 * tagged with the compiled QuestTag of the source content node as SourceFilter.
		 */
		if (OutEntryTagsByOutcome)
		{
			const UQuestlineGraph* ChildAsset = FQuestlineGraphTraversalPolicy::ResolveContainingAsset(Graph);
			for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
			{
				if (!Spec.bExposed) continue;
				if (!Spec.SourceNodeGuid.IsValid())
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry has unqualified incoming-signal spec (outcome '%s') — skipped. Re-run Import."),
						*TagPrefix,
						Spec.Outcome.IsValid() ? *Spec.Outcome.ToString() : TEXT("any")),
						EntryNode);
					continue;
				}

				const FName PinName = UQuestlineNode_Entry::BuildDisambiguatedPinName(Spec, EntryNode->IncomingSignals);
				UEdGraphPin* SpecPin = EntryNode->FindPin(PinName, EGPD_Output);
				if (!SpecPin)
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry spec (outcome '%s', source '%s') has no corresponding pin '%s' — skipped."),
						*TagPrefix,
						Spec.Outcome.IsValid() ? *Spec.Outcome.ToString() : TEXT("any"),
						*Spec.CachedSourceLabel,
						*PinName.ToString()),
						EntryNode);
					continue;
				}

				const FName SourceFilter = ResolveSourceFilterTag(Spec, ChildAsset);
				if (SourceFilter.IsNone())
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry spec (outcome '%s', source GUID %s) has unresolvable source — skipped. Re-run Import to refresh, or verify the parent asset is accessible."),
						*TagPrefix,
						Spec.Outcome.IsValid() ? *Spec.Outcome.ToString() : TEXT("any"),
						*Spec.SourceNodeGuid.ToString()),
						EntryNode);
					continue;
				}

				TArray<FName> DestTags;
				ResolvePinToTags(SpecPin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, DestTags);

				/**
				 * Bucket key: specific outcome for specific specs, FGameplayTag() (invalid) for any-outcome specs. The runtime looks
				 * up both the specific bucket (for matching IncomingOutcomeTag) and the invalid bucket (for source-only matches)
				 * when activating entry destinations.
				 */
				FQuestEntryRouteList& RouteList = OutEntryTagsByOutcome->FindOrAdd(Spec.Outcome);
				for (const FName& DestTag : DestTags)
				{
					FQuestEntryDestination Dest;
					Dest.DestTag = DestTag;
					Dest.SourceFilter = SourceFilter;
					RouteList.Destinations.Add(Dest);
					
					/**
					 * Entry source-qualified routing is a direct signal flow at runtime — the compiled source tag (SourceFilter)
					 * delivers Spec.Outcome to DestTag without any group dispatch. Record alongside content-outcome-pin direct reaches so
					 * cross-asset parallel-path collisions are detectable at analysis time. Spec.Outcome may be invalid for any-outcome-
					 * from-source specs; the collision test absorbs that via ParallelPathKeysCollide.
					 */
					DirectReachesByDest.FindOrAdd(DestTag).Add(FSourceOutcomeKey{ SourceFilter, Spec.Outcome });
				}
			}
		}

		break;
	}
	return EntryTags;
}

void FQuestlineGraphCompiler::DetectAndRecordTagRenames(UQuestlineGraph* InGraph, const TMap<FGuid, FName>& OldTagsByGuid)
{
    for (const auto& [TagName, NodeInstance] : InGraph->CompiledNodes)
    {
        if (!NodeInstance || !NodeInstance->GetQuestGuid().IsValid()) continue;
        if (const FName* OldTag = OldTagsByGuid.Find(NodeInstance->GetQuestGuid()))
        {
            if (*OldTag != TagName)
            {
                DetectedTagRenames.Add(*OldTag, TagName);
            }
        }
    }

    if (DetectedTagRenames.Num() == 0) return;

    // Chain-collapse the persistent ledger
    for (FQuestTagRename& Existing : InGraph->PendingTagRenames)
    {
        if (const FName* ChainedNew = DetectedTagRenames.Find(Existing.NewTag))
        {
            Existing.NewTag = *ChainedNew;
        }
    }

    // Add new entries not already covered by chain collapse
    TSet<FName> ExistingOldTags;
    for (const FQuestTagRename& Existing : InGraph->PendingTagRenames)
    {
        ExistingOldTags.Add(Existing.OldTag);
    }
    for (const auto& [OldTag, NewTag] : DetectedTagRenames)
    {
        if (!ExistingOldTags.Contains(OldTag))
        {
            InGraph->PendingTagRenames.Add({ OldTag, NewTag });
        }
    }

    // Prune identity entries (rename then rename back)
    InGraph->PendingTagRenames.RemoveAll([](const FQuestTagRename& E)
    {
        return E.OldTag == E.NewTag;
    });
	UE_LOG(LogSimpleQuest, Display, TEXT("Compiler: %d tag rename(s) detected, ledger: %d pending"),
		DetectedTagRenames.Num(), InGraph->PendingTagRenames.Num());

	/**
	 * Per-rename detail — walks the new CompiledNodes to recover the GUID and DisplayName of each renamed node so the
	 * Output Log identifies exactly which node is drifting. Intended for diagnosing stale or persistent renames where
	 * the same tag flips every compile without a designer-visible reason.
	 */
	for (const auto& [OldTag, NewTag] : DetectedTagRenames)
	{
		FGuid OffendingGuid;
		FText OffendingDisplayName;
		if (TObjectPtr<UQuestNodeBase>* Found = InGraph->CompiledNodes.Find(NewTag))
		{
			if (UQuestNodeBase* Node = *Found)
			{
				OffendingGuid = Node->GetQuestGuid();
				OffendingDisplayName = Node->GetNodeInfo().DisplayName;
			}
		}
		UE_LOG(LogSimpleQuest, Display, TEXT("  rename: '%s' -> '%s' (node '%s', GUID %s)"),
			*OldTag.ToString(),
			*NewTag.ToString(),
			*OffendingDisplayName.ToString(),
			*OffendingGuid.ToString(EGuidFormats::Digits));
	}
}


// -------------------------------------------------------------------------------------------------
// ResolvePinToTags - the node traversal engine
// -------------------------------------------------------------------------------------------------

void FQuestlineGraphCompiler::ResolvePinToTags(UEdGraphPin* FromPin, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TArray<FName>& OutTags, TMap<FGameplayTag, TArray<TWeakObjectPtr<const UEdGraphNode>>>* OutVisitedExitsByOutcome)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();

        // Knot: pass through to the other side
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
            {
                ResolvePinToTags(KnotOut, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, OutTags, OutVisitedExitsByOutcome);
            }
        }

        // Exit nodes: inject boundary tags for the parent graph so a child graph knows what its exits connect to on the parent level
        // Passed to children when compilation recurses into the child graph.
        else if (const UQuestlineNode_Exit* ExitNode = Cast<UQuestlineNode_Exit>(Node))
        {
            // Record this Exit visit for duplicate-Outcome detection in the caller (when requested). AddUnique on the node
            // pointer dedupes multiple-path reaches of the same Exit — only distinct Exit nodes sharing an OutcomeTag
            // constitute a duplicate-routing case.
            if (OutVisitedExitsByOutcome && ExitNode->OutcomeTag.IsValid())
            {
                OutVisitedExitsByOutcome->FindOrAdd(ExitNode->OutcomeTag).AddUnique(ExitNode);
            }

            if (const TArray<FName>* BoundaryTags = BoundaryTagsByOutcome.Find(ExitNode->OutcomeTag))
            {
                for (const FName& Tag : *BoundaryTags) OutTags.AddUnique(Tag);
            }
            // Fall back to Any Outcome boundary (stored under invalid tag by linked node handler)
            else if (const TArray<FName>* AnyBoundaryTags = BoundaryTagsByOutcome.Find(FGameplayTag()))
            {
                for (const FName& Tag : *AnyBoundaryTags) OutTags.AddUnique(Tag);
            }
            else if (!ExitNode->OutcomeTag.IsValid())
            {
                AddWarning(FString::Printf(TEXT("[%s] An exit node has no OutcomeTag set."), *TagPrefix), ExitNode);
            }
        }

        // Quest or Step: return the tag assigned during Pass 1
        else if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
            // Only resolve forward chain when wired to an Activate input. Prerequisite and Deactivate inputs are compiled
            // by their own dedicated passes.
            if (LinkedPin->PinType.PinCategory != TEXT("QuestActivation"))
                continue;

            const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if (!Label.IsEmpty())
            {
                const FName TagName = MakeNodeTagName(TagPrefix, Label);
                if (!TagName.IsNone())
                {
                    OutTags.AddUnique(TagName);
                }
            }
        }
        
        // Utility node: return its GUID-based key so the caller can route into NextNodesOnForward
        else if (const FName* UtilKey = UtilityNodeKeyMap.Find(Node))
        {
            OutTags.AddUnique(*UtilKey);
        }
    }
}


// -------------------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------------------

FString FQuestlineGraphCompiler::SanitizeTagSegment(const FString& InLabel) const
{
    return FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(InLabel);
}

FName FQuestlineGraphCompiler::MakeNodeTagName(const FString& TagPrefix, const FString& SanitizedLabel) const
{
    return FName(*FString::Printf(TEXT("Quest.%s.%s"), *TagPrefix, *SanitizedLabel));
}

void FQuestlineGraphCompiler::AddError(const FString& Message, const UEdGraphNode* Node)
{
    bHasErrors = true;
    NumErrors++;
    TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Error, FText::FromString(Message));
    if (Node) AddNodeNavigationToken(Msg, Node);
    Messages.Add(Msg);
    UE_LOG(LogSimpleQuest, Error, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::AddWarning(const FString& Message, const UEdGraphNode* Node)
{
    NumWarnings++;
    TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning, FText::FromString(Message));
    if (Node) AddNodeNavigationToken(Msg, Node);
    Messages.Add(Msg);
    UE_LOG(LogSimpleQuest, Warning, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::RegisterCompiledTags(UQuestlineGraph* InGraph)
{
    ISimpleQuestEditorModule::Get().RegisterCompiledTags(
        InGraph->GetPackage()->GetName(),
        InGraph->CompiledQuestTags);
}

void FQuestlineGraphCompiler::CollectTransitiveParentSources(UEdGraph* InGraph, const TArray<FString>& VisitedAssetPaths, TSet<FSourceOutcomeKey>& OutKeys,	TSet<UEdGraph*>& VisitedGraphs)
{
	if (!InGraph || VisitedGraphs.Contains(InGraph)) return;
	VisitedGraphs.Add(InGraph);

	TSet<FQuestEffectiveSource> LocalSources;
	TraversalPolicy->CollectEntryReachingSources(InGraph, LocalSources);

	for (const FQuestEffectiveSource& Source : LocalSources)
	{
		if (!Source.Pin) continue;

		const FString SourceAssetPath = Source.Asset ? Source.Asset->GetPathName() : FString();
		if (!VisitedAssetPaths.Contains(SourceAssetPath)) continue;

		UEdGraphNode* SourceNode = Source.Pin->GetOwningNode();

		/**
		 * Case A: source is a content-node outcome pin (or Any Outcome). Concrete terminal — record the compiled source tag
		 * and outcome, stop walking this branch.
		 */
		if (const UQuestlineNode_ContentBase* SourceContent = Cast<UQuestlineNode_ContentBase>(SourceNode))
		{
			const FName SourceTag = ComputeCompiledTagForContentNode(SourceContent, Source.Asset);
			if (SourceTag.IsNone()) continue;

			FGameplayTag OutcomeTag;
			if (Source.Pin->PinType.PinCategory == TEXT("QuestOutcome"))
			{
				OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(Source.Pin->PinName, false);
			}
			// QuestActivation "Any Outcome" from parent leaves OutcomeTag invalid — absorption handles it.
			OutKeys.Add(FSourceOutcomeKey{ SourceTag, OutcomeTag });
			continue;
		}

		/**
		 * Case B: source is an Entry pin — transitive continuation.
		 */
		if (const UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(SourceNode))
		{
			// B1: Entered sentinel — climb to this Entry's graph and gather its parent sources recursively.
			if (UQuestlineNodeBase::GetPinRoleOf(Source.Pin) == EQuestPinRole::AnyOutcomeOut)
			{
				CollectTransitiveParentSources(EntryNode->GetGraph(), VisitedAssetPaths, OutKeys, VisitedGraphs);
				continue;
			}

			/**
			 * B2: Source-qualified spec pin. The spec's SourceNodeGuid already encodes the original content source (that's
			 * the whole point of specs), so no recursion needed — resolve the source tag directly and record. Match the pin
			 * to its spec by recomputing the disambiguated pin name.
			 */
			if (Source.Pin->PinType.PinCategory == TEXT("QuestOutcome"))
			{
				for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
				{
					if (!Spec.bExposed) continue;
					if (UQuestlineNode_Entry::BuildDisambiguatedPinName(Spec, EntryNode->IncomingSignals) != Source.Pin->PinName) continue;

					const FName SourceTag = ResolveSourceFilterTag(Spec, Source.Asset);
					if (SourceTag.IsNone()) continue;

					OutKeys.Add(FSourceOutcomeKey{ SourceTag, Spec.Outcome });
					break;
				}
				continue;
			}
		}

		// Utility/prereq/other nodes as Entry-reaching sources shouldn't occur; if they do, we ignore them.
	}
}

FName FQuestlineGraphCompiler::ComputeCompiledTagForContentNode(const UQuestlineNode_ContentBase* SourceNode, const UQuestlineGraph* ContainingAsset) const
{
	if (!SourceNode || !ContainingAsset) return NAME_None;

	/**
	 * Walk up the Outer chain collecting sanitized labels. A content node either lives directly in the top-level asset graph
	 * or is nested inside one or more Quest node inner graphs. Each level contributes its label to the compiled tag path.
	 */
	TArray<FString> LabelsTopDown;
	const UEdGraphNode* Cursor = SourceNode;
	while (Cursor)
	{
		const UQuestlineNode_ContentBase* CursorContent = Cast<UQuestlineNode_ContentBase>(Cursor);
		if (!CursorContent) break;
		LabelsTopDown.Insert(SanitizeTagSegment(CursorContent->NodeLabel.ToString()), 0);

		const UEdGraph* Graph = Cursor->GetGraph();
		if (!Graph) break;
		UObject* Outer = Graph->GetOuter();
		if (const UQuestlineNode_Quest* ContainerQuest = Cast<UQuestlineNode_Quest>(Outer))
		{
			Cursor = ContainerQuest;
			continue;
		}
		break; // Reached the top-level asset graph.
	}

	if (LabelsTopDown.IsEmpty()) return NAME_None;

	const FString AssetPrefix = SanitizeTagSegment(ContainingAsset->GetQuestlineID().IsEmpty() ? ContainingAsset->GetName() : ContainingAsset->GetQuestlineID());
	FString FullPath = TEXT("Quest.") + AssetPrefix;
	for (const FString& Segment : LabelsTopDown) FullPath += TEXT(".") + Segment;
	return FName(*FullPath);
}

FName FQuestlineGraphCompiler::ResolveSourceFilterTag(const FIncomingSignalPinSpec& Spec, const UQuestlineGraph* ChildAsset) const
{
	if (!Spec.SourceNodeGuid.IsValid()) return NAME_None;

	/**
	 * Determine which asset contains the source. Same-asset (empty ParentAsset) uses ChildAsset; cross-asset sync-loads the
	 * referenced asset. Then recursively search all graphs in that asset for a content node with matching QuestGuid.
	 */
	UQuestlineGraph* SourceAsset = nullptr;
	if (Spec.ParentAsset.IsNull())
	{
		SourceAsset = const_cast<UQuestlineGraph*>(ChildAsset);
	}
	else
	{
		SourceAsset = Cast<UQuestlineGraph>(Spec.ParentAsset.TryLoad());
	}
	if (!SourceAsset || !SourceAsset->QuestlineEdGraph) return NAME_None;

	TFunction<const UQuestlineNode_ContentBase*(const UEdGraph*)> FindByGuid;
	FindByGuid = [&FindByGuid, &Spec](const UEdGraph* Graph) -> const UQuestlineNode_ContentBase*
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
			{
				if (ContentNode->QuestGuid == Spec.SourceNodeGuid) return ContentNode;
			}
			if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
			{
				if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
				{
					if (const UQuestlineNode_ContentBase* Found = FindByGuid(InnerGraph)) return Found;
				}
			}
		}
		return nullptr;
	};

	const UQuestlineNode_ContentBase* SourceNode = FindByGuid(SourceAsset->QuestlineEdGraph);
	if (!SourceNode) return NAME_None;

	return ComputeCompiledTagForContentNode(SourceNode, SourceAsset);
}

int32 FQuestlineGraphCompiler::CompilePrerequisiteFromOutputPin(UEdGraphPin* OutputPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, FPrerequisiteExpression& OutExpression)
{
    if (!OutputPin) return INDEX_NONE;
    UEdGraphNode* Node = OutputPin->GetOwningNode();

    if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
    {
        UEdGraphPin* KnotIn = Knot->FindPin(TEXT("KnotIn"), EGPD_Input);
        if (KnotIn && KnotIn->LinkedTo.Num() > 0)
        {
            return CompilePrerequisiteFromOutputPin(KnotIn->LinkedTo[0], TagPrefix, VisitedAssetPaths, OutExpression);
        }
        return INDEX_NONE;
    }
    
    // AND
    if (Cast<UQuestlineNode_PrerequisiteAnd>(Node))
    {
        return CompileCombinatorNode(EPrerequisiteExpressionType::And, Node, TagPrefix, VisitedAssetPaths, OutExpression);
    }

    // OR
    if (Cast<UQuestlineNode_PrerequisiteOr>(Node))
    {
        return CompileCombinatorNode(EPrerequisiteExpressionType::Or, Node, TagPrefix, VisitedAssetPaths, OutExpression);
    }
    
    // NOT
    if (UQuestlineNode_PrerequisiteNot* NotNode = Cast<UQuestlineNode_PrerequisiteNot>(Node))
    {
        FPrerequisiteExpressionNode ExprNode;
        ExprNode.Type = EPrerequisiteExpressionType::Not;
        const int32 NodeIndex = OutExpression.Nodes.Add(ExprNode);

		if (UEdGraphPin* CondPin = NotNode->GetPinByRole(EQuestPinRole::PrereqIn))
        {
            if (CondPin->LinkedTo.Num() > 0)
            {
                const int32 ChildIndex = CompilePrerequisiteFromOutputPin(CondPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, OutExpression);
                if (ChildIndex != INDEX_NONE)
                {
                    OutExpression.Nodes[NodeIndex].ChildIndices.Add(ChildIndex);
                }
            }
        }
        return NodeIndex;
    }

    // Getter: resolves to a Leaf on the group's Satisfied tag
    if (UQuestlineNode_PrerequisiteRuleExit* Getter = Cast<UQuestlineNode_PrerequisiteRuleExit>(Node))
    {
        if (!Getter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] A Prereq Group Getter has no GroupTag set and will be skipped."), *TagPrefix), Getter);
            return INDEX_NONE;
        }
        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = Getter->GroupTag;
        return OutExpression.Nodes.Add(LeafNode);
    }

	// Rule Entry Forward: direct-eval. Inline the Enter pin's linked expression subtree so a local Forward
	// consumer avoids the WorldState roundtrip that a cross-graph Exit would use. Behaviorally equivalent
	// to reading the rule's published tag, but evaluated directly without waiting on the Monitor's publish.
	if (UQuestlineNode_PrerequisiteRuleEntry* Entry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(Node))
	{
		if (!Entry->GroupTag.IsValid())
		{
			AddWarning(FString::Printf(TEXT("[%s] A Prerequisite Rule Entry has no rule tag set and will be skipped."), *TagPrefix), Entry);
			return INDEX_NONE;
		}

		if (UEdGraphPin* EnterPin = Entry->GetPinByRole(EQuestPinRole::PrereqIn))
		{
			if (EnterPin->LinkedTo.Num() > 0)
			{
				return CompilePrerequisiteFromOutputPin(EnterPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, OutExpression);
			}
		}

		// No wired expression on Enter — fall back to the tag-read leaf. Same expression a cross-graph Exit
		// compiles to; evaluates false at runtime unless the Monitor has somehow published the tag anyway.
		FPrerequisiteExpressionNode LeafNode;
		LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
		LeafNode.LeafTag = Entry->GroupTag;
		return OutExpression.Nodes.Add(LeafNode);
	}
    
    // Entry node: outcome pin → leaf checking entry outcome fact; "Any Outcome" → parent quest Active fact
    if (Cast<UQuestlineNode_Entry>(Node))
    {
        const FName QuestTagName = FName(*(TEXT("Quest.") + TagPrefix));

		if (UQuestlineNodeBase::GetPinRoleOf(OutputPin) == EQuestPinRole::AnyOutcomeOut)
        {
            // The parent quest's Active fact is always set when the inner graph is running
            FPrerequisiteExpressionNode LeafNode;
            LeafNode.Type = EPrerequisiteExpressionType::Leaf;
            LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTagName, FQuestStateTagUtils::Leaf_Active), false);
            return OutExpression.Nodes.Add(LeafNode);
        }

        const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutputPin->PinName, false);
        if (!OutcomeTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] Entry outcome pin '%s' does not resolve to a valid gameplay tag — prerequisite skipped."),
                                       *TagPrefix, *OutputPin->PinName.ToString()), Node);
            return INDEX_NONE;
        }

        // Warn if used in a top-level graph — entry outcome facts are only written when a Quest node receives an IncomingOutcomeTag
        UObject* GraphOuter = Node->GetGraph() ? Node->GetGraph()->GetOuter() : nullptr;
        if (!Cast<UQuestlineNode_Quest>(GraphOuter))
        {
            AddWarning(FString::Printf(TEXT("[%s] Entry outcome '%s' used as prerequisite in a top-level graph — this fact is only set when a parent Quest node is activated with a matching outcome."),
                                       *TagPrefix, *OutcomeTag.ToString()), Node);
        }

        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeEntryOutcomeFact(QuestTagName, OutcomeTag), false);
        return OutExpression.Nodes.Add(LeafNode);
    }

    
    // Content node: Success/Failure becomes single Leaf; Any Outcome builds OR(Succeeded, Failed)
    if (Cast<UQuestlineNode_ContentBase>(Node))
    {
		if (UQuestlineNodeBase::GetPinRoleOf(OutputPin) == EQuestPinRole::AnyOutcomeOut)
        {
            const UQuestlineNode_ContentBase* CN = Cast<UQuestlineNode_ContentBase>(Node);
            const FString Label = SanitizeTagSegment(CN->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            const FName NodeTagName = MakeNodeTagName(TagPrefix, Label);

            // Collect all named QuestOutcome pins on this node
            TArray<UEdGraphPin*> OutcomePins;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("QuestOutcome"))
                    OutcomePins.Add(Pin);
            }

            // No named outcomes — this node is satisfied by Leaf_Completed alone
            if (OutcomePins.IsEmpty())
            {
                FPrerequisiteExpressionNode LeafNode;
                LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
                LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(
                    FQuestStateTagUtils::MakeStateFact(NodeTagName, FQuestStateTagUtils::Leaf_Completed), false);
                return OutExpression.Nodes.Add(LeafNode);
            }

            // Build OR over all named outcome facts
            FPrerequisiteExpressionNode OrNode;
            OrNode.Type = EPrerequisiteExpressionType::Or;
            const int32 OrIndex = OutExpression.Nodes.Add(OrNode);

            for (UEdGraphPin* OutcomePin : OutcomePins)
            {
                const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutcomePin->PinName, false);
                if (!OutcomeTag.IsValid()) continue;

            	FPrerequisiteExpressionNode LeafNode;
            	LeafNode.Type = EPrerequisiteExpressionType::Leaf;
            	LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeNodeOutcomeFact(NodeTagName, OutcomeTag), false);
            	// Sequence the Add-to-Nodes (which may reallocate the TArray) BEFORE indexing back into Nodes —
            	// otherwise OutExpression.Nodes[OrIndex].ChildIndices holds a dangling reference when a grow happens.
            	const int32 LeafIdx = OutExpression.Nodes.Add(LeafNode);
            	OutExpression.Nodes[OrIndex].ChildIndices.Add(LeafIdx);
            }

            return OrIndex;
        }

        const FName FactTagName = ResolveOutputPinToStateFact(OutputPin, TagPrefix);
        if (FactTagName.IsNone()) return INDEX_NONE;

        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(FactTagName, false);
        return OutExpression.Nodes.Add(LeafNode);
    }

    return INDEX_NONE;
}

int32 FQuestlineGraphCompiler::CompileCombinatorNode(EPrerequisiteExpressionType Type, UEdGraphNode* Node, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, FPrerequisiteExpression& OutExpression)
{
    FPrerequisiteExpressionNode ExprNode;
    ExprNode.Type = Type;
    const int32 NodeIndex = OutExpression.Nodes.Add(ExprNode);

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction != EGPD_Input) continue;
        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            const int32 ChildIndex = CompilePrerequisiteFromOutputPin(LinkedPin, TagPrefix, VisitedAssetPaths, OutExpression);
            if (ChildIndex != INDEX_NONE)
            {
                OutExpression.Nodes[NodeIndex].ChildIndices.Add(ChildIndex);
            }
        }
    }
    return NodeIndex;
}

FGuid FQuestlineGraphCompiler::CombineGuids(const FGuid& Outer, const FGuid& Inner)
{
	if (!Outer.IsValid()) return Inner;
	return FGuid(
		HashCombine(Outer.A, Inner.A),
		HashCombine(Outer.B, Inner.B),
		HashCombine(Outer.C, Inner.C),
		HashCombine(Outer.D, Inner.D));
}

FPrerequisiteExpression FQuestlineGraphCompiler::CompilePrerequisiteExpression(UEdGraphPin* PrerequisiteInputPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths)
{
    FPrerequisiteExpression Expression;
    if (!PrerequisiteInputPin || PrerequisiteInputPin->LinkedTo.IsEmpty()) return Expression;

    // Schema enforces exactly one wire into any QuestPrerequisite input pin
    const int32 RootIndex = CompilePrerequisiteFromOutputPin(PrerequisiteInputPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, Expression);

    if (RootIndex == INDEX_NONE)
    {
        Expression.Nodes.Reset(); // unresolvable — fall back to Always
    }
    else
    {
        Expression.RootIndex = RootIndex;
    }

    return Expression;
}

FName FQuestlineGraphCompiler::ResolveOutputPinToStateFact(
    UEdGraphPin* OutputPin, const FString& TagPrefix) const
{
    const UQuestlineNode_ContentBase* ContentNode = Cast<const UQuestlineNode_ContentBase>(OutputPin->GetOwningNode());
    if (!ContentNode) return NAME_None;

    const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    if (Label.IsEmpty()) return NAME_None;

    const FName NodeTagName = MakeNodeTagName(TagPrefix, Label);
    const FName PinName = OutputPin->PinName;
    
    if (OutputPin->PinType.PinCategory == TEXT("QuestOutcome"))
    {
        const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(PinName, false);
        if (OutcomeTag.IsValid())
        {
            return FQuestStateTagUtils::MakeNodeOutcomeFact(NodeTagName, OutcomeTag);
        }
    }
    return NAME_None; // Any Outcome or Abandon — caller handles these
}

void FQuestlineGraphCompiler::ResolveDeactivatedPinToTags(
    UEdGraphPin* FromPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths,
    TArray<FName>& OutActivateTags, TArray<FName>& OutDeactivateTags)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();

        // Knot: pass through; the output side carries the category context to each destination
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
            {
                ResolveDeactivatedPinToTags(KnotOut, TagPrefix, VisitedAssetPaths, OutActivateTags, OutDeactivateTags);
            }
            continue;
        }

        // Content node: classify by which input pin was connected
        if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
            const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if (Label.IsEmpty()) continue;
            const FName TagName = MakeNodeTagName(TagPrefix, Label);
            if (TagName.IsNone()) continue;

            if (LinkedPin->PinType.PinCategory == TEXT("QuestActivation"))
            {
                // Deactivated to Activate: activate this node when the source deactivates
                OutActivateTags.AddUnique(TagName);
            }
            else if (LinkedPin->PinType.PinCategory == TEXT("QuestDeactivate"))
            {
                // Deactivated to Deactivate: cascade deactivation to this node
                OutDeactivateTags.AddUnique(TagName);
            }
            continue;
        }

        // Utility node: can only receive Activate, so always goes to OutActivateTags
        if (const FName* UtilKey = UtilityNodeKeyMap.Find(Node))
        {
            OutActivateTags.AddUnique(*UtilKey);
        }
    }
}

void FQuestlineGraphCompiler::AddNodeNavigationToken(TSharedRef<FTokenizedMessage>& Msg, const UEdGraphNode* Node)
{
    TWeakObjectPtr<UEdGraphNode> WeakNode = const_cast<UEdGraphNode*>(Node);

    Msg->AddToken(FActionToken::Create(
        FText::FromString(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()),
        NSLOCTEXT("SimpleQuestEditor", "GoToNode", "Navigate to this node in the graph editor"),
        FOnActionTokenExecuted::CreateLambda([WeakNode]()
        {
            UEdGraphNode* PinnedNode = WeakNode.Get();
            if (!PinnedNode || !PinnedNode->GetGraph()) return;

            // Walk outer chain to find the UQuestlineGraph asset
            UQuestlineGraph* QuestlineGraph = nullptr;
            for (UObject* Outer = PinnedNode->GetGraph(); Outer; Outer = Outer->GetOuter())
            {
                QuestlineGraph = Cast<UQuestlineGraph>(Outer);
                if (QuestlineGraph) break;
            }
            if (!QuestlineGraph || !GEditor) return;

            // Open the asset editor and navigate to the node
            UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            EditorSubsystem->OpenEditorForAsset(QuestlineGraph);

            if (IAssetEditorInstance* EditorInstance = EditorSubsystem->FindEditorForAsset(QuestlineGraph, false))
            {
                static_cast<FQuestlineGraphEditor*>(EditorInstance)->NavigateToLocation(PinnedNode->GetGraph(), PinnedNode);
            }
        })
    ));
}

bool FQuestlineGraphCompiler::ParallelPathKeysCollide(const FSourceOutcomeKey& A, const FSourceOutcomeKey& B)
{
	if (A.SourceTag != B.SourceTag) return false;
	// AnyOutcome (invalid) on either side absorbs the specific outcome on the other.
	if (!A.Outcome.IsValid() || !B.Outcome.IsValid()) return true;
	return A.Outcome == B.Outcome;
}

void FQuestlineGraphCompiler::EmitParallelPathWarnings()
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("Surface D: %d setter group(s), %d getter group(s), %d direct-reach destination(s)"),
		GroupSetterSourcesByTag.Num(), GroupGetterDestsByTag.Num(), DirectReachesByDest.Num());

	/**
	 * Cross-reference pass. For each group tag that has both setters and getters: for every destination the getters reach,
	 * check if any direct-reach source at that destination collides with any setter source for the group (under AnyOutcome
	 * absorption). Each collision emits one tokenized warning pointing at source, destination, setter, and getter.
	 */
	for (const auto& [GroupTag, GetterDests] : GroupGetterDestsByTag)
	{
		const TSet<FSourceOutcomeKey>* SetterSources = GroupSetterSourcesByTag.Find(GroupTag);
		if (!SetterSources || SetterSources->IsEmpty()) continue;

		for (const FName& DestTag : GetterDests)
		{
			const TSet<FSourceOutcomeKey>* DirectSources = DirectReachesByDest.Find(DestTag);
			if (!DirectSources || DirectSources->IsEmpty()) continue;

			for (const FSourceOutcomeKey& SetterSource : *SetterSources)
			{
				for (const FSourceOutcomeKey& DirectSource : *DirectSources)
				{
					if (!ParallelPathKeysCollide(SetterSource, DirectSource)) continue;
					EmitParallelPathCollisionWarning(GroupTag, SetterSource, DirectSource, DestTag);
				}
			}
		}
	}
}

void FQuestlineGraphCompiler::EmitDuplicateOutcomeRoutingWarning(const UEdGraphNode* SourceNode, const UEdGraphPin* SourcePin,
	const FGameplayTag& DuplicatedOutcomeTag, const TArray<TWeakObjectPtr<const UEdGraphNode>>& DuplicateExits, const FString& TagPrefix)
{
	TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);

	const FString PinDisplay = SourcePin ? SourcePin->PinName.ToString() : TEXT("<unknown pin>");
	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(
		TEXT("[%s] Output pin '%s' on"), *TagPrefix, *PinDisplay))));

	if (SourceNode) AddNodeNavigationToken(Msg, SourceNode);
	else Msg->AddToken(FTextToken::Create(FText::FromString(TEXT("<unknown source>"))));

	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(
		TEXT("reaches %d Outcome terminals sharing tag '%s':"), DuplicateExits.Num(), *DuplicatedOutcomeTag.ToString()))));

	for (const TWeakObjectPtr<const UEdGraphNode>& WeakExit : DuplicateExits)
	{
		if (const UEdGraphNode* ExitNode = WeakExit.Get())
		{
			AddNodeNavigationToken(Msg, ExitNode);
		}
	}

	Msg->AddToken(FTextToken::Create(FText::FromString(
		TEXT("(Ambiguous authoring: route each distinct outcome through a single terminal, or change the terminals' tags to be distinct.)"))));

	Messages.Add(Msg);
	NumWarnings++;

	UE_LOG(LogSimpleQuest, Warning, TEXT("Duplicate outcome routing: pin '%s' on '%s' reaches %d terminals tagged '%s'"),
		*PinDisplay,
		SourceNode ? *SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("<unknown>"),
		DuplicateExits.Num(),
		*DuplicatedOutcomeTag.ToString());
}

void FQuestlineGraphCompiler::EmitParallelPathCollisionWarning(const FGameplayTag& GroupTag, const FSourceOutcomeKey& SetterSource, const FSourceOutcomeKey& DirectSource, const FName& DestTag)
{
	/**
	 * Resolve editor-node refs for the navigation tokens. Source and destination come from the compile-tree-wide editor-node
	 * map keyed by compiled tag. Setter and getter come from the per-group editor-node maps populated during collection. If
	 * any ref is missing, the corresponding slot falls back to a plain-text token showing the tag/name so the message is
	 * still readable and informative.
	 */
	UEdGraphNode* SourceEdNode = AllCompiledEditorNodes.FindRef(SetterSource.SourceTag);
	UEdGraphNode* DestEdNode = AllCompiledEditorNodes.FindRef(DestTag);

	// Specific setter that contributed SetterSource to this group (not just any setter with the tag).
	UEdGraphNode* SetterEdNode = nullptr;
	if (const TMap<FSourceOutcomeKey, UEdGraphNode*>* Inner = SetterEdNodeByGroupAndSource.Find(GroupTag))
	{
		SetterEdNode = Inner->FindRef(SetterSource);
	}

	// Specific getter that reaches this destination via this group (not just any getter with the tag).
	UEdGraphNode* GetterEdNode = nullptr;
	if (const TMap<FName, UEdGraphNode*>* Inner = GetterEdNodeByGroupAndDest.Find(GroupTag))
	{
		GetterEdNode = Inner->FindRef(DestTag);
	}

	// Prefer the specific outcome when either side has it; fall back to "any outcome" for the AnyOutcome-absorption case.
	const FString OutcomeStr = DirectSource.Outcome.IsValid()
		? DirectSource.Outcome.ToString()
		: (SetterSource.Outcome.IsValid() ? SetterSource.Outcome.ToString() : TEXT("any outcome"));

	auto NodeTokenOrText = [this](UEdGraphNode* Node, const FString& Fallback, TSharedRef<FTokenizedMessage>& InMsg)
	{
		if (Node) AddNodeNavigationToken(InMsg, Node);
		else InMsg->AddToken(FTextToken::Create(FText::FromString(Fallback)));
	};

	TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);
	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Parallel path: outcome '%s' on"), *OutcomeStr))));
	NodeTokenOrText(SourceEdNode, SetterSource.SourceTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(TEXT("reaches"))));
	NodeTokenOrText(DestEdNode, DestTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("both directly and via activation group '%s' (set by"), *GroupTag.ToString()))));
	NodeTokenOrText(SetterEdNode, GroupTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(TEXT(", received by"))));
	NodeTokenOrText(GetterEdNode, GroupTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(TEXT("). Consider removing one path."))));

	Messages.Add(Msg);
	NumWarnings++;

	UE_LOG(LogSimpleQuest, Warning,
		TEXT("Surface D parallel path: outcome '%s' on '%s' reaches '%s' both directly and via group '%s'"),
		*OutcomeStr, *SetterSource.SourceTag.ToString(), *DestTag.ToString(), *GroupTag.ToString());
}

void FQuestlineGraphCompiler::CollectActivationGroupMetadata(UEdGraph* Graph, const FString& TagPrefix)
{
	if (!Graph) return;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		/**
		 * ActivationGroupSetter: walk backward from the Activate input to find every (source, outcome) pair that feeds this setter.
		 * CollectEffectiveSources handles knots, utility Forward, setter-Forward chains, and dereferences getters to their same-graph
		 * setters — transitive sources are captured so a parallel path through a chained group is still detected as a collision.
		 */
		if (UQuestlineNode_ActivationGroupEntry* Setter = Cast<UQuestlineNode_ActivationGroupEntry>(Node))
		{
			const FGameplayTag GroupTag = Setter->GetGroupTag();
			if (!GroupTag.IsValid()) continue;

			UEdGraphPin* ActivatePin = Setter->GetPinByRole(EQuestPinRole::ExecIn);
			if (!ActivatePin) continue;
			
			/**
			 * CollectEffectiveSources expects an output-side pin it can walk through (knots, utility Forward, setter Forward,
			 * getter Forward). Our ActivatePin is an input, so iterate its LinkedTo (each element is an output pin on an upstream
			 * node) and call the walker per-link. Sources accumulate into SourcePins across iterations via the shared out-set.
			 */
			TSet<const UEdGraphPin*> SourcePins;
			TSet<const UEdGraphNode*> VisitedNodes;
			for (const UEdGraphPin* Linked : ActivatePin->LinkedTo)
			{
				TraversalPolicy->CollectEffectiveSources(Linked, SourcePins, VisitedNodes);
			}

			for (const UEdGraphPin* SourcePin : SourcePins)
			{
				if (!SourcePin) continue;
				const UQuestlineNode_ContentBase* SourceContent = Cast<UQuestlineNode_ContentBase>(SourcePin->GetOwningNode());
				if (!SourceContent) continue; // Entry/utility sources have no content-node identity — skip

				const FString Label = Cast<const UQuestlineNode_LinkedQuestline>(SourceContent)
					? SanitizeTagSegment(SourceContent->NodeLabel.ToString())
					: SanitizeTagSegment(SourceContent->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				const FName SourceTag = MakeNodeTagName(TagPrefix, Label);
				if (SourceTag.IsNone()) continue;

				/**
				 * Outcome extraction: QuestOutcome pins carry a specific tag; QuestActivation "Any Outcome" leaves the outcome
				 * invalid to encode "any outcome from this source" — the collision test absorbs specific keys from the same source.
				 */
				FGameplayTag OutcomeTag;
				if (SourcePin->PinType.PinCategory == TEXT("QuestOutcome"))
				{
					OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(SourcePin->PinName, false);
					if (!OutcomeTag.IsValid()) continue;
				}

				const FSourceOutcomeKey Key{ SourceTag, OutcomeTag };
				GroupSetterSourcesByTag.FindOrAdd(GroupTag).Add(Key);
				SetterEdNodeByGroupAndSource.FindOrAdd(GroupTag).Add(Key, Setter);
			}
		}

		/**
		 * ActivationGroupGetter: walk forward from the Forward output to find every destination it reaches. CollectActivationTerminals
		 * terminates at content/exit Activate or Deactivate pins, passing transparently through knots, utility Forward, and
		 * setter-Forward chains. Destinations are recorded under the group tag so the analysis pass can cross-reference with
		 * setter sources for the same tag.
		 */
		if (UQuestlineNode_ActivationGroupExit* Getter = Cast<UQuestlineNode_ActivationGroupExit>(Node))
		{
			const FGameplayTag GroupTag = Getter->GetGroupTag();
			if (!GroupTag.IsValid()) continue;

			UEdGraphPin* ForwardPin = Getter->GetPinByRole(EQuestPinRole::ExecForwardOut);
			if (!ForwardPin) continue;

			TSet<const UEdGraphPin*> Terminals;
			TSet<const UEdGraphNode*> VisitedNodes;
			TraversalPolicy->CollectActivationTerminals(ForwardPin, Terminals, VisitedNodes);

			for (const UEdGraphPin* Terminal : Terminals)
			{
				if (!Terminal) continue;
				const UQuestlineNode_ContentBase* DestContent = Cast<UQuestlineNode_ContentBase>(Terminal->GetOwningNode());
				if (!DestContent) continue;

				const FString Label = Cast<const UQuestlineNode_LinkedQuestline>(DestContent)
					? SanitizeTagSegment(DestContent->NodeLabel.ToString())
					: SanitizeTagSegment(DestContent->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				const FName DestTag = MakeNodeTagName(TagPrefix, Label);
				if (DestTag.IsNone()) continue;

				GroupGetterDestsByTag.FindOrAdd(GroupTag).Add(DestTag);
				GetterEdNodeByGroupAndDest.FindOrAdd(GroupTag).Add(DestTag, Getter);			}
		}
	}
}

