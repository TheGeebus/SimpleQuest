// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestNodeInfo.generated.h"

/**
 * Compiled display metadata for a quest graph node. Populated by the compiler (DisplayName) and resolved at runtime (ContextualTag).
 * Self-contained so it can be embedded in event context structs without requiring a reference back to the originating node.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestNodeInfo
{
	GENERATED_BODY()

	/** The node's routing/identity tag. Resolved at runtime from the compiler-assigned FName. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	FGameplayTag QuestTag;

	/**
	 * Unsanitized display name from the editor node label. Safe for direct UI display. Preserves spaces, punctuation, and
	 * casing that the sanitized tag segment strips.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	FText DisplayName;

	/**
	 * The authored Order Bias, stamped by the compiler. Lives here rather than on the node itself so it rides every
	 * event payload for free - a display that wants to sort rows it has already received never has to look anything
	 * up. Higher sorts earlier, 0 means "no opinion, use natural order", and only siblings ever compare.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	int32 OrderBias = 0;

	/**
	 * Where this node sits in the authored progression - a depth-first walk of the graph from its entry points,
	 * following outcome wires, stamped at compile.
	 *
	 * This is the map, not the history: it exists before anything runs, it does not change when a player takes a
	 * shortcut or re-enters a node, and it never disagrees with the graph because it IS the graph. Sorting a display
	 * by it shows content where the designer put it rather than where the run happened to go.
	 *
	 * Only comparable between nodes from the same compile. Two roots that know nothing about each other have no
	 * shared position space, which is what Order Bias and natural order settle.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	int32 AuthoredPosition = INDEX_NONE;

	/**
	 * The last position reached by anything this node leads to. Together with AuthoredPosition it is the RANGE this
	 * node's downstream run occupies, which is what makes precedence an O(1) question: this node activates that one
	 * if this range contains that one's.
	 *
	 * *** WHY THIS EXISTS: Chapter 4's First, Second and Third are SIBLINGS BY TAG, so nothing in the tag hierarchy
	 * says that First is what activates the other two. Without the range, ordering sees three peers and lets a bias
	 * on Third lift it above the step that starts it. *** A node's cause always precedes it, and no authoring
	 * overrides that.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	int32 AuthoredSubtreeEnd = INDEX_NONE;
};