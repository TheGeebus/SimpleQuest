# Chapter 5 - Named Outcomes

*The Objective names how the Step ended. The graph routes on the name.*

Every Step so far has ended on *Reached*: Chapter 1's Objective completes on it, and the graphs wired that one pin while the other two sat unused. This room's first Step can end two ways, and which way is decided by a Blueprint you can read in a minute. Two beacons feed one Step, each carrying a different tag, and the Step's Objective turns the tag into an outcome - *Left* or *Right*. The graph sends each outcome down its own branch, through its own door, to a beacon of its own, and the branches meet again at a shared last Step. Then the room invites you to play it again the other way.

**By the end of this chapter you can name:** an outcome tag and a Completion Path, where a Step's yellow pins come from, *Complete Objective With Outcome*, *Refuse Trigger*, the Custom Tag a trigger sends, *Completed Beats By Outcome*, and why the graph can route on *Left* but never on "the left beacon."

---

## In the room

In order:

1. Chapter 5 activated as you finished Chapter 4, so on the way in two beats have printed - the chapter's, then the first Step's:

   > This chapter's first step can end in more than one way. Outcomes are named tags YOU define - not success or failure, just different named results. Each one can route to different content. Pick a beacon to see.

   > Two beacons, two outcomes. Touch one. You can replay later to try the other.

   Two beacons are lit. Neither carries a label - the beats call them left and right, and that is what they are. Two doors, one for each path, both shut.

<img width="1200" alt="The room on entry: the chapter's beat and the Fork's beat on the HUD, two beacons lit, both path doors shut" src="https://github.com/user-attachments/assets/5f9e5294-0023-46fd-8137-9de50ff9e3dd" />

2. Touch the left one. It plays the success sound and goes dark - and so does the right one, silently. Two beats print, in this order:

   > You chose the left beacon. The step completed with the SimpleQuest.Outcome.Left outcome, which routes you along the left path.

   > You took the left path. Touch the trigger to continue.

   The left door opens. The right door does not. The beacon on the left path lights.

<img width="1200" alt="After the left beacon: the Left completed beat and Left's activated beat on the HUD, the left door open, the right door shut, the right fork beacon dark, the beacon on the left path lit" src="https://github.com/user-attachments/assets/52d73981-ee8f-4249-97a0-f037928127ba" />

3. Touch that beacon.

   > In this questline, both paths ultimately converge back to the same point. Touch the trigger to complete the quest.

   One more beacon lights, where the two paths meet.

<img width="1200" alt="After the left-path beacon: Convergence's beat on the HUD, the last beacon lit" src="https://github.com/user-attachments/assets/8bc4d77f-6194-42d1-bb6b-324176f7edf1" />

4. Touch it, and the chapter completes:

   > Both beacons led to the same final conclusion, but they sent you along different paths to get there. Named outcomes let one step branch your quest into entirely different content. You can replay this chapter to take the other path. Go back through the door to continue.

   Chapter 6's door opens.

<img width="1200" alt="After the last beacon: the chapter's completed beat on the HUD, Chapter 6's door opening" src="https://github.com/user-attachments/assets/99bed0e8-28db-4987-b52c-099a8f7957bf" />

5. Take the invitation. Play the room again from its button: the chapter's two beats print again, and both fork beacons relight. Touch the right one this time:

   > You chose the right beacon. The step completed with the SimpleQuest.Outcome.Right outcome, which routes you along the right path.

   > You took the right path. Touch the trigger to continue.

   The right door opens, the beacon on the right path lights, and the rest plays as before. Two runs, two doors, one Step deciding which.

<img width="1200" alt="The replay, after the right beacon: the Right beats on the HUD, the right door open" src="https://github.com/user-attachments/assets/fbd52f51-68ce-4bde-bc9c-da74138562bc" />

The in-world tell is the door: the side you chose opens and the other stays shut, and the beacon you did not touch goes dark without a sound. Nothing the beacons do is left-or-right in the graph's eyes. One Step ended on one of two named paths, and everything after that was wiring.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/05_NamedOutcomes/QL_Ch5_NamedOutcomes`. Four Steps and an Outcome:

**Start → The Fork**, then **The Fork's *Left* pin → Left** and **The Fork's *Right* pin → Right**, **Left's *Reached* → Convergence**, **Right's *Reached* → Convergence**, and **Convergence's *Any Outcome* → Outcome (Reached)**.

