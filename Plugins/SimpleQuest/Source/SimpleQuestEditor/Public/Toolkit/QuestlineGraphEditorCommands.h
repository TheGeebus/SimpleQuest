// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"

class FQuestlineGraphEditorCommands : public TCommands<FQuestlineGraphEditorCommands>
{
public:
	FQuestlineGraphEditorCommands()
		: TCommands<FQuestlineGraphEditorCommands>(
			TEXT("QuestlineGraphEditor"),
			NSLOCTEXT("Contexts", "QuestlineGraphEditor", "Questline Graph Editor"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> CompileQuestlineGraph;
	TSharedPtr<FUICommandInfo> CompileAllQuestlineGraphs;
	TSharedPtr<FUICommandInfo> NavigateBack;
	TSharedPtr<FUICommandInfo> NavigateForward;
};