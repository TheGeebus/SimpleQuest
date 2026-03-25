// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleQuest.h"

#include "SimpleQuestLog.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "FSimpleQuestModule"

DEFINE_LOG_CATEGORY(LogSimpleQuest);

void FSimpleQuestModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

}

void FSimpleQuestModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSimpleQuestModule, SimpleQuest)