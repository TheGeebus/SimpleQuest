// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestActivationProvenance.h"
#include "Quests/Types/QuestEntryRecord.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Quests/Types/QuestRuntimeRecord.h"
#include "Quests/Types/QuestDisplayDataRecord.h"
#include "QuestStateSubsystem.generated.h"

struct FSimpleQuestSaveSnapshot;
struct FQuestRoleSourceInfo;

class UQuestDisplayData;
class UQuestObjective;
class UActorComponent;
class AActor;
class USignalSubsystem;
class UWorldStateSubsystem;

/**
 * Multicast fired after any mutation to the registry maps (resolutions, entries, prereq cache). See
 * UQuestStateSubsystem::OnAnyRegistryChanged for semantics.
 */
DECLARE_MULTICAST_DELEGATE(FOnAnyRegistryChanged);

/**
 * Public read-side surface for quest state queries: past resolutions (rich-record half of the two-layer
 * state architecture; SimpleCore's UWorldStateSubsystem provides the boolean-fact half) and present-tense
 * activation queries (cached prereq snapshots, computed activation blockers).
 *
 * Naming convention mirrors UWorldStateSubsystem in SimpleCore: "State Subsystem" denotes a public,
 * externally-accessible fact registry with potentially limited write access. Designers come here for
 * quest-state queries, and the manager subsystem stays a black box for orchestration and pushes facts here
 * when state changes.
 *
 * Writes are exclusive to UQuestManagerSubsystem via friend access. External code never mutates this
 * subsystem. The manager pushes:
 *  - Known quest tag registration on graph activation (RegisterQuestTag) — populates the KnownQuests
 *     map whose keys answer GetQuestTagsUnderPrefix for hierarchical catch-up subscribers.
 *  - Resolution records on quest completion (RecordResolution).
 *  - Entry records on quest start (RecordEntry) — carries Provenance + ActivationParamsSnapshot +
 *     PathIdentity alongside the existing cascade fields, capturing the merged final params delivered
 *     to the objective so save/load can reconstitute live questline state by-value.
 *  - Prereq status snapshots on giver-branch entry and enablement-watch transitions (UpdateQuestPrereqStatus).
 *  - Cache clears on quest leaving giver state (ClearQuestPrereqStatus).
 *  - Container classification on graph activation (RegisterContainerTag).
 *
 * Reads are pure: blocker enumeration reads WorldState facts and the local CachedPrereqStatus map. No
 * manager dependency at query time.
 */
