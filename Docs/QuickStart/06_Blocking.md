# Chapter 6 - Blocking

*Blocked is a switch, not a condition. Flip it from anywhere, and the giver and the trigger both refuse.*

<!-- SHOT LIST (page order; each number matches a slot comment below - replace the slot with the <img> line and delete the comment)
  6.1  The room on entry: the chapter's beat and the Step's ACTIVATED beat on the HUD; the giver cube yellow; the green button reading "Press E to Unblock this Quest", the red one "Locked - Quest is already Blocked"; the door shut.
  6.2  After walking into the giver: the cube red, the GIVE BLOCKED beat.
  6.3  After the green button: the UNBLOCKED beat; the green button now "Locked - Quest is already Unblocked", the red one "Press E to Block this Quest".
  6.4  After accepting: the STARTED beat; the cube blue.
  6.5  After the red button: the BLOCKED beat; the door open.
  6.6  Touching the beacon while blocked: the PROGRESS REFUSED beat; the beacon in its blocked color.
  6.7  After the second green button: the UNBLOCKED beat printing a second time, the same line as the first.
  6.8  After the beacon: the Step's completed beat and the chapter's; Chapter 7's door opening.
  6.9  QL_Ch6_Blocking, the whole graph: Start into Set Blocked, Forward into BlockTargetStep, Any Outcome into the Outcome node; the unwired Clear Blocked below; the comment boxes readable.
  6.10 Set Blocked selected: Details with Target Quest Tags naming the Step and Also Deactivate Targets unticked; the node face's "Tags to Block:" row.
  6.11 DA_Ch6_BlockingTargetStep open: all seven filled arrays visible.
  6.12 BP_BlockToggleButton2 (the red one) selected in the level: Block Mode = Block, Target Tag = the Step.
  6.13 BP_DoorPanel15 selected: Open Door Tag = SimpleQuest.Fact.QuickStart.Blocking.DoorOpen.
  6.14 The graph during PIE after the refused give: the Step with the Pending Giver halo, the red gating ring, and the refusal pulse.
  6.15 World State view filtered to Chapter_6.BlockTargetStep on entry: .Blocked beside .PendingGiver.
  6.16 The Output Log filtered to LogSimpleQuestActivation: the HandleBlockRequest line (source=Internal) from the graph, the refused-give Warning with its "Blocked" bullet, and the HandleClearBlockRequest line (source=External) from the button.
  6.17 Try it: the graph with Start wired straight into BlockTargetStep, Set Blocked bypassed.
  6.18 Try it: after the red button with Also Deactivate ticked: the beacon dark; World State showing .Deactivated on the Step.
-->

Chapter 4's refusal came from a condition the graph could evaluate. This one comes from a switch. One Step, one giver, one beacon, three buttons, and a door: the graph blocks the Step before it is even offered, a green button clears the block, the giver hands the quest over, the red button blocks it again - and opens the door to the beacon, which refuses you until the other green button clears it once more. Nothing about the Step changes between a refusal and an acceptance except one fact, and every actor in the room reads that fact.

**By the end of this chapter you can name:** the Blocked state and its fact, the Set Blocked and Clear Blocked nodes and the Blueprint calls behind them, GIVE BLOCKED, PROGRESS REFUSED with the reason *Blocked*, the one blocker vocabulary every refusal speaks, and why blocking is not deactivating.

---

## In the room

In order:

1. Chapter 6 activated as you finished Chapter 5, so on the way in two beats have printed - the chapter's, then the Step's:

   > Blocked is a lifecycle state you set and clear directly - from anywhere, anytime, no prerequisite involved.
   >
   > The buttons can Unblock and Block this quest. Experiment to see the effect on Givers and Triggers.

   > ACTIVATED. A Giver is offering this step, but it's Blocked.
   >
   > Try to accept it, to feel the refusal - then clear the block at the pedestal.

   A giver cube glows yellow. Look at the green button and it reads *Press E to Unblock this Quest*; the red one reads *Locked - Quest is already Blocked*. The door is shut.

<img width="1200" alt="The room on entry: the chapter's beat and the Step's ACTIVATED beat on the HUD, the giver cube yellow, the green button reading Press E to Unblock this Quest, the red one Locked, the door shut" src="https://github.com/user-attachments/assets/83c5d78a-00f7-496d-b766-a8614a98290e" />