- The Fork hosts `OBJ_LeftOrRight`, a Blueprint in this chapter's folder. Its face shows two yellow Completion Path pins, *Left* and *Right*, and the white *Any Outcome* beneath them. Both yellow pins are wired; *Any Outcome* is not.
- Left, Right, and Convergence host `OBJ_InteractWithTarget`, so they show Chapter 1's three pins - *Reached*, *Solved*, *Procedural*. Only *Reached* is wired on Left and Right, and only *Any Outcome* on Convergence.
- Two wires run into Convergence's *Activate*. An activation input takes as many wires as you like - each is one more way to start the node - where the *Prerequisites* pin (Chapter 4) takes exactly one.
- No dashed wires. Nothing in this room is gated; the order is entirely activation.

<img width="1200" alt="QL_Ch5_NamedOutcomes: the whole graph with its three comment boxes, the Fork's Left and Right pins wired to Left and Right, both Reached pins into Convergence's Activate, Convergence's Any Outcome into the Outcome node" src="https://github.com/user-attachments/assets/a775c6cf-0d86-4e7d-99c2-894755601138" />

**Three comment boxes. In short:**

- *Over The Fork* - the yellow pins come from the Objective class: `OBJ_LeftOrRight` declares two, *Left* and *Right*. Each of the two triggers owns a different gameplay tag that it sends with its trigger event, and the Objective branches on those tags.
- *Over Left and Right* - a Step shows every Completion Path its Objective class declares, and the graph uses what it needs. Only *Reached* is wired here.
- *Over Convergence* - both paths land here; reconverging takes no special node, just each path's pin wired into the same *Activate*. The activation still carries the outcome that drove it, so an Objective here could branch on which way the player came. This one has no need to.

### Things worth clicking:

<br>
  <img width="1198" alt="The Fork selected: the Details panel with Objective Class OBJ_LeftOrRight, Display Name Branching Paths, and Display Data DA_Ch5_Fork; the node's two yellow pins and its Triggers: 2 line" src="https://github.com/user-attachments/assets/3f2d2042-8834-4f07-aff0-46f62035f9f6" />
<br>

- **Select The Fork.** *Objective Class* reads `OBJ_LeftOrRight`, *Display Name* is *Branching Paths*, and *Display Data* is `DA_Ch5_Fork`. Nothing else on the node is set. The two pins are the whole difference from Chapter 1's Step, and they were not typed in anywhere on this panel. The summary line on the node face reads *Triggers: 2* - Chapter 3's shape, two actors on one Step - and the expander names both beacons.

<br>
  <img width="1200" alt="OBJ_LeftOrRight's event graph: the COMING IN, TRIGGER FEEDBACK, and GOING OUT comment columns" src="https://github.com/user-attachments/assets/ce857282-132b-43ee-9765-d22c6ed97a74" />
<br>

<br>
  <img width="1200" alt="OBJ_LeftOrRight, close: the two Complete Objective With Outcome nodes with SimpleQuest.Outcome.Left and SimpleQuest.Outcome.Right, and the Refuse Trigger node with MyGameTags.Refusal.WrongTag" src="https://github.com/user-attachments/assets/f2d62286-8865-49ba-a122-95e58ae5bc5a" />
<br>

- **Open `OBJ_LeftOrRight`.** The event graph reads left to right under three headings: *COMING IN* (the game's tags, on the trigger context), *TRIGGER FEEDBACK*, and *GOING OUT* (the graph's tags). *Try Complete Objective* is the entry. It stores the *In Context* it was handed, and two *Matches Tag* checks ask whether the context's *Custom Tag* is `MyGameTags.Fork.LeftPath` or `MyGameTags.Fork.RightPath`. Each match publishes *Trigger Satisfied* and then calls *Complete Objective With Outcome* - `SimpleQuest.Outcome.Left` on one branch, `SimpleQuest.Outcome.Right` on the other. A context that matches neither reaches the third branch: *Refuse Trigger* with `MyGameTags.Refusal.WrongTag`. The two completion nodes are the Fork's two pins, and the comment boxes around them say the rest.

<br>
  <img width="1200" alt="BP_QuestTriggerActor4 selected in the level: Step Tags to Trigger on its Trigger Component, Owned Outcome Tag set to MyGameTags.Fork.LeftPath" src="https://github.com/user-attachments/assets/9a1b312b-edf2-435e-9ba0-219e2a7f1786" />
<br>

