// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestTagComposer.h"

#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"

const FString& FQuestTagComposer::LeafToString(EQuestStateLeaf Leaf)
{
	static const FString S_Live         = TEXT("Live");
	static const FString S_Completed    = TEXT("Completed");
	static const FString S_PendingGiver = TEXT("PendingGiver");
	static const FString S_Deactivated  = TEXT("Deactivated");
	static const FString S_Blocked      = TEXT("Blocked");
	static const FString S_Unknown      = TEXT("Unknown");

	switch (Leaf)
	{
	case EQuestStateLeaf::Live:         return S_Live;
	case EQuestStateLeaf::Completed:    return S_Completed;
	case EQuestStateLeaf::PendingGiver: return S_PendingGiver;
	case EQuestStateLeaf::Deactivated:  return S_Deactivated;
	case EQuestStateLeaf::Blocked:      return S_Blocked;
	}
	return S_Unknown;
}

EQuestTagKind FQuestTagComposer::ClassifyTag(FName TagName)
{
	if (TagName.IsNone()) return EQuestTagKind::Unknown;
	const FString TagStr = TagName.ToString();
	if (TagStr.StartsWith(StateNamespace))           return EQuestTagKind::State;
	if (TagStr.StartsWith(OutcomeNamespace))         return EQuestTagKind::Outcome;
	if (TagStr.StartsWith(LegacyOutcomePrefix))      return EQuestTagKind::LegacyOutcome;
	if (TagStr.StartsWith(PrereqRuleNamespace))      return EQuestTagKind::PrereqRule;
	if (TagStr.StartsWith(ActivationGroupNamespace)) return EQuestTagKind::ActivationGroup;
	if (TagStr.StartsWith(IdentityNamespace))        return EQuestTagKind::Identity;
	return EQuestTagKind::Unknown;
}

bool FQuestTagComposer::IsIdentityTag(FName TagName)        { return ClassifyTag(TagName) == EQuestTagKind::Identity; }
bool FQuestTagComposer::IsStateTag(FName TagName)           { return ClassifyTag(TagName) == EQuestTagKind::State; }
bool FQuestTagComposer::IsOutcomeTag(FName TagName)
{
	const EQuestTagKind Kind = ClassifyTag(TagName);
	return Kind == EQuestTagKind::Outcome || Kind == EQuestTagKind::LegacyOutcome;
}
bool FQuestTagComposer::IsPrereqRuleTag(FName TagName)      { return ClassifyTag(TagName) == EQuestTagKind::PrereqRule; }
bool FQuestTagComposer::IsActivationGroupTag(FName TagName) { return ClassifyTag(TagName) == EQuestTagKind::ActivationGroup; }

FName FQuestTagComposer::MakeIdentityTag(const FString& AssetPrefix, TArrayView<const FString> Segments)
{
	FString Result = IdentityNamespace + AssetPrefix;
	for (const FString& Segment : Segments)
	{
		Result += TEXT(".");
		Result += Segment;
	}
	return FName(*Result);
}

FName FQuestTagComposer::MakeStateFact(FName IdentityTagName, EQuestStateLeaf Leaf)
{
	if (IdentityTagName.IsNone()) return NAME_None;
	FString TagStr = IdentityTagName.ToString();
	if (TagStr.StartsWith(IdentityNamespace))
	{
		TagStr = StateNamespace + TagStr.RightChop(IdentityNamespace.Len());
	}
	return FName(*(TagStr + TEXT(".") + LeafToString(Leaf)));
}

FName FQuestTagComposer::MakeStateFact(FGameplayTag IdentityTag, EQuestStateLeaf Leaf)
{
	return MakeStateFact(IdentityTag.GetTagName(), Leaf);
}

FName FQuestTagComposer::MakeNodePathFact(FName IdentityTagName, FName PathIdentity)
{
	if (PathIdentity.IsNone()) return NAME_None;
	FString NodeStr = IdentityTagName.ToString();
	if (NodeStr.StartsWith(IdentityNamespace))
	{
		NodeStr = StateNamespace + NodeStr.RightChop(IdentityNamespace.Len());
	}
	FString PathStr = PathIdentity.ToString();
	TryStripOutcomePrefix(PathStr);
	return FName(*(NodeStr + TEXT(".Path.") + PathStr));
}

