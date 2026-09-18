# Chapter 1 - Basic Trigger

*A single step, a single trigger, a single ending.*

The first room is the framework in miniature. A questline holds one Step. The Step names an Objective, which decides when the Step is done. A beacon in the room carries a Trigger, which reports to the Objective. When the Objective completes, its outcome runs into an Outcome node and the questline ends. Everything after this chapter is a variation on those four parts.

**By the end of this chapter you can name:** Questline, Step, Objective, Trigger, and Outcome; the ACTIVATED and COMPLETED events; and the tag that connects a node in a graph to an actor in a level.

## In the room

In order:

1. You walk in and the HUD prints the chapter's first beat:

   > A quest is now ACTIVATED.
   >
   > The framework is tracking it and watching for any of its objectives to be completed. Actors using a Quest Trigger Component can easily advance quests.
   >
   > Find the glowing Trigger volume to advance.
   >
   > ACTIVATED is always the first event in a quest's lifecycle.

2. A beacon is glowing. It is a `BP_QuestTriggerActor` - a cube with a Quest Trigger Component on it - and its aura is lit because the Step it watches is Live.

3. Walk into it. The beacon fires on overlap; it plays a sound cue and its aura changes color, the sidebar entry ticks, and the second beat prints:

   > The objective completed and the framework recorded the outcome.
   >
   > The quest fired a COMPLETED event, typically the last event in a quest's lifecycle.
   >
   > The COMPLETED event is a convenient place to perform cleanup.
   >
   > Head back through the door to continue.

4. The door ahead opens. It is Chapter 2's door, and it opened because Chapter 2 just activated - more on that under *Completion* below.

The in-world tell is the beacon: it glows only while its Step is Live, and when it fires it hears back from the Objective - a satisfaction signal that changes its aura, and a per-fire response that plays the sound cue. That, and the HUD, are how this room shows you the lifecycle. Neither the beacon nor the HUD knows about the graph. Both are reacting to events published on a tag.