UCLASS()
class SIMPLEQUEST_API UQuestStateSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ── Past resolution queries ──────────────────────────────────────────────────────────────────────────

    /** Returns the full resolution record for a quest, or nullptr if the quest hasn't resolved this session. */
    const FQuestResolutionRecord* GetQuestResolution(FGameplayTag QuestTag) const;

    /** Convenience predicate: whether this quest has any resolution record this session. */
    bool HasResolved(FGameplayTag QuestTag) const;

    /**
     * Whether this quest has resolved with the specified OutcomeTag at any point this session. O(1) lookup against
     * a parallel index maintained alongside QuestResolutions; populated on every RecordResolution call. Works for
     * any OutcomeTag the quest has actually fired with, regardless of whether that outcome was registered at
     * compile time.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    bool HasResolvedWith(FGameplayTag QuestTag, FGameplayTag OutcomeTag) const;

	/**
	 * Whether this quest has resolved through the specified PathIdentity at any point this session. O(1) lookup
	 * against a parallel index maintained alongside QuestResolutions; populated on every RecordResolution call.
	 * Distinct from HasResolvedWith: that one is outcome-keyed (satisfies on any path producing the named
	 * outcome); this one is path-keyed (satisfies only when the named quest resolved through this specific
	 * authored path). Drives the runtime evaluation of Leaf_Path prereqs emitted from pin-wired prereq
	 * authoring — a designer wiring from a specific output pin gets a leaf that only this exact path satisfies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|State")
	bool HasResolvedAtPath(FGameplayTag QuestTag, FName PathIdentity) const;

	/**
	 * Returns the originating event identity recorded when the given path-mirror fact was written, or an invalid
	 * identity if no such fact is currently set. A node woken by a path-mirror fact uses this to recover the resolution
	 * identity the WorldState fact event itself cannot carry.
	 */
	FOriginatingEventID GetPathFactWriteEventID(FGameplayTag PathFactTag) const;

	/**
	 * Whether ANY quest has resolved with the specified OutcomeTag (or any descendant via gameplay-tag hierarchy)
	 * at any point this session. Context-free — no quest-tag scoping. Backs the runtime evaluation of Leaf_Outcome
	 * prereqs emitted from the declarative PrerequisiteOutcome authoring node. Hierarchy walk via FGameplayTag::
	 * MatchesTag mirrors the bus's hierarchical delivery semantics for outcome-channel publishes: a leaf subscribed
	 * at SimpleQuest.Outcome.Victory satisfies on both Outcome.Victory and any descendant like Outcome.Victory.Flawless.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|State")
	bool HasAnyQuestResolvedWith(FGameplayTag OutcomeTag) const;

    /** Convenience accessor: how many times this quest has resolved this session. */
    int32 GetResolutionCount(FGameplayTag QuestTag) const;

    /**
     * Returns the full chronological resolution history for a quest (every entry appended via RecordResolution).
     * Empty array if the quest hasn't resolved this session.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    TArray<FQuestResolutionEntry> GetResolutionHistory(FGameplayTag QuestTag) const;

    /** Returns the most recent resolution entry for a quest, or a default-constructed entry if no resolutions. */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    FQuestResolutionEntry GetLatestResolution(FGameplayTag QuestTag) const;

    // ── Past entry queries ───────────────────────────────────────────────────────────────────────────────

    /** Returns the full entry record for a quest, or nullptr if the quest hasn't been entered this session. */
    const FQuestEntryRecord* GetQuestEntry(FGameplayTag QuestTag) const;

    /** Convenience predicate: whether this quest has any entry record this session. */
    bool HasEntered(FGameplayTag QuestTag) const;

    /**
     * Whether this quest has been entered with the specified IncomingOutcomeTag at any point this session.
     * O(1) lookup against a parallel index maintained alongside QuestEntries. Used by Leaf_Entry prereqs
     * evaluating against this registry rather than against a WorldState fact.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    bool HasEnteredWith(FGameplayTag QuestTag, FGameplayTag IncomingOutcomeTag) const;

    /** Convenience accessor: how many times this quest has been entered this session. */
    int32 GetEntryCount(FGameplayTag QuestTag) const;

    /** Returns the full chronological entry history for a quest. Empty array if never entered this session. */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    TArray<FQuestEntryArrival> GetEntryHistory(FGameplayTag QuestTag) const;

    /** Returns the most recent entry for a quest, or a default-constructed entry if no entries. */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    FQuestEntryArrival GetLatestEntry(FGameplayTag QuestTag) const;


	// ── Quest registration + per-quest historical context ───────────────────────────────────────────────
	//
	// KnownQuests is the registry of quest tags the manager has registered this session via RegisterQuestTag
	// (called from RegisterQuestlineGraph). Keys answer "is this a known quest tag" and "what tags live under
	// this prefix" — the hierarchical catch-up entry point. Values hold quest-level historical context that
	// isn't tied to an individual start arrival (per-start detail lives on FQuestEntryArrival).

	/**
	 * Returns every known canonical quest tag whose canonical-or-alias perspective matches Prefix or is a
	 * descendant of Prefix. Used by hierarchical catch-up subscribers to enumerate descendants of a parent-
	 * prefix subscription. Empty result for an invalid Prefix or when no descendants are known.
	 *
	 * Aliases get resolved to their underlying canonicals before return, so callers receive a uniform
	 * canonical-tag set suitable for fact lookups (which are keyed by canonical) and instance lookups in
	 * LoadedNodeInstances. A subscriber binding to an alias-shape prefix (e.g. SimpleQuest.Questline.NewTest
	 * when NewTest is loaded only as inlined content under another asset's compile) gets the canonical tags
	 * of the inlined nodes whose alias arrays contain a descendant of Prefix — the bus's hierarchical-walk
	 * semantic, applied to the registered-tag set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|State")
	TArray<FGameplayTag> GetQuestTagsUnderPrefix(FGameplayTag Prefix) const;

	/**
	 * True if ContextualTag has been registered with the manager this session via RegisterQuestTag. Distinct from
	 * FQuestTagComposer::IsTagRegisteredInRuntime, which checks compile-time gameplay tag registration; this
	 * predicate answers "has the runtime instance been wired up."
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	bool IsKnownQuestTag(FGameplayTag QuestTag) const;

	/** Number of known quest tags this session. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	int32 GetKnownQuestTagCount() const;

	/** Returns the runtime record for ContextualTag, or nullptr if the tag isn't a known quest tag. */
	const FQuestRuntimeRecord* GetQuestRuntimeRecord(FGameplayTag QuestTag) const;

	/**
	 * The actor that initiated the most-recent start of this quest (UQuestStep::ReceivedActivationContext.Instigator
	 * captured at start time, preserved past the live step's deactivation). Null for non-Step starts (containers
	 * have no objective; no params snapshot) and for starts where no Instigator was supplied.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	AActor* GetLastGiverActor(FGameplayTag QuestTag) const;

	/**
	 * Provenance of the most-recent start of this quest. EQuestActivationProvenance::Unknown if the quest hasn't started
	 * this session or pre-dates Provenance stamping.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	EQuestActivationProvenance GetLastActivationProvenance(FGameplayTag QuestTag) const;

	/**
	 * By-value snapshot of the merged final FQuestObjectiveActivationContext delivered to the objective at the most-
	 * recent start (UQuestStep::ReceivedActivationContext). Default-constructed for non-Step starts and for quests that
	 * haven't started this session. Sufficient to reconstitute the live questline's objective state for save/load.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	FQuestObjectiveActivationContext GetLastActivationParamsSnapshot(FGameplayTag QuestTag) const;

	/**
	 * Per-source routing identity from the most-recent start. NAME_None for entry-tag fires and any start that didn't
	 * carry per-source routing.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	FName GetLastPathIdentity(FGameplayTag QuestTag) const;
	

    // ── Read-side enumeration (inspection-only) ──────────────────────────────────────────────────────────
    //
    // Exposes the underlying registry maps as const refs so editor / debug surfaces (Quest State facts panel,
    // future telemetry tools) can walk the full registry without per-quest probes. Mutation stays friend-
    // protected through UQuestManagerSubsystem; these accessors are read-only by const-ness.

    /** All resolved quests this session, keyed by quest tag. Values are append-only history records. */
    const TMap<FGameplayTag, FQuestResolutionRecord>& GetAllResolutions() const { return QuestResolutions; }

    /** All entered destination quests this session, keyed by destination quest tag. */
    const TMap<FGameplayTag, FQuestEntryRecord>& GetAllEntries() const { return QuestEntries; }

	/**
	 * All known quest tags this session, keyed by quest tag. The map's keys are the canonical answer to
	 * "what tags has the manager registered" — used by the hierarchical catch-up walk. Values hold per-quest
	 * historical context (RegisteredTime; future quest-level fields).
	 */
	const TMap<FGameplayTag, FQuestRuntimeRecord>& GetAllKnownQuests() const { return KnownQuests; }
	
    /** All quests currently in PendingGiver state with a cached prereq snapshot. Cleared on giver-state exit. */
    const TMap<FGameplayTag, FQuestPrereqStatus>& GetAllCachedPrereqStatus() const { return CachedPrereqStatus; }

    /**
     * Multicast fired after any mutation to the registry maps — RecordResolution, RecordEntry, UpdateQuest-
     * PrereqStatus, ClearQuestPrereqStatus. Distinct from the per-quest FQuestResolutionRecordedEvent / FQuest-
     * EntryRecordedEvent publishes used by prereq-leaf subscribers. This is a "registry mutated, refresh if
     * you care about the whole map" signal for inspection surfaces (Quest State Facts Panel, future telemetry
     * tools). Fires synchronously inside the mutation method, after the per-quest publish (if any).
     */
    FOnAnyRegistryChanged OnAnyRegistryChanged;


    // ── Present-tense activation queries ─────────────────────────────────────────────────────────────────

    /**
     * Returns the current set of activation blockers for ContextualTag — empty array means the quest is currently
     * startable. State-fact blockers (UnknownQuest, AlreadyLive, Blocked, Deactivated, NotPendingGiver) come
     * first; PrereqUnmet comes last with UnsatisfiedLeafTags populated. Computed from WorldState facts +
     * cached prereq status. Pure read; no manager interaction.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    TArray<FQuestActivationBlocker> QueryQuestActivationBlockers(FGameplayTag QuestTag) const;

    /**
     * Returns the cached prereq status for a quest in PendingGiver state. For quests not in PendingGiver
     * state (or with no cached entry), returns a default-constructed status (bIsAlways=true, bSatisfied=true).
     * Caller should branch on QueryQuestActivationBlockers' UnknownQuest / NotPendingGiver blockers to
     * disambiguate "no prereqs to worry about" from "quest isn't currently in giver state."
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    FQuestPrereqStatus GetQuestPrereqStatus(FGameplayTag QuestTag) const;

	/**
	 * Whether ContextualTag's runtime instance is a UQuest container (wrapper). False for Steps, utility nodes, and
	 * any tag the manager hasn't registered. Public read surface — used by the blocker query and any consumer
	 * that needs to know a tag's structural classification.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	bool IsContainerTag(FGameplayTag QuestTag) const;
	
	/**
	 * Translates an input tag to the list of canonical ContextualTag(s) it represents. Returns [InputTag] when
	 * InputTag is a ContextualTag (or unknown — defensive); returns the alias-mapped contextual list when
	 * InputTag is a registered AssetScopedAliasTag. Used by alias-aware predicate / aggregate read APIs and by
	 * any consumer that needs to enumerate the active placements behind a tag (debug surfaces, designer tools,
	 * future BP integrations).
	 *
	 * Pointer-returning APIs (GetQuestResolution, GetQuestEntry) and "latest" / "single instance" APIs
	 * (GetLatestResolution, GetLastGiverActor, etc.) intentionally stay direct-only — cross-asset semantics for
	 * "the single record" are ambiguous with multiple placements. Callers wanting cross-asset visibility use the
	 * predicate / aggregate APIs (HasResolvedWith, GetResolutionHistory, etc.).
	 */
	TArray<FGameplayTag> ResolveCanonicalTags(FGameplayTag InputTag) const;
	
	/**
	 * Returns the AssetScopedAliasTags registered for ContextualTag (the inverse of ResolveCanonicalTags's alias
	 * walk). Empty when ContextualTag is unknown or has no registered aliases (top-level content). Used by
	 * catch-up dispatchers to build the channel set [canonical, ...aliases] for matched-channel selection.
	 */
	TArray<FGameplayTag> GetAssetScopedAliasTagsForCanonical(FGameplayTag ContextualTag) const;

	// ── Source registry queries ──────────────────────────────────────────────────────────────────────────
	//
	// Component-driven self-registration of which Trigger / Giver / Observer instances in the world handle a given
	// quest tag. Powers the blocker-introspection loop: an adopter holding an FQuestActivationBlocker's leaf tag
	// can ask "which Giver actor offers this quest?" without maintaining a parallel tag → actor registry.
	//
	// Registration alias-walks (ResolveCanonicalTags) at write time so a Giver authored under an alias and queried
	// under canonical (or vice versa) surfaces correctly. Phase 1 returns live entries only (bIsActive=true);
	// Phase 2 (0.5.0) adds an editor-baked QuestSourcesManifest channel that populates AuthoredActorPath /
	// AuthoredLevel / AuthoredTransform for streamed-out content without changing the struct shape.

	/** Returns every currently-registered UQuestTriggerComponent (or subclass) whose StepTagsToTrigger covers QueryTag,
	 *  alias-walked. Empty when no triggers are registered against this tag or its aliases. */
	UFUNCTION(BlueprintCallable, Category = "Quest|Source")
	TArray<FQuestRoleSourceInfo> GetActiveTriggersForTag(FGameplayTag QueryTag) const;

	/** Returns every currently-registered UQuestGiverComponent (or subclass) whose QuestTagsToGive covers QueryTag,
	 *  alias-walked. Empty when no givers are registered against this tag or its aliases. */
	UFUNCTION(BlueprintCallable, Category = "Quest|Source")
	TArray<FQuestRoleSourceInfo> GetActiveGiversForTag(FGameplayTag QueryTag) const;

	/** Returns every currently-registered UQuestObserverComponent (or subclass) whose ObservedTags covers QueryTag,
	 *  alias-walked. Note: derived components (Trigger, Giver) also register under this role for tags they observe
	 *  via the implicit-observed bridge — a single Giver component watching its QuestTagsToGive surfaces under both
	 *  GetActiveGiversForTag (giver role) AND GetActiveObserversForTag (observer role on the same tags). */
	UFUNCTION(BlueprintCallable, Category = "Quest|Source")
	TArray<FQuestRoleSourceInfo> GetActiveObserversForTag(FGameplayTag QueryTag) const;

	/** Returns the currently-live UQuestObjective for the Step at QueryTag (alias-walked), or nullptr when no Step
	 *  is currently active under that tag or no Objective is bound. Steps self-register their LiveObjective on
	 *  activation and unregister on deactivation/completion. */
	UFUNCTION(BlueprintCallable, Category = "Quest|Source")
	UQuestObjective* GetActiveObjectiveForTag(FGameplayTag QueryTag) const;

	// ── Source registry writes (framework infrastructure) ─────────────────────────────────────────────────
	//
	// Components call these from BeginPlay (register) and EndPlay (unregister). Not BlueprintCallable —
	// designers don't drive the registry directly. Public-but-non-BP to avoid friend declarations across the
	// component hierarchy; the methods themselves are cheap no-ops when the input is empty or invalid.

	/** Registers Component as a Trigger source for every tag in AuthoredTags + each tag's canonical resolution.
	 *  Idempotent — repeat calls replace the prior entry for this Component. */
	void RegisterTriggerSource(UActorComponent* Component, const FGameplayTagContainer& AuthoredTags);

	/** Registers Component as a Giver source for every tag in AuthoredTags + each tag's canonical resolution. */
	void RegisterGiverSource(UActorComponent* Component, const FGameplayTagContainer& AuthoredTags);

	/** Registers Component as an Observer source for every tag in AuthoredTags + each tag's canonical resolution. */
	void RegisterObserverSource(UActorComponent* Component, const FGameplayTagContainer& AuthoredTags);
	
	/**
	 * Granular counterpart to UnregisterAllRoleSources: drops Component as a source for a SINGLE tag in the named
	 * role registry — for components whose watched-tag set changes at runtime. No-op if Component isn't registered
	 * for Tag. Pass the same (authored) tag form used at registration.
	 */
	void UnregisterTriggerSource(UActorComponent* Component, FGameplayTag Tag);
	void UnregisterGiverSource(UActorComponent* Component, FGameplayTag Tag);
	void UnregisterObserverSource(UActorComponent* Component, FGameplayTag Tag);

	/**
	 * Removes every per-role entry pointing at Component. Safe no-op when the component never registered. Called
	 * from component EndPlay — explicit removal keeps the registry compact across repeated activate/end cycles
	 * (weak-pointer queries already skip dead entries; this trims them up front).
	 */
	void UnregisterAllRoleSources(UActorComponent* Component);

	/**
	 * Registers Objective under each tag in TagSet (canonical + aliases). The latest registration for any given tag
	 * wins — a Step re-activating under the same tag replaces the prior entry.
	 */
	void RegisterActiveObjective(UQuestObjective* Objective, const TArray<FGameplayTag>& TagSet);

	/** Removes every entry pointing at Objective. Called from UQuestStep when LiveObjective transitions to null. */
	void UnregisterActiveObjective(UQuestObjective* Objective);

	// ── Display data queries ─────────────────────────────────────────────────────────────────────────────

	/**
	 * Returns the authored UI display name for a Questline / Quest / Step tag. Returns the authored value as-is —
	 * empty FText when the designer didn't author one. No silent fallback to NodeLabel or to a derived leaf-name
	 * reformat: empty means the designer chose not to pipeline display content for this tag, distinct from a
	 * missing-record bug.
	 *
	 * Returns empty FText for unknown tags + logs a Warning on LogSimpleQuestState — that's the loud-failure
	 * path for "tag may be unregistered or compile may have missed it." Empty-authored-content (record exists,
	 * DisplayName field intentionally blank) is silent.
	 *
	 * An alias-form tag yields the same result as its canonical: the registry stores a parallel record under the
	 * canonical tag AND every alias key at registration, so a direct lookup on any perspective hits without a
	 * runtime canonical walk.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|Display")
	FText GetDisplayName(FGameplayTag Tag) const;

	/**
	 * Returns the authored description for a Questline / Quest / Step tag. Empty FText when the designer didn't
	 * author one. Returns empty FText for unknown tags and logs a Warning on LogSimpleQuestState.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|Display")
	FText GetDisplayDescription(FGameplayTag Tag) const;

	/**
	 * Returns the optional richer-data asset referenced by a Questline / Quest / Step. Nullptr when the designer
	 * didn't reference one. Returns nullptr for unknown tags and logs a Warning on LogSimpleQuestState. Caller casts
	 * to the expected UQuestDisplayData subclass to read typed fields.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|Display")
	UQuestDisplayData* GetDisplayData(FGameplayTag Tag) const;

	// ── Snapshot ─────────────────────────────────────────────────────────────────────────────────────────

	/**
	 * Captures the two data layers — WorldState facts + the resolution/entry registries — into a serializable snapshot.
	 * Pure read. Lower-level C++ primitive: the Blueprint entry point is USimpleQuestBlueprintLibrary::CaptureQuestState,
	 * which pairs this with the active-graph list + deferred-activation set that restore needs. Native callers composing
	 * their own save flow may still call this directly.
	 */
	FSimpleQuestSaveSnapshot CaptureSnapshot() const;

	/**
	 * Restores the two data layers from a snapshot: bulk-sets WorldState, overwrites the registries, rebuilds the
	 * parallel indices, and fires the registry/fact "refresh" multicasts. Returns false only on an unrecoverable version.
	 * Restores DATA only — it does not rebuild live objectives or re-arm deferred activations. Lower-level C++ primitive:
	 * the Blueprint entry point is USimpleQuestBlueprintLibrary::ApplyQuestSnapshot, which pairs this with the stash the
	 * per-graph restore consumes. Native callers composing their own load flow may still call this directly.
	 */
	bool ApplySnapshot(const FSimpleQuestSaveSnapshot& Snapshot);