2. Walk into the giver. The refuse sound plays, the cube turns red, and the beat you have not seen before prints:

   > GIVE BLOCKED. The offer won't accept while the step is Blocked, and no prerequisite is involved. The Blocked state was set directly.
   >
   > Clear it with the green button, then accept again.

   Step away and the cube is yellow again. The offer is still standing; it was refused, not withdrawn.

<img width="1200" alt="After walking into the giver: the cube red, the GIVE BLOCKED beat on the HUD" src="https://github.com/user-attachments/assets/6042d5ce-528e-428e-a93e-fbc362191204" />

3. Press E at the green button.

   > UNBLOCKED. The Blocked state was cleared directly - the Giver will accept now, and the Trigger can advance normally.

   The labels flip: the green button now reads *Locked - Quest is already Unblocked*, the red one *Press E to Block this Quest*.

<img width="1200" alt="After the green button: the UNBLOCKED beat on the HUD, the green button now Locked and the red one reading Press E to Block this Quest" src="https://github.com/user-attachments/assets/7c2b8c06-eacb-45ab-be91-3faa572efb32" />

4. Walk into the giver again. The accept sound, the cube turns blue, and the Step starts:

   > STARTED. Offer accepted, the step is now Live. You must block it again using the red button to open the door.

<img width="1200" alt="After accepting: the STARTED beat on the HUD, the cube blue" src="https://github.com/user-attachments/assets/480a2963-d5a6-4a09-9b9a-0f2d9a01563a" />

5. Press E at the red button. Two things happen at once: the Step is blocked again, and the door opens.

   > BLOCKED. Blocked again - the Giver refuses the offer, and a Trigger won't advance it either, until you clear it by pressing a green button.

   Beyond the door, the beacon is lit. Blocking the Step did nothing to it.

<img width="1200" alt="After the red button: the BLOCKED beat on the HUD, the door open" src="https://github.com/user-attachments/assets/295887a0-4ec3-4639-8f73-739b49ae8b25" />

6. Touch the beacon. The denied sound, the blocked color, and the event Chapter 4 introduced, with a different reason behind it:

   > PROGRESS REFUSED. The same block, a different gate - now refusing the Trigger instead of the Giver. Block stops both the offer and the progress.

   Step off and it is lit again.

<img width="1200" alt="Touching the beacon while blocked: the PROGRESS REFUSED beat on the HUD, the beacon in its blocked color" src="https://github.com/user-attachments/assets/2de9275d-1fb1-4b66-b993-fafbb4e66a53" />

7. Press E at the other green button. The beat is the one you read at step 3, word for word:

   > UNBLOCKED. The Blocked state was cleared directly - the Giver will accept now, and the Trigger can advance normally.

   The first time, this line freed the giver; this time it frees the beacon. One flag, one event to clear it, and whichever role was waiting is the one that moves.

<img width="1200" alt="After the second green button: the UNBLOCKED beat on the HUD a second time, the same line as before" src="https://github.com/user-attachments/assets/4fe625b6-6cc1-4d33-93e3-3457629e2e77" />

8. Touch the beacon. It completes, and the chapter with it:

   > Step complete.
   >
   > Manual blocking and prerequisites are independent - a step can be gated by a prereq AND directly Blocked at once. The framework treats them as separate states.

   > Well done.
   >
   > Blocked is just a flag, flipped on your own terms, that gates Givers and Triggers alike - until you clear it.

   Chapter 7's door opens.

<img width="1200" alt="After the beacon: the Step's completed beat and the chapter's on the HUD, Chapter 7's door opening" src="https://github.com/user-attachments/assets/fbe9aee1-d541-44cf-be71-989aaec7ee9f" />