- **Select the left fork beacon in the level** - `BP_QuestTriggerActor4` in the World Outliner. Its Trigger Component lists `...Chapter_5.The_Fork` under *Step Tags to Trigger*, and the actor's own *Owned Outcome Tag* is `MyGameTags.Fork.LeftPath`. `BP_QuestTriggerActor5` is the same actor class with the same Step tag and `MyGameTags.Fork.RightPath`. That one variable is the beacon's whole opinion about the matter, and despite its name it is not an outcome tag - the name is the example actor's, and what it holds is a tag your game owns.

<br>
  <img width="898" alt="DA_Ch5_Fork: Completed Beats By Outcome with its Left and Right entries, Completed Beats Default empty, Activated Beats with its one line" src="https://github.com/user-attachments/assets/853f9ae3-c766-4557-83d5-e6add85a27b4" />
<br>

- **Open `DA_Ch5_Fork`.** *Activated Beats* has the one line from the room. *Completed Beats Default* is empty. *Completed Beats By Outcome* is a map with two entries, keyed `SimpleQuest.Outcome.Left` and `SimpleQuest.Outcome.Right`, each holding the line you read after choosing. `DA_Ch5_Left`, `DA_Ch5_Right`, and `DA_Ch5_Converge` fill only *Activated Beats*: this chapter's Steps narrate on ACTIVATED where Chapter 4's did on STARTED, and for a Step with no giver those are the same instant.

- **Select Left, then Right.** Chapter 1's Objective, Chapter 1's three pins, one of them wired. Chapter 1's warning about the tickbox on `OBJ_InteractWithTarget` applies to both of them and to Convergence: switch it to *Solved* and three Steps in this room end on a pin nothing is wired to.

---

## Under the hood

### Where the pins come from

A Step node holds no list of outcomes. Its yellow pins are a readout of its Objective class - gathered when the class is picked, when a property on the node is edited, and again on every compile - from three places, merged:

1. Every *Complete Objective With Outcome* node placed in the class's Blueprint graphs, by the tag on the node. That is `OBJ_LeftOrRight`'s two, and `OBJ_InteractWithTarget`'s *Solved*.
2. In a C++ Objective, every `FGameplayTag` property marked `ObjectiveOutcome` - inherited ones included. `OBJ_InteractWithTarget`'s *Reached* is a property on its C++ parent, `GoToQuestObjective`, which is why Chapter 1 said the default arm completes by calling its parent.
3. Whatever the class returns from `GetPossibleOutcomes`, for outcomes computed rather than written down.

*Procedural* on Chapter 1's Objective is the first kind with a twist: a completion node whose outcome tag is wired at runtime carries a *Path Name* instead, and the pin takes that name. The closing page has it.

At compile, every pin becomes a per-node fact tag the runtime can write: `SimpleQuest.State.QuickStart.Chapter_5.The_Fork.Path.Left` and `...The_Fork.Path.Right` for the Fork; `...Left.Path.Reached`, `.Path.Solved`, and `.Path.Procedural` for Left; and so on. Chapter 4 read one of those facts through a dashed wire. This chapter is about writing one.

### Two vocabularies

The beacon does not know what *Left* means to the graph, and the graph does not know there are beacons. Between them sits the Objective, translating:

- **Coming in:** the trigger context. When a beacon fires, it fills the context's *Custom Tag* from its *Owned Outcome Tag* - `MyGameTags.Fork.LeftPath` or `...RightPath`, tags this project defines in its own gameplay tag list next to `MyGameTags.Refusal.WrongTag`. The framework carries the tag and never reads it. *Custom Data*, an instanced struct on the same context, is the same idea for anything a tag cannot hold.
- **Going out:** an outcome tag under `SimpleQuest.Outcome`, the only thing a Questline Graph can route on. The picker on *Complete Objective With Outcome* is filtered to that namespace, and `Left` and `Right` are two entries this project added to it.

The Objective's *Try Complete Objective* is where one becomes the other. That is the whole reason it is a Blueprint you write rather than a property you set: the rule that maps your game's facts onto the graph's outcomes is yours.

### The fire, and the two answers

The fire itself is Chapter 1's. The Step is Live and ungated, so the manager hands the trigger context to `OBJ_LeftOrRight`'s *Try Complete Objective*. From there:

1. The matching branch calls *Publish Trigger Satisfied* with the context it received. The manager republishes it on the Step's tag with the satisfied actor attached, and every Trigger Component watching the Step filters on that actor being its own owner - so only the beacon you touched plays the success cue and makes itself inactive. The other beacon hears the signal and ignores it.
2. The same branch calls *Complete Objective With Outcome* with `SimpleQuest.Outcome.Left`. The Objective marks itself completed and hands its Step the outcome and the path - for a completion node with no *Path Name*, the path is the outcome tag's own name.
3. **One completion per activation.** A second *Complete Objective With Outcome* in the same activation is refused and logged as a Warning; only the first outcome sticks. The two calls in `OBJ_LeftOrRight` sit on branches that cannot both run, which is the shape to keep.
4. **Any Outcome is not an outcome.** Completing with `SimpleQuest.Outcome.AnyOutcome` is refused outright, with a Warning. It names a pin that fires whatever the outcome; a Step cannot end on it.

The third branch is the one the room never takes: a context whose *Custom Tag* matches neither calls *Refuse Trigger* with `MyGameTags.Refusal.WrongTag`. The manager turns that into the *Refused* response on the firing beacon's *On Quest Trigger Responded* - the event Chapter 1 said the example beacon acts on only for *Refused*, with the denied sound and the blocked color. The Step stays Live, nothing is written, and the other beacon still works. This is not PROGRESS REFUSED. That refusal is the manager's, made before the Objective sees anything; this one is the Objective's own answer, after it looked. The first *Try it* makes it happen.

### What the outcome routes

Completion runs as Chapter 1 described - `.Live` removed, `.Completed` added, the resolution recorded with its outcome - with two additions the outcome makes:

1. The Fork is resettable, like everything under the master graph, so its per-run path fact is written: `...The_Fork.Path.Left`, and not `.Path.Right`. A dashed wire from the Fork's *Left* pin would read exactly this fact; one from *Any Outcome* would read either.
2. **COMPLETED** publishes carrying the outcome, and the HUD looks the outcome up: `DA_Ch5_Fork`'s *Completed Beats By Outcome* has an entry for `SimpleQuest.Outcome.Left`, so that line prints. An outcome with no entry falls through to *Completed Beats Default*, which for this asset is empty, so a third outcome would print nothing at all. The lookup is a function on the display data asset, *Get Completed Beats*, and the example HUD calls it rather than reading the map itself.

Then the chain. The manager takes the resolved path and looks it up in the Fork's compiled next-nodes table - named paths first, then whatever is wired from *Any Outcome*. *Left* holds one destination, Left; *Right* holds Right; *Any Outcome* holds nothing. Left activates with the outcome that drove it stamped on its activation. The Quest State view's *Entries* tab shows the row - destination Left, source The Fork, outcome `SimpleQuest.Outcome.Left`, provenance *ChainCascade* - and Left's Objective receives the same outcome in its runtime context as *Incoming Outcome Tag*, which is what the comment over Convergence means. Left's Activated beat prints, its door opens on its ACTIVATED, its beacon arms on its STARTED. Right is never activated: no facts under its tag, no halo, its door shut, its beacon dark. Right is not failed, refused, or skipped in any recorded sense. The graph never reached it.

Both fork beacons went dark because completion publishes the trigger-side wrap for the Step - the *Completed* response and *Deactivated* - to every trigger watching it. The touched beacon had already gone inactive on *Satisfied*, with the cue; the other goes inactive now, without one. Same wrap, two beacons, one sound, and the difference is which of them was told *Satisfied*.

The Output Log says all of this in three lines at its default verbosity:

```
HandleOnNodeCompleted: 'SimpleQuest.Questline.QuickStart.Chapter_5.The_Fork' outcome='SimpleQuest.Outcome.Left' path='SimpleQuest.Outcome.Left'
ChainToNextNodes: 'SimpleQuest.Questline.QuickStart.Chapter_5.The_Fork' outcome='SimpleQuest.Outcome.Left' path='SimpleQuest.Outcome.Left' - 1 path + 0 any-outcome downstream node(s)
ActivateNodeByTag: 'SimpleQuest.Questline.QuickStart.Chapter_5.Left' activated (source 'SimpleQuest.Questline.QuickStart.Chapter_5.The_Fork', outcome 'SimpleQuest.Outcome.Left')
```

### Where the paths meet

Left completes on *Reached*, from Chapter 1's Objective, and its *Reached* pin is wired to Convergence's *Activate*. Right's is wired to the same input. Convergence activates from whichever fires, and it activates once; the *Entries* tab row says which source it was, and on the second run a second row says the other. From there it is Chapter 1's ending: Convergence's *Any Outcome* into the Outcome node, the chapter resolving with `Reached`, the chapter's own completed beat - `DA_Ch5Main_NamedOutcomes` keeps that one in *Completed Beats Default*, since the chapter has one ending - and in the master graph the Chapter 5 node's *Any Outcome* satisfying Chapter 6's prerequisite.

