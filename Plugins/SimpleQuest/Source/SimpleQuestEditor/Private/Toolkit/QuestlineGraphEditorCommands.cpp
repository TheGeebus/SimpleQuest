// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineGraphEditorCommands.h"

#define LOCTEXT_NAMESPACE "QuestlineGraphEditor"

void FQuestlineGraphEditorCommands::RegisterCommands()
{
	UI_COMMAND(
		CompileQuestlineGraph,
		"Compile",
		"Compile the questline graph, writing inter-quest relationships to each Quest CDO",
		EUserInterfaceActionType::Button,
		FInputChord());
	UI_COMMAND(NavigateBack, "Back", "Navigate to the previous graph", EUserInterfaceActionType::Button, FInputChord(EKeys::LeftBracket, false, true, false, false));
	UI_COMMAND(NavigateForward, "Forward", "Navigate to the next graph", EUserInterfaceActionType::Button, FInputChord(EKeys::RightBracket, false, true, false, false));
	UI_COMMAND(CompileAllQuestlineGraphs, "Compile All Questlines",	"Compiles every questline graph in the project", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleGraphDefaults, "Graph Defaults",
		"Show the questline asset's own properties (QuestlineID, FriendlyName, etc.) in the Details panel. While pinned, selecting nodes in the graph doesn't replace the Details view. Click again to restore normal selection tracking.",
		EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ValidatePrereqTags, "Validate Tags",
		"Scan every questline graph in the project for broken or unused prerequisite references — leaves pointing at missing fact tags,\n"
		"Rule Exits pointing at rules no Rule Entry provides (or with no tag set), and Rule Entries that no Rule Exit references.\n"
		"Reports to the Quest Validator message log.\n\n"
		"Read-only; never modifies assets.\n\n"
		"Validation is independent of the compiler — flags cross-graph drift and possible authoring issues the per-graph compile can't see.\n"
		"Warnings are typically harmless leftovers; errors indicate wiring that will fail at runtime.",
		EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
