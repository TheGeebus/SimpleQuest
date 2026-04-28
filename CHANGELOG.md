# Changelog

All notable changes to SimpleQuest will be documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Upcoming
- Two-layer state pattern-establishing pass — extends the registry
  pattern formalized in 0.4.0 (`UQuestStateSubsystem` + manager-as-
  orchestrator) downward to step-level records and relocates
  transient `PendingActivationParams` from per-component fields to
  manager-side registries. Direct save/load groundwork.
- Runtime asset loading pass (0.5.0 — async graph load driven by
  signal subscription; prioritized synchronous fallback for
  critical paths). Lays groundwork for save/load by ensuring the
  right graph assets can come online from a save's tag references
  at restore time.
- Save/load system (0.6.0 — locks tag string format; the
  `SimpleQuest.*` namespace consolidation and state-registry split
  shipped in 0.4.0 are the pre-flight for this).

### Active Development
- Event-driven LinkedQuestline ref-index cache (incremental cross-asset
  dependency tracking to replace current Asset Registry scans on compile)

---

## [0.4.0] — TBD — Quest Lifecycle Architecture + Tag Namespace Consolidation

A multi-theme architectural pass that lays the foundations for the
0.6.0 save/load work by locking down tag identity, splitting
orchestration from fact-registry read access, and rebuilding the
quest event lifecycle around an Activated / Enabled / Started /
GiveBlocked vocabulary that lets designer-side UI choose between
"I might offer this soon" affordance and "I can actually offer this
right now" affordance without writing state-query boilerplate inside
every event handler.

Three intertwined themes:

1. **Tag namespace consolidation under `SimpleQuest.*`** — every
   plugin-introduced gameplay tag now lives under a single parent.
   Designers opening the Gameplay Tag Manager see a clean separation
   between plugin tags and game-authored tags. Locks tag string
   identity before the save format freezes in 0.6.0.

2. **Quest event lifecycle redesign** — `Activated` (execution reached
   this giver-gated quest, with prereq snapshot) and `Enabled`
   (genuinely accept-ready) are now distinct events; `GiveBlocked`
   is the structured "no" partner to `Started`'s "yes"; a
   `QueryQuestActivationBlockers` BP-callable query API surfaces
   the same blocker structure synchronously for greyed-out / locked-
   icon UI.

3. **Manager-orchestrator + state-registry split** — the manager
   subsystem is now a black-box orchestrator with no public read API.
   All public read access lives on `UQuestStateSubsystem` (renamed
   from `UQuestResolutionSubsystem` per the new "...StateSubsystem"
   convention — externally accessible fact registry with potentially
   limited write access; mirrors `UWorldStateSubsystem` across the
   suite).

Plus the supporting renames and surface refreshes that fall out of
those themes: **Active → Live** state rename across runtime + editor
(frees activation vocabulary for the new lifecycle event without
forcing past-tense "Started" as a state name); **BindToQuestEvent**
K2 node expansion to expose all 9 lifecycle events with per-instance
pin exposure via right-click context menu and mask-gated proxy
subscriptions; six discrete BP delegates on `UQuestGiverComponent`
matching the new lifecycle granularity.

### Tag namespace migration table

Six namespace roots migrated:

| Was | Now |
|---|---|
| `Quest.*` (quest identifiers) | `SimpleQuest.Quest.*` |
| `Quest.Outcome.*` (named outcomes) | `SimpleQuest.QuestOutcome.*` *(promoted out of Quest — outcomes are decorators, not lifecycle events)* |
| `QuestState.*` (state facts) | `SimpleQuest.QuestState.*` |
| `QuestPrereqRule.*` | `SimpleQuest.QuestPrereqRule.*` |
| `QuestActivationGroup.*` | `SimpleQuest.QuestActivationGroup.*` |
| `Quest.Channel.*` (signal-bus channels) | `SimpleQuest.QuestChannel.*` |

The 0.3.4 stale-tag commandlet was the pre-flight + post-flight
verification surface — the post-rename scan came back **byte-identical**
to the pre-rename baseline, confirming the redirect chain caught every
reference the migration touched.

A comprehensive `GameplayTagRedirects` block in
`Config/DefaultGameplayTags.ini` covers every namespace root and every
seeded child tag, ensuring authored references in existing project
assets migrate transparently at load time. Designers don't need to
manually rewire tag pickers — the redirect chain rewrites old tag
references on load and persists the new namespace on next save (BP
CDOs and other UPROPERTY-stored `FGameplayTag` fields automatically;
questline asset internals via the existing **Compile All Questlines**
toolbar action).

Also folded in: a pre-existing bug fix where prerequisites on
`UQuestlineNode_LinkedQuestline` nodes were never compiled into the
runtime — the compiler's `CompileOutputWiring` skip for LinkedQuestline
nodes (added to bypass output-wiring logic that depends on the node's
own title formula) inadvertently swept the prereq compilation block
along with it. Lifted the prereq compilation above the skip; prereq
gating on LinkedQuestline nodes now works as designers expect.

### Added

#### `SimpleQuest.*` namespace as the new tag tree root
- All plugin-introduced gameplay tags now live under a single
  `SimpleQuest.*` parent. Picker filters set on `Categories="SimpleQuest.Quest"`,
  `Categories="SimpleQuest.QuestOutcome"`, etc. scope correctly to the
  new tree.
- The compiler emits node identifier tags as `SimpleQuest.Quest.<QuestlineID>.<NodeName>`;
  state facts as `SimpleQuest.QuestState.<...>.Live` /
  `.Completed` / `.PendingGiver` / `.Deactivated` / `.Blocked`;
  per-node outcome facts as `SimpleQuest.QuestState.<...>.Outcome.<leaf>`.
- `FNativeGameplayTag` registration uses `SimpleQuest.SimpleQuest`
  module + plugin names, consistent with the new top-level identity.

#### `GameplayTagRedirects` block in `Config/DefaultGameplayTags.ini`
- Every namespace root has an explicit redirect:
  `Quest` → `SimpleQuest.Quest`, `Quest.Outcome` → `SimpleQuest.QuestOutcome`,
  `QuestState` → `SimpleQuest.QuestState`, `QuestPrereqRule` →
  `SimpleQuest.QuestPrereqRule`, `QuestActivationGroup` →
  `SimpleQuest.QuestActivationGroup`, `Quest.Channel` →
  `SimpleQuest.QuestChannel`.
- Every seeded child tag in the prior `DefaultGameplayTags.ini` block
  has its own explicit redirect (UE's tag redirects are exact-match
  per tag string; children of a redirected parent are not auto-rewritten).
- Every `UE_DEFINE_GAMEPLAY_TAG`-defined channel and example outcome
  also has an explicit redirect (defensive — the C++ definitions
  were updated to the new names but any external BP that referenced
  them by string still migrates cleanly).
- The pre-existing `QuestPrereqGroup → QuestPrereqRule` chain
  (from a prior rename) was updated to point directly at
  `SimpleQuest.QuestPrereqRule.*` rather than collapsing through
  the intermediate.

#### Defensive guards in `WriteCompiledTagsIni` / `RebuildNativeTags`
- Skip `FName::IsNone()` entries at the top of the per-tag loop —
  prevents the "None.Live" / "None.Blocked" leak into
  `CompiledTags.ini` that was caused by upstream NAME_None entries
  in cached `CompiledQuestTags` AR metadata.
- Recognize the legacy `"Quest.Outcome."` prefix (in addition to the
  new `"SimpleQuest.QuestOutcome."`) as a transitional safeguard.
  Stale AR metadata that hadn't been recompiled since the rename was
  state-fact-expanding outcome tags into garbage entries before this
  guard. Marked for removal in 0.4.1 once we're confident every
  authored asset has been touched.

#### Quest event lifecycle redesign
- **`FQuestActivatedEvent`** — fires once per giver-gated quest the
  moment its activation wire arrives. Always fires regardless of prereq
  state — designer-side UI uses the carried `FQuestPrereqStatus`
  payload (overall + per-leaf evaluation) to decide whether to show
  a locked-icon indicator. Non-giver quests skip this event and
  transition straight to `FQuestStartedEvent`.
- **`FQuestEnabledEvent`** — semantic refinement to "accept-ready":
  fires once when both `Activated` AND prereqs satisfy. Fires same
  tick as `Activated` if prereqs already met, or later when leaf
  facts flip. Pairs naturally with the "designer opts out of UI
  until prereqs fulfilled" idiom.
- **`FQuestDisabledEvent`** — symmetric partner to `FQuestEnabledEvent`.
  Fires when a quest that was Enabled returns to non-accept-ready
  state (e.g. NOT-prereq edge case where a leaf fact unsets after
  satisfying the expression).
- **`FQuestGiveBlockedEvent`** — refused-give response carrying a
  structured `TArray<FQuestActivationBlocker>` and the giver actor
  that initiated the attempt. Subscription model is symmetric with
  the rest of the event surface — global subscribers (debug
  overlays, telemetry, party-shared UI) opt in to the channel
  without coupling to the giver-side flow.
- **`FQuestActivationBlocker`** — structured "why can't this start"
  entry. `EQuestActivationBlocker` enum reasons: `PrereqUnmet`
  (with `UnsatisfiedLeafTags`), `Blocked`, `Deactivated`,
  `AlreadyLive`, `NotPendingGiver`, `UnknownQuest`.
- **`FQuestPrereqStatus` / `FQuestPrereqLeafStatus`** — overall +
  per-leaf evaluation snapshots. `FPrerequisiteExpression::EvaluateWithLeafStatus`
  is the new single-pass walker that produces them; the existing
  `Evaluate` path remains for boolean-only consumers.
- **`GiverActor` field on `FQuestStartedEvent`** — giver-given
  quests carry the offering actor in the event payload so subscribers
  can correlate which NPC initiated the chain. Catch-up subscribers
  receive `nullptr` for this field — the manager's transient bridge
  isn't preserved across late binding (see "Richer historical context
  on UQuestStateSubsystem" parking-lot capture in the project notes
  for the natural extension surface here).

#### Manager-orchestrator + state-registry split
- **`UQuestStateSubsystem`** (renamed from `UQuestResolutionSubsystem`).
  Naming convention shift: "...StateSubsystem" signals an externally
  accessible fact registry with potentially limited write access;
  mirrors `UWorldStateSubsystem` across the suite. Future suite
  plugins (dialogue, progression) will follow the same convention.
