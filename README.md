
# SimpleQuest

A source-available Unreal Engine plugin for graph-authored, non-linear quest systems. Designers compose questlines visually — nesting prerequisite expressions, naming outcomes, routing through activation groups and reusable rules — and the authored graph compiles into runtime quest data. Currently free for non-commercial use, with an MIT-licensed free-use release planned following a future funding milestone.

<img width="1913" height="1013" alt="SimpleQuestDemo-0 3 0-quick-build" src="https://github.com/user-attachments/assets/43efee3f-d276-4a39-b632-ceb2d465ee34" />

This version is for Unreal Engine 5.6.

See [CHANGELOG.md](CHANGELOG.md) for version history.

[![License: Polyform NC 1.0](https://img.shields.io/badge/License-Polyform%20NC%201.0-blue.svg)](LICENSE)
![Unreal Engine](https://img.shields.io/badge/Unreal-5.6-dea309?style=flat&logo=unrealengine)

---

## Highlights

- **Visual graph editor** — author questlines as a composable node graph. Custom Slate widgets for combinator, group, utility, and step nodes; inline tag pickers; dynamic pin management; in-editor compile with clickable diagnostics that navigate to the offending node.
- **Nested prerequisite expressions** — AND / OR / NOT combinators plus reusable Prerequisite Rule portals. Any content node can gate activation, progression, or completion on an arbitrarily deep boolean expression authored inline.
- **Named outcomes** — nodes resolve with designer-authored outcome tags. A combat step can complete with `Victory`, `Retreat`, or `Negotiated`, and downstream wiring routes each outcome independently. No binary success/failure constraint.
- **Structured objective activation** — activation delivers a typed `FQuestObjectiveActivationParams` struct (target actors, classes, element counts, typed CustomData, ActivationSource). Designer-authored step defaults, giver components, event-bus publishers, and step-to-step handoff all merge additively. An OriginTag and OriginChain track the cascade path across Quest and LinkedQuestline boundaries so objectives can branch on "who activated me" without glue code.
- **Linked questlines** — reference external questline graph assets inline. The compiler inlines the linked graph with bidirectional tag resolution and dual-tag (contextual + standalone) subscription support.
- **Live PIE inspection** — per-state colored halos on graph nodes, live leaf satisfaction tinting in the Prereq Examiner, and a searchable WorldState Facts panel showing every asserted fact. No log-diving required to understand the running state.
- **Authoring diagnostics** — project-wide **Prereq Tag Validator** toolbar action flags broken cross-graph references (orphaned leaves, Rule Exits pointing at missing Rule Entries, unused Rule Entries); **Stale Quest Tags** nomad tab scans loaded levels for quest-component fields referencing unregistered tags with one-click Clear. Together they catch authoring drift the compiler can't see.
- **Two-plugin architecture** — SimpleQuest sits on top of **SimpleCore**, a standalone coordination layer any system can use independently.

![SimpleQuestDemo-slate-node-preview-v2](https://github.com/user-attachments/assets/9cc8e4f9-ee60-46fc-881a-5c45e38ff9d4)

---

## Two-Plugin Architecture

SimpleQuest is a quest and narrative system built on SimpleCore. SimpleCore has zero knowledge of quests — it's a standalone coordination layer that any plugin or project system can use. Future suite plugins (dialogue, progression, inventory) will depend only on SimpleCore, inheriting automatic interoperability with SimpleQuest.

### SimpleCore

Foundational coordination layer. Runtime + editor modules. Free and unrestricted.

- **`USignalSubsystem`** — hierarchical tag router. Subscribers on a parent gameplay tag receive events from every descendant tag. Channels keyed by tag and payload type; payloads are plain USTRUCTs delivered via `FInstancedStruct`. Designed to complement `GameplayMessageRouter`, not replace it.
- **`UWorldStateSubsystem`** — persistent queryable fact store keyed by gameplay tag with integer counts. Boundary-transition broadcasting, bulk operations, and fact-added/removed events. Late-registering components read current truth directly — safe for streaming, dynamic spawn, and multiplayer join-in-progress.
- **`SimpleCoreEditor`** — editor module. Provides the WorldState Facts inspector nomad tab and PIE debug channel. Usable without SimpleQuest.

### SimpleQuest

Quest and narrative system. Runtime + editor modules, with an optional Electronic Nodes integration module.

- Tag-addressed, instance-based runtime. Every compiled quest node is a `UObject` addressed by gameplay tag. The manager subsystem, components, and signal bus all route by tag.
- Pull-based prerequisite activation. Deferred nodes subscribe to WorldState changes per leaf tag. When all conditions are met the node activates — no polling, no ticking.
- Component catch-up. Givers, watchers, and targets that register after quest events have already fired receive the current state immediately.
- Outcome-filtered watchers. A watcher component can filter which outcomes it responds to; empty filter = all outcomes.

---

## Requirements

- Unreal Engine 5.6 or later
- Visual Studio 2022 (Windows) or Xcode (Mac) with C++20 support enabled

---

## Installation

1. Copy the `SimpleCore` and `SimpleQuest` folders into your project's `Plugins/` directory.
2. Right-click your `.uproject` file and select **Generate Visual Studio project files**.
3. Open the solution and build the **Development Editor** target.
4. Enable both plugins in **Edit > Plugins** if they are not already active.

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
- **Activation Group Entry / Exit** — many-to-many node activation topology without per-wire bookkeeping.
- **LinkedQuestline** — reference another questline asset inline; the compiler expands it with full outcome pin synchronization.

The Questline Outliner tab, Group Examiner, and Prereq Expression Examiner panels all provide read-only inspection of the graph's structure — particularly useful as graphs grow beyond a single screen.

### 3. Compile the graph

Hit the **Compile** button on the graph editor toolbar (or **Compile All** from the editor's main menu). The compiler generates runtime node instances and registers the required Gameplay Tags. Errors and warnings appear in the message log with clickable navigation to the offending node.

### 4. Register at game start

Subclass `UQuestManagerSubsystem` (Blueprint or C++), add your compiled questline to the subclass's `InitialQuestlines` array, and register the subclass in **Project Settings > Plugins > Simple Quest > QuestManagerClass**. The subsystem activates everything in `InitialQuestlines` on game start.

For programmatic activation (e.g. from a dialogue trigger, save-load handler, or level streaming callback), call `ActivateQuestlineGraph(UQuestlineGraph*)` on the running subsystem instance.

### 5. Attach components to actors

| Component | Attach to | Purpose |
|---|---|---|
| `UQuestPlayerComponent` | Player Pawn or PlayerState | Tracks the local player's quest state |
| `UQuestGiverComponent` | NPC Actor | Offers and activates quests on interaction |
| `UQuestTargetComponent` | Enemy, item, or location Actor | Responds to trigger, kill, and interact events |
| `UQuestWatcherComponent` | Any Actor | Receives lifecycle events for one or more quests |

### 6. Inspect during PIE

Start Play In Editor. The graph panel shows per-state colored halos on content nodes (active, completed, blocked, etc.). Open the **Window > Developer Tools > Debug > World State Facts** panel for a searchable live view of every asserted fact. Hover any leaf in the Prereq Examiner to see whether it's satisfied, unsatisfied, or in-progress.

### 7. Catch authoring drift

Two additional surfaces surface broken or stale tag references the compiler can't flag on its own:

- **Validate Tags** (questline graph editor toolbar) — project-wide scan for prereq leaves pointing at missing fact tags, Rule Exits pointing at missing Rule Entries, and unused Rule Entries. Results go to the **Quest Validator** message log with per-node navigation tokens. Read-only; never modifies assets.
- **Stale Quest Tags** (Window > Developer Tools > Debug) — nomad tab listing quest-component tag references in loaded levels whose target isn't registered in the runtime tag manager. Per-row Find (selects and frames the actor) and Clear (removes the stale tag, marks the actor dirty). Filter bar + sortable columns.

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
Subscribers: watchers, givers, targets, UI
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
    virtual void OnObjectiveActivated_Implementation(const FQuestObjectiveActivationParams& Params) override
    {
        // Read Params.TargetActors, Params.NumElementsRequired, Params.CustomData (FInstancedStruct), Params.OriginTag, etc.
        // Wire up listeners, store local tracking state, spawn UI, and so on.
    }

    virtual void TryCompleteObjective_Implementation(const FQuestObjectiveContext& Context) override
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

**Blueprint** — drop the **Bind To Quest Event** async node, feed it a quest tag, and wire any of the four output exec pins you care about: `On Activated`, `On Started`, `On Completed` (with `OutcomeTag`), `On Deactivated`. The subscription stays bound across the quest's full lifecycle and can receive events for every descendant tag under a parent subscription (e.g. subscribe on `Quest.MyLine` to watch the whole line). Each pin carries the event's `FQuestEventContext` — `TriggeredActor`, `Instigator`, `NodeInfo`, `CustomData`. Call `Cancel` on the returned subscription when you're done, or let the GameInstance tear it down.

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

**Project Settings > Plugins > Simple Quest** — configure the runtime quest manager class, wire/pin/node color schemes, and hover highlight color.

**Log verbosity** — SimpleQuest logs under the `LogSimpleQuest` category and SimpleCore under `LogSimpleCore`. Set verbosity in `DefaultEngine.ini`:

```ini
[Core.Log]
LogSimpleQuest=Verbose
LogSimpleCore=Verbose
```

Log statements at `VeryVerbose` are stripped entirely in Shipping builds.

**Compiled tags INI** — the compiler persists registered Gameplay Tags to `Config/SimpleQuest/CompiledTags.ini` for startup availability before the Asset Registry finishes loading. This file is auto-generated; manual edits are overwritten on each compile.

---

## Roadmap

| Quarter | Deliverable | Status |
|---|---|---|
| Q2 2026 | Visual graph editor + SimpleCore foundation | **Shipped** (v0.3.0) |
| Q2 2026 | Objective activation lifecycle (typed params, origin chain, giver + runtime + step-handoff merge) | **Shipped** (v0.3.1) |
| Q2 2026 | Authoring diagnostics + runtime hardening (prereq validator, stale-tag cleanup panel, comment blocks, duplicate-outcome compile warning, event-subscription async action, soft class references) | **Shipped** (v0.3.2) |
| Q3 2026 | Save/Load system — `USaveGame` integration with mid-step state handling | Planned |
| Q3 2026 | Multiplayer replication — server-authoritative quest state with join-in-progress | Planned |
| Q4 2026 | GAS integration module — GameplayTag identifiers, GameplayEffect rewards, Gameplay Event triggers | Planned |
| Q1 2027 | Expanded objective library — timed, escort, collection, and conversation objectives | Planned |
| Q1 2027 | Example project and full API documentation | Planned |

---

## Contributing

Community feedback is welcome and valuable at this stage. If you encounter a bug, a compatibility issue, or have a feature request, please open an issue with the engine version and a description of the problem or suggestion.

Code contributions via pull request are not being accepted during the current pre-release phase while licensing terms are being finalized. This will be revisited ahead of the first public release. Watch the repository for updates.

For bug reports, include the engine version, a minimal reproduction case, and any relevant output from the `LogSimpleQuest` and `LogSimpleCore` log categories.

---

## License

SimpleQuest is licensed under [Polyform Noncommercial 1.0.0](LICENSE). It is free to use, modify, and distribute for any non-commercial purpose. Commercial use requires a separate license.

The project is planned to relicense to MIT upon a future funding milestone, at which point it will be free for all uses without restriction. Commercial licensing terms will be announced separately ahead of that transition.