### Replay, and the other path

The room's button calls *Activate Quest* on the chapter's tag. Because the chapter is resettable and has completed, the manager clears its per-run path facts - and, eagerly, every resettable descendant's - before it runs again: `...The_Fork.Path.Left` is gone the instant you press. The append-only records stay: `.Started` at 1, `.Completed` counting, the Resolutions rows. Then the chapter re-activates, the Fork goes Live again, both beacons arm, and the second choice writes `...The_Fork.Path.Right`. On the graph, the first run's green stays on Left while Right lights amber and then green; the Fork shows Live again while it runs, then Completed with a count of 2 behind it; and the Resolutions tab lists the Fork twice, once `Left` and once `Right`.

### What is watching

The halos tell the run's story: the Fork amber then green, Left amber then green, Right never lit, Convergence last. The World State view filtered to `Chapter_5.The_Fork` shows `.Path.Left` beside `.Completed` and `.Started`, and a filter on `Chapter_5.Right` finds nothing at all. The Quest State view's *Resolutions* tab has the Fork's row with `SimpleQuest.Outcome.Left` in its Outcome column, and its *Entries* tab has Left's row with the same outcome as the reason it started. Between them, those two tabs answer the two questions this chapter is about: how did that Step end, and why did this one start.

**The graph after the choice:**

<img width="1200" alt="The graph during PIE after the choice: the Fork with the Completed halo, Left with the Live halo, Right and Convergence unlit" src="https://github.com/user-attachments/assets/a8700f73-b731-4726-89e2-e2d0a05c41ca" />

**The Entries tab after the choice:**

<img width="1200" alt="Quest State view, Entries tab after the choice: Left's row with Source The_Fork, Outcome SimpleQuest.Outcome.Left, and Provenance ChainCascade" src="https://github.com/user-attachments/assets/f1c89ff3-9b36-4413-a05a-c67872df315b" />

**The fact the outcome wrote:**

<img width="1200" alt="World State view filtered to Chapter_5.The_Fork after the choice: the .Path.Left fact beside .Completed and .Started" src="https://github.com/user-attachments/assets/531efdcf-8946-4b5c-88c2-bdf5fe24547e" />

**The Resolutions tab after both runs:**

<img width="1200" alt="Quest State view, Resolutions tab after both runs: the Fork's two rows, Left and then Right" src="https://github.com/user-attachments/assets/56c0d2fd-3113-411e-8f31-414f7be41b5e" />

---

## Gotchas

**The framework never reads Custom Tag.** It carries it. Nothing routes on `MyGameTags.Fork.LeftPath`; your Objective does, in Blueprint, and if it does not, the tag arrives and is ignored. Put a `SimpleQuest.Outcome` tag on a trigger and it still routes nothing: the outcome is what the Objective completes with, not what the trigger sent.

**A pin is a placement.** Every *Complete Objective With Outcome* node in the class's graphs declares a pin, whether or not execution can reach it. A node left behind in a corner of the event graph is a pin nothing will ever fire and a path fact nothing will ever write. Delete what you do not use, then *Compile All* - the graph re-reads every Step's Objective when it compiles.

**An unwired path ends the branch.** Left, Right, and Convergence each show *Solved* and *Procedural*, wired to nothing. A Step that ends on an unwired path completes normally - facts, beats, COMPLETED - and then nothing happens, because nothing was wired to happen. The chapter stays open with no Live Step in it. Chapter 1's tickbox is the quickest way to see it; the second *Try it* builds one on purpose.

**Beats fall through, then fall silent.** An outcome with no entry in *Completed Beats By Outcome* prints the *Default*. An empty *Default* prints nothing. `DA_Ch5_Fork` is authored that way, so an outcome it does not expect is a Step that ends in silence - a fine thing to notice in a playtest and a bad thing to learn from a bug report.

**Two refusals, one sound.** The example beacon plays the same denied cue for PROGRESS REFUSED and for a *Refused* response, and they are different things. The first never reached the Objective and prints the Step's *Progress Refused Beats*; the second is the Objective's decision, has no beat array, is not recorded in the refusal history, and does not flash the node. If a beacon refuses you and no beat prints, look in the Objective, not at the gates.