The in-world tell is the same actor answering twice: the cube refuses you and then accepts you, the beacon refuses you and then completes, and nothing in the graph moved between the two answers. A fact did, and the buttons are the only thing that touched it.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/06_Blocking/QL_Ch6_Blocking`. One Step, two utility nodes, and an Outcome:

**Start → Set Blocked → (Forward) → BlockTargetStep → (Any Outcome) → Outcome (Victory)**, plus a **Clear Blocked** node wired to nothing.

- *Set Blocked* and *Clear Blocked* are utility nodes: an *Enter* input, a *Forward* output, and a row on the face listing the tags they act on. Activation passes through them; they do their one thing and forward it. They have no tag, no facts, no halo of their own.
- Set Blocked's row, *Tags to Block:*, names `SimpleQuest.Questline.QuickStart.Chapter_6.BlockTargetStep` - the Step's placed address under the master graph, not the asset's own `QL_Ch6_Blocking.BlockTargetStep`. Either resolves to the same node (Chapter 1's two addresses), and Chapter 8 is where the choice starts to matter.
- BlockTargetStep hosts `OBJ_InteractWithTarget`. Its *Display Name* is *Block and Unblock*, its face reads *Givers: 1 · Triggers: 1*, and only *Any Outcome* is wired.
- The Outcome node's tag is `SimpleQuest.Outcome.Victory`, not *Reached*. A chapter ends on whatever outcome its Outcome node names - *Victory* ships with the plugin's tags - and Chapter 7's prerequisite is wired from the Chapter 6 node's *Any Outcome*, so the name is the author's to choose.
- Clear Blocked lists the same tag under *Tags to Clear:* and nothing runs into its *Enter*. The room clears the block from the level instead.

<img width="1200" alt="QL_Ch6_Blocking: the whole graph, Start into Set Blocked, its Forward into BlockTargetStep, Any Outcome into the Outcome node, the unwired Clear Blocked below, and the comment boxes" src="https://github.com/user-attachments/assets/7df37091-99a3-4545-87d5-cc4c213f0b94" />

**Four comment boxes. In short:**

- *The frame, "Blocking and Unblocking"* - the title over the left half.
- *Over Set Blocked* - Blocked is a gate, not an ending. A blocked Step refuses to activate, and one that is already Live refuses to advance, but it stays Live and its trigger stays active, so the player gets feedback instead of silence. Deactivating is the thing that takes targets away. *Also Deactivate* is off, which is the default; on, it opts into interrupting whatever is in flight on the target.
- *Over Clear Blocked* - this node is deliberately not wired. The block is cleared from the button in the level, which calls Clear Blocked through the Blueprint library. Both sides reach the same gate, and a block set from one can be cleared by the other.
- *Over the Step* - Givers: 1, Triggers: 1. The block suppresses both roles, and both report it: the giver publishes Give Blocked with the blocker list and the giver actor that tried, and the offer survives the refusal; the trigger publishes Progress Refused with the same blocker and the trigger context. Both name Blocked from one shared vocabulary, so a UI can branch on the reason rather than on which role hit the wall - and Blocked reads differently from an unmet prerequisite, through the Reason on each blocker.

### Things worth clicking:

<br>
  <img width="1104" alt="Set Blocked selected: the Details panel with Target Quest Tags naming the Step and Also Deactivate Targets unticked" src="https://github.com/user-attachments/assets/be18cf2d-feff-4d6e-a639-db2190917349" />
<br>

- **Select Set Blocked.** The Details panel has two things: *Target Quest Tags*, a tag list with the Step's address in it, and *Also Deactivate Targets*, unticked. That is the whole node. It does not know whether its target exists yet, is waiting on a giver, or is running - it writes a fact against a tag, and at this point in the graph the Step has not activated.

<br>
  <img width="836" alt="DA_Ch6_BlockingTargetStep: Activated, Give Blocked, Started, Progress Refused, Completed Beats Default, Blocked, and Unblocked Beats all filled" src="https://github.com/user-attachments/assets/97249e9f-426f-4854-b19d-85664e07e333" />
<br>

- **Open `DA_Ch6_BlockingTargetStep`.** Seven arrays filled - *Activated*, *Give Blocked*, *Started*, *Progress Refused*, *Completed Default*, *Blocked*, *Unblocked* - the fullest display data in the tutorial. Every line you read in the room is one of them, and the three new ones are the three events this chapter adds. `DA_Ch6Main_Blocking` fills only *Activated Beats* and *Completed Beats Default*.

<br>
  <img width="1200" alt="BP_BlockToggleButton2 selected in the level: Block Mode set to Block, Target Tag set to the Step" src="https://github.com/user-attachments/assets/a09e1aca-47a5-4ff8-ba94-9129a5ea7c73" />
<br>

- **Select the red button in the level** - `BP_BlockToggleButton2` in the World Outliner. *Block Mode* is *Block* and *Target Tag* is the Step's address. The two green buttons, `BP_BlockToggleButton` and `BP_BlockToggleButton3`, are the same actor class with *Block Mode* set to *Unblock*. A button's label is read from the state when you look at it: *Is Quest Blocked* decides between *Press E to...* and *Locked - already...*, so a button is never offering the thing that is already true.

<br>
  <img width="1200" alt="BP_DoorPanel15 selected in the level: Open Door Tag set to SimpleQuest.Fact.QuickStart.Blocking.DoorOpen" src="https://github.com/user-attachments/assets/d647dda5-bf5b-43bf-9382-e06f4e6a4db9" />
<br>

- **Select the door** - `BP_DoorPanel15`. Its *Open Door Tag* is `SimpleQuest.Fact.QuickStart.Blocking.DoorOpen`, and that is not a quest tag. The red button is a subclass, `BP_BlockToggleButton_Door`, that does one thing more than its parent: the first time it is pressed, it adds that fact with SimpleCore's *Add Fact*. The door watches the fact, not the Step. It is the same mechanism as the pedestal buttons in *Before You Start*, borrowed to make sure you press red after you start.

- **Select the giver and the beacon.** `BP_QuestGiverActor3` lists the Step under *Quest Tags to Give*; `BP_QuestTriggerActor12` lists it under *Step Tags to Trigger*. Chapter 3's shapes, one each. Neither knows anything about blocking; they find out when they try.

---

## Under the hood

### A flag, not a phase

Chapter 1 gave the Step its lifecycle facts - `.Live`, `.Started`, `.Completed` - and Chapter 3 added `.PendingGiver`. Blocked is another: `SimpleQuest.State.QuickStart.Chapter_6.BlockTargetStep.Blocked`, written at both of the Step's addresses like the rest. What makes it different is that nothing in the lifecycle writes it. Activation, the give, the fire, completion - none of them set or clear it. Two things do, and they are the same thing twice:

- **A Set Blocked or Clear Blocked node**, when activation passes through it. The node publishes a block request on the framework's bus, with source *Internal*, and forwards.
- **The Blueprint calls** *Set Quest Blocked* and *Clear Quest Blocked*, from any Blueprint with a tag in hand. They publish the same request, with source *External*. The buttons use them.

The manager handles both requests identically: resolves the tag to the node's canonical address, writes or removes the fact, and publishes **BLOCKED** or **UNBLOCKED** on the Step's tag. Both are idempotent - blocking a blocked Step, or clearing an unblocked one, writes nothing and publishes nothing, which is why pressing a *Locked* button would do nothing even if the label let you. And because the fact is just a fact on a tag, it does not care what the Step is doing. This graph blocks the Step before the Step exists as anything but a registered instance; the room blocks it again while it is Live. Both are the same write - though only the second prints a beat. The HUD shows nothing for the block the graph sets at Start, so the first BLOCKED you read is the red button's.

*Also Deactivate* is the one option, and it is the boundary of the whole chapter: a block by itself takes nothing away. Targets stay armed, an offer stays pending, a Live Step stays Live. The flag refuses the *next transition*. Ticking *Also Deactivate* publishes a deactivate request alongside the block request, and that is what interrupts what is in flight - the second *Try it* shows the difference.

### Three gates, one reason

The fact is read at three places, and each answers with the same vocabulary - a blocker with a *Reason*, from the enum Chapter 4's refusal used:

1. **Activation.** A Step with no giver that is activated while blocked is refused: **ACTIVATION FAILED** publishes with the reason *Blocked*, the refusal is recorded, and nothing is queued - clearing the block later does not replay the activation. This room never takes that path, because its Step has a giver, and the guard deliberately lets a giver-gated activation through: the Step goes to Pending Giver, ACTIVATED and ENABLED publish, and the giver cube glows, so that the player can try and feel the refusal. The block is not a prerequisite; ENABLED means the prerequisites hold, and here there are none.
2. **The give.** The give handler asks the Quest State Subsystem for the Step's blockers before it does anything, and *Blocked* comes back. It publishes **GIVE BLOCKED** with the blocker list and the actor that tried, logs a Warning with the reasons as a bulleted list, and stops. `.PendingGiver` is untouched - the offer survives its own refusal, which is why the second walk into the cube works with nothing re-activated in between. The giver's *On Quest Give Blocked* runs with the blocker list and the giver actor; the example cube plays the refuse sound and turns red.
3. **The fire.** The manager checks Blocked first, before the prerequisite gate Chapter 4 described, and refuses with **PROGRESS REFUSED**, reason *Blocked*. The Objective never sees the fire. The beacon's *On Quest Trigger Blocked* is the same event Chapter 4's beacon reacted to, with a different reason inside it, and the example beacon does not read the reason - the HUD does, and prints the Step's *Progress Refused Beats* either way.

One struct carries all three refusals - the blocker with its *Reason* - so a UI can say "blocked" or "not yet" without knowing which role asked. The Quest State Subsystem's refusal history records each, and the graph's PIE overlay pulses the node for each.

### What the buttons and the door are

The green and red buttons are one actor class with a mode. Pressing one calls *Set Quest Blocked* or *Clear Quest Blocked* on its *Target Tag*, and its label is resolved from *Is Quest Blocked* whenever you look at it. That is the whole adopter-side surface of the feature: three Blueprint nodes, a tag, and nothing on the Step.

The door is deliberately not part of the quest state. The red button's subclass adds a World State fact, `SimpleQuest.Fact.QuickStart.Blocking.DoorOpen`, once, and the door panel opens when that fact arrives. It is SimpleCore's fact store doing what it does for the pedestal buttons: a boolean anyone can write and anyone can watch. Chapter 7 reads facts like it from the graph side, through a Fact Tag node.

### The chapter ends on Victory

BlockTargetStep completes on *Reached*, like every Step on Chapter 1's Objective, and its *Any Outcome* runs into the Outcome node - which names `SimpleQuest.Outcome.Victory`. The chapter resolves with *Victory*, `...Chapter_6.Path.Victory` is written, and in the master graph the Chapter 6 node's *Any Outcome* satisfies Chapter 7's prerequisite: compiled, that is an OR over Chapter 6's declared paths, and there is one. The outcome a chapter ends on is a tag on its Outcome node, nothing more. Chapter 5's Fork chose between two; this chapter simply names its one.

### What is watching

The World State view filtered to `Chapter_6.BlockTargetStep` tells the whole story in facts: `.Blocked` beside `.PendingGiver` on entry; `.Blocked` gone after the green button; `.Live` and `.Started` after the give; `.Blocked` back beside them after the red button; then gone, then `.Completed`. The graph draws the block as a **red gating ring** - the same ring Chapter 4 drew in purple for a prerequisite, in the color the overlay keeps for "externally locked out" - around whichever lifecycle halo the Step wears - Pending Giver's cyan, then Live's - with a refusal pulse for the refused give and again for the refused fire. The Quest State view's *Prereq Status* tab lists the Step while it waits on the giver, and says nothing about the block: it is not a prerequisite.

The Output Log filtered to `LogSimpleQuestActivation` names both writers:

```
HandleBlockRequest: 'SimpleQuest.Questline.QuickStart.Chapter_6.BlockTargetStep' - Blocked fact added, FQuestBlockedEvent published (source=Internal)
HandleGiveQuestEvent: 'SimpleQuest.Questline.QuickStart.Chapter_6.BlockTargetStep' refused - 1 blocker:
  • Blocked
