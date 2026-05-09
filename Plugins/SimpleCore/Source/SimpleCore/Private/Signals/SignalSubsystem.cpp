// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Signals/SignalSubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Utilities/SignalChannelUtils.h"


void USignalSubsystem::Deinitialize()
{
    bIsShuttingDown = true;
    ChannelSubscribers.Empty();
    Super::Deinitialize();
}

void USignalSubsystem::PublishRawMessage(const FGameplayTag Channel, const FInstancedStruct& Payload)
{
    if (bIsShuttingDown) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(USignalSubsystem_PublishRawMessage);

    TSet<FGameplayTag> VisitedTags;
    FGameplayTag CurrentTag = Channel;
    int32 LevelsFired = 0;

    while (CurrentTag.IsValid())
    {
        if (VisitedTags.Contains(CurrentTag)) break;
        VisitedTags.Add(CurrentTag);

        if (TArray<FSignalSubscriberRecord>* Records = ChannelSubscribers.Find(CurrentTag))
        {
            // Snapshot before iterating — a subscriber's dispatcher might subscribe or unsubscribe re-entrantly,
            // and we don't want those mutations to invalidate this walk. Mirrors the copy-before-broadcast pattern
            // the prior FSignalEventMulticast storage used implicitly.
            TArray<FSignalSubscriberRecord> RecordsSnapshot = *Records;
            for (const FSignalSubscriberRecord& Record : RecordsSnapshot)
            {
                if (!Record.Listener.IsValid()) continue;
                Record.Dispatcher(Channel, Payload);
            }
            ++LevelsFired;
        }
        CurrentTag = CurrentTag.RequestDirectParent();
    }

    UE_LOG(LogSimpleCore, VeryVerbose, TEXT("Signal::Publish: channel='%s' payload='%s' levelsFired=%d"),
        *Channel.ToString(),
        Payload.GetScriptStruct() ? *Payload.GetScriptStruct()->GetName() : TEXT("<empty>"),
        LevelsFired);
}

void USignalSubsystem::UnsubscribeMessage(const FGameplayTag Channel, const FDelegateHandle Handle)
{
    if (TArray<FSignalSubscriberRecord>* Records = ChannelSubscribers.Find(Channel))
    {
        // Handles are unique per Subscribe* call (UE's atomic ID counter), so at most one record
        // can match — IndexOfByPredicate's first-match semantic is correct and faster than RemoveAll.
        const int32 FoundIndex = Records->IndexOfByPredicate(
            [Handle](const FSignalSubscriberRecord& R) { return R.Handle == Handle; });
        if (FoundIndex != INDEX_NONE)
        {
            Records->RemoveAt(FoundIndex, 1, EAllowShrinking::No);
            UE_LOG(LogSimpleCore, Verbose, TEXT("Signal::Unsubscribe: channel='%s'"), *Channel.ToString());
        }
        if (Records->IsEmpty())
        {
            ChannelSubscribers.Remove(Channel);
        }
    }
}

void USignalSubsystem::PublishMessageOnChannelsRaw(TArray<FGameplayTag> Channels, const FInstancedStruct& Payload, bool bAllChannels)
{
    if (bIsShuttingDown) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(USignalSubsystem_PublishMessageOnChannelsRaw);

    // Drop invalid tags and de-duplicate the channel set up front. Subscriber dedup downstream depends on each channel
    // being walked at most once; trusting caller-side cleanliness would couple bus correctness to consumer discipline.
    TArray<FGameplayTag> CleanChannels;
    CleanChannels.Reserve(Channels.Num());
    TSet<FGameplayTag> ChannelSet;
    ChannelSet.Reserve(Channels.Num());
    for (const FGameplayTag& C : Channels)
    {
        if (C.IsValid() && !ChannelSet.Contains(C))
        {
            ChannelSet.Add(C);
            CleanChannels.Add(C);
        }
    }
    if (CleanChannels.IsEmpty()) return;

    DispatchOnChannels(CleanChannels, Payload, bAllChannels);
}

void USignalSubsystem::DispatchOnChannels(const TArray<FGameplayTag>& Channels, const FInstancedStruct& Payload, bool bAllChannels)
{
    int32 LevelsFired = 0;
    int32 DedupedSkipped = 0;

    if (bAllChannels)
    {
        // Sibling-publish — each channel walks its own hierarchy independently. Subscribers at common ancestors across
        // channels fire once per channel hit. Visited-set still prevents re-walking the same ancestor TAG within a
        // single channel's walk, but does NOT deduplicate the same SUBSCRIBER across channels.
        for (const FGameplayTag& Channel : Channels)
        {
            TSet<FGameplayTag> VisitedTags;
            FGameplayTag CurrentTag = Channel;
            while (CurrentTag.IsValid())
            {
                if (VisitedTags.Contains(CurrentTag)) break;
                VisitedTags.Add(CurrentTag);

                if (TArray<FSignalSubscriberRecord>* Records = ChannelSubscribers.Find(CurrentTag))
                {
                    TArray<FSignalSubscriberRecord> RecordsSnapshot = *Records;
                    for (const FSignalSubscriberRecord& Record : RecordsSnapshot)
                    {
                        if (!Record.Listener.IsValid()) continue;
                        Record.Dispatcher(Channel, Payload);
                    }
                    ++LevelsFired;
                }
                CurrentTag = CurrentTag.RequestDirectParent();
            }
        }
    }
    else
    {
        // Default dedup-on. Walk every channel's hierarchy; deduplicate subscribers across channels by FDelegateHandle. Each
        // subscriber fires once with the best-match channel from the publish set as the callback's first arg.
        TSet<FDelegateHandle> Delivered;

        for (const FGameplayTag& Channel : Channels)
        {
            TSet<FGameplayTag> VisitedTags;
            FGameplayTag CurrentTag = Channel;
            while (CurrentTag.IsValid())
            {
                if (VisitedTags.Contains(CurrentTag)) break;
                VisitedTags.Add(CurrentTag);

                if (TArray<FSignalSubscriberRecord>* Records = ChannelSubscribers.Find(CurrentTag))
                {
                    TArray<FSignalSubscriberRecord> RecordsSnapshot = *Records;
                    bool bAnyFiredAtThisLevel = false;
                    for (const FSignalSubscriberRecord& Record : RecordsSnapshot)
                    {
                        if (Delivered.Contains(Record.Handle)) { ++DedupedSkipped; continue; }
                        if (!Record.Listener.IsValid()) continue;

                        const FGameplayTag MatchedChannel = FSignalChannelUtils::PickBestMatchChannel(Channels, CurrentTag);
                        Record.Dispatcher(MatchedChannel, Payload);
                        Delivered.Add(Record.Handle);
                        bAnyFiredAtThisLevel = true;
                    }
                    if (bAnyFiredAtThisLevel) ++LevelsFired;
                }
                CurrentTag = CurrentTag.RequestDirectParent();
            }
        }
    }

    UE_LOG(LogSimpleCore, VeryVerbose,
        TEXT("Signal::PublishOnChannels: channels=%d payload='%s' bAllChannels=%s levelsFired=%d dedupedSkipped=%d"),
        Channels.Num(),
        Payload.GetScriptStruct() ? *Payload.GetScriptStruct()->GetName() : TEXT("<empty>"),
        bAllChannels ? TEXT("true") : TEXT("false"),
        LevelsFired,
        DedupedSkipped);
}

