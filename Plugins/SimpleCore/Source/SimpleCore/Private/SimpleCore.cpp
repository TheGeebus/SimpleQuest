// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "SimpleCore.h"
#include "Utilities/SimpleCoreLog.h"

#define LOCTEXT_NAMESPACE "FSimpleCoreModule"

DEFINE_LOG_CATEGORY(LogSimpleCore);

void FSimpleCoreModule::StartupModule() {}
void FSimpleCoreModule::ShutdownModule() {}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSimpleCoreModule, SimpleCore)
