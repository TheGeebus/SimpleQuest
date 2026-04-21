// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "SGraphNode.h"

class UQuestlineNode_Quest;
class SVerticalBox;

/**
 * Slate widget for Quest content nodes. Mirrors the Step widget's structural pattern (title boilerplate +
 * pin content area + error reporting + comment bubble) plus a Givers section listing actors in loaded
 * levels with a QuestGiverComponent tagged for this Quest's compiled tag. No target / element / reward
 * display — Quest is a container for Steps; those concerns live at the Step level.
 *
 * Giver query goes through FindCompiledTagForNode — designers will see stale giver state after a label
 * rename until recompile propagates. Matches the same trade-off the Step widget accepts for non-current
 * tag resolution.
 */
class SGraphNode_QuestlineQuest : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_QuestlineQuest) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineNode_Quest* InNode);

	virtual void UpdateGraphNode() override;

private:
	UQuestlineNode_Quest* QuestNode = nullptr;

	/** Givers watching this Quest's compiled tag. Refreshed each UpdateGraphNode. Empty when the Quest hasn't
		compiled yet or no actors in loaded levels target its tag. */
	TArray<FString> WatchingGiverNames;
};