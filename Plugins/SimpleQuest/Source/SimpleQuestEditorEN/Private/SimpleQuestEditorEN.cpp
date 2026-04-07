// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "SimpleQuestEditorEN.h"
#include "Modules/ModuleManager.h"

#if WITH_ELECTRONIC_NODES
#include "Graph/QuestlineGraphSchema.h"
#include "Graph/QuestlineENConnectionDrawingPolicy.h"
#endif

IMPLEMENT_MODULE(FSimpleQuestEditorEN, SimpleQuestEditorEN)

void FSimpleQuestEditorEN::StartupModule()
{
#if WITH_ELECTRONIC_NODES
	UQuestlineGraphSchema::RegisterENPolicyFactory([](int32 BackLayer, int32 FrontLayer, float Zoom, const FSlateRect& Clip,
	FSlateWindowElementList& Elements, UEdGraph* Graph)
		-> FConnectionDrawingPolicy*
	{
		if (!GetDefault<UElectronicNodesSettings>()->MasterActivate)
			return nullptr;

		return new FQuestlineENConnectionDrawingPolicy(BackLayer, FrontLayer, Zoom, Clip, Elements, Graph);
	});
#endif
}


void FSimpleQuestEditorEN::ShutdownModule()
{
#if WITH_ELECTRONIC_NODES
	UQuestlineGraphSchema::UnregisterENPolicyFactory();
#endif
}