- **`QueryQuestActivationBlockers(QuestTag) -> TArray<FQuestActivationBlocker>`**
  on `UQuestStateSubsystem`, BP-callable. Empty array → currently
  startable; non-empty → one entry per distinct blocker. Same private
  helper feeds both this synchronous query and the gate inside
  `HandleGiveQuestEvent` that publishes `FQuestGiveBlockedEvent` —
  single-source so the two paths can never drift. Folds in **Item 23**
  (Giver "why can't activate" query API) from the long-pending TODO.
- **`GetQuestPrereqStatus(QuestTag) -> FQuestPrereqStatus`** —
  accessor for the prereq snapshot stored at activation time. BP-
  callable.
- Friend-class enforced single-write discipline on the registry —
  `UpdateQuestPrereqStatus` / `ClearQuestPrereqStatus` are gated by
  `friend class UQuestManagerSubsystem` so only the orchestrator can
  push facts into the registry. Preserves the manager's black-box
  doctrine while exposing the read API publicly.

#### `UQuestGiverComponent` BP delegates — six discrete events
Matches the new lifecycle granularity. Designer-facing names:
- **`OnQuestActivated(QuestTag, PrereqStatus)`** — proactive UI on
  first wire arrival, with prereq snapshot for locked/unlocked styling.
- **`OnQuestEnabled(QuestTag)`** — fires on accept-ready transition.
- **`OnQuestDisabled(QuestTag)`** — symmetric partner to Enabled.
- **`OnQuestStarted(QuestTag, GiverActor)`** — the moment of
  acceptance.
- **`OnQuestDeactivated(QuestTag)`** — mid-lifecycle interrupt.
- **`OnQuestGiveBlocked(QuestTag, Blockers)`** — reactive refusal
  handling. Bind a dialogue widget here and you get contextual
  "go talk to the innkeeper" responses for free, no
  `QueryQuestActivationBlockers` call required from the designer.
- **`ActivatedQuestTags`** container parallel to `EnabledQuestTags`,
  surfacing the proactive set for UI iteration.
- **`QueryActivationBlockers(QuestTag)`** BP method — convenience
  wrapper that delegates to the state subsystem.

#### `BindToQuestEvent` K2 node — full lifecycle exposure + per-instance configurability
- **9 lifecycle event pins** in chronological lifecycle order:
  `On Activated`, `On Enabled`, `On Give Blocked`, `On Started`,
  `On Progress`, `On Completed`, `On Deactivated`, `On Blocked`.
  (Plus the always-on `Subscription` reference output.)
- **Per-instance pin exposure** via right-click context menu —
  designer toggles individual pins on/off per node placement, three
  sub-sections matching lifecycle phase (Offer / Run / End). Reduces
  visual clutter; a node that only listens to OnCompleted shows just
  OnCompleted. `ReconstructNode` preserves saved pin connections by
  name across the disappear → reappear cycle so toggling pins off
  and back on rejoins existing wires automatically.
- **`EQuestEventTypes` UENUM(Bitflags)** — bitmask vocabulary for
  the proxy-side subscription mask. `K2Node_BindToQuestEvent` computes
  the mask from per-event `bExpose*` UPROPERTYs and stuffs it into
  the factory call; the proxy gates `SubscribeMessage` and
  `RunCatchUp` by mask. Result: a node with only OnStarted exposed
  only subscribes to `FQuestStartedEvent` on the SignalSubsystem —
  no waste.
- **`AsyncTask` → `Subscription` rename** on the proxy reference
  output pin. Polished async action nodes typically override the
  `UBlueprintAsyncActionBase` default with something that names
  what the object IS (Download, Login, etc.); "Subscription" is
  the natural fit for our proxy. Existing nodes with the old
  pin have their wire orphaned on first reload (acceptable
  pre-release migration cost).
- **Defensive pin creation** for `OutcomeTag`, `PrereqStatus`,
  `Blockers`, `GiverActor` — pins that belong only to one or two
  delegate paths get created from a single union of the relevant
  exposure bools, so they're discoverable regardless of which event
  is exposed.
- **Tooltip refresh** — every pin tooltip rewritten to BP convention
  (Pin Label / Type Description / Description prose). Outcome Tag
  description corrected against the 0.3.3 registry-recovery reality
  (catch-up returns the recovered outcome via the registry; only
  EmptyTag when no record exists for the quest in the current session).

### Changed

- **`FQuestStateTagUtils::Namespace`** constant migrated from
  `"QuestState."` to `"SimpleQuest.QuestState."`. Every `MakeStateFact`
  / `MakeOutcomeFact` / `MakeNodeOutcomeFact` / `MakeEntryOutcomeFact`
  call inherits the new prefix; the `StartsWith` / `Mid` pairs adjusted
  for the new prefix length (6 → 18). Doc-comment fact-shape examples
  updated.
- **`UE_DEFINE_GAMEPLAY_TAG` channel + outcome literals** updated:
  six signal-bus channel `.cpp` files in `Plugins/SimpleQuest/Source/SimpleQuest/Private/Events/`
  and two example objective outcome `.cpp` files in
  `Plugins/SimpleQuest/Source/SimpleQuest/Private/Objectives/Examples/`.
- **`UPROPERTY meta=(Categories=...)`** filters across 11 declarations
  (component classes, content nodes, K2 nodes, spec structs, example
  objectives) updated to scope to the new namespaces.
- **`GetTagFilterString()`** virtual returns on portal node headers (4
  classes, 6 returns) updated.
- **`SGameplayTagCombo::Filter`** strings on the inline outcome pickers
  in `SGraphNode_Exit` and `SGraphNode_CompleteObjective` updated.
- **Compiler tag composition** in `QuestlineGraphCompiler` and
  `SimpleQuestEditorUtils` updated at every `TEXT("Quest.")` /
  `TEXT("Quest.Outcome.")` literal site. The `Quest.Outcome.` outcome
  prefix constant in the inline-outcome-label parser was specifically
  rewritten as `SimpleQuest.QuestOutcome.` with matching offset
  arithmetic.
- **Prereq examiner contextual rewrite** in
  `QuestPIEDebugChannel.cpp` updated `Mid(6)` → `Mid(18)` at two
  call sites to strip the new `"SimpleQuest.Quest."` prefix length
  rather than the legacy `"Quest."` length. Without this fix, the
  examiner's contextual-rewrite for nested LinkedQuestline graphs
  produced malformed tag names that never matched WorldState facts —
  silent breakage where the leaf-state coloring stayed Unknown.

#### Active → Live state rename
- WorldState fact: `SimpleQuest.QuestState.<X>.Active` → `...Live`.
  Frees the activation vocabulary for the new `FQuestActivatedEvent`
  without forcing the awkward past-tense "Started" as a state name.
  "Live" is idiomatic English for an ongoing condition AND carries
  useful graph-system connotation (a live wire carries current).
- Constant: `FQuestStateTagUtils::Leaf_Active` → `Leaf_Live` (= "Live").
- Method: `SetQuestActive` → `SetQuestLive`.
- Container: `ActiveStepTriggerHandles` → `LiveStepTriggerHandles`
  (and the dead `ActiveClassTriggerHandles` deleted).
- Delegate: `OnNodeActivated` → `OnNodeStarted` (matches the existing
  `FQuestStartedEvent` name; past-tense for the moment of transition).
- Field: `UQuestStep::ActiveObjective` → `LiveObjective`.
- Accessor: `GetActiveObjective` → `GetLiveObjective`.
- BP API: `USimpleQuestBlueprintLibrary::IsQuestActive` → `IsQuestLive`.
- Enum value: `EQuestNodeDebugState::Active` → `::Live`.

#### State subsystem rename + read-API split
- **`UQuestResolutionSubsystem` → `UQuestStateSubsystem`.** Naming
  shift signals that this is an externally accessible fact registry
  (read API public; write API gated by `friend class UQuestManagerSubsystem`),
  matching `UWorldStateSubsystem` across the suite.
- **`UQuestManagerSubsystem` public read API removed.** Manager is
  now a black-box orchestrator; consumers query the state subsystem
  for resolution records, prereq status, and activation blockers.
  This codifies the manager-orchestrator + state-registry pattern as
  a project-wide convention — see project notes for the architectural
  framing (`overview.txt` §"Orchestrators and Fact Registries" plus
  `notes-product-vision.txt` §"Architectural Pattern — Orchestrators
  and Fact Registries").
- **`FQuestEnabledEvent` semantic refinement** — fires once on
  accept-ready transition rather than on first wire arrival.
  Previous semantics moved to `FQuestActivatedEvent`. Existing
  consumers that bound `OnEnabled` for the "first notice" affordance
  should rewire to `OnActivated`.

### Fixed

- **Prereqs on `UQuestlineNode_LinkedQuestline` nodes never compiled
  into runtime instances.** A pre-existing bug, surfaced during
  0.4.0 testing. The compiler's `CompileOutputWiring` had
  `if (Cast<UQuestlineNode_LinkedQuestline>(ContentNode)) continue;`
  at line 641 to bypass output-wiring logic that depends on the
  node's own title formula (LinkedQuestline labels come from the
  linked asset, not the node's title). The prereq compilation
  block at line 770–776 was inside that skipped loop body, so
  LinkedQuestline nodes never had `Instance->PrerequisiteExpression`
  populated — the runtime check at `QuestManagerSubsystem.cpp:140`
  short-circuited on `IsAlways()` returning true, and prereqs on
  LinkedQuestline nodes were silently bypassed. Lifted the prereq
  block above the LinkedQuestline skip; output-wiring +
  `bCompletesParentGraph` stay below the skip per the existing
  intent.

- **Giver-gated quest prereq evaluation gap.** Three runtime sites
  collectively bypassed prereq evaluation for giver-gated quests:
  `ActivateNodeByTag`'s giver branch published `FQuestEnabledEvent`
  with no prereq check; `UQuestGiverComponent::CanGiveAnyQuests`
  returned `!EnabledQuestTags.IsEmpty()` with no prereq check; and
  the manager's prereq checks short-circuited on
  `!Step->IsGiverGated()`, explicitly trusting the giver to have
  done the prereq check (which it never did). The lifecycle redesign
  closes this gap: the giver lights up immediately on first wire
  arrival regardless of prereq state (via `FQuestActivatedEvent`
  with the prereq snapshot for designer-side UI decisions);
  acceptance is the gate that protects "started" (the manager
  refuses `GiveQuestByTag` if blockers exist and publishes
  `FQuestGiveBlockedEvent` with the structured blocker info instead
  of mutating state).

### Removed

- Anonymous-namespace `ScanActorForStaleTags` reverted alias from
  pre-namespace cleanup pass (already removed in 0.3.5; noted here
  for symmetry with the broader migration story).

### Known Limitations

- **Inert legacy tag string fragments persist in some questline
  `.uasset` binaries.** Functionally inert — `CompiledTags.ini` is
  fully `SimpleQuest.*` (524 entries, 0 legacy), runtime tag
  registry is fully migrated, post-flight scan matches pre-flight
  byte-for-byte. The leftover bytes are in stale FName tables or
  serialization buffers (UE asset serialization sometimes appends
  rather than replaces). They `grep` but don't affect runtime
  behavior; will compress out via natural authoring activity over
  time. BP CDO assets cleaned fully via `ResavePackages`; questline
  binary fragments are aesthetic, not load-bearing.

### Internal — audit playbook updates

For the next namespace operation:

- Grep for `UE_DEFINE_GAMEPLAY_TAG` macros explicitly, not just
  string literals — that's how `Quest.Channel.*` (signal-bus
  channels) and the example objective outcomes (`Quest.Outcome.Reached`,
  `Quest.Outcome.TargetKilled`) were missed in the original audit
  and surfaced one at a time as user-visible leaks during Pass A.
