# SimpleQuest
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) ![Unreal Engine](https://img.shields.io/badge/Unreal-5.6%20%7C%205.7-dea309?style=flat&logo=unrealengine)

MIT licensed, source-available Unreal Engine plugin for graph-authored, non-linear quest systems. Designers compose questlines visually — nesting prerequisite expressions, naming outcomes, routing through activation groups and reusable rules — and the authored graph compiles into runtime quest data.
<img width="1913" height="1013" alt="SimpleQuestDemo-0 3 0-quick-build" src="https://github.com/user-attachments/assets/43efee3f-d276-4a39-b632-ceb2d465ee34" />

This version targets Unreal Engine 5.6 and is verified compatible with 5.7; the Electronic Nodes visual integration activates automatically on 5.7+ when EN is installed.

Please visit the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS) for community showcases and additional support, including bug reports and feature requests.

See [CHANGELOG.md](CHANGELOG.md) for version history.

---

## Highlights

- **Visual graph editor** — author questlines as a composable node graph. Designer-friendly node widgets with inline tag pickers and dynamic pin layouts; in-editor compile with clickable diagnostics that jump straight to the offending node.
- **Nested prerequisite expressions** — AND / OR / NOT combinators plus reusable Prerequisite Rules. Any content node can gate activation, progression, or completion on an arbitrarily deep boolean expression authored inline.
- **Named outcomes** — nodes resolve with designer-authored outcome tags. A combat step can complete with `Victory`, `Retreat`, or `Negotiated`, and downstream wiring routes each outcome independently. No binary success/failure constraint.
- **Structured objective activation** — activation delivers a typed `FQuestObjectiveActivationContext` struct: target actors, target classes, element counts, custom payload (`FInstancedStruct`), activation source. Step authoring, giver components, Blueprint-callable activation, and step-to-step handoff all contribute — values merge additively at activation time. An origin chain tracks "who activated me" across Quest and LinkedQuestline boundaries, so branching on the activating actor doesn't need glue code.
- **Linked questlines** — reference one questline graph asset from inside another. The compiler inlines the linked content with full tag resolution in both contexts — subscribers bound through the parent path AND subscribers bound through the standalone path both receive the events, with no duplication.
- **Tag rename resilience** — rename a quest node or asset and every reference updates on the next compile: Blueprints, components, data assets, data tables, custom structs. Assets that weren't loaded at rename time heal automatically on next load and flag for save. The editor blocks renames that would silently rebind active subscriptions to a different node, so you never get a quiet "wrong-listener" bug.
- **Live PIE inspection** — colored halos on graph nodes show which states each node is in; the Prereq Examiner tints individual conditions by whether they're satisfied; a searchable World State Facts panel lists every asserted fact. No log-diving required to understand the running state.
- **Authoring diagnostics** — **Prereq Tag Validator** runs a project-wide scan and flags broken cross-graph references: prerequisite conditions pointing at missing fact tags, Rule Exits pointing at missing Rule Entries, unused Rule Entries. **Stale Quest Tags** covers the full project's component tag references (loaded levels, Actor Blueprint defaults, unloaded levels including World Partition) with per-row navigation, single-and-multi-select Clear with confirmation + undo, and a headless commandlet variant for CI gating.
- **Two-plugin architecture** — SimpleQuest sits on top of **SimpleCore**, a standalone coordination layer any system can use independently.

