#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/QuestManagerSubsystem.h"
#include "SimpleQuestSettings.generated.h"


/**
 * BP-friendly mirror of ELogVerbosity::Type for the log-category settings on USimpleQuestSettings. Maps 1:1 to UE's
 * underlying enum via ToELogVerbosity in SimpleQuestSettings.cpp; kept separate so the Project Settings dropdown
 * shows clean designer-facing labels without exposing the engine enum.
 */
UENUM(BlueprintType)
enum class EQuestLogVerbosity : uint8
{
	NoLogging   UMETA(DisplayName = "Off"),
	Fatal       UMETA(DisplayName = "Fatal"),
	Error       UMETA(DisplayName = "Error"),
	Warning     UMETA(DisplayName = "Warning"),
	Display     UMETA(DisplayName = "Display"),
	Log         UMETA(DisplayName = "Log"),
	Verbose     UMETA(DisplayName = "Verbose"),
	VeryVerbose UMETA(DisplayName = "Very Verbose"),
};

/**
 * Broadcasts when AdditionalOutcomePickerCategories or AdditionalQuestlinePickerCategories changes via the
 * editor's Project Settings UI. SimpleQuestEditor subscribes at module startup to refresh K2 node widgets
 * across open Blueprint editors so existing pickers re-query GetPinMetaData and pick up the new categories
 * without forcing designers to close + reopen the affected Blueprints.
 */
DECLARE_MULTICAST_DELEGATE(FOnPickerCategoriesChanged);