FName FQuestTagComposer::MakeEntryPathFact(FName IdentityTagName, FName PathIdentity)
{
	if (PathIdentity.IsNone()) return NAME_None;
	FString NodeStr = IdentityTagName.ToString();
	if (NodeStr.StartsWith(IdentityNamespace))
	{
		NodeStr = StateNamespace + NodeStr.RightChop(IdentityNamespace.Len());
	}
	FString PathStr = PathIdentity.ToString();
	TryStripOutcomePrefix(PathStr);
	return FName(*(NodeStr + TEXT(".EntryPath.") + PathStr));
}

FGameplayTag FQuestTagComposer::ResolveStateFactTag(FGameplayTag IdentityTag, EQuestStateLeaf Leaf)
{
	const FName FactName = MakeStateFact(IdentityTag, Leaf);
	if (FactName.IsNone()) return FGameplayTag();
	return UGameplayTagsManager::Get().RequestGameplayTag(FactName, false);
}

FName FQuestTagComposer::SwapNamespacePrefix(FName InTagName, const FString& From, const FString& To)
{
	if (InTagName.IsNone()) return NAME_None;
	const FString TagStr = InTagName.ToString();
	if (!TagStr.StartsWith(From)) return InTagName;
	return FName(*(To + TagStr.RightChop(From.Len())));
}

FString FQuestTagComposer::GetLeafSegment(FName TagName)
{
	const FString Full = TagName.ToString();
	int32 LastDot = INDEX_NONE;
	if (Full.FindLastChar(TEXT('.'), LastDot))
	{
		return Full.Mid(LastDot + 1);
	}
	return Full;
}

bool FQuestTagComposer::TryGetParentTag(FName TagName, FName& OutParentTag)
{
	const FString Full = TagName.ToString();
	int32 LastDot = INDEX_NONE;
	if (!Full.FindLastChar(TEXT('.'), LastDot)) return false;
	OutParentTag = FName(*Full.Left(LastDot));
	return true;
}

void FQuestTagComposer::EnumerateAncestors(FName TagName, TFunctionRef<bool(FName)> Visitor)
{
	FName Cursor = TagName;
	FName Parent;
	while (TryGetParentTag(Cursor, Parent))
	{
		if (!Visitor(Parent)) return;
		Cursor = Parent;
	}
}

bool FQuestTagComposer::TryStripOutcomePrefix(FString& InOutPathString)
{
	if (InOutPathString.StartsWith(OutcomeNamespace))
	{
		InOutPathString.RightChopInline(OutcomeNamespace.Len());
		return true;
	}
	if (InOutPathString.StartsWith(LegacyOutcomePrefix))
	{
		InOutPathString.RightChopInline(LegacyOutcomePrefix.Len());
		return true;
	}
	return false;
}

FText FQuestTagComposer::FormatOutcomeForDisplay(FName OutcomeTagName)
{
	FString Remainder = OutcomeTagName.ToString();
	TryStripOutcomePrefix(Remainder);

	TArray<FString> Segments;
	Remainder.ParseIntoArray(Segments, TEXT("."));
	for (FString& Seg : Segments)
	{
		Seg = FName::NameToDisplayString(Seg, false);
	}
	return FText::FromString(FString::Join(Segments, TEXT(": ")));
}

bool FQuestTagComposer::IsTagRegisteredInRuntime(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return false;
	return UGameplayTagsManager::Get().RequestGameplayTag(Tag.GetTagName(), false).IsValid();
}

FGameplayTagContainer FQuestTagComposer::FilterToRegisteredTags(const FGameplayTagContainer& Container, const FString& ContextLabel)
{
	FGameplayTagContainer Result;
	for (const FGameplayTag& Tag : Container)
	{
		if (IsTagRegisteredInRuntime(Tag)) { Result.AddTag(Tag); continue; }
		UE_LOG(LogSimpleQuest, Warning,
			TEXT("%s : filtering stale tag '%s' — no longer registered. ")
			TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
			*ContextLabel, *Tag.ToString());
	}
	return Result;
}

FName FQuestTagComposer::MakeStateFact(FName IdentityTagName, const FString& Leaf)
{
	if (IdentityTagName.IsNone()) return NAME_None;
	FString TagStr = IdentityTagName.ToString();
	if (TagStr.StartsWith(IdentityNamespace))
	{
		TagStr = StateNamespace + TagStr.RightChop(IdentityNamespace.Len());
	}
	return FName(*(TagStr + TEXT(".") + Leaf));
}

FName FQuestTagComposer::MakeStateFact(FGameplayTag IdentityTag, const FString& Leaf)
{
	return MakeStateFact(IdentityTag.GetTagName(), Leaf);
}

