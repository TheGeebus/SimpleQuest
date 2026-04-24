# Changelog

All notable changes to SimpleQuest will be documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Active Development
- Duplicate-Outcome-routing compile warning (single Outcome pin reaching
  two identically-tagged Outcome terminals)
- `BindToQuestEvent` convenience wrapper (C++ template + BP K2 node)
- Event-driven LinkedQuestline ref-index cache (incremental cross-asset
  dependency tracking to replace current Asset Registry scans on compile)
- Utility B Tier 2 — commandlet-capable project-wide stale-tag scan
  covering Actor Blueprint CDOs + unloaded levels (ships-pipeline hook
  for pre-ship validation)

### Upcoming
- Tag namespace consolidation under a single `SimpleQuest.*` root
  (pre-save/load — locks tag strings before the save format freezes)
- Save/load system
- Runtime asset loading pass (async graph load driven by signal
  subscription, prioritized synchronous fallback for critical paths)

### Known Issues
- Giver "why can't activate" query API (Item 23) still deferred until
  deactivation system stabilizes

---

## [0.3.2] — 2026-04-24 — Pre-Ship Hardening + Authoring-Tag Tooling

Three adjacent items targeting stale-tag drift across the project. A
user-reported ensure from a Blueprint overlap handler pulled in a
runtime hardening pass, which surfaced the need for authoring-side
diagnostics and level-side cleanup surfaces. Ships with defense-in-
depth guards so the ensure path is unreachable regardless of authoring
state, plus a toolbar-driven project-wide validator and a nomad tab for
component-side tag cleanup.

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

### Fixed

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
  downstream step's authored defaults. Both `InCompletionData` and
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
