// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "SimpleCore.h"
#include "Settings/SimpleCoreSettings.h"
#include "Utilities/SimpleCoreLog.h"

#define LOCTEXT_NAMESPACE "FSimpleCoreModule"

DEFINE_LOG_CATEGORY(LogSimpleCore);

void FSimpleCoreModule::StartupModule()
{
	// Apply log verbosity from Project Settings. UDeveloperSettings's Config flow loads the values during engine boot
	// before module startup, so GetDefault here returns settings already populated from DefaultSimpleCore.ini. Project
	// Settings → Plugins → Simple Core → Logging is the single source for the LogSimpleCore dial.
	GetDefault<USimpleCoreSettings>()->ApplyLogVerbosity();
}

void FSimpleCoreModule::ShutdownModule() {}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSimpleCoreModule, SimpleCore)