private:
    friend class UQuestManagerSubsystem;

    UPROPERTY()
    TMap<FGameplayTag, FQuestResolutionRecord> QuestResolutions;

    /**
     * Parallel O(1) index for HasResolvedWith. Maintained alongside QuestResolutions: every RecordResolution call
     * adds the (ContextualTag, OutcomeTag) pair to this map. Avoids walking History for outcome-keyed prereq queries.
     */
    TMap<FGameplayTag, TSet<FGameplayTag>> ResolvedOutcomesByQuest;

	/**
	 * Parallel O(1) index for HasResolvedAtPath. Maintained alongside QuestResolutions: every RecordResolution
	 * call adds the (ContextualTag, PathIdentity) pair to this map. Separate from ResolvedOutcomesByQuest because
	 * Path and Outcome are independently queryable concerns — a quest with two paths sharing an outcome will
	 * appear once in ResolvedOutcomesByQuest (under the shared outcome) but twice in ResolvedPathsByQuest (one
	 * entry per path). TSet handles deduplication for repeat resolutions through the same path.
	 */
	TMap<FGameplayTag, TSet<FName>> ResolvedPathsByQuest;

	/**
	 * Flat session-wide set of every outcome tag any quest has resolved with. Maintained alongside the per-quest
	 * ResolvedOutcomesByQuest map: RecordResolution inserts here too. Backs HasAnyQuestResolvedWith — the
	 * context-free outcome query that catch-up logic for Leaf_Outcome prereqs queries on subscribe.
	 *
	 * Hierarchy walk happens at query time (HasAnyQuestResolvedWith iterates and calls MatchesTag) rather than
	 * at storage time, so the set stays compact regardless of tag hierarchy depth.
	 */
	TSet<FGameplayTag> ResolvedOutcomes;

    UPROPERTY()
    TMap<FGameplayTag, FQuestEntryRecord> QuestEntries;

    /**
     * Parallel O(1) index for HasEnteredWith. Maintained alongside QuestEntries: every RecordEntry call
     * adds the (ContextualTag, IncomingOutcomeTag) pair. TSet handles deduplication so repeat entries with the
     * same outcome don't bloat the set.
     */
    TMap<FGameplayTag, TSet<FGameplayTag>> EnteredOutcomesByQuest;
    
    /** Cache of current prereq status per quest in PendingGiver state. Populated by the manager's giver branch
     *  and updated on enablement-watch transitions. Cleared when the quest leaves giver state. */
    TMap<FGameplayTag, FQuestPrereqStatus> CachedPrereqStatus;

    /** Manager calls these via friend access. */
    void RecordResolution(
    	FGameplayTag QuestTag,
    	FGameplayTag OutcomeTag,
    	FName PathIdentity,
    	double ResolutionTime,
    	EQuestResolutionSource Source,
    	const FOriginatingEventID& OriginatingEventID = {});

	/** Records the originating event identity for a path-mirror fact as it is written. Paired with ClearPathFactWriteEventID. */
	void StampPathFactWriteEventID(FGameplayTag PathFactTag, const FOriginatingEventID& EventID);

	/** Drops the recorded identity for a path-mirror fact when that fact is cleared, keeping the map bounded to live facts. */
	void ClearPathFactWriteEventID(FGameplayTag PathFactTag);
	
    void UpdateQuestPrereqStatus(FGameplayTag QuestTag, const FQuestPrereqStatus& Status);
    void ClearQuestPrereqStatus(FGameplayTag QuestTag);
    void RecordEntry(
	    FGameplayTag QuestTag,
	    FGameplayTag SourceQuestTag,
	    FGameplayTag IncomingOutcomeTag,
	    double EntryTime,
	    EQuestActivationProvenance Provenance,
	    const FQuestObjectiveActivationContext& ActivationParamsSnapshot,
	    FName PathIdentity,
	    const FOriginatingEventID& OriginatingEventID = {});

	/**
	 * Registers ContextualTag into KnownQuests with a default-constructed FQuestRuntimeRecord stamped with current world time.
	 * Idempotent — repeat calls on the same tag preserve the earliest RegisteredTime. Called from
	 * UQuestManagerSubsystem::RegisterQuestlineGraph for every valid resolved tag in the graph's compiled nodes.
	 */
	void RegisterQuestTag(FGameplayTag QuestTag);
	
    /** Resolves the GameInstance's WorldState subsystem for the blocker-fact lookups. */
    UWorldStateSubsystem* ResolveWorldState() const;
    
    /** Resolves the GameInstance's SignalSubsystem for publishing FQuestResolutionRecordedEvent on RecordResolution. */
    USignalSubsystem* ResolveSignalSubsystem() const;

	/**
	 * Pushed by the manager during graph activation: marks ContextualTag as a container (UQuest wrapper). Lets the
	 * blocker query distinguish Step-vs-container semantics for the AlreadyLive blocker without cross-subsystem
	 * coupling — containers' Live state is derived from inner Step state and shouldn't gate forward activation.
	 */
	void RegisterContainerTag(FGameplayTag QuestTag);

	/**
	 * Registers an AssetScopedAliasTag → ContextualTag mapping bidirectionally (forward + reverse index) AND registers the
	 * AssetScopedTag in KnownQuests as a perspective tag in its own right. Aliases are first-class perspectives for any-form
	 * queries; folding the KnownQuests registration in here enforces the invariant at the API boundary instead of relying
	 * on every caller to remember the pairing.
	 *
	 * Top-level content (where AssetScopedTag == ContextualTag) is a no-op — no aliasing needed; no double-registration of
	 * the same tag.
	 *
	 * Called by the manager during RegisterQuestlineGraph for each AssetScopedAliasTag carried by a registered instance.
	 */
	void RegisterAlias(FGameplayTag AssetScopedTag, FGameplayTag ContextualTag);

	/**
	 * Set of compiled QuestTags whose runtime instance is a UQuest container. Populated by the manager during
	 * ActivateQuestlineGraph; read by QueryQuestActivationBlockers. Persists with the subsystem instance.
	 */
	TSet<FGameplayTag> ContainerTags;
	
	/** All quest tags registered this session, mapped to their per-quest runtime record. The key set answers
	 *  GetQuestTagsUnderPrefix for hierarchical catch-up; the value set holds quest-level historical context
	 *  (RegisteredTime today; future quest-level fields land here without restructuring). Manager pushes via
	 *  RegisterQuestTag during RegisterQuestlineGraph. */
	UPROPERTY()
	TMap<FGameplayTag, FQuestRuntimeRecord> KnownQuests;

	/**
	 * Forward alias index — AssetScopedAliasTag → list of ContextualTags it aliases. Multiple ContextualTags
	 * may share the same alias when a linked asset is placed multiple times across the project (each placement
	 * gets its own ContextualTag; they share their inner asset's StandaloneTag-shape alias). Read by the alias-
	 * walk in ResolveCanonicalTags + GetQuestTagsUnderPrefix.
	 */
	TMap<FGameplayTag, TArray<FGameplayTag>> ContextualTagsByAssetScopedTag;

	/**
	 * Reverse alias index — ContextualTag → list of AssetScopedAliasTags. Empty for top-level content (no
	 * LinkedQuestline ancestors). Read by RecordResolution / RecordEntry's multi-publish so cross-asset
	 * subscribers receive the fact-mutation events on their bound alias channels.
	 */
	TMap<FGameplayTag, TArray<FGameplayTag>> AssetScopedAliasTagsByContextualTag;

	/**
	 * Calls Op once per perspective for CanonicalTag: the canonical itself, then each AssetScopedAliasTag the
	 * alias index has registered for it. Iteration is silent when no aliases exist (top-level content). Used by
	 * the multi-write mutators (RecordResolution / RecordEntry / UpdateQuestPrereqStatus / ClearQuestPrereqStatus)
	 * so registry maps stay symmetric with the WorldState multi-perspective fact-write model and the bus's
	 * multi-channel publish — query and iteration from any perspective surface the same data without alias-
	 * walking at every read site.
	 */
	template<typename TFunc>
	void ForEachPerspective(FGameplayTag CanonicalTag, TFunc&& Op) const
	{
		if (!CanonicalTag.IsValid()) return;
		Op(CanonicalTag);

		if (const TArray<FGameplayTag>* Aliases = AssetScopedAliasTagsByContextualTag.Find(CanonicalTag))
		{
			for (const FGameplayTag& AliasTag : *Aliases)
			{
				if (AliasTag.IsValid() && AliasTag != CanonicalTag)
				{
					Op(AliasTag);
				}
			}
		}
	}
	
	/**
	 * Per-tag display-data records keyed by registered tag. Manager registers under canonical + every alias key on
	 * graph load (mirrors the LoadedNodeInstances multi-key registration invariant) so query-side direct lookup works
	 * regardless of which perspective the caller passes. Cleared on graph unregister + PIE reset.
	 */
	UPROPERTY()
	TMap<FGameplayTag, FQuestDisplayDataRecord> DisplayDataByTag;

	/**
	 * Friend-only write: store display data for a single tag. Called by manager during graph registration, once per
	 * tag perspective (canonical + each alias). Replaces any existing record under the same key.
	 */
	void RegisterDisplayData(FGameplayTag Tag, const FText& InDisplayName, const FText& InDescription, UQuestDisplayData* InDisplayData);

	/**
	 * Friend-only write: clear all display-data records associated with a graph's tag set. Called on graph unregister.
	 * Caller passes the full list of perspectives (canonical + aliases) the graph contributed.
	 */
	void UnregisterDisplayDataForTags(const TArray<FGameplayTag>& Tags);

	/** Friend-only: clear DisplayDataByTag entirely. Called on PIE reset alongside the existing transient-state clear. */
	void ClearDisplayDataRegistry();

	/**
	 * Per-role source registries — TMap<TagKey, TArray<TWeakObjectPtr<UActorComponent>>>. Keys cover both authored
	 * and canonical forms (registration alias-walks at write time via ResolveCanonicalTags so query-time lookup is
	 * direct). Weak pointers ensure GC'd actors don't pollute results; explicit cleanup via UnregisterAllRoleSources
	 * keeps the registry compact across repeated component activate/end cycles.
	 */
	TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>> TriggerSourcesByTag;
	TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>> GiverSourcesByTag;
	TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>> ObserverSourcesByTag;

	/**
	 * Live-objective registry — one entry per (canonical or alias) tag pointing at the Step's bound LiveObjective.
	 * Steps self-register from ActivateInternal and unregister from DeactivateInternal / OnObjectiveComplete /
	 * ResetTransientState.
	 */
	TMap<FGameplayTag, TWeakObjectPtr<UQuestObjective>> ActiveObjectivesByTag;

	/**
	 * Shared body for the three Get*ForTag query methods. Alias-walks QueryTag, looks up against the per-role map,
	 * filters dead weak refs, builds Phase-1-shaped FQuestRoleSourceInfo entries (bIsActive=true; authored fields
	 * default). MatchedVia stamps the tag this query matched on (canonical or alias).
	 */
	TArray<FQuestRoleSourceInfo> QueryRoleSources(
		FGameplayTag QueryTag,
		const TMap<FGameplayTag,
		TArray<TWeakObjectPtr<UActorComponent>>>& SourceMap) const;

	/**
	 * Shared body for the three Register*Source methods. Iterates AuthoredTags, resolves canonical for each, adds
	 * Component under every distinct key (deduped). Replaces any prior entry for the same Component to keep
	 * Idempotent semantics.
	 */
    static void RegisterRoleSource(
		UActorComponent* Component,
		const FGameplayTagContainer& AuthoredTags,
		TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>>& SourceMap);

	static void UnregisterRoleSource(
		UActorComponent* Component,
		FGameplayTag Tag,
		TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>>& SourceMap);

	/**
	 * Builds the full synonym set for QueryTag: the input + every canonical it alias-walks to + every alias each of
	 * those canonicals fans out to. Catches every perspective form a component / objective could have been registered
	 * under regardless of register-time alias-index state — components register at BeginPlay but graphs register
	 * lazily via WarmReachableGraphs, so register-time canonical walks may miss aliases that hadn't been registered yet.
	 * Order: input first, then canonicals, then aliases. Each entry unique.
	 */
	TArray<FGameplayTag> BuildTagSynonymSet(FGameplayTag QueryTag) const;

	/**
	 * Maps a resettable-replay path-mirror fact tag to the originating event identity of the resolution that wrote it.
	 * A WorldState fact-added event carries no quest identity, so a node woken by one of these mirror facts cannot tell
	 * which resolution cascade triggered it. The Prerequisite Gate needs that identity to recognize when one resolution
	 * has reached it through both its prerequisite and its direct Enter wire, and collapse the two arrivals into a single
	 * fire. The manager stamps it as each mirror fact is written; a fact-woken node reads it back. Entries are dropped
	 * when the mirror is cleared, so the map only ever holds currently-set mirror facts.
	 */
	TMap<FGameplayTag, FOriginatingEventID> PathFactWriteEventIDs;

	/**
	 * Rebuilds ResolvedOutcomesByQuest / ResolvedPathsByQuest / ResolvedOutcomes / EnteredOutcomesByQuest from
	 * the restored histories — mirrors RecordResolution / RecordEntry's per-perspective index maintenance.
	 */
	void RebuildRegistryIndices();
};