HandleClearBlockRequest: 'SimpleQuest.Questline.QuickStart.Chapter_6.BlockTargetStep' - Blocked fact cleared, FQuestUnblockedEvent published (source=External)
```

*Internal* is the graph's node; *External* is the button. The refused give is a Warning, on purpose - a refused give is something an author usually wants to see. The refused fire prints nothing at the default verbosity; the HUD and the graph carry it.

**The graph after the refused give:**

<img width="1200" alt="The graph during PIE after the refused give: BlockTargetStep with the Pending Giver halo, the red gating ring outside it, and the refusal pulse" src="https://github.com/user-attachments/assets/561fd088-420e-4d9a-83fe-035c7c2efe57" />

**The facts on entry:**

<img width="1200" alt="World State view filtered to Chapter_6.BlockTargetStep on entry: the .Blocked fact beside .PendingGiver" src="https://github.com/user-attachments/assets/dd2186c0-ec9b-48c3-9798-59f312529601" />

**Both writers in the log:**

<img width="1200" alt="The Output Log filtered to LogSimpleQuestActivation: the HandleBlockRequest line with source=Internal, the refused-give Warning with its Blocked bullet, and the HandleClearBlockRequest line with source=External" src="https://github.com/user-attachments/assets/7c2e5a53-3743-48c6-8f6a-4039ea285aed" />

---

## Gotchas

**Blocked is not a prerequisite.** Nothing in the Prerequisite Examiner or the *Prereq Status* tab shows it, no dashed wire carries it, and it does not care whether any condition holds. The two are independent and can both be true at once: a Step can be gated by a prerequisite *and* blocked, and it will report whichever it is asked about first - the fire checks Blocked before the prerequisite. If a Step refuses and the examiner says every condition holds, filter the World State for `.Blocked`.

**Blocked is not Deactivated.** A block refuses the next transition and leaves everything else exactly where it is. The beacon behind the door stayed lit through the block; the giver kept offering through the first one. If you need targets to disarm and the Step to stop, that is *Deactivate* - the pins Chapter 7 wires, or *Also Deactivate* on the block. Reading the two as one thing is the most common way to build a room that refuses the player and looks broken doing it.

**Clearing a block does not replay what it refused.** A giver-gated Step keeps its offer, so the give is the retry. A plain Step that was refused at activation has nothing waiting: clear the block and it stays inactive until something activates it again. The graph does not remember the attempt.

**The node addresses a tag, not a wire.** Set Blocked reaches any Step by tag - in this graph, in a linked one, in another questline entirely. Nothing on the target announces it; the only evidence is the fact and the ring. When a Step of yours refuses and its own graph shows no reason, search every graph and Blueprint for its tag.

**Two buttons, one flag.** Either green button clears the block; pressing the second after the first does nothing, and the manager says so only at Verbose. The button's label saves you the press, because it reads the state - a UI that shows a "Block" control without asking *Is Quest Blocked* will offer no-ops.

**The example giver reacts to every refusal on its tag.** *On Quest Give Blocked* carries the giver actor that tried, and the example cube does not check it against itself. With one giver, as here, that is invisible; with two givers on one Step, one refused give would turn both cubes red. Read *Giver Actor* before you react.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **Blocked** is a state a Step can be in at any point of its lifecycle - not started, waiting on a giver, Live - set and cleared from outside it. Nothing on the Step node configures it, and nothing in the lifecycle writes it.
- **Set Blocked and Clear Blocked** are utility nodes: *Enter*, *Forward*, and a tag list. They run when activation passes through them and forward it, and they write against a tag, not a wire.
- **Set Quest Blocked, Clear Quest Blocked, and Is Quest Blocked** are the Blueprint side of the same three operations.
- **Three refusals speak one vocabulary:** ACTIVATION FAILED, GIVE BLOCKED, and PROGRESS REFUSED each carry a blocker with a *Reason* - *Blocked* here, *Prereq Unmet* in Chapter 4.
- **Also Deactivate** is the line between a block and an interruption. Off, nothing in flight is touched.
- **Display Data** has beats for all of it: *Give Blocked Beats*, *Blocked Beats*, and *Unblocked Beats* join the arrays Chapters 1 through 5 filled.

Still to come: the Deactivate pins, AND / OR / NOT, the Prerequisite Gate, and a Fact Tag node reading a fact like the door's (Chapter 7), and the Config Asset (Chapter 11).

---

## Try it

### Skip the opening block:

In `QL_Ch6_Blocking`, disconnect the wire from Start into Set Blocked's *Enter*, and the one from its *Forward* into the Step, and wire Start straight into BlockTargetStep's *Activate*. *Compile All*. Play the room from its button: the giver accepts you on the first touch, Chapter 3's way, and the green buttons read *Locked - Quest is already Unblocked* before you have pressed anything. The red button still works - press it and the Step blocks, the door opens, and the beacon refuses exactly as before. The graph's node was one writer of the flag; the room's buttons are two more, and none of them needs the others. Put the wires back and *Compile All*.

**The graph with the block bypassed:**

<img width="1200" alt="QL_Ch6_Blocking with Start wired straight into BlockTargetStep's Activate, Set Blocked bypassed" src="https://github.com/user-attachments/assets/5ade6497-1eba-43dc-beca-4c6ca0609691" />

### Block and deactivate:

Open `BP_BlockToggleButton`, find *Set Quest Blocked* in *Handle Button Press*, and tick its *Also Deactivate* pin. Compile the Blueprint - no graph compile; nothing in a questline changed. Play the room from its button, clear the block, accept the quest, and press red. This time the beacon beyond the door goes dark: alongside the block, the button published a deactivate request, the Step left Live, `.Deactivated` is written, and the trigger disarmed on the Step's DEACTIVATED. Now press a green button. UNBLOCKED prints, `.Blocked` clears - and nothing else happens. The beacon stays dark, a touch is dropped silently as Chapter 1's third fate, and the chapter cannot end, because clearing a block re-activates nothing and the Step's offer was consumed when you accepted it. That is the difference the comment box draws: the block was a gate; the deactivation was an ending. Untick the pin, compile, and replay the room from its button to get it back.

**The beacon after a block that also deactivated:**

<img width="1200" alt="After the red button with Also Deactivate ticked: the World State view showing .Deactivated on the Step" src="https://github.com/user-attachments/assets/55082141-5368-4161-b434-34218c1d06f8" />

---

Previous: [Chapter 5 - Named Outcomes](05_NamedOutcomes.md) | Next: [Chapter 7 - Prerequisites](07_Prerequisites.md)