- Grep for `Mid(<small_int>)` byte-offset patterns near tag-string
  operations. Length-dependent strip operations need updating when
  prefix lengths change; the prereq examiner's contextual-rewrite at
  `QuestPIEDebugChannel.cpp:225,229` silently broke until manually
  surfaced via designer testing (linked-graph examiner stayed Unknown).

---

## [0.3.5] — 2026-04-25 — Stale Quest Tags Polish

A polish pass on the Stale Quest Tags panel that shipped a few hours
earlier in 0.3.4, plus designer-facing log clarity improvements across
`BindToQuestEvent`, the resolution subsystem, and the panel's own UX
failure paths. The panel gains **multi-row mass-clear** with a per-
source confirmation breakdown and atomic undo (single transaction
wraps the batch), a **sortable + filterable Level column** with per-
source semantics, and **near-instant undo** on cleared instance
entries — was multi-second on Full Project Scan history; now sub-
millisecond regardless of scan history. The Refresh button is
renamed **Scan Loaded** for symmetric pairing with Full Project Scan,
and the Full Project Scan progress bar now advances on completion
instead of jumping to 100% before the work begins.

### Added

#### Stale Quest Tags panel — multi-row mass-clear
- **Multi-row selection** (`ESelectionMode::Multi`) on the SListView.
  Standard Ctrl+Click / Shift+Click multi-select gestures
- New header button **"Clear Selected (N)"** with live label binding
  on `ListView->GetNumItemsSelected()` and `IsEnabled` bound to the
  same count. Disabled when nothing is selected
- Confirmation dialog with **per-source breakdown**: shows count of
  Loaded / BP CDO / Unloaded entries in the selection plus the
  number of unique packages affected, before any mutation
- **Single `FScopedTransaction`** wraps the entire batch — Ctrl+Z
  restores all Loaded + Unloaded clears as one atomic operation
- `ClearOneEntry` helper extracted from `HandleClearClicked` so
  single-row and bulk paths share the source-specific dirty-resolution
  logic (Loaded → `MarkPackageDirty` on actor; BP CDO → outer-chain
  walk to UBlueprint + `MarkBlueprintAsModified`; Unloaded →
  `MarkPackageDirty` on the unloaded-level actor)

#### Stale Quest Tags panel — Level column
- New sortable + filterable **Level** column between Source and
  Actor (140px fixed width)