UCLASS(config=SimpleQuest, DefaultConfig, meta=(DisplayName="Simple Quest"))
class SIMPLEQUEST_API USimpleQuestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/**
	 * Quest manager class loaded when the game starts. Set to a Blueprint or C++ subclass of UQuestManagerSubsystem
	 * to customize manager behavior; defaults to the native UQuestManagerSubsystem.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Initialization", meta=(DisplayName="Quest Manager"))
	TSoftClassPtr<UQuestManagerSubsystem> QuestManagerClass = TSoftClassPtr<UQuestManagerSubsystem>(UQuestManagerSubsystem::StaticClass());

	/**
	 * Manager activation flow — cascade entry, chain advancement, live-state writers, container Live derivation.
	 * Raise to Verbose when debugging "why isn't my quest going Live" or chain-advancement issues.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Activation"))
	EQuestLogVerbosity LogSimpleQuestActivationVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Subscriber wiring — Observer/Trigger/Giver registration, K2 node subscriptions, catch-up fanout, prereq-leaf
	 * enablement watches. Raise to Verbose when debugging "bound but never fires" or catch-up replay behavior.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Subscription"))
	EQuestLogVerbosity LogSimpleQuestSubscriptionVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Compile-time output — graph compile, native tag registration, rename-redirect machinery, stale-tag warnings.
	 * Bulk of the historical Verbose noise; raise selectively when investigating compile or rename behavior.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Compiler"))
	EQuestLogVerbosity LogSimpleQuestCompilerVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Import/export pipeline — reading and writing external tables, mapping columns onto node properties, and the
	 * in-place re-import plan. Raise to Verbose when debugging "why didn't my column land on the node I expected."
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Resolver"))
	EQuestLogVerbosity LogSimpleQuestResolverVerbosity = EQuestLogVerbosity::Log;

	/**
	 * UQuestStateSubsystem registry mutations — resolutions, entries, tag/alias registrations. The durable record of
	 * what happened (becomes load-bearing once save/load lands in 0.5.0). Raise to Verbose when debugging "what's
	 * persisted vs ephemeral."
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="State"))
	EQuestLogVerbosity LogSimpleQuestStateVerbosity = EQuestLogVerbosity::Log;

	/**
	 * Umbrella channel — module startup, settings, debug overlay, and anything not covered by the specialized channels
	 * above. Live-applied: changes take effect immediately without restart.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Logging", meta=(DisplayName="Module"))
	EQuestLogVerbosity LogSimpleQuestVerbosity = EQuestLogVerbosity::Log;

	// ── Authoring extensibility ──────────────────────────────────────────────────────────────────────────
	//
	// Designer-facing pickers (K2 node Outcome / Questline pins) narrow their gameplay-tag filter to
	// SimpleQuest-internal namespaces by default so designers don't scroll through every project tag.
	// Adopters bridging quest outcomes / identities with their own game tag trees can extend these
	// filters via the two arrays below — no source fork required.

	/**
	 * Additional gameplay-tag namespaces shown in K2 node Outcome pickers alongside the default
	 * SimpleQuest.Outcome namespace. Each entry is a tag prefix (e.g., "Game.Outcome",
	 * "MyStudio.Quest.Outcome") whose descendants will appear in the picker. Empty by default.
	 * Affects K2Node_CompleteObjectiveWithOutcome's OutcomeTag pin picker; runtime behavior unchanged.
	 *
	 * Two rules enforced at compose time (violations are dropped with a warning log):
	 *  - Entries under the framework-owned SimpleQuest namespace are rejected — extend with your own
	 *    namespace ("Game.*", "MyStudio.*", etc.).
	 *  - Entries that overlap each other (duplicates, parent/child pairs) are pruned to one — the
	 *    underlying gameplay-tag picker can't render overlapping namespace roots without crashing.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Authoring", meta=(DisplayName="Additional Outcome Picker Categories"))
	TArray<FName> AdditionalOutcomePickerCategories;

	/**
	 * Additional gameplay-tag namespaces shown in K2 node Questline pickers alongside the default
	 * SimpleQuest.Questline namespace. Affects K2Node_ObserveQuestLifecycle's QuestTag pin picker; runtime
	 * behavior unchanged.
	 *
	 * Same two compose-time rules as the Outcome list above: no SimpleQuest sub-namespaces, no
	 * overlapping entries.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Authoring", meta=(DisplayName="Additional Questline Picker Categories"))
	TArray<FName> AdditionalQuestlinePickerCategories;

	// ── Resolver ─────────────────────────────────────────────────────────────────────────────────────────
	//
	// The data resolver reads and writes questline content as editable tables (for diffing, bulk edits, or
	// importing from an external pipeline) in a chosen on-disk format. This is the format used when no more
	// specific choice is made at the point of an import or export.

	/**
	 * Default format the resolver reads and writes. Chosen from the formats currently available; the built-in
	 * choices are "TSV" and "JSON", and a studio that plugs in its own format sees it listed here too.
	 */
	UPROPERTY(config, EditAnywhere, Category="Resolver", meta=(DisplayName="Default Import/Export Format", GetOptions="GetAvailableFormatNames"))
	FName DefaultImportFormat = TEXT("TSV");

	/** The format names offered by the setting above. Populated by the editor from its registered formats. */
	static void SetAvailableFormats(const TArray<FString>& InFormatNames);

	/** Backs the DefaultImportFormat dropdown from the names the editor supplied (empty outside the editor). */
	UFUNCTION()
	static TArray<FString> GetAvailableFormatNames();

	/** Fires when AdditionalOutcomePickerCategories or AdditionalQuestlinePickerCategories changes. */
	static FOnPickerCategoriesChanged OnPickerCategoriesChanged;
	
	/**
	 * Composes the default outcome namespace with adopter-configured additional namespaces into the
	 * comma-separated string format Categories meta expects. Default is always first. Overlapping
	 * adopter entries are dropped and logged.
	 */
	FString ComposeOutcomePickerCategories() const;

	/**
	 * Composes the default questline namespace with adopter-configured additional namespaces. Default
	 * is always first. Overlapping adopter entries are dropped and logged.
	 */
	FString ComposeQuestlinePickerCategories() const;

private:
	/**
	 * Shared composition body for both picker-category helpers. Always keeps DefaultNamespace; iterates
	 * AdditionalCategories in authored order, keeping each entry only when it doesn't overlap (identity,
	 * ancestor, or descendant in tag-namespace hierarchy) with any already-kept entry. Overlapping
	 * entries are dropped with a warning log — SGameplayTagPicker's STreeView crashes when the
	 * Categories list contains overlapping roots.
	 */
	static FString ComposePickerCategories(const FString& DefaultNamespace, const TArray<FName>& AdditionalCategories);

	/**
	 * The resolver format names the editor supplied via SetAvailableFormats. Static because the source of truth
	 * (the editor's format registry) is editor-only; the value flows editor -> here, the setting reads it back.
	 */
	static TArray<FString> AvailableFormats;

public:

	/**
	 * Pushes every verbosity value to its log category. Called from PostEditChangeProperty for live-apply during editor
	 * sessions, and from FSimpleQuest::StartupModule on engine startup so settings take effect before any UE_LOG fires.
	 */
	void ApplyLogVerbosity() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};


UCLASS()
class SIMPLEQUEST_API UGameInstanceSubsystemInitializer : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	virtual void Initialize(FSubsystemCollectionBase& Collection) override final;
};