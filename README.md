# SimpleQuest
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) ![Unreal Engine](https://img.shields.io/badge/Unreal-5.6%20%7C%205.7-dea309?style=flat&logo=unrealengine)

**An event-driven game progression framework for Unreal Engine.** Free and MIT licensed.

Build progression systems using reusable Objective objects - stateful observers that respond to gameplay events through a decoupled event bus. Author complex progression flows visually with a graph-based editor.

SimpleQuest started as an answer to a targeted question, *how do you make a single quest genuinely non-linear?* Working through that meant building primitives for branching outcomes, prerequisite composition, and lifecycle events - the infrastructure required to model complex gameplay progression. As those primitives evolved, they stopped being about quests specifically. What emerged is a framework that treats quests as one instance of a broader class: any progression you can express as branching content, prerequisite logic, and named outcomes.

**Build more than quests:**
- Story missions with branching outcomes
- Tutorials that adapt to player behavior
- Achievements and challenges
- Reputation and relationship systems
- World state changes
- Unlock trees and progression paths
- Multi-step puzzles and interactions

<img width="1913" height="1013" alt="SimpleQuestDemo-0 3 0-quick-build" src="https://github.com/user-attachments/assets/43efee3f-d276-4a39-b632-ceb2d465ee34" />

This version targets Unreal Engine 5.6 and is verified compatible with 5.7; the Electronic Nodes visual integration activates automatically on 5.7+ when EN is installed.

Please visit the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS) for community showcases and additional support, including bug reports and feature requests.

See [CHANGELOG.md](CHANGELOG.md) for version history.

---

## Highlights

- **Objective-based progression** — the atomic unit of the framework. A stateful observer subscribes to gameplay events, decides when it's complete, and emits a named outcome tag. Subclass `UQuestObjective`, override a couple of methods, integrate any UE event source (signals, GAS abilities, inventory changes, dialogue beats). Activation delivers typed context with target actors, custom payload, and an origin chain across LinkedQuestline boundaries.
- **Decoupled event bus** — systems publish tagged events, and objectives observe them. No direct references between publisher and observer. Combat doesn't know it's completing a quest step; the quest step doesn't know what fired the event. Routing is SimpleCore's job, and both sides stay independently testable.
- **Named outcomes** — objectives resolve with designer-authored outcome tags rather than binary success/failure. A combat step can complete with `Victory`, `Retreat`, or `Negotiated`, and downstream logic routes each outcome independently through the graph.
- **Prerequisite composition** — AND / OR / NOT combinators plus reusable Prerequisite Rules gate activation, progression, or completion on arbitrarily deep boolean expressions. Rules are named and referenced from multiple content nodes without duplication.
- **Rewards as adapters** — self-configuring reward types read completion context and broadcast to any actor whose Reward Recipient component watches that reward type. The granting node never needs to know who's listening. A preview query returns what a completion advertises *before* it's earned: the "do this, get this" data a quest-giver hub or bounty board needs.
- **Visual graph authoring** — compose progressions in a node graph with designer-friendly widgets, inline tag pickers, and in-editor compile with clickable diagnostics that navigate straight to the offending node. The graph is the authoring surface on the framework, but the framework works without opening a graph editor.
- **Save/load** — full progression state packs into a single serializable snapshot you embed in whatever save game your project already uses. Plain value copy, safe to hand off to an async save. Restored actors see events flagged as catch-up so they can jump straight to settled state rather than replaying transitions.
- **Late-registration catch-up** — components that register after an event has fired receive the recorded state immediately on bind. Streaming levels, dynamically spawned actors, late-joining players, and save-game restoration all work without special-casing.
- **Live PIE inspection** — colored halos on graph nodes show which lifecycle state each is in. The Prereq Examiner tints individual conditions by satisfaction. A searchable World State Facts panel lists every asserted fact. No log-diving to understand runtime state.
- **Authoring diagnostics** — the Prereq Tag Validator runs a project-wide scan for broken cross-graph references. The Stale Quest Tags panel covers loaded levels, Actor Blueprint defaults, and unloaded levels (including World Partition), with per-row navigation, atomic multi-select clear, and a headless commandlet variant for CI gating.
- **Two-plugin architecture** — SimpleQuest is built on **SimpleCore**, a standalone coordination layer (a hierarchical tag-routed event bus and a persistent fact store) that any UE plugin or system can consume independently.