![SimpleQuestDemo-slate-node-preview-v2](https://github.com/user-attachments/assets/9cc8e4f9-ee60-46fc-881a-5c45e38ff9d4)

---

## Two-Plugin Architecture

SimpleQuest is a quest and narrative system built on SimpleCore. SimpleCore has zero knowledge of quests — it's a standalone coordination layer that any plugin or project system can use. Future suite plugins (dialogue, progression, inventory) will depend only on SimpleCore, inheriting automatic interoperability with SimpleQuest.

### SimpleCore

Foundational coordination layer. Runtime + editor modules. Free and unrestricted.

- **`USignalSubsystem`** — gameplay-tag-routed event bus. Publishers send events on a tag; the bus delivers them to subscribers bound on that tag or any ancestor tag in the hierarchy. Each callback receives the original published tag, so a subscriber on a parent tag knows which descendant fired. When the same logical event publishes under multiple tags simultaneously (a common case with linked questlines, where one node carries both its standalone tag and inlining-context aliases), the bus collapses delivery so each subscriber gets exactly one callback — not one per matching tag. Compared to Unreal's built-in `GameplayMessageRouter`: GMR excels at flat tag-keyed routing; `USignalSubsystem` adds hierarchical descendant-aware delivery plus the multi-tag deduplication that hierarchical-event systems need.
- **`UWorldStateSubsystem`** — queryable, gameplay-tag-keyed fact store with integer reference counts. Add a fact, query whether it's present, remove it. Transition events fire when a fact appears (count goes 0→1) or disappears (1→0), so subscribers can react to state changes without polling. Components that register late read current truth directly — safe for streaming, dynamic spawn, multiplayer join-in-progress, and save game restoration.
- **`SimpleCoreEditor`** — editor module. Provides the World State Facts inspector panel and PIE debug channel. Usable without SimpleQuest.

### SimpleQuest

Quest and narrative system. Runtime + editor modules, with an optional Electronic Nodes integration module.

- Tag-addressed runtime. Every compiled quest node is a `UObject` addressed by a `FGameplayTag`. The manager subsystem, components, and event bus all route by tag.
- Pull-based prerequisite activation. Quest nodes waiting on prerequisites subscribe to the relevant World State fact tags. When all conditions are met the node activates — no polling, no ticking.
- Hierarchical catch-up for late subscribers. Givers, observers, and `Observe Quest Lifecycle` Blueprint nodes that register after a quest event has already fired receive the recorded state immediately — original outcome, prerequisite snapshot, blocker information, the giver actor that initiated activation, the activation source (giver / cascade / external API / initial entry), and the merged-final activation parameters. Subscriptions bound on a parent tag (e.g. `SimpleQuest.Questline.MyArc`) fan out to every known descendant on bind, mirroring the live bus's hierarchical delivery for catch-up.
- Two-layer state architecture. World State answers "did it happen?" with boolean fact storage. `UQuestStateSubsystem` answers "what's the structured detail?" with rich event records — the same data the save/load system uses to reconstruct a session. The manager subsystem owns all writes; `UQuestStateSubsystem` is the public read surface. Future suite plugins will follow the same `...StateSubsystem` convention for their domain.
- Outcome-filtered observers. An observer component can filter which outcomes it responds to; empty filter means all outcomes.

---

## Requirements

- Unreal Engine 5.6 or 5.7
- Visual Studio 2022 (Windows) or Xcode (Mac) with C++20 support enabled
- [Git LFS](https://git-lfs.com/) installed locally (required for cloning — see below)

---

## Installation

> **Important — do not use GitHub's "Code → Download ZIP" button.** This repository uses Git LFS for `.uasset` and `.umap` binaries. The ZIP archive endpoint does not resolve LFS pointers, so the download will contain ~130-byte stub files instead of the real assets. Clone the repo (with `git-lfs` installed) or download a Release artifact instead.

1. Install [Git LFS](https://git-lfs.com/) and run `git lfs install` once.
2. Clone the repository: `git clone https://github.com/TheGeebus/SimpleQuestDemo.git` — LFS pointers resolve automatically during clone.
3. Copy the `SimpleCore` and `SimpleQuest` folders from the cloned `Plugins/` directory into your project's `Plugins/` directory. The plugins are zero-config — compiled tags, designer-authored tags, and demo content all travel with the plugin folders.
4. Right-click your `.uproject` file and select **Generate Visual Studio project files**.
5. Open the solution and build the **Development Editor** target.
6. Enable both plugins in **Edit > Plugins** if they are not already active.

To use SimpleQuest as a source dependency in another plugin, add `"SimpleQuest"` (and `"SimpleCore"` where coordination APIs are consumed directly) to your `.uplugin` or `Build.cs` dependencies.

---

## Quick Start

### 1. Create a Questline Graph asset

Right-click in the Content Browser and select **Quest > Questline Graph**. This creates a `UQuestlineGraph` asset containing an empty editor graph with only an Entry node and a default Outcome terminal.

### 2. Author the graph

Open the asset to launch the graph editor. From Entry, drag off a wire and place a **Quest** node. Quests are composed of ordered **Step** nodes with inline objective class pickers — drop the objective type from the Step widget, assign targets and parameters, and wire outcomes to downstream paths.

Useful constructs as your graph grows:
- **AND / OR / NOT combinators** — wire into any node's `Prerequisites` pin to gate activation on a boolean expression.
- **Prerequisite Rule Entry / Exit** — define a named rule once; reference it from multiple content nodes without duplication.
- **Prerequisite Fact Tag** — gate on any World State fact tag. Decoupled from graph topology, so you can compose with any UE gameplay system that tracks tag-keyed state (faction reputation, inventory, system flags).
- **Prerequisite Outcome** — gate on a context-free outcome tag. Satisfied when any quest resolves with the picked outcome, regardless of which quest produced it.
- **Activation Group Entry / Exit** — many-to-many node activation topology without per-wire bookkeeping.
- **LinkedQuestline** — reference another questline asset inline; the compiler expands it with full outcome pin synchronization.

The Questline Outliner tab, Group Examiner, and Prereq Expression Examiner panels all provide read-only inspection of the graph's structure — particularly useful as graphs grow beyond a single screen.

### 3. Compile the graph

Hit the **Compile** button on the graph editor toolbar (or **Compile All** from the editor's main menu). The compiler generates runtime node instances and registers the required Gameplay Tags. Errors and warnings appear in the message log with clickable navigation to the offending node.

### 4. Activate at runtime

Call `USimpleQuestBlueprintLibrary::StartQuestline(UQuestlineGraph*)` from any startup hook of your choosing — player pawn `BeginPlay`, GameMode `BeginPlay`, a custom GameInstance subsystem, a dialogue trigger, save-load rehydration, level-streaming callback, etc. One pattern serves static startup, procedural orchestration, and dynamic activation alike.

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

```
UQuestlineGraph (asset, authored in the graph editor)
  │
  │  (FQuestlineGraphCompiler)
  ▼
Compiled nodes + Gameplay Tags (registered as native at game start)
  │
  ▼
UQuestManagerSubsystem.ActivateQuestlineGraph()
  │
  ▼
UQuestNodeBase instances (Quest, Step, portals) — keyed by compiled tag
  │
  │  (USignalSubsystem — tag-hierarchy publish)
  │  (UWorldStateSubsystem — persistent fact store)
  ▼
Subscribers: observers, givers, targets, UI
```

### Compiler

Editor-side compilation translates authored graphs into runtime node instances and compiled Gameplay Tags. The compiler is factory-registered via `ISimpleQuestEditorModule::RegisterCompilerFactory` — subclass `FQuestlineGraphCompiler` and register your factory to replace the pipeline wholesale. The default compiler supports LinkedQuestline expansion, named outcomes, prerequisite expression flattening, and cross-graph signal resolution.

### Runtime

`UQuestManagerSubsystem` orchestrates activation, deactivation, prerequisite monitoring, and quest lifecycle events. Scoped to `GameInstance` so quest state persists across level transitions. Every quest node carries a stable `QuestGuid` (save identity) and `QuestTag` (routing identity) — GUIDs survive rename operations, tags resolve to specific nodes on load.

### Coordination layer (SimpleCore)

`USignalSubsystem` and `UWorldStateSubsystem` provide the underlying tag routing and fact storage. SimpleQuest is a consumer of these subsystems — it does not implement its own event bus or state store. Any system in your project can use the same coordination primitives without touching SimpleQuest.

---

## Extending the Plugin

Three tiers of extensibility, matched to the scope of the change:

### Tier 1 — Self-describing node types (subclass + override)

Add a new quest node type by subclassing the relevant editor base class and overriding classification virtuals (`IsExitNode`, `IsContentNode`, `IsPassThroughNode`, etc.). Traversal, schema validation, and compilation all read these virtuals — no registration required. Matches Unreal's native pattern for extending `UK2Node` or `UEdGraphNode`.

### Tier 2 — Replaceable policies (subclass + register)

`FQuestlineGraphTraversalPolicy` encapsulates classification decisions used during graph traversal and compilation. Subclass it and register your subclass via `ISimpleQuestEditorModule` to override classification project-wide — useful for projects with bespoke node-type behavior that differs from the defaults.

### Tier 3 — Factory-registered algorithms (subclass + register factory)

For full algorithmic replacement. Subclass `FQuestlineGraphCompiler` and register via `ISimpleQuestEditorModule::RegisterCompilerFactory` to take over the entire pipeline. Use when the compilation algorithm itself must change.

### Custom Objectives

Subclass `UQuestObjective` (or `UCountingQuestObjective` for progress-tracking objectives). Override `OnObjectiveActivated_Implementation` to consume the typed activation params, and `TryCompleteObjective_Implementation` to decide when to resolve — calling `CompleteObjectiveWithOutcome` with the named outcome tag:

```cpp
UCLASS(Blueprintable)
class UMyObjective : public UQuestObjective
{
    GENERATED_BODY()

protected:
    virtual void OnObjectiveActivated_Implementation(const FQuestObjectiveActivationContext& Params) override
    {
        // Read Params.TargetActors, Params.NumElementsRequired, Params.CustomData (FInstancedStruct), Params.OriginTag, etc.
        // Wire up listeners, store local tracking state, spawn UI, and so on.
    }

    virtual void TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& Context) override
    {
        if (/* your completion condition */)
        {
            CompleteObjectiveWithOutcome(MyOutcomeTag, Context);
        }
    }
};
```

Your objective class appears in the inline objective picker on any Step node.

### Custom Orchestration

Subclass `UQuestManagerSubsystem` (C++ or Blueprint) and set it as the configured class in **Project Settings > Plugins > Simple Quest > QuestManagerClass**. Override lifecycle hooks to add analytics, integrate save systems, or inject custom activation logic without touching plugin source.

### Reacting to Quest Events

**Blueprint** — drop the **Observe Quest Lifecycle** async node, feed it a quest tag, and toggle on the lifecycle pins you care about via right-click context menu (Offer Phase: `On Activated`, `On Enabled`, `On Disabled`, `On Give Blocked`; Run Phase: `On Started`, `On Progress`, `On Completed`; End Phase: `On Deactivated`, `On Blocked`, `On Unblocked`). The observation stays bound across the quest's full lifecycle and can receive events for every descendant tag under a parent subscription (e.g. observe on `SimpleQuest.Questline.MyLine` to watch the whole line). Each pin carries the event's `FQuestEventPayload` — `TriggeredActor`, `Instigator`, `NodeInfo`, `CustomData` — plus the event-specific extras (`OutcomeTag` on Completed, `PrereqStatus` on Activated, `Blockers` on GiveBlocked, `GiverActor` on Started). The proxy subscribes only to events whose pins you've enabled, so unused subscriptions cost nothing. Call `Cancel` on the returned `Observer` reference when you're done, or let the GameInstance tear it down.

**C++** — use the library template for direct handle-based subscriptions:

```cpp
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

| Channel | Coverage |
|---|---|
| `LogSimpleQuest` | Module startup, settings, debug overlay, and anything not covered by a specialized channel below |
| `LogSimpleQuestActivation` | Quest activation flow — starts, chain advancement, deactivation |
| `LogSimpleQuestSubscription` | Component and Blueprint subscriptions; catch-up event delivery |
| `LogSimpleQuestCompiler` | Graph compile output, native tag registration, tag rename propagation |
| `LogSimpleQuestState` | Quest history recording — resolutions, entries, tag registrations |

SimpleCore logs under `LogSimpleCore`. Set verbosity per channel via the Project Settings pages above — changes apply live without editor restart. The `[Core.Log]` section in `DefaultEngine.ini` still works as a fallback for non-editor builds.

Log statements at `VeryVerbose` are stripped entirely in Shipping builds.

**Compiled tags INI** — the compiler persists registered Gameplay Tags to `Plugins/SimpleQuest/Config/Tags/SimpleQuestCompiledTags.ini` for startup availability before the Asset Registry finishes loading. This file is auto-generated; manual edits are overwritten on each compile. It lives in the plugin folder so adopters inherit the demo's compiled tags by copying the plugin in, zero-config.

**Authored tags INI** — the plugin ships a default tag set in `Plugins/SimpleQuest/Config/Tags/SimpleQuestAuthoredTags.ini` (the example activation groups, prereq rule groups, and named outcomes the demo content references). Your project's own gameplay tags should go in `<YourProject>/Config/Tags/*.ini` per standard UE convention — Unreal auto-scans the project's tag config directory. Edit the plugin's authored-tags file only if you're contributing to SimpleQuest itself; project-level edits won't be clobbered on plugin upgrade.

---

## Roadmap

| Quarter | Deliverable | Status |
|---|---|---|
| Q2 2026 | Visual graph editor + SimpleCore foundation | **Shipped** (v0.3.0) |
| Q2 2026 | Objective activation lifecycle (typed params, origin chain, giver + runtime + step-handoff merge) | **Shipped** (v0.3.1) |
| Q2 2026 | Authoring diagnostics + runtime hardening (prereq validator, stale-tag cleanup panel, comment blocks, duplicate-outcome compile warning, event-subscription async action, soft class references) | **Shipped** (v0.3.2) |
| Q2 2026 | Catch-up outcome recovery + two-layer state foundations (`UQuestStateSubsystem` rich-record store + BindToQuestEvent reliability fixes + pin-precise drag-create alignment) | **Shipped** (v0.3.3) |
| Q2 2026 | Stale Quest Tags Tier 2 — project-wide stale-tag scanning (Actor Blueprint defaults + unloaded levels including World Partition; editor panel Full Project Scan + headless commandlet with CI-friendly exit codes) | **Shipped** (v0.3.4) |
| Q2 2026 | Stale Quest Tags polish + design captures (multi-row mass-clear with confirmation + atomic undo, sortable Level column, sub-millisecond per-actor PostUndo rescan, designer-facing log clarity pass; rewards-design + scope-tag system docs) | **Shipped** (v0.3.5) |
| Q2 2026 | Architectural cohesion + adopter ergonomics — distinct lifecycle events for offer-availability, accept-readiness, give-refusal, and activation failure; trigger response surface (per-fire response, structural-block, and per-lifecycle feedback delegates on the Trigger Component); rich payload propagation across all activation entry points; `SimpleQuest.*` namespace finalization with transparent migration redirects; tag rename resilience across Blueprints, components, and data assets; pin-wired prereq Path/Outcome separation with new Prerequisite Fact Tag and Prerequisite Outcome authoring nodes; outcome-channel event publishing for cross-quest subscribers; Blueprint-callable signal bus subscriptions plus `FSignalEventBase` marker for picker filtering; component model unification (single Giver component replaces stacked components); per-channel log verbosity; Electronic Nodes integration rewritten against the stock marketplace plugin on 5.7+ (no fork dependency); UE 5.7 compatibility verified | **Shipped** (v0.4.0) |
| Q3 2026 | Save/Load system — `USaveGame` integration with mid-step state handling | Planned (v0.5.0) |
| Q4 2026 | Rewards system — designer-authored reward bundles wired to outcome resolution | Planned (v0.6.0) |
| Q1 2027 | Expanded objective library — timed, escort, collection, and conversation objectives | Planned (v0.7.0) |
| Q2 2027 | Pre-release polish + API documentation + sample project | Planned (v0.9.0 → v1.0.0) |
| Post-1.0 | Multiplayer replication — server-authoritative quest state with join-in-progress | Pro Module |
| Post-1.0 | GAS integration — GameplayTag identifiers, GameplayEffect rewards, Gameplay Event triggers | Pro Module |

---

## Contributing

Community feedback is welcome and valuable at this stage. If you encounter a bug, a compatibility issue, or have a feature request, please open an issue with the engine version and a description of the problem or suggestion. For faster support, join the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS).

Code contributions via pull request will be reviewed by the author. For contribution guidelines, see the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS).

For bug reports, include the engine version, a minimal reproduction case, and any relevant output from the `LogSimpleQuest` and `LogSimpleCore` log categories.

---

## License

SimpleQuest is licensed under the standard [MIT License](LICENSE).
