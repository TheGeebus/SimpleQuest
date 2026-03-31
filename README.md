# SimpleQuest

A source-available Unreal Engine plugin for non-linear, branching quest systems. Currently free for non-commercial use, a future MIT licensed free-use release is planned following a funding milestone. SimpleQuest is built on a directed acyclic graph of quest steps with a typed publish/subscribe event bus, giving designers the freedom to craft questlines that feel alive rather than scripted.

This version is for Unreal Engine 5.6.

See [CHANGELOG.md](CHANGELOG.md) for version history.

[![License: Polyform NC 1.0](https://img.shields.io/badge/License-Polyform%20NC%201.0-blue.svg)](LICENSE)

---

## Features

- **Visual quest creation graph** -- See the links between story elements laid out as wire connections between nodes. Easily make changes and craft your story in the familiar visual language of blueprints.
- **Multi-level quest graphs** -- Quest graphs can be nested as nodes in other quest graphs, affording near infinite possibility for narrative customization while preserving a clean organizational structure that's intuitive and easy to navigate.  
- **True non-linear quest creation** -- Quest steps form a DAG with bidirectional prerequisite and next-step edges. Multiple steps can be active simultaneously, branches can converge, and completing one path can permanently close or re-enable another.
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

The following features are in active development:

| Quarter | Deliverable |
|---|---|
| Q3 2026 | Visual graph editor -- node-based quest authoring in the Unreal editor |
| Q4 2026 | Save/Load system -- `USaveGame` integration with mid-step state handling |
| Q1 2027 | Multiplayer replication -- server-authoritative quest state with join-in-progress support |
| Q1 2027 | GAS integration module -- GameplayTag identifiers, GameplayEffect rewards, Gameplay Event triggers |
| Q2 2027 | Expanded objective library -- timed, escort, collection, and conversation objectives |
| Q2 2027 | Example project and full API documentation |

---

## Contributing

Community feedback is welcome and valuable at this stage. If you encounter a bug, a compatibility issue, or have a feature request, please open an issue with the engine version and a description of the problem or suggestion.

Code contributions via pull request are not being accepted during the current pre-release phase while licensing terms are being finalized. This will be revisited ahead of the first public release. Watch the repository for updates.

For bug reports, include the engine version, a minimal reproduction case, and any relevant output from the `LogSimpleQuest` log category.

---

## License

SimpleQuest is licensed under [Polyform Noncommercial 1.0.0](LICENSE). It is free to use, modify, and distribute for any non-commercial purpose. Commercial use requires a separate license.

The project is planned to relicense to MIT upon a future funding milestone, at which point it will be free for all uses without restriction. Commercial licensing terms will be announced separately ahead of that transition.
