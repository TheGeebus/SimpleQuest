# Chapter 3 - Basic Giver

*A quest offered by an actor: activated is not started.*

Chapters 1 and 2 had Steps that were running the moment the graph reached them. This room puts an actor between those two moments. Two Steps, both on the Objective from Chapter 1, and two quest givers in the room: a Step the graph has reached is *offerable*, and only a giver handing it over makes it *run*. The first Step is watched by both givers, and either can start it. The second is offered by one of those same givers, a second time. Nothing on either Step node says any of this.

**By the end of this chapter you can name:** a Quest Giver Component, the Pending Giver state, the ENABLED event, the order ACTIVATED → ENABLED → STARTED, and where the decision that a Step has a giver actually lives.

---

## In the room

In order:

1. Chapter 3 activated as you finished Chapter 2, so by the time you are through the door two beats have printed - the chapter's, and the first Step's:

   > This chapter's quests have GIVERS, actors that decide when you may take a quest.
   >
   > Until a Giver hands one over, the quest exists but it is not running.

   > ACTIVATED. This quest is offerable, and TWO Givers are watching its tag. Either will start the quest.

   Two cubes in the room glow yellow. They are `BP_QuestGiverActor` instances - the same open cube as Chapter 1's beacon, carrying a Quest Giver Component instead of a Trigger Component - and yellow is the example actor's color for "I have a quest to offer." The beacons are dark.

<img width="1200" alt="The room on entry: both giver cubes glowing yellow, the beacons dark, the chapter's beat and the ACTIVATED beat for Either Giver Will Do on the HUD" src="https://github.com/user-attachments/assets/1ec37177-926b-4aa0-8f3d-3678f7bda2f6" />

2. Walk into either giver. The giver's accept sound plays - a different cue from the beacon's - both cubes turn blue, the beacon lights, and the Step's second beat prints:

   > STARTED. You accepted, and the quest is now Live. Only now do Triggers become active.
   >
   > The other Giver stopped offering the moment this one handed it over. Both were watching the same tag, but the quest needed only one of them.
   >
   > Find the glowing Trigger.

<img width="1200" alt="After walking into a giver: the STARTED beat on the HUD, both cubes blue, the first beacon lit" src="https://github.com/user-attachments/assets/5a5cb8e1-13d6-4c5f-a9f1-d014d6d973a2" />

3. Touch the beacon. The Step completes - its beat is one line - and the second Step activates on the same touch. One cube turns yellow again. It offered the first Step too, whether or not you took it from there.

   > COMPLETED. The Trigger advanced the objective, and the step resolved.

   > ACTIVATED. And the Giver offering this one is an actor who also offered the earlier quest.
   >
   > A Giver Component lists the quests it offers. Nothing limits it to one.

<img width="1200" alt="After the first beacon: the COMPLETED beat and the ACTIVATED beat for Same Giver, Again, one cube yellow again" src="https://github.com/user-attachments/assets/29a96cc8-d318-4978-8024-c131ffb1a2d6" />

4. Walk into that giver. The Step starts, and this time two beacons light: the one you already used, and a second one.

   > STARTED. Same actor, different quest.
   >
   > Notice that both Triggers are now active at once. Either satisfies the Objective. Like Givers, Triggers may watch the same step as each other and may be reused.

<img width="1200" alt="After the second give: the STARTED beat, two beacons lit at once" src="https://github.com/user-attachments/assets/402e403f-d53d-40f2-8f2f-c15282b294bc" />

5. Touch either beacon. The other goes dark with it, the Step's beat is the single word `COMPLETED.`, and the chapter's completed beat follows:

   > A Giver declares which quests it offers, and a quest waiting on one takes the first offer it gets. Neither owns the other, which is why several Givers can gate one quest - and one Giver can offer several.
   >
   > A Giver gates a quest from Activated to Live. A Trigger advances it once it is Live. Many quests use both.
   >
   > Head back through the door to continue.

   Chapter 4's door opens. Chapter 4 just activated.

