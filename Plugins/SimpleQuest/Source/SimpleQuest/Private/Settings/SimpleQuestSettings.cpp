#include "Settings/SimpleQuestSettings.h"

#include "SimpleQuestLog.h"
#include "Subsystems/QuestManagerSubsystem.h"


FOnPickerCategoriesChanged USimpleQuestSettings::OnPickerCategoriesChanged;

namespace
{
	ELogVerbosity::Type ToELogVerbosity(EQuestLogVerbosity Verbosity)
	{
		switch (Verbosity)
		{
		case EQuestLogVerbosity::NoLogging:   return ELogVerbosity::NoLogging;
		case EQuestLogVerbosity::Fatal:       return ELogVerbosity::Fatal;
		case EQuestLogVerbosity::Error:       return ELogVerbosity::Error;
		case EQuestLogVerbosity::Warning:     return ELogVerbosity::Warning;
		case EQuestLogVerbosity::Display:     return ELogVerbosity::Display;
		case EQuestLogVerbosity::Log:         return ELogVerbosity::Log;
		case EQuestLogVerbosity::Verbose:     return ELogVerbosity::Verbose;
		case EQuestLogVerbosity::VeryVerbose: return ELogVerbosity::VeryVerbose;
		default:                              return ELogVerbosity::Log;
		}
	}
}

void USimpleQuestSettings::ApplyLogVerbosity() const
{
	LogSimpleQuest.SetVerbosity(ToELogVerbosity(LogSimpleQuestVerbosity));
	LogSimpleQuestActivation.SetVerbosity(ToELogVerbosity(LogSimpleQuestActivationVerbosity));
	LogSimpleQuestCompiler.SetVerbosity(ToELogVerbosity(LogSimpleQuestCompilerVerbosity));
	LogSimpleQuestSubscription.SetVerbosity(ToELogVerbosity(LogSimpleQuestSubscriptionVerbosity));
	LogSimpleQuestState.SetVerbosity(ToELogVerbosity(LogSimpleQuestStateVerbosity));
}

#if WITH_EDITOR
void USimpleQuestSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyLogVerbosity();

	// Picker-category changes broadcast so the editor module can refresh K2 node widgets in any open
	// Blueprints. Without this, designers have to close and reopen the affected BPs to see fresh categories
	// in the pickers as the underlying Slate widgets cache the Categories filter at construction time.
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(USimpleQuestSettings, AdditionalOutcomePickerCategories)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(USimpleQuestSettings, AdditionalQuestlinePickerCategories))
	{
		OnPickerCategoriesChanged.Broadcast();
	}
}
#endif

void UGameInstanceSubsystemInitializer::Initialize(FSubsystemCollectionBase& Collection)
{
	const USimpleQuestSettings* SimpleQuestSettings = GetDefault<USimpleQuestSettings>();

	// check if the quest manager is invalid
	UClass* NewManagerClass = SimpleQuestSettings->QuestManagerClass.LoadSynchronous();
	if (!NewManagerClass)
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UGameInstanceSubsystemInitializer::Initialize : QuestManagerClass is not valid, %s"), *GetFullName())
		return;
	}

	// Register the quest manager
	UE_LOG(LogSimpleQuest, Log, TEXT("Designated quest manager class: %s (creation gated by ShouldCreateSubsystem)"), *NewManagerClass->GetFullName());
	Collection.InitializeDependency(NewManagerClass);
}

FString USimpleQuestSettings::ComposeOutcomePickerCategories() const
{
	return ComposePickerCategories(TEXT("SimpleQuest.Outcome"), AdditionalOutcomePickerCategories);
}

FString USimpleQuestSettings::ComposeQuestlinePickerCategories() const
{
	return ComposePickerCategories(TEXT("SimpleQuest.Questline"), AdditionalQuestlinePickerCategories);
}

FString USimpleQuestSettings::ComposePickerCategories(const FString& DefaultNamespace, const TArray<FName>& AdditionalCategories)
{
	TArray<FString> Kept;
	Kept.Add(DefaultNamespace);

	// Overlap test: identical, or one is ancestor of the other via dot-separated namespace hierarchy.
	// Case-insensitive throughout — gameplay tag names are case-insensitive at lookup time.
	auto Overlaps = [](const FString& A, const FString& B) -> bool
	{
		return A.Equals(B, ESearchCase::IgnoreCase)
			|| A.StartsWith(B + TEXT("."), ESearchCase::IgnoreCase)
			|| B.StartsWith(A + TEXT("."), ESearchCase::IgnoreCase);
	};

	for (const FName& Extra : AdditionalCategories)
	{
		if (Extra.IsNone()) continue;
		const FString Candidate = Extra.ToString();
		if (Candidate.IsEmpty()) continue;

		// Reject anything under the framework-owned SimpleQuest namespace. The framework already exposes
		// its designer-facing pickers (Outcome / Questline) and reserves the rest of the namespace for
		// internal tags (State, PrereqRule, ActivationGroup, Path, etc.). Adopter extensions belong in
		// their own namespace ("Game.*", "MyStudio.*", etc.) — no legitimate use case for adding
		// SimpleQuest sub-namespaces, and doing so risks exposing framework internals to the picker.
		if (Candidate.Equals(TEXT("SimpleQuest"), ESearchCase::IgnoreCase)
			|| Candidate.StartsWith(TEXT("SimpleQuest."), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogSimpleQuest, Warning,
				TEXT("USimpleQuestSettings: picker category '%s' is under the framework-owned SimpleQuest namespace — dropping. ")
				TEXT("Use your own tag namespace ('Game.*', 'MyStudio.*', etc.) for picker extensions."),
				*Candidate);
			continue;
		}

		// Drop any remaining candidate that overlaps with an earlier-kept adopter entry. SGameplayTag-
		// Picker's STreeView crashes on overlapping namespace roots — same tag node would render under
		// multiple roots and violate the tree's uniqueness invariant.
		const FString* Conflict = Kept.FindByPredicate([&](const FString& Existing) { return Overlaps(Candidate, Existing); });
		if (Conflict)
		{
			UE_LOG(LogSimpleQuest, Warning,
				TEXT("USimpleQuestSettings: picker category '%s' overlaps with already-kept '%s' — dropping to prevent tag-picker crash. ")
				TEXT("Make picker category entries disjoint."),
				*Candidate, **Conflict);
			continue;
		}

		Kept.Add(Candidate);
	}

	return FString::Join(Kept, TEXT(","));
}