**Any Outcome is a wire's property.** It is never the answer to "which outcome did this Step end on." *Complete Objective With Outcome* refuses it, and a dashed wire from it means "any of them," which is a different question.

**One Step, one ending per run.** Two beacons on one Step is Chapter 3's shape, not a race. The first fire that completes the Objective ends the Step, and the wrap goes to both beacons. A design that wants both beacons to count is a multi-target Objective - *Publish Trigger Satisfied* per beacon, one completion when all are in - and the closing page says where that is documented.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **Objective Class** decides the yellow pins. They are discovered from the class - its *Complete Objective With Outcome* nodes, its `ObjectiveOutcome` properties, its `GetPossibleOutcomes` - and refreshed on compile. Change the class, change the pins.
- **A Completion Path** is one way the Step can end. The Objective picks it by the tag it completes with; the graph routes on it, or does not, pin by pin. An unwired path ends the branch.
- **Complete Objective With Outcome** ends the Step: once per activation, and never with *Any Outcome*.
- **The trigger context** is how the world talks to the Objective - the actor, the counts, a *Custom Tag*, and *Custom Data*, none of which the framework interprets.
- **Refuse Trigger** is the Objective's own no: a *Refused* response to the beacon that fired, with a reason tag you chose, and the Step still Live.
- **Display Data** narrates per outcome: *Completed Beats By Outcome*, keyed by the outcome tag, with *Completed Beats Default* behind it.
- **An activation carries its outcome.** The next Step's Objective can read which path started it, and the *Entries* tab shows it.

Still to come: the Blocked state and a refused give (Chapter 6), the Deactivate pins and AND / OR / NOT (Chapter 7), the Config Asset (Chapter 11), and *Procedural*, *Forward Params*, and multi-target Objectives on the closing page.

---

## Try it

### Retag a beacon:

Select `BP_QuestTriggerActor4` - the left fork beacon - in the World Outliner and set its *Owned Outcome Tag* to `MyGameTags.Fork.RightPath`. No compile: the tag lives on the placed actor. Play the room from its button and touch the *left* beacon. The right beat prints, the right door opens, and the Fork's path fact is `.Path.Right`. The graph never had a left. It had a tag, and you changed it.

Now clear the tag and play again. Touch the left beacon: the denied sound, the blocked color, no beat, and no pulse on the Fork's halo. The Objective refused the fire with `MyGameTags.Refusal.WrongTag`, and the Step is still Live - step off, and the right beacon still completes it. With *Activation* set to *Verbose* under Project Settings > Plugins > Simple Quest > Logging, the Output Log shows the refusal on its way through: `UQuestObjective::RefuseTrigger : Reason=MyGameTags.Refusal.WrongTag, TriggeredActor=BP_QuestTriggerActor_C_4 …`, then `HandleOnNodeRefused: '...Chapter_5.The_Fork' reason='MyGameTags.Refusal.WrongTag' …`. Put `MyGameTags.Fork.LeftPath` back when you are done.

**The left beacon, refused by the Objective:**

<img width="1200" alt="The left beacon with its tag cleared, refused by the Objective: the beacon in its blocked color and no beat on the HUD" src="https://github.com/user-attachments/assets/cfe26b59-4750-44fe-8572-d89ccabc5339" />

### Declare a third path:

Open `OBJ_LeftOrRight`, drop a *Complete Objective With Outcome* node anywhere in the event graph - connected to nothing - and pick `SimpleQuest.Outcome.Solved` on it. Compile the Blueprint, then *Compile All*. Open `QL_Ch5_NamedOutcomes`: The Fork has a third yellow pin, *Solved*, with nothing wired to it. That is a pin being a placement. To watch a Step end on it, wire the new node in place of *Refuse Trigger* on the no-match branch, clear the left beacon's tag as in the first experiment, *Compile All*, and touch that beacon: both fork beacons go dark, no beat prints, neither door opens, and the chapter is stuck with no Live Step - the unwired-path gotcha, built by hand. Delete the node, restore *Refuse Trigger* and the beacon's tag, and *Compile All*.

**The Fork with a third pin:**

<img width="953" alt="The Fork after the compile, with a third yellow pin, Solved, wired to nothing" src="https://github.com/user-attachments/assets/8b705eb6-a4a6-4b0e-82d7-7edf33438a37" />

---

Previous: [Chapter 4 - Sequential Steps](04_SequentialSteps.md) | Next: [Chapter 6 - Blocking](06_Blocking.md)