<img width="1200" alt="After the second beacon: the chapter's completed beat, Chapter 4's door opening" src="https://github.com/user-attachments/assets/194329cc-27c5-418c-b367-b510687bee73" />

The in-world tell is two kinds of actor lighting up on two different events. The givers change color on ACTIVATED, before anything is running; the beacons wait for STARTED, as they did in Chapter 1. Stand between a yellow giver and a dark beacon and you are looking at the difference this chapter is about. Neither actor knows the other exists. Each is reacting to events on one tag.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/03_BasicGiver/QL_Ch3_BasicGiver`. Two Steps and an Outcome:

**Start → Either Giver → Reuse Giver → Outcome (Reached)**

- Both Steps host `OBJ_InteractWithTarget`, so both complete on *Reached* when a beacon is touched.
- Both continue from their white *Any Outcome* pin rather than from *Reached* - Chapter 2's lesson applied: the room ends however the Objective ends.
- No property on either node says "giver." What marks them is the summary line on the node face - *Givers: 2 · Triggers: 1* on Either Giver, *Givers: 1 · Triggers: 2* on Reuse Giver. That line is the editor reading the level, not a property of the Step.

<img width="1200" alt="QL_Ch3_BasicGiver: the whole graph, both comment boxes, and the Givers and Triggers counts on the two Step nodes" src="https://github.com/user-attachments/assets/0b86f744-b75b-4a8b-bb9e-ee49ec01091e" />

Two comment boxes sit in the graph. In short:

- *Giver Component* - watches a Step and gates its transition to Live, which follows STARTED. Givers are shown on every Step, Quest, or Linked Questline node they gate.
- *The arrow at the bottom of a Step node* - opens the list of actors with Trigger or Giver components watching its tag, and says which questline they watch it through when that is a linked one.

### Things worth clicking:

<br>
  <img width="358" alt="Either Giver expanded: two givers and one trigger listed, each marked (via QuickStart)" src="https://github.com/user-attachments/assets/1c512a59-9de0-4e80-81e7-5ce8ed6d5452" />
<br>

- **Expand Either Giver.** Two givers - `BP_QuestGiverActor2` and `BP_QuestGiverActor4` - and one trigger, `BP_QuestTriggerActor6`, all marked *(via QuickStart)*.

<br>
  <img width="370" alt="Reuse Giver expanded: one giver and two triggers listed" src="https://github.com/user-attachments/assets/681978f5-a698-4d99-a687-f63f3d24fd58" />
<br>

- **Expand Reuse Giver.** One giver, `BP_QuestGiverActor2` again, and two triggers: `BP_QuestTriggerActor6` again, and `BP_QuestTriggerActor16`. The reuse is symmetrical - one actor offers both Steps, and one actor can fire both.

- **Select Either Giver.** The Details panel is Chapter 1's: Node Label, Display Name, Display Data, Objective Class, and the rest. Nothing here refers to a giver. *Display Name* is "Either Giver Will Do" and *Display Data* is `DA_Ch3_StepA`; the tag is built from the label, so it ends in `Either_Giver`.

<br>
  <img width="1200" alt="BP_QuestGiverActor2's Quest Giver component in the Details panel: Quest Tags to Give holding both Steps' tags, Step Tags to Trigger and Observed Tags empty" src="https://github.com/user-attachments/assets/6d6eb189-aa45-4b31-aad8-2091cd0bed18" />
<br>

- **Select `BP_QuestGiverActor2` in the level** - find it in the World Outliner - and its Quest Giver component. Under *Quest*, *Quest Tags to Give* holds two entries, the placed spellings of both Steps: `SimpleQuest.Questline.QuickStart.Chapter_3.Either_Giver` and `...Reuse_Giver`. Beneath it, *Step Tags to Trigger* and *Observed Tags* are empty. Both are on this component too, inherited, because a Giver Component is a Trigger Component is an Observer Component. Select `BP_QuestGiverActor4` next: one entry.

<br>
  <img width="1200" alt="BP_QuestGiverActor's event graph: the five bound events and their comment boxes" src="https://github.com/user-attachments/assets/f86d0cde-3c30-4848-a468-c75a881ccff1" />
<br>

- **Open `BP_QuestGiverActor`** in `SimpleQuest Content/ExampleBlueprints/Actors`. Five events do the whole job. *Begin Play* creates the aura material. *On Give Availability Changed* recolors the cube and switches its collision on or off from what the component reports. *On Component Begin Overlap* calls *Give Quest* for every tag in the component's activated set. *On Quest Started* plays the accept sound, `QuestAccepted`. *On Quest Give Blocked* turns the cube red and plays the refusal, `QuestStartFail`. Blue, yellow, red: nothing to offer, offering, refused. The beacon has its own pair - `QuestStepSuccess` when the Objective marks it satisfied, `QuestStepDenied` when a fire is refused - so the tutorial's four sounds tell you which actor answered, and how, without looking.

<br>
  <img width="745" alt="DA_Ch3_StepA: Activated Beats, an empty Enabled Beats, Started Beats, and Completed Beats Default" src="https://github.com/user-attachments/assets/487c4c61-2f0b-426b-abbe-9e4524c932e3" />
<br>

- **Open `DA_Ch3_StepA`.** Three arrays are filled this time - *Activated Beats*, *Started Beats*, and *Completed Beats Default*. The *Enabled Beats* array between the first two is empty, and ENABLED did fire for this Step. An event with no line authored prints nothing.

---

## Under the hood

### Two events, one moment - until something separates them

Chapter 1 published ACTIVATED and STARTED back to back and promised an explanation. They answer different questions. **ACTIVATED** says activation reached this node: it is in scope, it can be offered, and the framework is tracking it. **STARTED** says it is running: `.Live` is written, the Objective has been instantiated and is listening, and triggers arm. For a Step with nothing standing between them, the second follows the first inside the same call and the distinction is invisible. A giver is the first thing in the tutorial that puts time between them.

### The gate is declared before the graph runs

A Quest Giver Component publishes the tags in its *Quest Tags to Give* when its actor initializes - at component initialization, before any actor's Begin Play, so before the green button can be pressed. The quest manager collects these into one set: the Steps that have a giver somewhere in the loaded world. Two actors declared `...Chapter_3.Either_Giver`; one would have been enough.

When activation reaches Either Giver, the manager finds its tag in that set, and instead of starting the Step:

1. Writes `SimpleQuest.State.QuickStart.Chapter_3.Either_Giver.PendingGiver` - and the same fact under the asset's own spelling, as in Chapter 1. No `.Live`, no `.Started`. The chapter's own `.Live` is written anyway: a container counts a Step waiting on a giver as activity.
2. Publishes **ACTIVATED** on the Step's tag, carrying the result of evaluating the Step's prerequisite expression. The HUD prints the beat. Both givers hear it - each is subscribed to exactly the tags it offers - and move the tag into their *Activated* set, which is what turns the cubes yellow.
3. Publishes **ENABLED**, because the prerequisite is satisfied - there is none. Enabled means activated *and* the prerequisites hold: a give is expected to succeed. If the Step had an unmet prerequisite, ENABLED would wait, and the manager would watch the prerequisite's facts and publish it when they changed. A Blocked Step is deliberately not checked here - it still publishes both events, so the giver stays visible and the refusal comes at the give (Chapter 6). The givers move the tag into their *Enabled* set. Nothing prints; the *Enabled Beats* array is empty.
4. Returns. No STARTED, no Objective instance, no `.Started`. The beacon that watches this Step arms on STARTED, so it has heard nothing.

The graph shows the state with its own halo color - cyan, where Live is yellow - and the Facts Panel's Quest State view lists the Step on its *Prereq Status* tab for as long as it waits.

**The World State view while the Step waits:**

<img width="1200" alt="World State view filtered to Either_Giver while the Step waits: .PendingGiver under both spellings, no .Live or .Started" src="https://github.com/user-attachments/assets/b16e7f90-e6ba-499e-9e40-11e1b34679da" />

**The Pending Giver halo:**

<img width="400" alt="The cyan Pending Giver halo on Either Giver during PIE" src="https://github.com/user-attachments/assets/f8b3be63-d490-4dcb-952c-e804c5ed5fc4" />

**The Quest State view, Prereq Status tab:**

<img width="1200" alt="Quest State view, Prereq Status tab: the row for Either_Giver while it waits" src="https://github.com/user-attachments/assets/bc069276-8e97-4dd6-8521-4d80f9941451" />

### The give

Walking into the giver runs its overlap event, which calls *Give Quest* on the component for every tag in its activated set - here, one. The component subscribes, one-shot, to a refusal on that tag; sets the give's *Instigator* to its own actor if the caller left it empty; and publishes a give request. That request is a message on the bus like everything else, and the same one is available as *Give Quest* in the Blueprint library, with no component involved, for a dialogue widget to send.

The manager receives it:

1. It asks the Quest State Subsystem what would block this give: the Step already Live, Blocked, not actually waiting on a giver, or waiting on an unmet prerequisite. Any answer refuses the give, and a **give blocked** event goes out on the Step's tag with the reasons and the actor that asked. Every giver watching the tag hears it; the payload's *Giver Actor* says whose give it was. The example actor's *On Quest Give Blocked* turns it red. Chapter 6 refuses a give on purpose.
2. Nothing blocks here. The manager clears `.PendingGiver`, notes which actor gave, stamps the give's context onto the Step, and activates the Step past the gate. `.Live` and `.Started` are written, **STARTED** publishes with the giver actor in its payload - ACTIVATED is not published a second time - the Objective is instantiated, and the Step's entry is recorded: the Quest State view's *Entries* tab shows it with provenance *GiverGate* and the giver's name.
3. Both givers hear STARTED and drop the tag from their *Activated* and *Enabled* sets. Their *On Give Availability Changed* fires with the tag under *Newly Unavailable*, and the example actor turns blue and switches its collision off. The giver whose give was in flight also records the tag in its *Given Quests* history, and the example actor checks that history before playing the accept sound, so only the cube you touched plays it. The other giver never had anything to refuse. It was told the quest had gone.
4. The beacon hears STARTED on the tag it watches, arms, and lights. From here it is Chapter 1.

**The Entries tab after the give:**

<img width="1200" alt="Quest State view, Entries tab after the give: the Either_Giver row with Provenance GiverGate and the giver actor's name" src="https://github.com/user-attachments/assets/d20ca8ed-10ee-47fe-a673-00ab6fc12867" />

**The Output Log across the gate and the give:**

<img width="1200" alt="The Output Log filtered to LogSimpleQuestActivation: the gated-by-giver line and the HandleGiveQuestEvent line clearing PendingGiver" src="https://github.com/user-attachments/assets/b7fa4d20-f98c-42ac-8bd5-2edb08730ef4" />

### Completion, twice

Touching the beacon completes the Objective on *Reached*, and COMPLETED publishes on Either Giver's tag - Chapter 1's completion, step for step. Then *Any Outcome* carries activation into Reuse Giver, and the gate fires again, because `BP_QuestGiverActor2` declared that tag too: `.PendingGiver`, ACTIVATED, ENABLED, the cube yellow. `BP_QuestGiverActor4` stays blue and silent. It never listed the second Step, so it is not subscribed to it.

The second give runs exactly as the first. STARTED on Reuse Giver's tag arms two beacons at once: `BP_QuestTriggerActor6` for the second of its two tags, and `BP_QuestTriggerActor16` for its only one. Whichever you touch fires. The Objective completes, the beacon that fired hears *Satisfied* and the *Completed* response and shows its satisfied color, and both beacons hear the Step end, so the other simply goes dark. *Any Outcome* runs into the Outcome node, the questline resolves with `Reached`, the questline's rewards pay, the chapter's completed beat prints, and in the master graph the Chapter 3 node's *Any Outcome* satisfies Chapter 4's prerequisite. Chapter 4 activates, and its door opens.

### Who was listening

- Each giver is subscribed to the tags in its *Quest Tags to Give* and to nothing beneath them: the lifecycle events on exactly those tags - ACTIVATED, ENABLED, STARTED, COMPLETED, and the rest - plus the refusal to its own give while one is in flight. A giver for a Quest node would not hear that Quest's Steps.
- Each beacon is subscribed as in Chapter 1, once per tag it watches. `BP_QuestTriggerActor6` holds two, so it was armed twice in this room.
- The HUD hears everything under the master questline and prints what has beats.
- The Step knows none of them. It has no giver property, no trigger list, and no observer list. The gate exists because actors in the world said so before the graph ran, and the manager believed them.

---

## Gotchas

**A giver is declared, not configured.** Nothing on the Step node makes it wait for a giver; the wait exists because an actor in the loaded world listed the Step's tag. Remove both giver cubes and the Step goes Live on arrival, exactly as in Chapter 1. The set is consulted at the moment activation reaches the Step, so a giver that comes online after that moment finds the Step already running, and a giver in a level that is not loaded yet does not exist. A component whose *Quest Tags to Give* is empty says so in the Output Log when it registers.

**A beacon touched while its Step waits on a giver does nothing - silently.** Not PROGRESS REFUSED; dropped. The beacon is not even armed, and a *Send Trigger Event* against a Step that is pending a giver finds neither a Live Step to advance nor a structural reason to refuse. Chapter 1's rule holds: a fire against a Step the runtime has not started is not the trigger's concern. If a giver-gated Step of yours "ignores the trigger," look at the halo. Cyan means nobody has given it yet.

**Enabled is not Activated.** A giver's *Activated* set means "the quest reached me"; its *Enabled* set means "I could give it right now." The example actor gives everything in its activated set on overlap and lets a refusal turn it red, which is the right choice for a tutorial that wants to show refusals. A giver in your game that should only offer what will succeed reads *Current Enabled* from the availability change, or asks *Can Give Any Quests*.

**A second give is refused, not queued.** Once a Step is Live, any further give to it comes back on *On Quest Give Blocked* with *Already Live*. The example actor makes that impossible to trigger here - it drops the tag from its activated set on STARTED and switches its collision off - but a giver you write that keeps offering after a give will hear the refusal.

**The give carries who gave.** The component defaults the give's *Instigator* to its actor, and that reference travels into the Objective's activation context, the STARTED payload, and the Quest State record. The library's *Give Quest* defaults nothing: call it from a widget without setting an instigator and the Step starts with none. And a give from anywhere still needs a Giver Component somewhere to have declared the tag. The declaration is what makes the Step wait; the give only answers it. A give to a Step nothing declared arrives to find it already Live, and is refused.

**One component, three roles.** Quest Giver Component derives from Quest Trigger Component, which derives from Quest Observer Component. A single component can offer one Step through *Quest Tags to Give*, fire another through *Step Tags to Trigger*, and watch a third through *Observed Tags*. The tutorial keeps them on separate actors so each cube's color means one thing.

**Replaying re-gates.** Start this room again from its button and Either Giver returns to Pending Giver: the cubes go yellow, the beacon stays dark, and you give it again. *Given Quests* on the component is a history for the session, not a lock.

**The placed spelling is what the actors hold.** Both givers and both beacons carry `SimpleQuest.Questline.QuickStart.Chapter_3.…`, the placement inside the master graph, as in Chapter 1. Chapter 8 is where a Step has more than one placement and the spelling on the giver starts to matter.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **The Step has no giver property.** Whether it waits is decided outside the graph, by a Quest Giver Component in the world listing its tag. The node face reports what the level says - *Givers: 2* - and the expander names them.
- **ACTIVATED and STARTED are two events**, and a giver is what puts time between them. The Objective is instantiated at STARTED, not before; triggers arm at STARTED, not before. Between the two, the Step is Pending Giver and the Facts Panel shows `.PendingGiver`.
- **A give carries a runtime context into the Objective.** The instigator - the giver - plus any custom data the giver attaches, merged with the Step's own defaults and delivered to the Objective when it activates. None of the tutorial's Objectives read it, but the *Entries* tab shows what arrived, and the closing page says where it is documented.
- **Per-Step Display Data has one array per event**, and the ones you leave empty print nothing. Both Steps carry their own asset, and their sidebar names are their *Display Name*: "Either Giver Will Do" and "Same Giver, Again." ENABLED fired twice in this room and never printed.

Still to come: the Prerequisites pin and the gate mode (Chapter 4), an Objective you write yourself (Chapter 5), the Deactivate pins (Chapter 7), and the Config Asset (Chapter 11).

---

## Try it

### Watch the gate from three windows:

Nothing to change. Play the room from its button with `QL_Ch3_BasicGiver` open beside the viewport, the World State view filtered to `Either_Giver`, and a Quest State view on its *Prereq Status* tab. Before you touch a giver: a cyan halo, `.PendingGiver` under both spellings and nothing else for the Step, and one row on the *Prereq Status* tab. Touch a giver: the halo turns yellow, `.PendingGiver` is gone, `.Live` and `.Started` are there, the row is gone, and the *Entries* tab has a row with *GiverGate* and the giver's name. Then select the giver cube in the World Outliner while the game runs. Its Quest Giver component shows *Activated Quest Tags*, *Enabled Quest Tags*, and *Given Quest Tags* live in the Details panel. Replay from the button and use the other cube: the *Entries* tab gets a second row with the other name.

The Output Log filtered to `LogSimpleQuestActivation` prints the gate and the give as two lines: `gated by giver - Activated published, prereqs satisfied (Enabled fired)` when the Step is reached, and `HandleGiveQuestEvent: … clearing PendingGiver, activating 1 placement(s)` when you accept.

**The giver's component during play:**

<img width="1200" alt="A giver cube selected during PIE: Activated Quest Tags, Enabled Quest Tags, and Given Quest Tags in its Details panel" src="https://github.com/user-attachments/assets/37129122-61a5-42e1-89d0-3b82c9f2d240" />

### Take the gate away:

Select `BP_QuestGiverActor2` and `BP_QuestGiverActor4` in the World Outliner and, on each Quest Giver component, empty *Quest Tags to Give*. No compile: the tags live on the placed actors, not in a graph. Play the room from its button. Both cubes stay blue, the Output Log warns that each giver has nothing to give, and Either Giver goes Live on arrival: a yellow halo, the beacon lit, `.Live` and `.Started` written with no `.PendingGiver` first - Chapter 1's behavior, from the same graph and the same Step. The first beat still says two givers are watching, because beats are authored text, not a query. Touch the beacon and Reuse Giver goes Live the same way, both beacons lighting at once. Undo the two edits when you are done, or re-add the tags from the picker.

**The room with no gate:**

<img width="1200" alt="The room on entry with Quest Tags to Give emptied on both givers: the cubes blue, the first beacon already lit" src="https://github.com/user-attachments/assets/95d9dba5-e4a6-4dac-95e9-943ecd1414cf" />

---

Previous: [Chapter 2 - Rewards](02_Rewards.md) | Next: [Chapter 4 - Sequential Steps](04_SequentialSteps.md)
