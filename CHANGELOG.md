# Changelog

All notable changes to SimpleQuest will be documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Active Development
- Node editor polish pass: `FriendlyName` field on `UQuestlineGraph`,
  LinkedQuestline title format + inline asset picker, Graph Defaults
  toolbar button, Outcome node inline tag picker
- Project-wide Validate-all-prereq-tags scanner
- `BindToQuestEvent` convenience wrapper (C++ template + BP K2 node)

### Upcoming
- Tag namespace consolidation under a single `SimpleQuest.*` root
  (pre-save/load — locks tag strings before the save format freezes)
- Save/load system
- Runtime asset loading pass (async graph load driven by signal
  subscription, prioritized synchronous fallback for critical paths)

### Known Issues
- Node duplication (Ctrl-D / copy-paste) leaves `NodeLabel` collisions
  with the source; duplicate-resolve auto-suffix helper not yet
  implemented. Compile warning fires on collision but designer
  intervention is still required.

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
