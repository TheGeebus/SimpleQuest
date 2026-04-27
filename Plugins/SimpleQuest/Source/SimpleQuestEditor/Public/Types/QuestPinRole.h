// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestPinRole.generated.h"

/**
 * Semantic role a pin plays on a questline graph node. Decouples pin behavior from pin FName so node types can rename
 * their pins freely (e.g., "Activate" → "Enter" on utility nodes, "Forward" → "Exit" on portal Exits) without breaking
 * schema / compiler / autowire plumbing.
 *
 * The base-class default implementation of UQuestlineNodeBase::GetPinRole maps the current codebase's inline pin-name
 * literals to these roles. Derived classes with non-conforming pin names (e.g., UQuestlineNode_Knot's KnotIn / KnotOut)
 * override GetPinRole to supply their own mapping.
 */
UENUM()
enum class EQuestPinRole : uint8
{
	/** Default / unclassified. */
	None,

	/** Execution trigger input — fires node activation. "Activate" on content, "Enter" on utility/portal entries. */
	ExecIn,

	/** Execution forward output — fires downstream wires. "Forward" on content/Entry locals, "Exit" on portal Exits. */
	ExecForwardOut,

	/** Deactivation trigger input. Fires when upstream node's Deactivated output activates. */
	DeactivateIn,

	/** Deactivation passthrough output. Fires when this node is deactivated while in the Live state. */
	DeactivatedOut,

	/** Per-graph any-outcome sentinel ("Entered" on Entry terminal, "Any Outcome" legacy). */
	AnyOutcomeOut,

	/** Tag-specific outcome output — QuestOutcome-category pins named after their outcome tag. */
	NamedOutcomeOut,

	/** Prerequisite expression consumer. "Prerequisites" on content, "Condition_N" on combinators, future "Enter" on Prereq Entry. */
	PrereqIn,

	/** Prerequisite expression producer. Combinator output, future "Forward" on Prereq Entry, "Exit" on Prereq Exit. */
	PrereqOut
};