- Per-source display: leaf umap name (via `FPackageName::GetShortName`)
  with full-path tooltip on hover for Loaded / Unloaded entries; em-
  dash `—` with muted color and "Not applicable" tooltip for BP CDO
  entries (BP defaults aren't level-bound)
- Underlying sort/filter value is the full umap path for Loaded /
  Unloaded and empty for BP CDO — typing partial path text matches
  level rows but never matches BP CDO rows; em-dash entries cluster
  at one end of any Level sort
- Backend reuses `FStaleQuestTagEntry::PackagePath` (already populated
  for all three sources by the Tier 2 scanner); no schema change needed

#### Stale Quest Tags panel — fast PostUndo via per-actor targeted rescan
- New `ActorsTouchedByClear` map (per-actor `Source` + `PackagePath`)
  populated on every Clear (single + bulk). BP CDO entries opt out —
  their "actor" is a CDO, not a level instance
- New `UpdateFromAffectedActors` method drops `AllEntries` for tracked
  actors then re-emits via `ScanActorForStaleTags`
- `PostUndo` / `PostRedo` call `UpdateFromAffectedActors` instead of
  `Refresh(LastScope)`. Net effect: undo of a cleared instance entry
  is now sub-millisecond regardless of `LastScope`. Sibling entries
  on the same actor are preserved automatically because the rescan
  emits all stale tags fresh with valid weak ptrs

#### Stale Quest Tags panel — designer-visible BP CDO permanence warnings
- **Per-row Clear tooltip** on BP CDO entries calls out "Cannot be
  undone — the mutation propagates permanently to the Blueprint's
  class state. Use the Blueprint editor to manually re-add the tag if
  needed."
- **Clear Selected (N) header button tooltip** distinguishes Loaded /
  Unloaded undo (Ctrl+Z works, single transaction wraps the batch)
  from BP CDO permanence (Ctrl+Z is a no-op for those rows)
- **Confirmation dialog** conditionally appends a `[Warning]` block
  when the selection contains any BP CDO entries — clean dialog
  otherwise so the warning isn't always present

#### `FSimpleQuestEditorUtilities::ScanActorForStaleTags` (new public utility)
- Promoted from anonymous-namespace helper to public static method
  on `FSimpleQuestEditorUtilities`. Walks an actor's components,
  dispatches by component type (Giver / Target / Watcher), emits
  one `FStaleQuestTagEntry` per stale tag found. Useful for any
  targeted recovery / scan flow that wants to re-derive entries
  for a specific actor without re-walking the entire project

### Changed

- **"Refresh" button renamed "Scan Loaded"** for symmetric pairing
  with Full Project Scan. Method renamed `HandleRefreshClicked` →
  `HandleScanLoadedClicked` for internal consistency. Tooltip
  clarifies the Tier-1 scope and references Full Project Scan as
  the alternative
- **Slow-task progress bar** in Full Project Scan now advances on
  completion (`SlowTask.EnterProgressFrame(1.f)` moved to after
  `Refresh`) instead of jumping to 100% before the scan begins.
  UE's secondary asset-loading bar continues to show real-time
  progress during sync-loads
- **`UQuestResolutionSubsystem` resolution-recording log** promoted
  Verbose → Log. Quest resolution is a high-signal event; designers
  debugging quest flow now see "recorded" entries in the Output Log
  without enabling Verbose
- **`SStaleQuestTagsPanel` actor-not-found-post-load log** promoted
  Verbose → Log with more actionable wording: "actor may have been
  renamed or removed since the scan." Class-prefix added for
  consistency with other panel logs
- **`UQuestEventSubscription` subsystem-resolution failure Warning**
  gained a "Common causes:" hint — `BindToQuestEvent` fired pre-init,
  or the `WorldContextObject` pin is wired to an actor whose UWorld
  isn't valid

### Removed

- **Anonymous-namespace `ScanActorForStaleTags`** in
  `SimpleQuestEditorUtils.cpp` — replaced by the public version above
- **`SimpleQuest.Debug.ScanBlueprintCDOs` and
  `SimpleQuest.Debug.ScanUnloadedLevels` console commands** — original
  author's note explicitly slated these for removal once Phase 4's
  panel button was in place. Phase 4 + Phase 5 (commandlet) both
  shipped in 0.3.4; the console commands were strictly inferior to
  the panel + commandlet surfaces

### Known Limitations

- **BP CDO undo is a no-op.** Clear on a BP CDO row persists
  permanently across save (the underlying mutation propagates to
  the Blueprint's class state). Ctrl+Z does not visually restore
  the row, and the underlying tag stays cleared on the BP. Two
  approaches were tried during development and both failed —
  force-recompile in `PostUndo` (didn't fix the visibility issue
  and added several seconds of lag per touched BP) and shadow-of-
  cleared-entries (the predicate's component weak-ptr check failed
  post-undo, dropping sibling rows on unrelated actors). Root
  cause is the BP-side template / CDO propagation contract — needs
  a refactor that routes through `FProperty::PreEditChange` /
  `PostEditChange` flow rather than direct container mutation.
  Slated for a focused investigation post-0.4.0; not blocking.
  Designer-visible warnings landed alongside on the per-row tooltip,
  Clear Selected (N) header tooltip, and bulk-clear confirmation
  dialog so designers see the limitation BEFORE the click rather
  than discovering it after
- **Slow-task progress bar lacks per-step granularity.** Outer bar
  stays at 0% during the scan and advances to 100% on completion;
  UE's secondary asset-loading bar shows real-time progress during
  sync-loads. Cosmetic. Plumbing per-step progress through
  `ScanActorBlueprintCDOs` / `ScanUnloadedLevels` /
  `ScanWorldPartitionActors` requires threading a callback through
  the scan internals — minor lift but no immediate user-pain driver

---

## [0.3.4] — 2026-04-25 — Stale Quest Tags Tier 2 (Project-Wide Scanning)

Project-wide stale quest-tag scanning. The Stale Quest Tags panel and a
new headless commandlet now cover the full project surface — loaded
levels (Tier 1, unchanged), Actor Blueprint defaults, and unloaded
levels including World Partition. Designers click **Full Project Scan**
in the panel to surface stale references that a normal pass wouldn't
catch; ship pipelines and CI runs invoke the commandlet directly via a
Windows `.bat` helper or a `UnrealEditor-Cmd.exe -run=StaleQuestTagsScan`
line, get structured JSON output, and gate on exit code: `0` clean, `1`
stale references found, `2` infra failure (couldn't init, JSON write
failed, etc.). Designed as the pre-flight + post-flight validator for
tag-identity work — particularly the `SimpleQuest.*` root namespace
consolidation slated for 0.4.0 — but useful any time a project ships.

The new scan tiers are opt-in and source-aware. The panel keeps its
sub-second Tier 1 refresh as the default; clicking Full Project Scan
fans out to Blueprint CDOs and unloaded levels (with a comprehensive-
vs-class-filtered World Partition toggle) wrapped in a slow-task with
progress notifications. Each row carries a Source badge — *Loaded* /
*BP CDO* / *Unloaded* — and the navigation affordance morphs to match:
Find frames the actor in its viewport for loaded entries, **Open BP**
opens the Blueprint editor for CDO entries, **Open Level** loads the
containing umap for unloaded entries. Per-row Clear works on all three
sources; affected packages roll into a panel-header **Save All
Modified (N)** button so the designer can review and save in bulk.

### Added

#### Stale Quest Tags panel — Tier 2 surfaces
- **Full Project Scan** button alongside the existing Refresh control.
  Refresh stays at Tier 1 (loaded levels — sub-second); Full Project
  Scan fans out to Tiers 1+2+3 wrapped in `FScopedSlowTask` with
  progress notifications. The panel caches the last-used scope so
  `PostUndo` / `PostRedo` re-scan against the same view (stops Ctrl+Z
  from silently narrowing the scope back to Tier 1 and dropping any
  Tier 2 rows the designer pulled in)
- **Source-aware row morphing**: per-row icon + a Source column with
  *Loaded* / *BP CDO* / *Unloaded* badges. Find button morphs into
  **Open BP** for CDO entries (opens the Blueprint editor) and **Open
  Level** for unloaded entries (loads the containing umap)
- **Save All Modified (N)** button in the panel header. Per-row Clear
  marks the affected package dirty and surfaces it here as a
  `TSet<TWeakObjectPtr<UPackage>>`; one click saves all of them and
  drops them from the tracking set. Stale entries pointing at packages
  that have been re-loaded since the last scan are dropped cleanly via
  the weak pointer
- Comprehensive-vs-class-filtered **World Partition scan mode** toggle
  (default: comprehensive — loads every WP actor; class-filtered:
  loads only descriptors whose actor class is in the quest-component
  class set, much faster at the cost of missing per-instance component
  additions)

#### Stale Quest Tags scan commandlet
- `UStaleQuestTagsScanCommandlet` (`UCommandlet` subclass; `IsEditor=
  true`, `IsClient/IsServer=false`, `LogToConsole=true`,
  `ShowErrorCount=true`). Run via
  `UnrealEditor-Cmd.exe <project>.uproject -run=StaleQuestTagsScan`
- Args:
  - `-OutputJson=<path>` writes structured output. JSON shape:
    `{ totalCount, openCount, bpCDOCount, unloadedCount, entries: [{
    source, actor, component, field, tag, package }] }`
  - `-FastWP` runs WP iteration in class-filtered mode (skips actor
    descriptors whose class can't carry a quest component); default
    is comprehensive (loads every WP actor)
- **Exit codes** for CI gating:
  - `0` — no stale references found
  - `1` — one or more stale references found
  - `2` — commandlet itself failed (init error, JSON write failure,
    Asset Registry timeout, etc.)
- Asset Registry is primed at the top of `Main` via
  `FAssetRegistryModule::Get().SearchAllAssets(/*synchronous*/ true)`
  before any scan runs — without this the freshly-spawned commandlet
  editor reports zero AR entries and every scan finds nothing
- Per-source summary line on stdout
  (`StaleQuestTagsScan: summary — Open=N, BPCDOs=M, Unloaded=K,
  Total=T`) plus one Warning-verbosity log line per stale entry
  (`StaleQuestTagsScan: [<Source>] actor=X component=Y field=Z
  tag=Q.R.S package=/Game/Foo`) so log-only runs without
  `-OutputJson` are still actionable

#### Windows `.bat` helper
- `SimpleQuestDemo/Scripts/RunStaleQuestTagsScan.bat` — resolves
  `UE_PATH` (inline assignment in the script OR the env var), finds
  the project's `.uproject`, invokes `UnrealEditor-Cmd.exe` with
  `-unattended -nopause -stdout` and forwards any extra args to the
  commandlet (e.g. `RunStaleQuestTagsScan.bat
  -OutputJson=stale-tags.json -FastWP`). Forwards the commandlet's
  exit code to the caller for CI / make-script consumption

#### Backend scan surfaces
- `FSimpleQuestEditorUtilities::FStaleTagScanScope` — boolean flag
  struct (`bLoadedLevels` / `bActorBlueprintCDOs` / `bUnloadedLevels`
  + `bComprehensiveWPScan`). Default-constructed scope =
  `{bLoadedLevels=true}`, preserving Tier 1 caller behavior bit-for-
  bit
- `FSimpleQuestEditorUtilities::EStaleQuestTagSource` — `Loaded` /
  `ActorBlueprintCDO` / `UnloadedLevelInstance`. Carried on every
  `FStaleQuestTagEntry` so the panel and commandlet output can
  attribute each stale reference to its discovery surface
- `ScanActorForStaleTags` — single helper consolidating the per-
  component walk logic shared across all three scan surfaces.
  Replaces the duplicated walk that previously lived inline in the
  Tier 1 scan path
- `ScanActorBlueprintCDOs` — walks every `UBlueprint` asset via
  Asset Registry filter, hydrates each generated class's CDO, runs
  `ScanActorForStaleTags` against actor-derived CDOs only.
  Non-actor BPs short-circuit before generated-class load
- `ScanUnloadedLevels` — iterates every `UWorld` asset in the AR,
  builds a skip set from currently-loaded editor world packages
  (Tier 1's territory), sync-loads each remaining umap, and
  dispatches by world type:
  - Non-WP world: walks `PersistentLevel->Actors` directly
  - WP world: hands off to `ScanWorldPartitionActors`
- `ScanWorldPartitionActors` — uses
  `FWorldPartitionHelpers::ForEachActorWithLoading` (the cooker's
  per-actor load/unload iteration pattern) wrapped in
  `FScopedEditorWorld` for commandlet-mode lifecycle discipline.
  Optional class-filter set built lazily from
  `BuildQuestComponentClassSet` (FastWP mode); default is
  comprehensive (loads every WP actor)

### Changed

- Stale-tag scan summary log lines (per-tier and overall) bumped from
  `Verbose` to `Display` verbosity. The commandlet's stdout pipeline
  now produces actionable per-world descriptor counts and final
  per-source totals without needing a `-LogCmds="LogSimpleQuest
  Verbose"` override
- The `Stale Quest Tags` panel's status line summarizes both the
  current visible-row count AND the last scope used (so it's clear
  at a glance whether the panel reflects a Tier-1-only refresh or a
  Full Project Scan)

### Fixed

- **Commandlet-mode World Partition iteration crashed during the
  helper's per-batch GC.**
  `FWorldPartitionHelpers::DoCollectGarbage` calls `CollectGarbage(
  IsRunningCommandlet() ? RF_NoFlags : GARBAGE_COLLECTION_KEEPFLAGS,
  true)` — in commandlet mode it passes `RF_NoFlags` as `KeepFlags`,
  which means *only root-set objects survive the GC pass*.
  `RF_Standalone` doesn't protect, asset-package ownership doesn't
  protect. A manually sync-loaded `UWorld` therefore becomes
  unreachable mid-iteration; `UWorldPartition::BeginDestroy` then
  asserts because the WP we just initialized isn't in `Uninitialized`
  state, and the sibling `UWorldPartitionSubsystem` (a tickable world
  subsystem) ensures because it was destroyed while still
  initialized. The fix: route every sync-loaded world through
  `FScopedEditorWorld` (engine's RAII helper at
  `Editor/UnrealEd/Public/EditorWorldUtils.h`, used internally by
  the WP convert commandlet). Construction handles `AddToRoot`,
  `GWorld` + `EditorWorldContext` swap, `InitWorld`,
  `UpdateModelComponents`, `UpdateWorldComponents`,
  `UpdateLevelStreaming`. Destruction handles `GEditor->Cleanse`,
  `DestroyWorld` (which routes through `CleanupWorld` + per-
  subsystem `Deinitialize` + `WP::Uninitialize`), `RemoveFromRoot`,
  `GWorld` + `EditorWorldContext` restore. The dispatch branches on
  `bIsWorldInitialized` so resident-from-prior-run worlds and
  externally-owned worlds (which the helper would assert against)
  scan directly without the wrapper

---

## [0.3.3] — 2026-04-25 — Catch-Up Outcome Recovery + Two-Layer State Foundations

A targeted release that closes the catch-up outcome recovery gap left by
0.3.2's BindToQuestEvent work. Subscriptions and watchers that bind to an
already-resolved quest now recover the actual `OutcomeTag` — not the
previous `EmptyTag` placeholder — by reading from a new
`UQuestResolutionSubsystem` rich-record store keyed by quest tag. This
release also formalizes a two-layer state-architecture pattern: WorldState
remains the fast boolean-fact layer ("did X happen?" in O(1)); per-plugin
subsystems hold typed rich-record state ("what are the details?" in O(1)).

Three follow-on `BindToQuestEvent` reliability fixes ship in the same
release because they're inseparable from the catch-up behavior contract:
catch-up deferral to the next tick (fixes Accessed-None on user-cached
proxy references), per-phase duplicate-broadcast guards (closes the
one-tick race window opened by the deferral), and `RegisterWithGameInstance`
on the factory (canonical lifetime anchor; removes fragile dependency on
caller-side BP variable references).

Also folds in a tactile graph-ergonomics improvement: pin-precise drag-create
alignment in the questline graph editor — the connecting pin's connector nub
now lands exactly under the cursor on drag-from-pin spawns, regardless of
node type, content size, or which pin connects.

### Added

#### Two-Layer State Architecture (MVP)
- `UQuestResolutionSubsystem` — new `UGameInstanceSubsystem` exposing a
  read-only public API for quest resolution post-mortems:
  `GetQuestResolution(QuestTag)`, `HasResolved(QuestTag)`,
  `GetResolutionCount(QuestTag)`. Writes are private and gated by
  `friend class UQuestManagerSubsystem` — consumers physically can't
  mutate the registry. Preserves the manager's black-box doctrine:
  the manager remains the sole owner of orchestration; rich-data
  queries route through specialized read-only subsystems
- `FQuestResolutionRecord` — `USTRUCT(BlueprintType)` with three
  `BlueprintReadOnly` fields: `OutcomeTag`, `ResolutionTime` (double,
  world time at resolution), `ResolutionCount` (per-session repeat
  counter — subsumes the prior `QuestCompletionCounts` map)
- `UQuestManagerSubsystem::SetQuestResolved` writes the WorldState
  boolean fact AND the registry record atomically — single choke point
  preserves the two-layer write invariant. Never touch one layer
  without the other
- Lifetime is GameInstance-scoped, mirroring `UWorldStateSubsystem` and
  `UQuestManagerSubsystem`. Records reset naturally on PIE transitions

#### Catch-Up Outcome Recovery
- `UQuestEventSubscription::RunCatchUp` queries the registry for the
  recovered `OutcomeTag` instead of broadcasting `FGameplayTag::EmptyTag`
  on the no-filter path. Listeners that bind to an already-resolved
  quest now receive the actual outcome on `OnCompleted`
- `UQuestWatcherComponent::RegisterQuestWatcher`'s `bWatchEnd` catch-up
  block replaces its dual-path WorldState probing (per-filter-tag
  probes when filter set / EmptyTag fallback when not) with a single
  registry lookup followed by post-hoc `OutcomeFilter` matching.
  Mirrors the live `WatchedQuestCompletedEvent` decision path; the
  EmptyTag fallback is gone

### Fixed

- **BindToQuestEvent: Accessed None on cached proxy from catch-up.**
  `UK2Node_AsyncAction`'s standard expansion calls `Activate()` before
  firing the user's `Then` exec output. Designers wiring the AsyncTask
  pin into a `Set` off the primary `Then` chain hadn't cached it yet
  at the moment Activate ran. If `Activate()` fired a lifecycle delegate
  synchronously inside `RunCatchUp` (quest already resolved), the
  designer's downstream chain (e.g. `Print → Cancel(<var>)`) read a
  null reference. Fixed by deferring `RunCatchUp` to next tick via
  `SetTimerForNextTick` with a weak-pointer-protected lambda — same
  pattern as engine async tasks like `UAsyncTaskDownloadImage`. The
  K2 node's standard expansion now reliably completes (Activate
  returns → ThenOut fires → user's Set node runs) before any catch-up
  delegate fires
- **BindToQuestEvent: duplicate broadcast in deferral window.** The
  one-tick deferral introduced a narrow window during which a live
  signal could fire and, on next tick, catch-up could observe the
  same WorldState fact and broadcast the same lifecycle phase a
  second time. Closed via per-phase `bSawLive*` flags on
  `UQuestEventSubscription` set inside each `Handle*` after the
  `bCancelled` early-out and checked in `RunCatchUp` before each
  phase's broadcast. Listeners now receive exactly one broadcast per
  state transition. Documented edge case: a parent-tag subscription
  with the parent's *own* `Completed` fact also set during a child's
  live completion will suppress catch-up for the parent (the listener
  already received an `OnCompleted` for the child and would inspect
  `QuestTag` to differentiate). Accepted tradeoff vs. double-broadcast
- **BindToQuestEvent: missing `RegisterWithGameInstance` on factory.**
  `USimpleQuestBlueprintLibrary::BindToQuestEvent` constructed the
  proxy with `NewObject<UQuestEventSubscription>()` but never called
  `RegisterWithGameInstance(WorldContextObject)` — the canonical
  `UBlueprintAsyncActionBase` lifetime anchor. Without it, the
  action's lifetime depended on whatever strong references happened
  to exist (BP member variable, exec stack mid-fire), `SetReadyToDestroy`
  was a no-op, and fire-and-forget patterns risked premature GC.
  Fix: factory now anchors via `RegisterWithGameInstance`. `GameInstance`
  owns the strong reference until `SetReadyToDestroy` (called by
  `Cancel`); PIE exit cleans up automatically when the GameInstance
  tears down

### Changed

- `UQuestManagerSubsystem::QuestCompletionCounts` removed —
  `UQuestResolutionSubsystem::GetResolutionCount` is the new
  authoritative count source. `GetQuestCompletionCount` on the manager
  delegates to the subsystem (back-compat for any internal callers;
  external code should switch to `GetResolutionCount` directly)

#### Pin-precise drag-create alignment in the questline graph editor
- Replaces the previous `GetPinAlignmentOffset` heuristic (a fixed
  per-node-type offset assuming a "title bar ~24 + half pin-row ~12"
  layout) with a deferred Slate-geometry correction. Drag-from-pin
  spawns now land the connecting pin's connector nub exactly at cursor
  regardless of node type (Step / Quest / LinkedQuestline /
  combinator / group / etc.) or content-driven height variance
- Two placement paths covered behind a single panel-side queue:
  - QWERX hotkey path (`SQuestlineGraphPanel::OnPreviewKeyDown`) —
    enqueues alignment immediately after the spawn + autowire pass
  - Right-click action-menu path —
    `SQuestlineGraphPanel::OnGraphAddedNodeNotify` hooks
    `UEdGraph::OnGraphChanged` Add events and queues a pin-lookup
    that runs at the next Tick, after `FEdGraphSchemaAction_NewNode::PerformAction`
    has set `NodePosX/Y` to the action's drop location and
    `AutowireNewNode` has made the connection
- Centers on the connector glyph specifically (5.5px inset from the
  outer pin-widget edge per `FAppStyle`'s 11×11 `Graph.Pin.Connected`
  brush), not the pin+label widget center — corrects an early-version
  half-label-width offset
- Action-menu drop anchor is read from `Node->NodePosX/Y` at Tick
  time, not from `FSlateApplication::Get().GetCursorPos()` —
  preserves the original wire-drop point even if the user moved the
  cursor across the action menu before clicking
- `UQuestlineNode_Knot` blacklisted from the alignment queue — the
  schema's `TryCreateConnection` self-loop logic spawns knots at
  specific arch coordinates that should not be overridden
- Key+click no-drag spawn path keeps the existing heuristic offset
  (no FromPin to align against)

---

## [0.3.2] — 2026-04-24 — Authoring Diagnostics + Runtime Hardening

A full authoring-diagnostics pass plus the runtime safety net that
surfaced the need for it. Started from a user-reported ensure in a
Blueprint overlap handler — a stale gameplay tag tripped UE's
`FGameplayTag::MatchesAny` container iteration and hung the editor for
~10 seconds via `FDebug::EnsureFailed`. The fix layered out into
sanitized component getters, defense-in-depth publish/subscribe guards,
and a shared `IsTagRegisteredInRuntime` helper. Surfacing and cleaning
up the stale references needed its own tools, so the release also ships
a project-wide prereq validator and a component-side stale-tag cleanup
panel.

Rounded out with four additional authoring + runtime conveniences:
a duplicate-Outcome-routing compile warning, comment block support on
the questline graphs, a blueprint async action for subscribing to
quest lifecycle events, and a `FEditorUndoClient` hook on the graph
editor that fixes undo for any third-party node type (discovered while
investigating comment-node undo specifically).

### Added

#### Prereq Tag Validator (new toolbar action)
- `Validate Tags` toolbar button on the Questline Graph Editor —
  scans every `UQuestlineGraph` in the project, emits tokenized
  diagnostics to a new `QuestValidator` MessageLog listing with
  clickable per-node navigation. Read-only; never modifies assets
- Four diagnostic categories:
  - **Error**: prereq leaf references a missing fact tag
  - **Error**: Rule Exit with no GroupTag set
  - **Error**: Rule Exit references a rule that no Rule Entry in the
    project provides (authoring cross-reference; catches both
    unregistered tags and stale-registered orphans)
  - **Warning**: unused Rule Entry — no Rule Exit references it;
    message points at the Stale Quest Tags panel as the sweep path
- Validation is independent of the compiler: flags cross-graph drift
  and authoring hygiene the per-graph compile can't see. Validates
  against the runtime tag manager rather than the Asset Registry's
  `CompiledQuestTags` cache — catches `.Completed` state facts and
  `QuestPrereqRule.*` group tags that don't serialize into the AR tag

#### Stale Quest Tags panel (new nomad tab)
- Window → Developer Tools → Debug → Stale Quest Tags — sibling to
  the World State Facts panel. Lists quest-component tag references
  whose target isn't registered in the runtime tag manager. Pull-
  based; never auto-runs
- Scans loaded editor worlds, walks every
  `UQuestGiverComponent` / `UQuestTargetComponent` /
  `UQuestWatcherComponent` across `GEditor->GetWorldContexts`. One row
  per stale tag reference
- Per-row surfaces: Find (magnifying-glass icon, selects + frames the
  actor in its level viewport) and Clear (removes the stale tag from
  the component, marks actor dirty)
- Filter bar with case-insensitive substring match + live highlighting
  across Actor / Component / Field / Stale Tag columns
- Per-column sortable (ascending/descending toggle via header arrows)
- Alternating zebra row backgrounds + vertical-centered text
- Tier 1 scope: loaded levels only. Tier 2 (unloaded + Actor Blueprint
  CDOs, commandlet-capable) logged as follow-up

#### Runtime Helpers
- `FQuestStateTagUtils::IsTagRegisteredInRuntime(Tag)` — true iff
  `Tag` is well-formed AND currently registered in
  `UGameplayTagsManager`. Foundation for every stale-tag check across
  the runtime and editor surfaces
- `FQuestStateTagUtils::FilterToRegisteredTags(Container, ContextLabel)`
  — returns a copy of `Container` with unregistered tags stripped, with
  Warning logs per stale tag naming the context. Used by the new BP-
  facing sanitized getters
- `UQuestComponentBase::RemoveTags(TagsToRemove)` — new virtual,
  parallels `ApplyTagRenames`. Concrete overrides on giver / target /
  watcher remove matching tags from authored containers and mark the
  owning actor dirty. Powers the Stale Quest Tags panel's Clear action

#### BP-Safe Sanitized Getters
- `UQuestGiverComponent::GetRegisteredQuestTagsToGive()` — registration-
  filtered view of `QuestTagsToGive`. Safe to pass to tag-library
  `Filter` / `HasAny` / `MatchesAny` calls that assert on stale entries
- `UQuestTargetComponent::GetRegisteredStepTagsToWatch()` — same pattern
  for `StepTagsToWatch`
- `UQuestWatcherComponent::GetRegisteredWatchedStepTags()` and
  `GetRegisteredWatchedQuestKeys()` — for `WatchedStepTags` and the keys
  of the `WatchedTags` TMap
- Raw accessors on `UQuestWatcherComponent` (`GetWatchedStepTags` /
  `GetWatchedTags`) — const-ref views of the authored containers, for
  the editor-side stale-tag scan

#### Duplicate-Outcome-Routing Compile Warning
- The compiler now emits a tokenized warning when a single content-node
  output pin reaches two or more distinct Outcome terminals that share
  an `OutcomeTag`. Authoring is ambiguous in that configuration — the
  compiler accepts the union of reached destinations, but one outcome
  should route through exactly one terminal. Navigation tokens on the
  source node and every duplicate terminal
- `FQuestlineGraphCompiler::ResolvePinToTags` threads an optional
  per-tag visited-exits collector through the forward walk (defaults to
  `nullptr`; existing call sites unchanged). The outcome-routing loop
  passes a collector, inspects it post-walk for duplicates, emits one
  warning per (OutcomeTag, set-of-Exits) group

#### Comment Blocks
- `UEdGraphNode_Comment` support on all questline graph tiers (top-level,
  Quest inner, LinkedQuestline view). Press `C` with nodes selected to
  wrap them in a comment box with standard 50px padding; press `C` with
  no selection to drop a blank comment at the cursor. Right-click the
  graph background → "Add Comment…" in the action palette as an
  alternative entry point
- `FGraphEditorCommands::CreateComment` mapped on the questline graph
  editor's `GraphEditorCommands` list; schema contributes the palette
  entry via `GetGraphContextActions` (suppressed when dragging from a
  pin since comments don't participate in wiring)

#### Bind To Quest Event (BP async action + C++ helpers)
- `UQuestEventSubscription` — new `UBlueprintAsyncActionBase` subclass
  with four output delegates: `OnActivated`, `OnStarted`, `OnCompleted`
  (carrying the `OutcomeTag`), `OnDeactivated`. Subscribes to all four
  lifecycle event channels on a single quest tag and stays bound until
  `Cancel()` is called or the `UGameInstance` tears down. Designed for
  hierarchical tag subscriptions — subscribing on a parent tag
  (e.g. `Quest.MyLine`) receives events for every descendant quest
- Catch-up on activation: any already-asserted quest-state fact fires
  the corresponding pin immediately, mirroring
  `UQuestWatcherComponent::RegisterQuestWatcher`
- BP factory `UQuestEventSubscription::BindToQuestEvent(WorldContext, QuestTag)`
  — DisplayName "Bind To Quest Event", `BlueprintInternalUseOnly` +
  `HidePin`/`DefaultToSelf` on `WorldContextObject` so the pin is
  auto-wired to Self in BP graphs
- C++ one-liner template on the BP library for direct handle-based
  subscriptions: `USimpleQuestBlueprintLibrary::SubscribeToQuestEvent<TEvent>`
  resolves the signal subsystem, guards the tag against
  `IsTagRegisteredInRuntime`, and returns an `FDelegateHandle`.
  Companion `UnsubscribeFromQuestEvent(WorldContext, QuestTag, Handle)`
  for teardown

### Fixed

- Undo failure for `UEdGraphNode_Comment` placement — `FQuestlineGraphEditor`
  now inherits `FEditorUndoClient` and forces `NotifyGraphChanged` on
  the current graph from `PostUndo` / `PostRedo`. `UK2Node_AsyncAction`
  and other third-party node types whose `PostEditUndo` doesn't
  broadcast graph-change now get a reliable post-undo refresh. Covers
  every node type uniformly, not just comments

- Runtime ensure hang on stale-tag Blueprint iteration — UE's
  `FGameplayTag::MatchesAny` ensures when iterating a container
  holding an unregistered tag. Demo `BP_QuestGiverActor`'s overlap
  handler was passing the giver's raw `QuestTagsToGive` into
  `FGameplayTagContainer::Filter`, producing a ~10 second `EnsureFailed`
  hang (stack walk + crash report). Fixed via the sanitized getters
  above + BP node swap on the demo actor. No more ensure; stale tags
  skipped silently with a Warning log pointing at the Stale Quest Tags
  panel
- `UQuestNodeBase::ResolveQuestTag` was calling `RequestGameplayTag`
  without `ErrorIfNotFound=false` — the one outlier across the whole
  plugin. Latent foot-gun that would ensure on any path passing an
  unregistered `TagName`. Now passes `false` explicitly with a Warning
  log + early return on invalid

### Changed

- `UQuestGiverComponent::GiveQuestByTag` and `RegisterQuestGiver` loop
  guards upgraded from `IsValid` to `IsTagRegisteredInRuntime` — stale
  tags skipped with a Warning log naming the stale tag and the actor
- `UQuestTargetComponent::BeginPlay` subscribe loop — same upgrade
- `UQuestWatcherComponent::RegisterQuestWatcher` subscribe loop — same
  upgrade

#### Soft class references across authoring + runtime
- `UQuestlineNode_Step::ObjectiveClass`, `::RewardClass`, `::TargetClasses`
  flipped from `TSubclassOf` / `TSet<TSubclassOf<>>` to `TSoftClassPtr` /
  `TSet<TSoftClassPtr<>>`. The runtime counterparts
  (`UQuestStep::QuestObjective`, `UQuestNodeBase::Reward`) were already
  `TSoftClassPtr`; runtime `UQuestStep::TargetClasses`,
  `FQuestObjectiveActivationParams::TargetClasses`, and
  `UQuestObjective::TargetClasses` all flipped to match
- Questline asset packages no longer record hard dependencies on
  designer-authored Objective / Reward / target Actor BP classes.
  Measured impact: a populated test questline dropped from ~500 MB
  to ~54 KiB package footprint
- Soft→hard resolution happens at well-defined boundaries:
  `UQuestObjective::EnableQuestTargetClasses` and
  `UQuestManagerSubsystem::ActivateNodeByTag`'s Step branch call
  `LoadSynchronous` at step activation time; the already-loaded
  `UClass*` is cached in the runtime `ClassFilteredSteps` multimap so
  event-dispatch checks stay fast
- Slate widget display in `SGraphNode_QuestlineStep` uses
  `TSoftClassPtr::GetAssetName()` for class-name rendering without
  forcing the class asset to load
- Existing assets migrate on resave — `TSubclassOf` and `TSoftClassPtr`
  share the same `FSoftObjectPath` serialization shape, so UE
  transparently reinterprets older data. Resave each affected
  questline to drop the stale hard-dep records from its package

---

## [0.3.1] — 2026-04-23 — Objective Activation Lifecycle + Structured Payloads

Dominant feature: a restructuring of the objective activation surface.
Activation now delivers a typed `FQuestObjectiveActivationParams`
struct to objectives, with named fields + `FInstancedStruct CustomData`
extension — symmetric with the existing `FQuestObjectiveContext` on
the completion side. Four entry points feed the struct (authored step
defaults, external event bus, quest giver components, step-to-step
handoff), all merging additively. New `OriginTag` + `OriginChain` give
objectives first-class "who activated me" tagging and full activation-
history awareness across cascades, Quest containers, and
LinkedQuestline boundaries.

Also bundled: the graph editor polish pass — cross-graph giver display,
auto-compile for linked questlines, copy/paste/duplicate/cut command
wiring, Quest inner-graph deep-copy on paste, toolbar and picker
conveniences, plus a batch of rename- and compile-refresh fixes.

### Added

#### Activation Params Struct (dominant feature)
- `FQuestObjectiveActivationParams` — named activation-time fields
  (`TargetActors`, `TargetClasses`, `NumElementsRequired`,
  `ActivationSource`, `OriginTag`, `OriginChain`) plus
  `FInstancedStruct CustomData` for game-specific runtime extension.
  Symmetric with `FQuestObjectiveContext` on the completion side
- `UQuestObjective::OnObjectiveActivated` replaces `SetObjectiveTarget`
  — `BlueprintNativeEvent` taking the full params struct, accessed
  via `BlueprintProtected` + public `DispatchOnObjectiveActivated`
  wrapper. Subclasses override to read authored fields or typed
  `CustomData`

#### External Activation Entry Point 
- `FQuestActivationRequestEvent` published on
  `Tag_Channel_QuestActivationRequest` — programmatic activation
  entry for procedural generators, dialogue systems, save/load
  rehydration, test harnesses. Manager subscribes and routes without
  exposing a new public method on the subsystem (black-box preserved)

#### Giver-Authored Params 
- `UQuestGiverComponent::ActivationParams` — designer-authored
  `FQuestObjectiveActivationParams` carried with every give. Placed
  world singletons (shrines, dungeon-entrance actors) author their
  specific `TargetActors`, counts, `CustomData`, `OriginTag` directly
  in the Details panel
- `UQuestGiverComponent::GiveQuestByTag(QuestTag, Params)` promoted
  signature — optional runtime `Params` arg (`AutoCreateRefTerm`
  makes the BP pin truly optional). Merges additively with the
  component's authored `ActivationParams` using the same rules as
  the step-side merge
- `ActivationSource` defaults to `GetOwner()` when neither authored
  nor caller sets it; designer-authored `OriginTag` seeds the
  initial `OriginChain`

#### Step-to-Step Forward Params 
- `UQuestObjective::CompleteObjectiveWithOutcome` gains optional
  `InForwardParams` arg (`AutoCreateRefTerm`) — completing objective
  specifies an `FQuestObjectiveActivationParams` to carry forward
  into the next step's activation. Merges additively with the
  downstream step's authored defaults. Both `InCompletionContext` and
  `InForwardParams` are BP-optional via `AutoCreateRefTerm`
- `K2Node_CompleteObjectiveWithOutcome` — new `Forward Params`
  input pin with per-pin tooltip explaining the additive merge
  rules + common uses. `Completion Data` pin now also carries a
  full per-pin tooltip. Node-level tooltip rewritten to cover all
  three authored inputs

#### Chain Propagation
- `OriginTag` — immediate-origin tag stamped onto the activating
  node; designer escape hatch for "who activated me?" BP branching
- `OriginChain` — full activation history array, oldest-first.
  Extended at every hop: step-to-step via `ChainToNextNodes`, across
  `UQuest` boundaries via `ActivateNodeByTag`'s Quest branch,
  across `LinkedQuestline` boundaries automatically (LinkedQuestline
  inlines as a `UQuest` at compile time). Every hop contributes
  exactly one entry — no gaps, no duplicates across boundaries

#### Graph Editor Polish
- Copy / paste / duplicate / cut command wiring on the questline
  graph editor (`FGenericCommands::Copy` / `Paste` / `Duplicate` /
  `Cut` + handlers modeled on `FBlueprintEditor`, clipboard via
  `FPlatformApplicationMisc` with `ApplicationCore` module dep)
- Quest inner-graph deep-copy on paste — pasted Quest nodes carry
  a full deep copy of their inner graph, labels + topology + pin
  connections intact, with fresh compiler-level identities
  (`RegenerateInnerGraphIdentitiesRecursive` + `PostPasteNode`)
- Graph Defaults toolbar button — jumps to the graph's root
  properties (FriendlyName, metadata) without hunting in the
  Details panel
- Outcome node inline tag picker — outcome tag pickable directly
  on the Outcome node widget rather than via the Details panel
- Auto-compile linked questlines — compiling a graph automatically
  recompiles any graphs that link into it, keeping cross-graph
  state consistent without manual compile cycles
- Cross-graph contextual giver display on all content nodes —
  Quest / LinkedQuestline / Step nodes show their associated giver
  actors with context-aware resolution across linked graph
  boundaries; `bGiversExpanded` lifted to component base
- `FriendlyName` `FText` on `UQuestlineGraph` — preferred over
  asset name in titles, tooltips, and outliner root display
- LinkedQuestline title format + inline asset picker (title lock
  when an asset is picked)

### Changed

#### Struct Promotions
- `PendingActivationParams` promoted from `UQuestStep` to
  `UQuestNodeBase` so any node type can be pre-stamped by cascade
  routing — unblocks `UQuest` / LinkedQuestline boundary chain
  preservation

#### Authoring
- Details-panel rename propagation — editing a node's name from
  the Details panel triggers the same propagation + compile-
  invalidation flow as in-graph rename
- Container rename propagation — renaming a Quest container
  updates all inner-graph-contained node tags and downstream
  references
- Stale-warning banner generalization in the Details panel —
  previously hardcoded to a single case; now covers all content-
  node kinds uniformly

### Removed
- `UQuestStep::TargetVector` + `UQuestlineNode_Step::TargetVector`
  — positional data now routes through `CustomData` (vectors have
  no sensible additive merge semantic)

### Fixed
- Compile-status icon regression after auto-compile-linked landed
  — neighbor-broadcast refreshes were resetting status to Unknown;
  fixed with `bSuppressDirtyOnGraphChange` guard across
  `RefreshAllNodeWidgets`
- Cross-asset compile refresh — when a linked questline recompiles,
  parent assets that reference it now refresh their compile status
  without a manual re-open
- Refresh recursion — graph-change notification loops through
  nested Quest inner graphs without duplicate work
- Objective BP visibility lockdown — internal objective lifecycle
  methods (`TryCompleteObjective`, `ReportProgress` et al.)
  properly `BlueprintProtected` + public C++ dispatcher wrappers;
  no longer leak into arbitrary BP call menus
- Linked-questline runtime participation — previously a
  LinkedQuestline node could fail to wire into the runtime graph
  depending on load order; fixed by normalizing the compile-time
  inline handoff
- Prerequisite expression compilation dropped leaves on multi-input
  AND / OR when the AnyOutcome branch fired — a chained
  `OutExpression.Nodes[Idx].ChildIndices.Add(OutExpression.Nodes.Add(...))`
  held a dangling reference when the `TArray` reallocated mid-loop,
  silently losing every child after the first. Symptoms: AND(Left, Right)
  prereqs that only checked Left; unreleasable steps in deep graphs.
  Fix sequences the `Nodes.Add` before the back-index
- Utility node (ActivationGroup Entry/Exit, GroupSetter/Getter,
  PrereqRule monitor) output-pin wiring resolved with the wrong
  `TagPrefix` when compile recursion unwound — Pass 2b iterated the
  shared `UtilityNodeKeyMap` unconditionally and rewrote nested
  utility nodes' `NextNodesOnForward` using each outer graph's prefix.
  Symptoms: group-entry cascades targeting shallow tags like
  `Quest.NewTest.Left_Two` when destinations lived at
  `Quest.NewTest.Secret_Level.Near.Left_Two`. Pass 2b now iterates
  only utility nodes belonging to the current graph
- `UQuestManagerSubsystem` subclass arbitration — every concrete
  subclass was being instantiated by UE's default
  `ShouldCreateSubsystem`, not just the one designated in project
  settings. Designated quest manager class now gates creation
  (`ShouldCreateSubsystem` override checks `USimpleQuestSettings`);
  other subclasses are silently suppressed so signal-bus handlers
  don't double-fire
- Prerequisite Rule monitors latched permanently on first
  satisfaction, breaking any expression with a `NOT` — the rule
  would publish immediately at activation time if the negated leaf
  wasn't yet asserted, then unsubscribe and never re-evaluate when
  the leaf actually fired. Rule monitors are now dynamic: they
  stay subscribed for the rule's lifetime and mirror the
  expression's current truth value, adding or retracting the
  group fact as leaves transition
- Cross-PIE-session state leak on compiled node instances — the
  instances live on the `UQuestlineGraph` asset and survive PIE
  transitions, but their subscription handles, deferred-prereq
  state, giver-gated flags, and Piece D scratch slots were from
  the prior session's dead subsystems. Session 2 would skip
  re-subscription (handles map already populated), silently
  disconnecting rule monitors from the live signal bus. New
  `UQuestNodeBase::ResetTransientState` virtual wipes this state;
  called per compiled node by `ActivateQuestlineGraph` before any
  other wiring
- Linked-asset PIE debug visualization was blind to cross-graph
  context — halos and the Prerequisite Examiner both queried
  `WorldState` with the standalone-compile tag (e.g.
  `Quest.SideQuestQL.Near.Left`) while live facts were nested
  under the active parent (`Quest.NewTest.Secret_Level.Near.Left`).
  Viewing a linked questline asset during PIE showed no feedback
  on any inner node. `FQuestPIEDebugChannel::ResolveRuntimeTag`
  and `QueryLeafState` now fall back to contextual tags via a new
  `CollectContextualNodeTagsForEditorNode` Asset Registry walk
  (extracted from the existing contextual-giver/watcher machinery)
- Diagnostic-log volume under `LogSimpleQuest VeryVerbose` —
  `FindCompiledTagForNode` was printing a per-slot iteration dump
  on every call from editor paints, reaching ~260 lines per tick.
  `IsContentNodeTagCurrent` and `ReconstructNodeTag` similarly
  logged per-invocation on the hot path. All three are now silent
  on the success path; misses retain their Warning/Verbose logs

---

## [0.3.0] — 2026-04-26 — Compiler, Portal Vocabulary, Inspection Surfaces, PIE Debug Overlay

A major design iteration centered on the visual graph editor. The graph
now compiles into runtime quest data. Designers author nested
prerequisite expressions, named outcomes, reusable prerequisite rules,
and hierarchical activation groups — all visually. Four inspection
surfaces expose runtime state directly in the editor during PIE.
SimpleCore graduates to a multi-module plugin with its own editor-side
inspector.

### Added

#### Compiler + Runtime Bridge
- `FQuestlineGraphCompiler` translates authored graphs into runtime
  nodes and compiled Gameplay Tags, registered as native via
  `FNativeGameplayTag`
- Compile toolbar action (single graph) + Compile All menu command
- Compiled tags persisted to `Config/SimpleQuest/CompiledTags.ini` for
  startup availability before the Asset Registry finishes loading
- Asset Registry scan registers tags from all questline assets at game
  start
- `FMessageLog("QuestCompiler")` replaces `UE_LOG` for compiler
  diagnostics — clickable toasts open a dedicated log page on failure
- Cross-graph parallel-path compile-time warning

#### Graph Authoring
- Named outcomes replace binary Success / Failure — designers author
  any number of named outcome tags per quest/objective and wire each
  to its own downstream path
- Prerequisite expression system with full AND / OR / NOT combinators
  supporting arbitrary nesting
- Reusable Prerequisite Rules — Entry/Exit portal pair letting a named
  rule be evaluated from multiple sites without duplication
- Activation Groups — Entry/Exit portal pair providing many-to-many
  node activation topology without per-wire bookkeeping
- Utility nodes: `SetBlocked` / `ClearBlocked` with tag-container
  picker for designer-authored quest gating
- Step-level nodes — quests composed of ordered Step nodes carrying
  inline objective class pickers
- `LinkedQuestline` node embedding a referenced questline asset, with
  outcome pins synced from the linked asset
- Custom Slate widgets for combinator, group, utility, and step nodes
  — compact layouts, inline tag pickers, dynamic pin UI, "Add pin"
  controls where applicable
- Double-click on a content / portal node navigates to that node in
  the graph editor (cross-graph navigation supported)
- Tagged content handles: every content node carries a stable
  `QuestGuid` surviving rename; outliner + examiner panels use
  GUID-based references so navigation survives rename operations

#### Inspection Surfaces
- **Questline Outliner** — nomad panel tab listing every content and
  portal node in the open questline with double-click navigation
- **Entry Source panel** — Details customization on Entry nodes
  showing every effective source reaching the Entry via a cross-graph
  walk
- **Group Examiner** — panel listing Activation and Prerequisite
  groups with member counts, expansion, navigation to source nodes
- **Prereq Expression Examiner** — nested algebraic box layout of the
  pinned prerequisite expression; per-operator color inheritance
  (AND green, OR cyan, NOT red, RuleRef amber); collapsible
  combinators and RuleRefs; cross-editor hover halos that highlight
  the corresponding graph nodes
- **PIE Graph Debug Overlay (Tier 1)** — per-state colored halos on
  content-node widgets in the graph editor while PIE is active,
  driven by the new `FQuestPIEDebugChannel` reading `QuestState.*`
  facts from the PIE world's `UWorldStateSubsystem`
- **Prereq Examiner PIE coloring (Tier 2)** — live leaf satisfaction
  tints with AND / OR / NOT roll-up on every layer of the expression
  tree; hover emphasis model decouples fill (follows cursor) from
  border (emphasizes inputs)
- **WorldState Facts panel** — new `SimpleCoreEditor` module; nomad
  tab listing all asserted `WorldState` facts during PIE with
  case-insensitive substring filter, alphabetical sort, live per-tick
  refresh

#### Quest Event Context Model
- `FQuestObjectiveContext` struct carrying `TriggeredActor`,
  `Instigator`, counter state, and `FInstancedStruct CustomData` for
  game-specific extension
- `FQuestNodeInfo` struct carrying compiled display metadata
  (`QuestTag`, `DisplayName` `FText`) baked by the compiler
- `FQuestEventContext` wrapping `FQuestNodeInfo` + optional
  `FQuestObjectiveContext` for outbound events
- All outbound events (Started, Ended, Enabled, Deactivated,
  Progress) carry `FQuestEventContext`
- `UQuestTargetComponent::Send*` methods accept optional `CustomData`
  via `AutoCreateRefTerm` (BP pin optional, C++ default-constructs)
- `FQuestObjectiveTriggered` / `Killed` / `Interacted` gain
  `CustomData` fields plumbed through to subsystem handlers

#### Runtime
- `UCountingQuestObjective` subclass — counter state
  (`CurrentElements` / `MaxElements`, `SetCurrentElements`,
  `AddProgress`) extracted from base `UQuestObjective`; non-counting
  objectives inherit cleanly from the minimal base
- `FQuestProgressEvent` fires per trigger (not just on completion) for
  UI reactivity (e.g. kill-counter updates from 3/5 to 4/5)
- Quest state facts in the `QuestState.<Tag>.*` namespace:
  `.Active`, `.Completed`, `.PendingGiver`, `.Deactivated`, `.Blocked`
- Autowire: rule-aware priority walker with Deactivation pin
  auto-expansion (dragging from a `Deactivated` output auto-expands
  the target node's deactivation pins before routing the wire)

#### SimpleCore
- `UWorldStateSubsystem::GetAllFacts()` — read-only const-ref
  accessor for inspection surfaces (not a UFUNCTION; mutation still
  exclusively through `AddFact` / `RemoveFact` / `ClearFact`)
- `SubscribeRawMessage<T>` / `PublishRawMessage` — `FInstancedStruct`
  delivery without type-slicing, supporting generic forwarding
  (e.g. SimpleCore ↔ GameplayMessageRouter bridges)
- New module: **`SimpleCoreEditor`** — editor-side PIE debug channel
  (`FSimpleCorePIEDebugChannel`) and `WorldStateFacts` nomad tab,
  usable by any SimpleCore consumer regardless of whether SimpleQuest
  is present

#### Plugin Presentation
- `SimpleQuestStyle` Slate style set with SVG class icons (64px, 16px)
- Plugin icon applied to the asset type thumbnail, K2 Complete
  Objective node, and Questline Outliner tab

#### K2 Nodes
- `K2Node_CompleteObjectiveWithOutcome` — custom K2 node with
  outcome tag pin, typed Context pin, and integrated Slate widget

### Changed

#### Vocabulary + Renames
- Terminal nodes: **Entry → Start**, **Exit → Outcome** (user-facing)
- Portal nodes: **Group Setter / Getter → Entry / Exit** (activation
  and prerequisite both use the portal metaphor)
- WorldState namespace: `Quest.State.*` → `QuestState.*`
  (`QuestStateTagUtils::Namespace` as the single source of truth)
- Runtime class renames to match portal vocabulary:
  `GroupSignalSetterNode` → `ActivationGroupSetterNode`,
  `GroupSignalGetterNode` → `ActivationGroupGetterNode`

#### Graph Schema
- Unified signal-identity model: every wire carries a Signal; Signal
  identity is `(source node, outcome)` with `AnyOutcome` absorbing
  specifics on the same node
- `CanCreateConnection` / `TryCreateConnection` collapsed onto a
  shared comparator (`PinsRepresentSameSignal`, `SignalSetsCollide`)
- Duplicate-source dedupe and parallel-path detection unified across
  previously separate code paths
- Self-loop rules tightened: only `QuestOutcome` and
  `QuestActivation` outputs may loop back to the same node's
  `Activate`; all other self-connections blocked
- `UQuestlineNodeBase::AutowireNewNode` replaces the schema-level
  hook — per-node priority walker with role-based pin categorization

#### Event Payloads
- `FSignalEventBase` dissolved — all events are plain USTRUCTs with
  no base-class requirement
- Routing tag is an explicit `PublishMessage` parameter (no longer
  embedded in payload)
- `SignalSubsystem` walks tag hierarchy on publish — a subscriber on
  `Quest.Questline1` receives events from every quest within

#### Class Hierarchy
- `UQuestNodeBase` (runtime) and `UQuestlineNodeBase` (editor)
  restructured as base classes; content nodes descend from
  `UQuestlineNode_ContentBase`
- `UQuestlineGraph` gains `FriendlyName` `FText` — preferred over
  asset name in titles, tooltips, outliner root display
- `UQuestlineNodeBase::PostEditUndo` broadcasts `NotifyGraphChanged`
  so dynamic-pin widgets rebuild after undo operations

#### Extensibility
- `ISimpleQuestEditorModule` formalized with registration-based
  extensibility tiers:
  - Tier 1: self-describing types (subclass + override classification
    virtuals)
  - Tier 2: replaceable policies (`FQuestlineGraphTraversalPolicy`)
  - Tier 3: factory-registered algorithms (`FQuestlineGraphCompiler`)

### Removed

#### Legacy Events
- `FQuestPrerequisiteCheckFailed`
- `FQuestRegistrationEvent`
- `FQuestRewardEvent` (reward data now carried as a field on
  completion events)
- `FQuestStepCompletedEvent`, `FQuestStepPrereqCheckFailed`,
  `FQuestStepStartedEvent`
- `FQuestTryStartEvent`
- `FQuestlineEndedEvent` (vestige of old two-level model)
- `FSignalEventBase` base class

#### Legacy Nodes
- `UQuestlineNode_Exit_Success` / `UQuestlineNode_Exit_Failure` —
  replaced by a single `UQuestlineNode_Exit` (Outcome) with a
  designer-picked `OutcomeTag` (any outcome; Success / Failure are
  just conventional names)

#### Dead Code
- `PossibleOutcomes` UPROPERTY (discovery via K2 + UPROPERTY
  reflection supersedes)
- `UQuestSignalSubsystem` — renamed to `USignalSubsystem` and
  extracted into SimpleCore
- `SignalTypes.h`, `SignalUtilities.h`
- Legacy `Config/Tags/SimpleQuestCompiledTags.ini` location —
  relocated to `Config/SimpleQuest/CompiledTags.ini` with automatic
  migration delete on editor startup

### Fixed
- Undo / redo crash on dynamic-pin operations (Import Outcome Pins
  and other pin-rebuild flows) — `PostEditUndo` now broadcasts
  `NotifyGraphChanged` so Slate pin widgets reconstruct against the
  restored `Pins` array
- Self-loop connection validation rejected legitimate loops and
  accepted some illegal combinations (e.g. `Deactivated → Activate`);
  guard moved before the deactivation block
- Auto-knot insertion fired for illegal connections
  (same root cause)
- Stale tag cleanup on recompile — registry key mismatch between
  startup path (`GetObjectPathString`) and compile path
  (`GetPackage()->GetName()`) left two entries per graph, old one
  never replaced. Normalized to `PackageName` across the board
- Group setter forward-reach dedupe — colliding signals reached a
  setter via getter + direct path; new `CheckGroupSetterForwardReach`
  catches the retroactive hole
- Hover halo off-screen paint — graph-space viewport culling added
  to all halo passes in `SQuestlineGraphPanel::OnPaint`
- Group Examiner pin-role drift after portal rename — stale FName
  literals ("Activate" / "Forward") replaced with `EQuestPinRole`
  role-based lookup
- PIE debug channel subsystem resolution for Simulate In Editor —
  primary path now uses `GEditor->PlayWorld` (works for both PIE
  and SIE); world-context iteration retained as fallback
- Compile toolbar button visually present but silently did nothing
  — `CompileQuestlineGraph` command was never mapped to an action;
  `MapAction` added in `BindGraphCommands`

---

## [0.2.0] — 2026-03-28 — Visual Graph Editor: Schema & Connections

### Added
- Full connection validation in `UQuestlineGraphSchema::CanCreateConnection`:
    - Prevents duplicate signal paths from the same source quest node
    - Prevents parallel paths to the same destination through reroute nodes
    - Enforces exit node rules: a quest node may not route multiple outputs
      to the same exit node, directly or through reroutes
    - Enforces that a single output pin leads to at most one exit node
- Custom wire rendering via `FQuestlineConnectionDrawingPolicy`:
    - Green wires for Quest Success paths
    - Red wires for Quest Failure paths
    - White wires for Any Outcome and Activation paths
    - Dashed wire rendering for Prerequisites connections
    - Spline hover detection for all wire types
- Double-clicking a wire now inserts a Reroute node in-place
- Hotkey node placement matching standard Blueprint conventions:
    - `Q + click` — place Quest node
    - `S + click` — place Quest Success exit node
    - `F + click` — place Quest Failure exit node
    - `R + click` — place Reroute node
    - Pressing a valid key while dragging a wire places and connects
      the corresponding node immediately at the cursor
        - Node placement by hotkey aligns the cursor with the node's
          relevant input pin, matching standard Blueprint behavior
- `SQuestlineGraphPanel` wrapper widget providing graph-aware input
  handling with correct Slate focus lifecycle management

---

## [0.1.0] — 2026-03-25 — Visual Graph Editor: Scaffolding

### Added
- `UQuestlineGraphSchema` — custom graph schema for the questline editor
- Editor node types:
    - `UQuestlineNode_Entry` — questline start node (non-deletable)
    - `UQuestlineNode_Quest` — represents a single quest; pins: Activate,
      Prerequisites (input), Success, Failure, Any Outcome (output)
    - `UQuestlineNode_Exit_Success` / `UQuestlineNode_Exit_Failure` — terminal
      outcome nodes
    - `UQuestlineNode_Knot` — reroute/passthrough node with dynamic
      type propagation matching the first connected wire
- `FQuestlineGraphEditor` — asset editor toolkit hosting the graph viewport
- `UQuestlineGraph` — editor graph asset containing the `UEdGraph`
- Basic node auto-wiring (`AutowireNewNode`) on all node types:
    - Output pin drags connect to Activate by default on Quest nodes
    - Input pin drags connect from Any Outcome by default on Quest nodes
    - Exit nodes only accept connections from output pins

---

## [0.0.1] — 2025-10-22 — Event Bus

### Added
- Runtime event bus supporting decoupled quest state communication

---

## [0.0.0] — 2025-05-30 — Initial Prototype (GameDev.tv Game Jam — USS Proteus)

### Added
- Core DAG data structure for representing questline graphs at runtime
- Quest node data model with Success, Failure, and Any Outcome resolution
- Initial proof-of-concept during game jam development