<img width="1200" alt="The room on entry: beacon lit, the ACTIVATED beat" src="https://github.com/user-attachments/assets/699ab65f-b12a-4b5b-9649-d16820dd1cec" />
<img width="1200" alt="After the overlap: satisfied beacon, the COMPLETED beat, Chapter 2's door opening" src="https://github.com/user-attachments/assets/e0c61636-1e36-423f-b450-d47732d2e13d" />

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/01_BasicTrigger/QL_Ch1_BasicTrigger`. Three nodes:

**Start → Basic Trigger → Outcome (Reached)**

- **Start** is the questline's entry. Its *Entered* pin fires when the questline activates.
- **Basic Trigger** is a Step. The dropdown on its face names the Objective class, `OBJ_InteractWithTarget`. It has three yellow Completion Path pins - *Reached*, *Solved*, *Procedural* - one for each outcome the Objective declares, and a white *Any Outcome* pin beneath them. Only *Reached* is wired.
- **Outcome** carries the tag `SimpleQuest.Outcome.Reached`. When activation reaches it, the questline resolves with that outcome.

<img width="1200" alt="QL_Ch1_BasicTrigger: the whole graph and all comments" src="https://github.com/user-attachments/assets/1d5392de-7094-4f62-b35e-aea33b6af238" />

Four comment boxes sit beside the nodes. Read them in the graph; in short:

- *Questline Graph* - the asset is the authoring surface, and compiling it mints a tag, `SimpleQuest.Questline.<QuestlineID>`, that every node inside it hangs from.
- *Step nodes* - the unit of work. A Step names an Objective, which is a Blueprintable class holding the completion logic, and mints its own tag one level down. The comment points at the example Objective this whole tutorial reuses.
- *Trigger Component* - watches a Step, receives an activation when the Step goes Live, can fire while it is Live, and hears when it ends. Fires carry a payload your game defines; the Objective decides what the payload means.
- *Outcome node* - ends the graph with a named result, and becomes a pin on any node that places this questline inside another.

### Things worth clicking:

<br>
  <img width="490" height="324" alt="The Step expanded: the beacon listed (via QuickStart)" src="https://github.com/user-attachments/assets/44cd76c5-12e6-4a65-b2f5-f18d55697211" />
<br>

- **Expand the Step.** The arrow at the bottom of the node opens a detail view listing the actors watching it. The beacon is there, marked *(via QuickStart)* - it watches this Step through the tutorial's master questline rather than through this asset directly. That distinction is the subject of *Under the hood*.

<br>
  <img width="548" height="562" alt="Details panel with the Step selected" src="https://github.com/user-attachments/assets/23a7d053-15ad-4bc0-a730-c4eb205153a3" />
<br>

- **Select the Step.** In the Details panel: *Node Label* (the identity the tag is built from); *Display Name*, *Description*, and *Display Data* (UI text - separate from identity, empty by default); the *Objective Class*; an optional *Config Asset*; *Target Actors* and *Target Classes*; *Number of Elements*; and the *Prerequisite Gate Mode* (Chapter 4). This Step has no display data of its own; the beats you read belong to the chapter. The properties on the Step node will be discussed throughout the other chapters.

<br>
  <img width="547" height="375" alt="Graph Defaults including Display Name, Display Data, and Questline Rewards" src="https://github.com/user-attachments/assets/f713d2da-f6e3-4f88-aaf4-cabc283241f7" />
<br>

- **Click empty canvas.** The Details panel switches to the questline's own settings - the same thing the *Graph Defaults* button on the toolbar shows. Here the *Display Name* is "Chapter 1 — Quests and Triggers", the *Display Data* is `DA_Ch1Main_BasicTrigger`, and *Questline Rewards* holds the reward set that pays at the end of every chapter (Chapter 2 explains it).

<br>
  <img width="1200" alt="	DA_Ch1Main_BasicTrigger: display data, Activated Beats and Completed Beats Default" src="https://github.com/user-attachments/assets/10b0d279-cd06-4d3d-a465-dc57b0e5ee73" />
<br>

- **Open `DA_Ch1Main_BasicTrigger`** in the chapter's `DisplayData` folder. It is a Quest Lifecycle Display Data asset: one text array per lifecycle event. *Activated Beats* and *Completed Beats Default* are filled; the others are empty. That is the entire source of what the HUD printed.

<br>
  <img width="1200" alt="QL_QuickStart on the Chapter 1 node: its Reached pin, the prerequisite wire leaving Any Outcome" src="https://github.com/user-attachments/assets/d67ab8e8-254d-4bae-b347-adff075e19d5" />
<br>

- **Open `QL_QuickStart`** and find the Chapter 1 node. It is a Linked Questline node, and it has a *Reached* Completion Path pin. That pin *is* this graph's Outcome node, seen from outside.


## Under the hood

### Compile turns labels into tags

Nothing in the graph exists at runtime until it is compiled. Compile reads each node's *Node Label*, sanitizes it into a tag segment (anything that isn't a letter, digit, or underscore becomes an underscore - "Basic Trigger" becomes `Basic_Trigger`), and registers a Gameplay Tag for it under the questline's own tag:

| Node          | Tag                                                       |
|---------------|-----------------------------------------------------------|
| The questline | `SimpleQuest.Questline.QL_Ch1_BasicTrigger`               |
| Basic Trigger | `SimpleQuest.Questline.QL_Ch1_BasicTrigger.Basic_Trigger` |

The questline segment is the asset's *Questline ID* if one is set, otherwise the asset name. The compiled tags are written to `Plugins/SimpleQuest/Config/Tags/SimpleQuestCompiledTags.ini` so they exist from startup, and they show up in the Gameplay Tag Manager like any other tag.

Now the part the *(via QuickStart)* marker was pointing at. Chapter 1 is not played as `QL_Ch1_BasicTrigger` on its own; it is played as a Linked Questline node inside `QL_QuickStart`, whose Questline ID is `QuickStart`. Compiling the master graph mints a second set of tags for the placement:

| Node                     | Tag as placed                                              |
|--------------------------|------------------------------------------------------------|
| The Chapter 1 placement  | `SimpleQuest.Questline.QuickStart.Chapter_1`               |
| Basic Trigger, inside it | `SimpleQuest.Questline.QuickStart.Chapter_1.Basic_Trigger` |

The beacon's Quest Trigger Component watches the placed form. The same Step therefore has two addresses - the one in its own asset and the one in each graph that places it - and Chapter 8 is about what you can do with that. For now: this room's actors are wired to the placement.

<img width="1200" alt="The beacon's Quest Trigger Component: Step Tags to Trigger" src="https://github.com/user-attachments/assets/08ff21e9-64d4-4ea2-af84-b845c592e7cb" />

### Activation

Pressing the green button calls `Start Questline` on `QL_QuickStart`. The master graph activates every chapter at once and holds each behind a prerequisite. Chapter 1's prerequisite is a fact the button just published, so it proceeds immediately. What fires, in order:

1. **ACTIVATED** and **STARTED** on `SimpleQuest.Questline.QuickStart.Chapter_1` - the chapter itself entering scope. The HUD hears the first of these and prints the Activated beat.
2. The Start node's *Entered* pin fires and travels along the solid white wire into Basic Trigger.
3. **ACTIVATED** and **STARTED** on `...Chapter_1.Basic_Trigger`, in the same tick as above. A Step with no giver goes Live the moment activation reaches it. Chapter 3 explains why ACTIVATED and STARTED are two distinct events, even if they often coincide.
4. World State facts are written for both nodes: `SimpleQuest.State.QuickStart.Chapter_1.Basic_Trigger.Live` and `...Started` for the Step, and `SimpleQuest.State.QuickStart.Chapter_1.Live` and `...Started` for the chapter, which as a container takes its Live from the Steps inside it. Open the Facts Panel's World State view and all four are there - twice. Each also appears under the asset's own address, `SimpleQuest.State.QL_Ch1_BasicTrigger.Basic_Trigger.…` and `SimpleQuest.State.QL_Ch1_BasicTrigger.…`, because the framework writes every state fact at both of a node's addresses. Anything reading either address sees the same truth. Chapter 8 shows why you would choose the standalone address over a contextualized placement's address.
5. The Step creates an instance of `OBJ_InteractWithTarget` and activates it. The Objective is now listening for trigger fires on the Step's tag.
6. The beacon's Trigger Component hears STARTED on the tag it watches and fires its own *On Quest Trigger Activated* event. The actor lights its aura.

<img width="1200" alt="Where the Facts Panel lives: Window → Developer Tools → Debug → Facts Panel" src="https://github.com/user-attachments/assets/529690f4-bf70-46d7-b479-028ffcd14c7b" />
<img width="1200" alt="World State view, filter empty, right after walking in with several tags visible" src="https://github.com/user-attachments/assets/c268a0b1-ef3b-4f77-b593-fc372b68d2da" />

### The fire

Walking into the beacon calls `Send Trigger Event` on its Trigger Component. The component checks the state of every Step it watches before publishing anything:

- If the Step is Live, the fire is published to the Objective.
- If the Step is activated but cannot progress - Blocked, or waiting on a prerequisite - the component publishes **PROGRESS REFUSED** instead, with the reason (Chapters 4 and 6).
- If the Step is not activated at all, the fire is dropped silently. A trigger against a Step the runtime hasn't reached is not the trigger's concern.

Here the Step is Live, so the fire lands on the Objective's *Try Complete Objective*. `OBJ_InteractWithTarget` does two things with it:

1. *Publish Trigger Satisfied* - a signal back to the one Trigger Component whose fire this was, saying the fire counted. The beacon's *On Quest Trigger Satisfied* event runs and it changes its aura.
2. *Complete Objective With Outcome* with `SimpleQuest.Outcome.Reached`. (The Objective's default arm calls its parent class, which completes on *Reached*; a tickbox on the Blueprint switches it to *Solved*. The tutorial ships with the default.)

Every fire also gets a **response** from the Objective - *Progress*, *Completed*, or *Refused* - delivered to the Trigger Component that fired, on its *On Quest Trigger Responded* event. Here the response is *Completed*, and it rides the completion below. The beacon's sound cue plays from that response. Satisfied and Responded are the two signals in this chapter that travel back toward the actor that caused them; everything else flows outward.

### Completion

Completing the Objective resolves the Step:

1. The `.Live` fact is removed and a `.Completed` fact is added. `.Started` stays. It is the append-only record that the Step has run at least once, held at a count of 1 however many times the Step runs - where `.Completed` counts: resolve the Step a second time and it reads 2. That is the World State side. The resolution itself - outcome `Reached`, the time, and what caused it - is recorded in the Quest State Subsystem, where `Is Quest Resolved With` and the catch-up path read it later. The Facts Panel's Quest State view is the window onto that record, and this completion is one row on its *Resolutions* tab.
2. **COMPLETED** publishes on the Step's tag, carrying the outcome. Alongside it, the beacon's Trigger Component receives the *Completed* response to its fire (the sound cue) and the trigger-side wrap for the Step: it disarms its own subscriptions, and the actor's *On Quest Trigger Deactivated* event runs with reason *Completed*.
3. The Step's *Reached* path fires, and so does *Any Outcome* - the white pin fires on every completion, the yellow pins are exclusive. Only *Reached* is wired, so activation runs into the Outcome node.
4. The Outcome node resolves the questline with `Reached`. **COMPLETED** publishes on `SimpleQuest.Questline.QuickStart.Chapter_1`, the questline-level rewards are granted, and the HUD prints the Completed beat.
5. Back in the master graph, the Chapter 1 node's *Reached* pin fires, and so does its *Any Outcome* pin - and *Any Outcome* is the one wired to the prerequisite holding Chapter 2 back. The master gates on the chapter having ended, not on how it ended. Chapter 2 - activated at the start and deferred since - proceeds, and **ACTIVATED** publishes on `SimpleQuest.Questline.QuickStart.Chapter_2`.
6. The door ahead opens. It belongs to Chapter 2, not Chapter 1: its observer watches Chapter 2's tag and opens on that ACTIVATED. Completing Chapter 1 opened it only because completion cascaded into Chapter 2's activation. Play in unlocked mode, where the chain is cut, and finishing this room leaves the door shut until you press Chapter 2's own start button - which activates Chapter 2, which opens the door.

Two stores were written in step 1, and they have different owners. World State is shared: SimpleQuest publishes its lifecycle facts into it, and your own systems can publish and read theirs alongside - the green button that started the tutorial wrote a fact there before any quest existed. The Quest State record is the framework's alone. You read it; the quest manager writes it.

The World State view on completion:
<img width="1200" alt="World State view after completion: .Completed is present, .Live is gone, .Started is still there" src="https://github.com/user-attachments/assets/dd74a6b8-e420-4049-8289-e7671c78a558" />

The Quest State view on completion:
<img width="1200" alt="Quest State view, Resolutions tab: both Reached rows" src="https://github.com/user-attachments/assets/831907d3-d5f3-4016-8d34-13d2fabe2e56" />

### Who was listening

None of the actors held a reference to the graph, or to each other.

- The HUD has a Quest Observer Component bound to `SimpleQuest.Questline.QuickStart` with *Descendants* routing, so one subscription hears every event from every node under the master questline. On each event it looks up the display data registered for the event's tag and prints the beats for that event type.
- The beacon's Trigger Component is bound to one Step tag. It hears that Step start and end, it is the only thing that can fire it, and it is the only thing that hears the Objective's answer to its fire.
- The door ahead has a Quest Observer Component watching Chapter 2's tag. It opens on ACTIVATED - every chapter's door does, for its own chapter.

Publisher and subscriber meet at a tag and nowhere else. That is what the Objective's comment means by the game's vocabulary coming in and the graph's vocabulary going out - the Trigger's payload is yours, the outcome is the graph's, and the Objective is where one becomes the other.

**PIE Debug Halo - Live state:**

<img width="668" height="401" alt="Image" src="https://github.com/user-attachments/assets/38ae7ccb-68fe-4184-b079-c391940c34a2" />

**PIE Debug Halo - Completed state:**

<img width="668" height="401" alt="Image" src="https://github.com/user-attachments/assets/ad13b347-562f-405b-927a-69d190824b7a" />

## Gotchas

**The tag comes from the Node Label, not the Display Name.** *Node Label* is identity. *Display Name* is UI text and nothing else. Display Name is empty by default and is never substituted from the label - a HUD that asks for a display name gets nothing back until you author one. Rename a node's label and the node shows a *Recompile to update tags* marker until you compile. On compile, the rename propagates to every loaded actor, Blueprint default, and data asset that referenced the old tag, and assets that weren't loaded heal on their next load.

- Though it is intended to be a flexible approach that provides a place to author display data for each Step, it's not mandatory to source your display data through this system. Your HUD controller or any other manager actor can instead subscribe to quest events and use data from the payload to assemble text to show or drive additional queries through any external system as needed.

**Compile before you wire an actor.** A Trigger Component's tag picker lists registered tags, and a Step's tag does not exist until its graph has been compiled. Add nodes, compile, then place actors. When a chapter of yours does nothing in play, the first check is whether the tag the component holds is the tag the graph currently compiles to.

**Two spellings of one Step, and they mean different things.** The beacon watches `SimpleQuest.Questline.QuickStart.Chapter_1.Basic_Trigger` - the placed address. Watching `SimpleQuest.Questline.QL_Ch1_BasicTrigger.Basic_Trigger` - the asset's own address - would also work, and would reach the Step in *every* graph that places this questline. With one placement the two are indistinguishable. Chapter 8 places one questline twice, and there the choice matters.

**A fire before the Step is Live is silent.** Not refused, not logged at default verbosity - dropped. If a beacon of yours "does nothing," look at the halo first: the Step has to be Live for the fire to reach the Objective. If the Step is activated but gated, you get PROGRESS REFUSED, which is a different problem with a different fix.

**A placement's Path pins are the inner graph's Outcome nodes.** The *Reached* pin on the Chapter 1 node in `QL_QuickStart` exists because this graph has an Outcome node tagged *Reached*. Change that tag, or delete the node, and the pin changes on every node that places this questline the next time the outer graph loads or compiles - taking the wire that was on it. Edit an Outcome node knowing it is an edit to every placement's shape.

**Any Outcome already covers the yellow pins.** The yellow Path pins are mutually exclusive - a completion travels down at most one - and the white *Any Outcome* pin fires on every completion. So the editor refuses to wire a node's *Any Outcome* and one of its own Path pins into the same input, knots included: the second wire could never say anything the first doesn't. Where one node is reachable both ways through something in between - a Grant Rewards node on the named path, say - it is activated once per completion, not twice.

**A Step resolves once per activation.** A second *Complete Objective With Outcome* on the same activation is refused and logged. Only the first Completion counts.

**The example Objective is shared.** `OBJ_InteractWithTarget` is the Objective on most of the tutorial's Steps. Flipping its tickbox to complete on *Solved* changes every chapter that uses it, not just this one - and Chapter 1's graph only wires *Reached*, so the questline would never end. If you flip the tickbox to see this in action, be sure to flip it back to enable progress again.

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **Node Label** is the Step's identity. The tag is built from it, and everything that watches or fires the Step addresses that tag.
- **Objective Class** is the completion logic, and it declares the Completion Path pins. Change the class and the pins change with it.
- **Display Name, Description, and Display Data** are UI text, kept apart from identity. This chapter authored them on the questline rather than on the Step. From Chapter 2 on, Steps carry their own.
- An Objective **answers every fire** it receives - a satisfaction signal and a Progress / Completed / Refused response - and the actor that fired is the only one that hears the answer.

Still to come: a Step offered by a giver (Chapter 3), the Prerequisites pin and the gate mode (Chapter 4), an Objective you write yourself (Chapter 5), the Deactivate pins (Chapter 7), and the Config Asset (Chapter 11).

## Try it

Nothing here needs changing. Play the room again with `QL_Ch1_BasicTrigger` open beside the viewport and the Facts Panel's World State view open with its filter empty - the list is short this early: the fact the green button wrote, the master questline's own state, and Chapter 1's facts under both spellings. Watch the halo on Basic Trigger and the `.Live` facts appear together when you walk in, and the halo turn to Completed and the facts become `.Completed` when you touch the beacon. Filter to `Basic_Trigger` to follow just the Step. Then switch the panel to Quest State: the Resolutions tab has a row for the Step and a row for the chapter, both `Reached`. That is the whole lifecycle, and it is the same one every later chapter runs.

Then run it once more. The room's start button re-activates the chapter now that you have reached it. Touch the beacon again and read the Count column: `.Completed` is at 2, `.Started` still at 1, and the Resolutions tab has a second row. Gold pays again and experience does not - the Grant Once modifier Chapter 2 explains counts the same resolutions this column does.

<img width="1200" alt="Image" src="https://github.com/user-attachments/assets/012bd8d2-2461-49ff-815c-de20944e20f6" />

Previous: [Before You Start](00_BeforeYouStart.md) | Next: Chapter 2 - Rewards.