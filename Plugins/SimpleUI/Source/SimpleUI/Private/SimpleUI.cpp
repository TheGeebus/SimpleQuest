// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "SimpleUI.h"
#include "Utilities/SimpleUILog.h"

#define LOCTEXT_NAMESPACE "FSimpleUIModule"

DEFINE_LOG_CATEGORY(LogSimpleUI);

void FSimpleUIModule::StartupModule() {}
void FSimpleUIModule::ShutdownModule() {}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSimpleUIModule, SimpleUI)