# SimpleQuest

A source-available Unreal Engine plugin for non-linear, branching quest systems. SimpleQuest is built on a directed acyclic graph of quest steps with a typed publish/subscribe event bus, giving designers the freedom to craft questlines that feel alive rather than scripted. Currently free for non-commercial use, an upcoming MIT licensed free-use release is planned following a future funding milestone.

![SimpleQuestDemo-v0 3](https://github.com/user-attachments/assets/4c800c0e-f417-4e22-941d-824b321b7cfc)

This version is for Unreal Engine 5.6.

See [CHANGELOG.md](CHANGELOG.md) for version history.

[![License: Polyform NC 1.0](https://img.shields.io/badge/License-Polyform%20NC%201.0-blue.svg)](LICENSE)

---

## Dev Branch — Upcoming Pre-Release

> **The `dev` branch contains a ground-up rewrite of SimpleQuest and introduces SimpleCore, a new foundational plugin.** Everything below describes work that is feature-complete on `dev` and targeting a pre-release in the near future. The `main` branch still reflects the older class-based system described in the rest of this README.

### SimpleCore — Free Coordination Layer

SimpleQuest now sits on top of **SimpleCore**, a standalone plugin providing general-purpose game infrastructure that any system can use — not just quests.

- **Hierarchical Tag Router** — A signal bus where subscribing to a parent gameplay tag receives events from all descendant tags. Designed to complement (not replace) Epic's GameplayMessageRouter. Decoupled from actors — any system can publish or subscribe.
- **World State Subsystem** — A persistent, queryable fact store keyed by gameplay tags with integer counts. Boundary-transition broadcasting, bulk operations, and fact-added/removed events. Think GAS tag counting without the ability system coupling.

### Visual Graph Editor

The headline feature. Quest authoring moves from data arrays to a full node-based graph editor inside the Unreal editor.

- **Recursive graph model** — Quests and steps can nest to any depth in any combination. No fixed "questline > quest > step" hierarchy — the graph structure is whatever the design requires.
- **Named outcomes** — Nodes resolve with designer-authored outcome tags (not binary success/failure). A combat step can complete with `Victory`, `Retreat`, or `Negotiated`, and downstream wiring routes each outcome independently.
- **Prerequisite combinators** — AND, OR, and NOT operator nodes wire into any content node's prerequisite input. Prerequisites can gate activation, gate progression, or defer completion depending on the gate mode.
- **Deactivation system** — Nodes can be externally deactivated, blocked, and unblocked. Deactivation cascades, block/clear utility nodes, and group signal nodes enable complex quest state machines.
- **Linked questline nodes** — Reference external questline graph assets inline. The compiler inlines the linked graph's wiring with bidirectional tag resolution and dual-tag (contextual + standalone) support.
- **Live editing feedback** — Step nodes display objective class, target actors/classes, watching givers and targets from loaded levels, gate mode, reward class, and named outcomes at a glance. Expand for full detail. Red border when configuration is incomplete.
- **Multi-pass compiler** — Compiles the full graph tree into flat runtime data with clickable error and warning messages that navigate directly to the offending node.
- **Tag rename propagation** — Rename a node, recompile, and all quest components in loaded levels are automatically updated. No broken references.

### Runtime Overhaul

The runtime has been rebuilt from scratch around gameplay tags and UObject instances.

- **Tag-addressed, instance-based** — Every quest node is a `UObject` instance addressed by gameplay tag. No class-per-quest, no struct arrays. The subsystem, components, and signal bus all route by tag.
- **Pull-based prerequisite activation** — Deferred activation subscribes to world state changes per prerequisite leaf tag. When all conditions are met, the node activates. No polling, no ticking.
- **Component catch-up** — Givers, watchers, and targets that register after quest events have already fired (streaming, late spawn, join-in-progress) receive the current state immediately on registration.
- **Outcome-filtered watchers** — Watcher components can filter which outcomes they respond to. Empty filter = all outcomes (backward compatible).

### What's Left Before Pre-Release

The graph and runtime are feature-complete. Remaining work before the first external testing round:

- Stale pin preservation (prevent silent wire breakage on node config changes)
- Stale tag cleanup on compilation
- Self-loop connection rule tightening
- Packaged build tag registration
- Logging pass and readability pass for external contributors

---

## Features (Main Branch)

- **Non-linear quest graph** -- Quest steps form a DAG with bidirectional prerequisite and next-step edges. Multiple steps can be active simultaneously, branches can converge, and completing one path can permanently close or re-enable another.
- **Typed publish/subscribe event bus** -- Channels keyed by `(UObject*, EventType)` pairs guarantee that events are structurally unreachable by unintended subscribers. No conditional filtering required at any call site.
- **Blueprint/C++ parity** -- All objectives, rewards, and components are fully accessible from Blueprint. Core systems are implemented in C++ with `BlueprintNativeEvent` override points throughout.
- **Late-registration state replay** -- Components that register after quest events have already fired receive the current in-flight state automatically. Safe for streaming levels, dynamically spawned actors, and multiplayer join-in-progress scenarios.
- **Editor-time validation** -- Asset validation via `IAssetRegistry` detects duplicate Quest IDs across the entire project at cook time. Runtime collision detection provides a second pass on load.
- **Extensible without forking** -- Subclass `UQuestManagerSubsystem` and inject it via project settings to add custom orchestration logic without modifying plugin source.
- **Save-ready by design** -- Each quest asset carries a stable `QuestID` property intended as a save key. Compatible with `USaveGame` out of the box.
- **CoreRedirects included** -- `Config/DefaultSimpleQuest.ini` maintains backward compatibility when classes or properties are renamed.

---

## Requirements

- Unreal Engine 5.6 or later
- Visual Studio 2022 (Windows) or Xcode (Mac) with C++20 support enabled

---

## Installation

1. Copy the `SimpleQuest` folder into your project's `Plugins/` directory.
2. Right-click your `.uproject` file and select **Generate Visual Studio project files**.
3. Open the solution and build the **Development Editor** target.
4. Enable the plugin in **Edit > Plugins** if it is not already active.

To use SimpleQuest as a source dependency in another plugin, add `"SimpleQuest"` to your `.uplugin` or `Build.cs` dependencies.

---

## Quick Start
_(Soon to be replaced with visual graph questline authoring)_

### 1. Create a Quest asset

Right-click in the Content Browser, select **Blueprint Class**, and choose `Quest` as the class. Open the asset and assign a unique **Quest ID**.

### 2. Add steps

Each `FQuestStep` in the `Steps` array carries:

- **ObjectiveClass** -- the `UQuestObjective` subclass to instantiate when this step activates
- **PrerequisiteStepIDs** -- indices of steps that must complete before this one activates
- **NextStepIDs** -- indices of steps to unlock when this one completes
- **TargetActors / TargetClass** -- optional actor references passed to the objective on activation

Steps with no prerequisites activate immediately when the quest starts. Steps with prerequisites activate the moment all of their prerequisites are satisfied, without any polling.

### 3. Start the quest

```cpp
UQuestManagerSubsystem* QuestManager = GetGameInstance()->GetSubsystem<UQuestManagerSubsystem>();
QuestManager->StartQuest(UMyQuest::StaticClass());
```

From Blueprint, call **Start Quest** on the **Quest Manager Subsystem** node and pass your quest class.

### 4. Attach components to actors

| Component | Attach to | Purpose |
|---|---|---|
| `UQuestPlayerComponent` | Player Pawn or PlayerState | Tracks the local player's quest state |
| `UQuestGiverComponent` | NPC Actor | Offers and activates quests on interaction |
| `UQuestTargetComponent` | Enemy, item, or location Actor | Responds to trigger, kill, and interact events |
| `UQuestWatcherComponent` | Any Actor | Receives lifecycle events for one or more quests |

---

## Architecture

```
UQuest (data asset)
  └─ UQuestManagerSubsystem.StartQuest()
       └─ Activates FQuestSteps with no unmet prerequisites
            └─ Instantiates UQuestObjective per step
                 └─ Notifies registered UQuestTargetComponents
                      via UQuestSignalSubsystem
                          └─ Player interaction calls
                               QuestManagerSubsystem.CountQuestElement()
                                   └─ Objective.TryCompleteObjective()
                                        └─ Unlocks next steps,
                                             ends quest, or
                                             starts next quest
```
Steps within a quest form a directed acyclic graph. The broader quest network supports cycles to enable replayability and conditional re-activation.

### UQuestManagerSubsystem

The orchestration hub. Maintains maps of registered givers and watchers, validates prerequisites, progresses steps, and publishes lifecycle events. Scoped to the `GameInstance` so quest state persists across level transitions.

### UQuestSignalSubsystem

A typed pub/sub event bus. Channels are keyed by `(UObject*, UScriptStruct*)` pairs. Subscribers capture weak object pointers and are silently dropped on broadcast if the subscriber has been garbage collected. C++20 `derived_from` concept constraints enforce type safety at compile time.

---

## Extending the Plugin

### Custom Objective

Subclass `UQuestObjective` and override `TryCompleteObjective`. The subsystem calls this each time `CountQuestElement` is invoked for the relevant step.

```cpp
UCLASS(Blueprintable)
class UMyObjective : public UQuestObjective
{
    GENERATED_BODY()

public:
    virtual bool TryCompleteObjective() override
    {
        return CurrentElements >= MaxElements;
    }
};
```

Override `SetObjectiveTarget` to receive the target actor assigned in the step definition.

### Custom Reward

Subclass `UQuestReward` and implement your reward grant logic. Assign the class to the `RewardClass` field on any `FQuestStep`.

### Custom Orchestration

Subclass `UQuestManagerSubsystem` and register it in **Project Settings > SimpleQuest** via `UGameInstanceSubsystemInitializer`. Use this to add analytics hooks, custom prerequisite logic, or save system integration without touching plugin source.

---

## Configuration

**Log verbosity** -- SimpleQuest logs under the `LogSimpleQuest` category. Set verbosity in `DefaultEngine.ini`:

```ini
[Core.Log]
LogSimpleQuest=Verbose
```

Log statements at `VeryVerbose` are stripped entirely in Shipping builds.

**CoreRedirects** -- When renaming any public class or property, add a redirect to `Config/DefaultSimpleQuest.ini` to avoid breaking existing consumers.

---

## Roadmap

| Quarter | Deliverable | Status |
|---|---|---|
| ~~Q3 2026~~ **Q2 2026** | Visual graph editor + SimpleCore foundation | **Feature-complete** — in pre-release testing |
| Q3 2026 | Save/Load system — `USaveGame` integration with mid-step state handling | Planned |
| Q3 2026 | Multiplayer replication — server-authoritative quest state with join-in-progress support | Planned |
| Q4 2026 | GAS integration module — GameplayTag identifiers, GameplayEffect rewards, Gameplay Event triggers | Planned |
| Q1 2027 | Expanded objective library — timed, escort, collection, and conversation objectives | Planned |
| Q1 2027 | Example project and full API documentation | Planned |

---

## Contributing

Community feedback is welcome and valuable at this stage. If you encounter a bug, a compatibility issue, or have a feature request, please open an issue with the engine version and a description of the problem or suggestion.

Code contributions via pull request are not being accepted during the current pre-release phase while licensing terms are being finalized. This will be revisited ahead of the first public release. Watch the repository for updates.

For bug reports, include the engine version, a minimal reproduction case, and any relevant output from the `LogSimpleQuest` log category.

---

## License

SimpleQuest is licensed under [Polyform Noncommercial 1.0.0](LICENSE). It is free to use, modify, and distribute for any non-commercial purpose. Commercial use requires a separate license.

The project is planned to relicense to MIT upon a future funding milestone, at which point it will be free for all uses without restriction. Commercial licensing terms will be announced separately ahead of that transition.