![SimpleQuestDemo-slate-node-preview-v2](https://github.com/user-attachments/assets/9cc8e4f9-ee60-46fc-881a-5c45e38ff9d4)

---

## Two-Plugin Architecture

SimpleQuest is built on SimpleCore. SimpleCore has zero knowledge of quests - it's a standalone coordination layer that any plugin or project system can use.

### SimpleCore

Foundational coordination layer. Runtime and editor modules.

- **`USignalSubsystem`** — gameplay-tag-routed event bus. Publishers send events on a tag, and the bus delivers them to subscribers bound on that tag or any ancestor tag in the hierarchy. Each callback receives the original published tag, so a subscriber on a parent tag knows which descendant fired. When the same logical event publishes under multiple tags simultaneously (a common case with linked questlines, where one node carries both its standalone tag and inlining-context aliases), the bus collapses delivery so each subscriber gets exactly one callback, not one per matching tag. Compared to Unreal's built-in `GameplayMessageRouter`: both handle hierarchical tag-keyed routing, and `USignalSubsystem` adds multi-branch publish with cross-branch deduplication for the case where a single logical event carries multiple tags simultaneously.
- **`UWorldStateSubsystem`** — queryable, gameplay-tag-keyed fact store with integer reference counts. Add a fact, query whether it's present, remove it. Transition events fire when a fact appears (count goes 0→1) or disappears (1→0), so subscribers can react to state changes without polling. Components that register late read current truth directly - safe for streaming, dynamic spawn, join-in-progress, and save game restoration.
- **`SimpleCoreEditor`** — editor module. Provides the World State Facts inspector panel and PIE debug channel. Usable without SimpleQuest.

### SimpleQuest

Game progression framework. Runtime and editor modules, with an optional Electronic Nodes integration module (not affiliated).

- **Tag-addressed runtime** — Every compiled quest node is a `UObject` addressed by a `FGameplayTag`. The manager subsystem, components, and event bus all route by tag. Tags are automatically generated and registered on graph compilation. Write the progression, compile, and the tags appear in UE's Gameplay Tag Manager.
- **Pull-based prerequisite activation** — Quest nodes waiting on prerequisites subscribe to the relevant World State fact tags. When all conditions are met, the node activates. No polling, no ticking.
- **Hierarchical catch-up for late subscribers** — Givers, observers, and `Observe Quest Lifecycle` Blueprint nodes that register after a quest event has already fired receive the recorded state immediately: original outcome, prerequisite snapshot, blocker information, the giver actor that initiated activation, the activation source (giver / cascade / external API / initial entry), and any activation parameters. Subscriptions bound on a parent tag (e.g. `SimpleQuest.Questline.MyArc`) fan out to every known descendant on bind, mirroring the live bus's hierarchical delivery for catch-up.
- **Two-layer state architecture** — World State answers "did it happen?" with boolean fact storage. `UQuestStateSubsystem` answers "what's the structured detail?" with rich event records, the same data the save/load system uses to reconstruct a session. The manager subsystem owns all writes. `UQuestStateSubsystem` is the public read surface.
- **Outcome-filtered observers** — An observer component can filter which outcomes it responds to. An empty filter means all outcomes.
- **Adapter-based rewards** — A reward is a `UQuestRewardBase` subclass that computes a grant from the completion context and hands it out - the framework doesn't dictate what a reward *is*. Grants publish on a reward-type channel, and a `UQuestRewardRecipientComponent` receives the types it watches, hierarchically. Advertised-reward queries surface what a completion (or a whole questline) pays without granting it, live or straight off an unstarted asset. Reference rewards ship for the common cases (experience, currency, loot table, recipient-scaled value).

---

## Requirements

- Unreal Engine 5.6 or 5.7
- Visual Studio 2022 (Windows) or Xcode (Mac) with C++20 support enabled
- [Git LFS](https://git-lfs.com/) installed locally (required for cloning - see below)

---

## Installation

> **Important -- do not use GitHub's "Code > Download ZIP" button.** 
>
>This repository uses Git LFS for `.uasset` and `.umap` binaries. The ZIP archive endpoint does not resolve LFS pointers, so the download will contain ~130-byte stub files instead of the real assets. Clone the repo (with `git-lfs` installed) or download a .zip from the [Releases](https://github.com/TheGeebus/SimpleQuest/releases) page instead.

1. Install [Git LFS](https://git-lfs.com/) and run `git lfs install` once.
2. Clone the repository: `git clone https://github.com/TheGeebus/SimpleQuestDemo.git` - LFS pointers resolve automatically during clone.
3. Copy the `SimpleCore` and `SimpleQuest` folders from the cloned `Plugins/` directory into your project's `Plugins/` directory. The plugins are zero-config. Compiled tags, designer-authored tags, and demo content all travel with the plugin folders.
4. Right-click your `.uproject` file and select **Generate Visual Studio project files**.
5. Open the solution and build the **Development Editor** target.
6. Enable both plugins in **Edit > Plugins** if they are not already active.

To use SimpleQuest as a source dependency in another plugin, add `"SimpleQuest"` (and `"SimpleCore"` where coordination APIs are consumed directly) to your `.uplugin` or `Build.cs` dependencies.

---

## Quick Start

### 1. Create a Questline Graph asset

Right-click in the Content Browser and select **Gameplay > Questline Graph**. This creates a `UQuestlineGraph` asset containing an empty editor graph with only an Entry node. The "Entered" pin will pass an activation signal to any connected nodes when this Questline activates.

### 2. Author the graph

Open the asset to launch the graph editor. From Entry, drag off a wire and place a **Step** node. Questlines are composed of ordered Step nodes with inline objective class pickers. Drop the objective type from the Step widget, assign targets and parameters, and wire completion paths to downstream paths.

Objective choice is where your game's specific gameplay meets the framework. The Step's inline picker lists the reference Objectives shipped in the plugin (counting-based, location-reach, multi-target interaction) plus any you author for your own event sources. See [Objectives](#objectives) for the lifecycle model and how to author your own.

Completion Path pins (or simply "Path" pins, which are always an output) connected to Activation pins (always an input) automatically create Activation wires (solid), indicating the connection represents the flow of execution and dictates the order and timing of node activations. Connections between Path output pins and Prerequisite input pins automatically create Prerequisite wires (dashed), indicating activation is contingent on a given completion path.

When a content node receives an activation signal, it checks satisfaction of any attached prerequisite expression. If the prerequisite is satisfied - or if there is no connected prerequisite wire - the node activates. If a connected prerequisite remains unsatisfied, further progress is deferred until that prerequisite is satisfied. A node that has been activated but deferred proceeds automatically when the prerequisite gating it is fulfilled, no additional signalling is needed.

Useful constructs as your graph grows:
- **Quest** — a node that contains another graph with its own nodes and that has its own independent tag address, which nests in the parent questline - much like animation state machines that can nest within each other. Double-click to open the contained graph. Use the Questline Outliner panel or the breadcrumbs at the top of the graph editor panel to return to the parent level.
- **AND / OR / NOT combinators** — wire into any node's `Prerequisites` pin to gate activation on a boolean expression.
- **Prerequisite Rule Entry / Exit** — define a named rule once; reference it from multiple content nodes without duplication.
- **Prerequisite Fact Tag** — gate on any World State fact tag. Decoupled from graph topology, so you can compose with any UE gameplay system that tracks tag-keyed state (faction reputation, inventory, system flags).
- **Prerequisite Outcome** — gate on a context-free outcome tag. Satisfied when any quest resolves with the picked outcome, regardless of which quest produced it.
- **Prereq Gate** — gate an activation cascade on a prerequisite expression as a standalone, graph-visible primitive. Handles "satisfy these conditions in any order, then proceed" without wiring a phantom downstream quest just to borrow its prereq gating.
- **Add / Remove / Clear Facts** — write World State facts directly from the questline graph, so a graph both reacts to and produces the shared tag-keyed state any other system (or another questline) can gate on.
- **Activation Group Entry / Exit** — many-to-many node activation topology without per-wire bookkeeping.
- **LinkedQuestline** — reference another questline asset inline; the compiler expands it with full outcome pin synchronization.
- **Grant Rewards** — drop onto any completion path (after a step, on a specific outcome, or on Any Outcome) to grant one or more rewards when the flow reaches it; its output continues the flow. Configure each reward inline, or author your own reward type. A questline can also carry its own completion rewards in its details panel, keyed by outcome, which fire whenever it completes — standalone or embedded in another questline.

The Questline Outliner tab, Group Examiner, and Prereq Expression Examiner panels all provide read-only inspection of the graph's structure, particularly useful as graphs grow beyond a single screen.

### 3. Compile the graph

Hit the **Compile** button on the graph editor toolbar (or **Compile All** from the editor's main menu). The compiler generates runtime node instances and registers the required Gameplay Tags. Errors and warnings appear in the message log with clickable navigation to the offending node.

Compile also propagates any tag renames from this session. If you renamed a quest node or asset, references in Blueprints, components, data assets, data tables, and custom struct fields update on this compile. Assets that weren't loaded at rename time heal on next load and flag for save. The editor blocks renames that would silently rebind an active subscription to a different node - protection against the quiet "wrong-listener" bug.

### 4. Activate at runtime

Call `USimpleQuestBlueprintLibrary::StartQuestline(UQuestlineGraph*)` from any startup hook of your choosing - player pawn `BeginPlay`, GameMode `BeginPlay`, a custom GameInstance subsystem, a dialogue trigger, save-load rehydration, level-streaming callback, etc. One pattern serves static startup, procedural orchestration, and dynamic activation alike.

The plugin's demo content shows the canonical static-startup pattern: `BP_QuestPlayerExample::BeginPlay` calls `Start Questline(QL_Main)`. Drop that BP into your player pawn slot and the demo questline runs end-to-end.

If you need custom orchestration logic (analytics, save integration, bespoke activation gating), subclass `UQuestManagerSubsystem` and register your subclass under **Project Settings > Plugins > Simple Quest > QuestManagerClass**. The default native class works without configuration.

### 5. Attach components to actors

| Component | Attach to | Purpose |
|---|---|---|
| `UQuestGiverComponent` | NPC Actor | Offers and activates quests on interaction |
| `UQuestTriggerComponent` | Enemy, item, or location Actor | Publishes trigger fires; receives per-fire response, structural-block, and per-lifecycle feedback |
| `UQuestObserverComponent` | Any Actor | Receives lifecycle events for one or more quests |

### 6. Inspect during PIE

Start Play In Editor. The graph panel shows per-state colored halos on content nodes (active, completed, blocked, etc.). Open the **Window > Developer Tools > Debug > World State Facts** panel for a searchable live view of every asserted fact. Hover any leaf in the Prereq Examiner to see whether it's satisfied, unsatisfied, or in-progress.

### 7. Catch authoring drift

Two surfaces catch broken or stale tag references the compiler can't flag on its own:

- **Validate Tags** (questline graph editor toolbar) — project-wide scan for prerequisite conditions pointing at missing fact tags, Rule Exits pointing at missing Rule Entries, and Rule Entries that no Exit references. Results go to the **Quest Validator** message log with per-node navigation tokens. Read-only; never modifies assets.
- **Stale Quest Tags** (Window > Developer Tools > Debug) — lists quest-component tag references whose target isn't registered in the runtime tag manager. **Scan Loaded** typically runs in under a second; **Full Project Scan** also covers Actor Blueprint defaults and unloaded levels (including World Partition). Per-row badges indicate whether each entry came from a loaded actor, a Blueprint default, or an unloaded level, with matching navigation: **Find** frames a loaded actor in the level, **Open BP** opens a Blueprint editor, **Open Level** loads the containing map. Per-row Clear works on every source; multi-select + Clear Selected wraps a confirmation dialog and atomic undo. Affected packages roll into a panel-header **Save All Modified** button. A headless commandlet variant supports CI gating — `UnrealEditor-Cmd.exe <project>.uproject -run=StaleQuestTagsScan -OutputJson=<path>` writes structured JSON and returns `0` (clean) / `1` (stale found) / `2` (infra failure). A Windows `.bat` wrapper at `Scripts/RunStaleQuestTagsScan.bat` is included for convenience.

Both are pull-based: you open them, review, decide. Neither runs automatically or modifies data without an explicit click.

---

## Architecture

SimpleQuest is an event-driven progression framework. Systems publish tagged events to an event bus. Stateful Objectives observe those events, decide when they're complete, and emit tagged outcomes that drive graph transitions and rewards.

```
                 Your Game
                      |
       +--------------+--------------+
       |              |              |
    Combat        Dialogue       Triggers
       |              |              |
       +------>  Event Bus <---------+
                (SimpleCore)
                      |
       +--------------+--------------+
       |              |              |
 Objective A    Objective B    Objective C
       |              |              |
       +-----> Outcome Tags <--------+
                      |
                      v
              Questline Graph
                      |
        +-------------+-------------+
        |                           |
   Next Objectives             Rewards

```

### Compiler

Editor-side compilation translates authored graphs into runtime node instances and compiled Gameplay Tags, registered native at game start. The compiler is factory-registered via `ISimpleQuestEditorModule::RegisterCompilerFactory` - subclass `FQuestlineGraphCompiler` and register your factory to replace the pipeline wholesale. The default compiler supports LinkedQuestline expansion, named outcomes, prerequisite expression flattening, and cross-graph signal resolution.

### Runtime

`UQuestManagerSubsystem` orchestrates activation, deactivation, prerequisite monitoring, and quest lifecycle events. Scoped to `GameInstance` so quest state persists across level transitions. Every quest node carries a stable `QuestGuid` (save identity) and `QuestTag` (routing identity) - GUIDs survive rename operations, tags resolve to specific nodes on load.

### Coordination layer (SimpleCore)

`USignalSubsystem` and `UWorldStateSubsystem` provide the underlying tag routing and fact storage. SimpleQuest is a consumer of these subsystems - it does not implement its own event bus or state store. Any system in your project can use the same coordination primitives without touching SimpleQuest.

---

## Objectives

Objectives are the primary variation point of the framework. Most games will author at least one custom Objective subclass - Objectives are where your specific gameplay meets the progression system, and the reference implementations shipped in the plugin cover common patterns but aren't a complete library.

### What an Objective is

An Objective is a stateful observer that subscribes to gameplay events, decides when it's complete, and emits a named outcome tag. Each Step in a Questline hosts exactly one Objective at runtime. The Objective owns the state that determines completion and the outcome that routes what happens next.

### Reference implementations

Base classes intended to be subclassed:

- **`UQuestObjective`** — the raw base. Every custom Objective inherits directly or transitively from this.
- **`UCountingQuestObjective`** — adds a `CurrentElements` / `MaxElements` counter initialized from the Step's authored `NumElementsRequired` (plus any caller contribution at activation) and an `AddProgress` convenience that increments, checks threshold, and fires exactly one event per call. Use for any objective with progress semantics.

Example implementations in `Objectives/Examples/`:

- **`UGoToQuestObjective`** — location-reach objective. Completes with a `Reached` outcome when the tracked target enters the goal.
- **`UInteractAllTargetsObjective`** — multi-target objective. Auto-discovers Trigger Components targeting the owning Step at activation via `GetTriggersTargetingThisStep`; completes when every discovered target has fired. See the header for the full adopter wiring pattern.

The reference set is expected to expand in v0.8.0 with timer, world-state-fact, and composite (ANY/ALL wrapper) objectives.

### The lifecycle contract

Three protected `BlueprintNativeEvent` methods drive the Objective lifecycle. Override in C++ or Blueprint:
```c++
    UCLASS(Blueprintable)
    class UMyObjective : public UQuestObjective
    {
        GENERATED_BODY()

    protected:
        virtual void OnObjectiveActivated_Implementation(const FQuestObjectiveAuthoredConfig& Authored,
                                                         const FQuestObjectiveRuntimeContext& Runtime) override
        {
            // Wire up listeners, subscribe to signals, store local tracking state, spawn UI, etc.
            // Authored: design-time config from the Step's UPROPERTYs (target classes/actors, element count,
            //           config asset). Owned by the Step, not the caller.
            // Runtime:  caller's IncomingContext (instigator, custom data, lineage, target overrides) plus
            //           Provenance and IncomingOutcomeTag stamped by the framework.
            // The framework does NOT merge these — compose what you need, with full provenance over which
            // values are authored vs caller-supplied.
        }

        virtual void TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext) override
        {
            // InContext carries values supplied by the trigger fire: TriggeredActor, Instigator, and CustomData
            // (an FInstancedStruct the trigger can populate with any shape you define — position, damage payload,
            // interaction type, whatever your game needs to pass to the completion check).

            if (/* your completion condition */)
            {
                // Trigger context from the inbound fire auto-forwards onto the completion event when omitted here.
                CompleteObjectiveWithOutcome(MyOutcomeTag);
            }
        }

        virtual void OnObjectiveDeactivated_Implementation() override
        {
            // Symmetric partner to OnObjectiveActivated. Fires on BOTH the completion path AND interruption
            // paths (abandon, blocked, cascade-deactivated). Unsubscribe from external event sources, tear
            // down UI handles, stop timers. The Objective is still live at this point — safe to inspect
            // state stored during activation.
        }
    };
```
`UGoToQuestObjective` and `UInteractAllTargetsObjective` in `Objectives/Examples/` are working references for this pattern.

### Completion Path discovery

The editor discovers Completion Paths (Step node output pins) automatically from two sources, additive across the inheritance chain:

- **C++ subclasses**: declare `FGameplayTag` properties with the `ObjectiveOutcome` metadata specifier. Back each tag with `UE_DEFINE_GAMEPLAY_TAG` to guarantee registration before CDO construction.
```c++
  // Header
  UPROPERTY(EditDefaultsOnly, meta = (Categories = "SimpleQuest.Outcome", ObjectiveOutcome))
  FGameplayTag Outcome_Reached;

  // .cpp (file scope)
  UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_Reached, "SimpleQuest.Outcome.Reached")

  // Constructor
  Outcome_Reached = Tag_Outcome_Reached;
```
- **Blueprint subclasses**: place `Complete Objective With Outcome` K2 nodes in event graphs. Each node's OutcomeTag is discovered automatically and surfaced as an independent Path pin on the hosting Step node.

Both sources merge into a single deduplicated set - pins on the Step node reflect the union. Use "Add Call to Parent Function" on a Blueprint override to add a C++ base call to the appropriate branch.

For fully programmatic outcomes that can't be expressed as individual UPROPERTYs or K2 nodes, override `GetPossibleOutcomes()` as a fallback.

### Completion and trigger-context forwarding

Call `CompleteObjectiveWithOutcome(OutcomeTag)` from `TryCompleteObjective` (or from the Blueprint `Complete Objective With Outcome` K2 node) to signal completion. The framework:

- Auto-derives PathIdentity from the outcome tag if not explicitly specified and routes through the Step's structurally-keyed pin map.
- Auto-forwards `TriggeredActor`, `Instigator`, and `CustomData` from the last inbound trigger context to the completion event if the adopter doesn't pass them explicitly. Adopters who pass explicit attribution win; the framework-owned `OriginatingTriggerComponent` is force-overridden from the last trigger context (immune to adopter mutation, guaranteeing the publishing trigger component receives its own feedback).

### Progress and refusal

- **`ReportProgress(ProgressContext)`** — broadcast an explicit progress signal. The framework never auto-fires incremental progress reporting. Objectives with per-fire progress semantics call this explicitly. Binary objectives never call it and listeners receive zero progress events, no event-shape filtering needed at the listener side.
- **`RefuseTrigger(RefusalReason, TriggerContext)`** — decline a trigger fire that reached the Objective but didn't satisfy game-logic conditions (wrong actor, missing item, wrong phase). Publishes a refused-response event on the Step's channel so trigger actors can surface the refusal to UI or audio rather than silently dropping it.

### Save/load hooks

Objectives that carry durable per-instance progress (a counter, a phase index, a partial state) override two virtuals:
```c++
    virtual FSimpleQuestObjectiveSaveState CaptureObjectiveState() const override;
    virtual void RestoreObjectiveState(const FSimpleQuestObjectiveSaveState& State) override;
```
Base returns empty / no-op because a stateless Objective needs nothing. Capture runs at save. Restore runs *after* the Objective has been rebuilt on load (which resets it), so the override re-applies the saved values. `UCountingQuestObjective` overrides these to persist its counter as a reference example.

### When to write a custom Objective vs use a reference

Use the shipped references when they fit: `UCountingQuestObjective` handles a substantial fraction of "N of X" completion patterns, `UGoToQuestObjective` and `UInteractAllTargetsObjective` cover their respective shapes. 

Write a custom Objective when your completion condition depends on a game-specific event source the references don't cover: dialogue beats, GAS ability uses, inventory changes, faction reputation thresholds, cooldown expirations, weather changes, AI brain transitions - anything your game publishes as an event the framework doesn't know about. The custom Objective is where you bridge that event source into the progression pipeline.

Common patterns you'll likely write:

- **Signal-observer** — subscribe to a specific tag-channeled signal your game publishes, apply your own completion logic.
- **Timer-based** — start a `FTimerManager` handle in `OnObjectiveActivated`, complete on expiry.
- **World-state-fact-based** — subscribe to a specific WorldState fact tag, complete when it appears or disappears.
- **Composite** — wrap other Objectives with ANY/ALL semantics for cases the graph combinators don't cover cleanly.

The v0.8.0 release ships reference implementations for the timer, world-state-fact, and composite patterns. Until then they're straightforward to write against the base class.

---

## Extending the Plugin

Three tiers of extensibility, matched to the scope of the change:

### Tier 1 — Self-describing node types (subclass + override)

Add a new quest node type by subclassing the relevant editor base class and overriding classification virtuals (`IsExitNode`, `IsContentNode`, `IsPassThroughNode`, etc.). Traversal, schema validation, and compilation all read these virtuals - no registration required. Matches Unreal's native pattern for extending `UK2Node` or `UEdGraphNode`.

### Tier 2 — Replaceable policies (subclass + register)

`FQuestlineGraphTraversalPolicy` encapsulates classification decisions used during graph traversal and compilation. Subclass it and register your subclass via `ISimpleQuestEditorModule` to override classification project-wide. Useful for projects with bespoke node-type behavior that differs from the defaults.

### Tier 3 — Factory-registered algorithms (subclass + register factory)

For full algorithmic replacement. Subclass `FQuestlineGraphCompiler` and register via `ISimpleQuestEditorModule::RegisterCompilerFactory` to take over the entire pipeline. Use when the compilation algorithm itself must change.

### Custom Orchestration

Subclass `UQuestManagerSubsystem` (C++ or Blueprint) and set it as the configured class in **Project Settings > Plugins > Simple Quest > QuestManagerClass**. Override lifecycle hooks to add analytics, integrate save systems, or inject custom activation logic without touching plugin source.

### Reacting to Quest Events

**Blueprint** — drop the **Observe Quest Lifecycle** async node, feed it a quest tag, and toggle on the lifecycle pins you care about via right-click context menu (Offer Phase: `On Activated`, `On Enabled`, `On Disabled`, `On Give Blocked`; Run Phase: `On Started`, `On Progress`, `On Completed`; End Phase: `On Deactivated`, `On Blocked`, `On Unblocked`). The observation stays bound across the quest's full lifecycle and can receive events for every descendant tag under a parent subscription (e.g. observe on `SimpleQuest.Questline.MyLine` to watch the whole line). Each pin carries the event's `FQuestEventPayload` - `TriggeredActor`, `Instigator`, `NodeInfo`, `CustomData` - plus the event-specific extras (`OutcomeTag` on Completed, `PrereqStatus` on Activated, `Blockers` on GiveBlocked, `GiverActor` on Started). The proxy subscribes only to events whose pins you've enabled, so unused subscriptions cost nothing. Call `Cancel` on the returned `Observer` reference when you're done, or let the GameInstance tear it down.

**C++** — use the library template for direct handle-based subscriptions:

```c++
#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"
#include "Events/QuestStartedEvent.h"

FDelegateHandle Handle = USimpleQuestBlueprintLibrary::SubscribeToQuestEvent<FQuestStartedEvent>(
    this, QuestTag, this, &AMyActor::HandleQuestStarted);

// ... later, to unsubscribe:
USimpleQuestBlueprintLibrary::UnsubscribeFromQuestEvent(this, QuestTag, Handle);
```

Same semantics as the async action, but returns a raw `FDelegateHandle` for caller-managed lifetime. Guards against stale tags via `IsTagRegisteredInRuntime` and returns an invalid handle if the subsystem or tag can't be resolved.

---

## Configuration

**Project Settings > Plugins > Simple Quest** — runtime quest manager class plus per-channel log verbosity dials.

**Project Settings > Plugins > Simple Core** — `LogSimpleCore` verbosity dial.

**Editor Preferences > Plugins > Simple Quest Visuals** — wire, pin, node title, and debug-highlight colors used by the questline graph editor. Per-developer; not committed to source control.

**Log verbosity** — SimpleQuest's logging is split into five channels for independent dial control:

| Channel | Coverage                                                                                         |
|---|--------------------------------------------------------------------------------------------------|
| `LogSimpleQuest` | Module startup, settings, debug overlay, and anything not covered by a specialized channel below |
| `LogSimpleQuestActivation` | Quest activation flow - starts, chain advancement, deactivation                                  |
| `LogSimpleQuestSubscription` | Component and Blueprint subscriptions; catch-up event delivery                                   |
| `LogSimpleQuestCompiler` | Graph compile output, native tag registration, tag rename propagation                            |
| `LogSimpleQuestState` | Quest history recording - resolutions, entries, tag registrations                                |

SimpleCore logs under `LogSimpleCore`. Set verbosity per channel via the Project Settings pages above - changes apply live without editor restart. The `[Core.Log]` section in `DefaultEngine.ini` still works as a fallback for non-editor builds.

Log statements at `VeryVerbose` are stripped entirely in Shipping builds.

**Compiled tags INI** — the compiler persists registered Gameplay Tags to `Plugins/SimpleQuest/Config/Tags/SimpleQuestCompiledTags.ini` for startup availability before the Asset Registry finishes loading. This file is auto-generated; manual edits are overwritten on each compile. It lives in the plugin folder so adopters inherit the demo's compiled tags by copying the plugin in, zero-config.

**Authored tags INI** — the plugin ships a default tag set in `Plugins/SimpleQuest/Config/Tags/SimpleQuestAuthoredTags.ini` (the example activation groups, prereq rule groups, and named outcomes the demo content references). Your project's own gameplay tags should go in `<YourProject>/Config/Tags/*.ini` per standard UE convention - Unreal auto-scans the project's tag config directory. Edit the plugin's authored-tags file only if you're contributing to SimpleQuest itself; project-level edits won't be clobbered on plugin upgrade.

---

## Roadmap

| Quarter  | Deliverable                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | Status                             |
|----------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------|
| Q2 2026  | Visual graph editor + SimpleCore foundation                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | **Shipped** (v0.3.0)               |
| Q2 2026  | Objective activation lifecycle (typed params, origin chain, giver + runtime + step-handoff merge)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | **Shipped** (v0.3.1)               |
| Q2 2026  | Authoring diagnostics + runtime hardening (prereq validator, stale-tag cleanup panel, comment blocks, duplicate-outcome compile warning, event-subscription async action, soft class references)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | **Shipped** (v0.3.2)               |
| Q2 2026  | Catch-up outcome recovery + two-layer state foundations (`UQuestStateSubsystem` rich-record store + BindToQuestEvent reliability fixes + pin-precise drag-create alignment)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | **Shipped** (v0.3.3)               |
| Q2 2026  | Stale Quest Tags Tier 2 — project-wide stale-tag scanning (Actor Blueprint defaults + unloaded levels including World Partition; editor panel Full Project Scan + headless commandlet with CI-friendly exit codes)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | **Shipped** (v0.3.4)               |
| Q2 2026  | Stale Quest Tags polish + design captures (multi-row mass-clear with confirmation + atomic undo, sortable Level column, sub-millisecond per-actor PostUndo rescan, designer-facing log clarity pass; rewards-design + scope-tag system docs)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | **Shipped** (v0.3.5)               |
| Q2 2026  | Architectural cohesion + adopter ergonomics — distinct lifecycle events for offer-availability, accept-readiness, give-refusal, and activation failure; trigger response surface (per-fire response, structural-block, and per-lifecycle feedback delegates on the Trigger Component); rich payload propagation across all activation entry points; `SimpleQuest.*` namespace finalization with transparent migration redirects; tag rename resilience across Blueprints, components, and data assets; pin-wired prereq Path/Outcome separation with new Prerequisite Fact Tag and Prerequisite Outcome authoring nodes; outcome-channel event publishing for cross-quest subscribers; Blueprint-callable signal bus subscriptions plus `FSignalEventBase` marker for picker filtering; component model unification (single Giver component replaces stacked components); per-channel log verbosity; Electronic Nodes integration rewritten against the stock marketplace plugin on 5.7+ (no fork dependency); UE 5.7 compatibility verified | **Shipped** (v0.4.0)               |
| Q2 2026  | Authoring primitives + subscriber routing — Prereq Gate utility node; Add / Remove / Clear Facts nodes (questline graphs as first-class World State publishers); Resettable Replay prerequisite setting for honest re-gating on replayable content; subscriber-side hierarchical-vs-exact routing control; observer surface broadening (catch-all `OnAnyQuestEvent`, run-phase `ProgressRefused`); runtime add/remove of component watched-tag sets for dynamic-spawn join-in-progress; quest-resolution attribution fixes                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | **Shipped** (v0.4.1)               |
| Q3 2026  | Save/Load system — struct-based snapshot embedded in your own save game, with mid-step state handling                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | **Shipped** (v0.5.0)               |
| Q3 2026  | Rewards — first-class reward nodes + self-configuring reward adapters, broadcast-to-recipient delivery, the "do this, get this" advertisement query surface, and questline-level rewards                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | **Shipped** (v0.6.0, fixes v0.6.1) |
| Q3 2026  | Pluggable data resolver — user-writable adapters for bidirectional graph ↔ text data pipelines; version-controllable, diff-friendly progression data outside .uassets, fitting existing studio content workflows                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | ***Active Development*** (v0.7.0) |
| Q1 2026  | Sample project + expanded objective library — a complete playable progression exercising branching outcomes, prerequisites, linked questlines, rewards, and save/load end to end; timer, world-state-fact, and composite reference objectives built alongside it | Planned (v0.8.0) |
| Q2 2027  | Documentation pass — full authoring and API documentation grounded in the sample project's real usage | Planned (v0.9.0) |
| Q2 2027  | SimpleCore public API versioning + 1.0 polish — freeze the coordination-layer contract once SimpleQuest, the sample project, and early adopters have exercised it | Planned (v1.0.0) |
| Post-1.0 | Multiplayer replication — server-authoritative quest state with join-in-progress                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | Pro Module                         |
| Post-1.0 | GAS integration — GameplayTag identifiers, GameplayEffect rewards, Gameplay Event triggers                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | Pro Module                         |

---

## Contributing

Community feedback is welcome and valuable at this stage. If you encounter a bug, a compatibility issue, or have a feature request, please open an issue with the engine version and a description of the problem or suggestion. For faster support, join the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS).

Code contributions via pull request will be reviewed by the author. For contribution guidelines, see the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS).

For bug reports, include the engine version, a minimal reproduction case, and any relevant output from the `LogSimpleQuest` and `LogSimpleCore` log categories.

---

## License

SimpleQuest is licensed under the standard [MIT License](LICENSE).
