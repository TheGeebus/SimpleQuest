// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSimpleQuestEditorEN : public IModuleInterface
{
public:
	virtual ~FSimpleQuestEditorEN() = default;
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
