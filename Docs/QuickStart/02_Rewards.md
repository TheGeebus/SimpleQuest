# Chapter 2 - Rewards

*Grant Rewards nodes, the experience bar and gold readout.*

Chapter 1 ended a questline. This room pays for it. Two beacons, two Steps, and four Grant Rewards nodes hung off their Completion Path pins - one of which never fires, on purpose - plus a reward set that every chapter in the tutorial shares. It is also the one room built to be edited: nothing you can change in its graph breaks a later chapter, so it is where the *Try it* is a real experiment.

**By the end of this chapter you can name:** a Grant Rewards node, a reward, a modifier, a reward set, a Reward Recipient Component, and the difference between a reward on a path and a reward on a questline.

---

## In the room

In order:

1. You walk in and two beats print together - the chapter's, and the first Step's:

   > Each chapter in this tutorial is a questline, and each questline can grant REWARDS.
   >
   > Keep an eye on your experience and gold at the bottom of the screen as you progress.

   > Touch the Trigger to get some more gold. A Grant Rewards node sits on this step's completion path.

<img width="1200" alt="The room on entry: both beats on screen (the chapter's and Simple Grant's), the first beacon lit, the readout at its starting values" src="https://github.com/user-attachments/assets/07fb8500-3fbb-4a3e-bf1a-0577fb78fc39" />

2. Touch the first beacon. The gold readout at the bottom of the screen goes up by 20, and the Step's completed beat prints:

   > REWARD GRANTED. Completing the step carried the flow into a Grant Rewards node, and the node paid some gold.
   >
   > A reward is authored as a node on a path, not as a field on the step. So one step can pay differently depending on how it ends.

   The second Step activates on the same touch, and its beat sets up the next one:

   > Rewards can have various modifiers that compose to determine how they behave, like scaling their amounts, changing their recipients, or dropping them entirely based on some condition.
   >
   > This step grants Experience Points via the 'Any Outcome' path, and it uses the 'Grant Once' modifier to ensure that only happens the first time. Any subsequent replay won't grant more experience, but it will still grant gold.

<img width="1200" alt="After the first beacon: the REWARD GRANTED beat, gold up by 20, the second beacon lit" src="https://github.com/user-attachments/assets/ce51bc14-9c1a-4bad-b5ed-7d3a4664e9d8" />

3. Touch the second beacon. Gold goes up again, the experience bar moves, and two completed beats land - the Step's, then the chapter's:

   > Step completed. Every completion grants gold, but only the first one also grants experience points.

   > REWARDS ARE EVENTS YOU DEFINE. Your game provides the payload and determines what each grant actually means and does. Gold and experience points are just the examples employed by this demo.
   >
   > A questline can also carry rewards of its own, granted whenever it completes, without placing a node in a graph. That is what pays you at the end of each chapter.
   >
   > Head back through the door to continue.

<img width="1200" alt="After the second beacon: both completed beats, the experience bar moved, the gold total" src="https://github.com/user-attachments/assets/54152288-3d69-458b-b7b8-3d2145cd2670" />

The in-world tell is the readout itself: the numbers at the bottom of the screen are not the framework's. They belong to the player character, which received each grant as an event and decided what to do with it. The framework never learned what gold is.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/02_Rewards/QL_Ch2_Rewards`. Two Steps, four Grant Rewards nodes, and an Outcome:

**Start → Simple Grant → [Grant: 20 gold] → Reward Trigger →** three branches:
- *Reached* → **[Grant: 40 gold, scaled ×2]** - a dead end
- *Solved* → **[Grant: 5,000 gold]** - a dead end, and unused by default
- *Any Outcome* → **[Grant: 500 XP, Grant Once]** → **Outcome (Reached)**

Both Steps host `OBJ_InteractWithTarget`, the same Objective as Chapter 1, so each completes on *Reached* when a beacon is touched. Read the three branches against that: the *Reached* branch pays, the *Any Outcome* branch pays and carries the flow to the Outcome node, and the *Solved* branch does nothing at all - not because it is broken, but because nothing in this room ever completes on *Solved* (see the "Try it" section). Chapter 5 is where a Step ends more than one way, but this shows rewards waiting on mutually exclusive paths.

<img width="1200" alt="QL_Ch2_Rewards, the whole graph: two Steps, four Grant Rewards nodes, the three branches off Reward Trigger readable" src="https://github.com/user-attachments/assets/f3c89d6e-da0d-4f45-be84-a9c417de2834" />

Three comment boxes sit in the graph. In short:

- *Over the first Grant Rewards node* - rewards are authored along a path as a standalone node, configured in the Details panel. The graph associates the node with the content node upstream of it, so this reward belongs to *Simple Grant* and can be queried by *Simple Grant*'s tag.
- *Over Reward Trigger's branches* - each Completion Path can grant distinct rewards, and a node on the *Any Outcome* path fires after every completion, so it applies to all of them.
- *Over the Outcome node* - a questline asset can carry rewards of its own, set in Graph Defaults and keyed by an outcome or by Any Outcome, delivered when the whole questline completes and shown on the Linked Questline node wherever the questline is placed.

### Things worth clicking:

  <img width="1200" alt="The Reached-branch Grant Rewards node selected: Details showing Rewards [Currency: Gold, 40] and its Modifiers [Scale Amount, 2.0]" src="https://github.com/user-attachments/assets/33604db3-9422-40af-92b9-a60e7193fac2" />
<br>

- **Select the Grant Rewards node on the *Reached* branch.** In the Details panel, *Rewards* is an array of configured reward objects - here one Currency reward, `SimpleQuest.Reward.Currency.Gold`, amount 40 - and each reward has its own *Modifiers* array: one Scale Amount, multiplier 2. That is how 40 became 80 in the readout. *Reward Sets* is empty on this node.

<br>
  <img width="1200" alt="The Any Outcome node selected: Rewards [XP, 500] with Modifiers [Grant Once]" src="https://github.com/user-attachments/assets/7d91038d-fb3a-4159-9201-02d8ddf77452" />
<br>

- **Select the node on the *Any Outcome* branch.** One XP reward, 500, carrying a Grant Once modifier. This node is also the one that continues to the Outcome node, which is why the chapter can end no matter which path the Step took.

<br>
  <img width="1200" alt="The Solved-branch node: Currency Gold 5,000, no modifiers" src="https://github.com/user-attachments/assets/1b3d8f5c-a5c6-4df3-8241-1efa8c9f56d0" />
<br>

- **Select the node on the *Solved* branch.** Five thousand gold, no modifiers. An amount chosen to be impossible to miss, for the day you make it fire (see *Try it*).

<br>
  <img width="456" height="417" alt="Graph Defaults: Questline Rewards with an Any Outcome entry whose Reward Sets lists QR_QuickStartRewardSet" src="https://github.com/user-attachments/assets/80944ce3-8963-45fe-9002-ef98b705eb9f" />
<br>

- **Click empty canvas.** Under *Questline Rewards*, one entry keyed *Any Outcome*, and instead of inline rewards it references a set: `QR_QuickStartRewardSet`. Every chapter's questline carries this same entry. It is the payout the chapter beat calls "rewards of its own."

<br>
  <img width="572" height="560" alt="QR_QuickStartRewardSet open: two rewards, XP with Grant Once and Gold with Scale By Recipient" src="https://github.com/user-attachments/assets/de5e9ef4-f382-4d0b-a64a-f329a288895a" />
<br>

- **Open `QR_QuickStartRewardSet`** in the `QuickStart` folder. A Reward Set is a data asset holding rewards, and optionally other sets, authored once and referenced from anywhere. This one holds two: an XP reward with Grant Once - experience pays once per chapter, so finishing all eleven fills the bar exactly - and a Gold reward with Scale By Recipient, which asks the recipient what to multiply by.

<br>
  <img width="1200" alt="BP_QuestPlayerExample's Quest Reward Recipient Component: Reacts To Reward Types listing Experience and Currency.Gold" src="https://github.com/user-attachments/assets/e8aa41a2-f934-400c-9558-df8a20a69568" />~~~~
<br>

- **Open `BP_QuestPlayerExample`** in `SimpleQuest Content/ExampleBlueprints/Actors` and select its Quest Reward Recipient Component. *Reacts To Reward Types* lists `SimpleQuest.Reward.Experience` and `SimpleQuest.Reward.Currency.Gold`. Its *On Reward Granted* event is bound in the event graph to the handler - Apply Reward, which reads the reward tag and calls *Add Experience Points* and/or *Add Gold*, which update the totals the HUD reads. This is the whole receiving end.

---

## Under the hood

### A reward is a node on a path

A Grant Rewards node is a utility node: it has no tag of its own, sits on a wire like any other node, and when activation reaches it, it grants what it holds and then forwards activation out of its *Forward* pin unconditionally. A node with nothing wired downstream pays normally; a node wired onward carries the flow. That is the whole reason rewards are nodes rather than fields on a Step - a Step has several Completion Path pins, and putting a node on each one is how the same Step pays differently depending on how it ended.

The compiler still remembers whose reward it is. Each Grant Rewards node is attributed to the content node upstream of it and the path it hangs off, so a query by *Reward Trigger*'s tag can answer "what does this pay on *Reached*, and what does it pay on any outcome" without the node needing a tag. Those queries - `Get Advertised Rewards`, `Get Advertised Rewards For Outcome`, `Get Questline Rewards`, and the `...From Asset` forms that read an unstarted asset cold - are what a journal or a bounty board is built on. They describe; they never grant.

### What a grant is

When the node activates, each reward in its array computes a **grant**: a reward type tag, a recipient, and a payload your game defines. The framework does not know what gold is. `CurrencyReward` and `XPReward` are reference classes that pack an amount; `GenericReward` lets you attach any payload struct without subclassing; `LootTableReward` rolls a table. Your own reward is a Blueprint or C++ subclass with one override.

The grant is then **published on its reward type tag** - `SimpleQuest.Reward.Currency.Gold`, `SimpleQuest.Reward.Experience` - as an event on the same bus everything else in the framework uses. The Grant Rewards node never knows who is listening. A **Quest Reward Recipient Component** on any actor subscribes to the types it cares about, hierarchically (`SimpleQuest.Reward.Currency` catches every currency), checks that the grant is addressed to its owner, and fires *On Reward Granted*. The example player binds that to *Add Gold* and *Add Experience Points*. An NPC whose faction shifts, a HUD that toasts, a world object that opens - the same component, bound to whatever the grant means there.

The recipient is the actor that completed the Step - the instigator of the trigger fire - unless a reward or modifier redirects it. In this room that is the player who touched the beacon.

### Modifiers change a grant on the way out

A reward decides *what* to grant. A **modifier** changes it after the fact, in the reward's *Modifiers* array, in array order: scale it, cap it, redirect it, or drop it unless a condition holds. Six ship:

| Modifier                     | Does                                                                                                      |
|------------------------------|-----------------------------------------------------------------------------------------------------------|
| **Scale Amount**             | Multiplies an amount by a number you author                                                               |
| **Scale By Recipient**       | Multiplies by a number the recipient reports through a small interface; an actor without it scales by one |
| **Clamp Amount**             | Caps an amount at a maximum                                                                               |
| **Grant Once**               | Drops the grant on every completion after the first                                                       |
| **Require Completion Count** | Pays only between a first and last completion of the node                                                 |
| **Require Fact**             | Drops the grant unless a World State fact holds - at a minimum count, since facts are counted             |

A modifier is one grant in, one or none out. It can never emit more; that is a reward's job, and keeping the line sharp is why modifiers are their own layer instead of fields on a reward class. It is also why scaled loot can exist: a loot table computes its amounts internally, and a modifier runs after those amounts are decided.

Grant Once deserves one more sentence, because it explains the experience bar. It writes nothing down. It reads the node's resolution history in the Quest State Subsystem - the same record Chapter 1's *Resolutions* tab showed - and the resolution is recorded *before* the rewards on it are granted, so the first grant sees a count of one and every replay sees more. Its advertisement changes with it: a collected reward still comes back from a query, marked *Already collected*, rather than vanishing.

### Rewards on the questline, and sets

Graph Defaults carries a second place to author rewards: **Questline Rewards**, a map keyed by the outcomes the questline's own Outcome nodes can resolve with, or by *Any Outcome*. These fire when the questline resolves - after the Step's own nodes, at the moment the Outcome node is reached - and they fire whether the questline ran on its own or was placed inside another. That is what pays at the end of every chapter.

Here the entry references a **Reward Set** rather than listing rewards inline. A set is a data asset: rewards, plus optionally other sets, authored once and referenced from any Grant Rewards node or any questline. At compile the set's contents are flattened into the graph that references it - referenced sets first, in order, then the inline rewards - so the runtime sees one flat list and the set asset itself is never loaded in play.

### Who was listening, and in what order

On the second beacon, four grants fire in a fixed order:

1. **The *Reached* branch** - 40 gold, scaled to 80. Named-path nodes go first.
2. **The *Any Outcome* branch** - 500 XP, if this is the first completion. Any Outcome nodes go after the named path.
3. Activation continues through that node into the Outcome node, and the questline resolves *Reached*.
4. **The questline's set** - the shared XP, if first, and the shared gold, scaled by the recipient. Questline-level rewards fire at resolution, last.

Each grant reaches the player's Reward Recipient Component as an event, and the HUD reads the totals the player keeps. Open the Output Log filtered to `LogSimpleQuestActivation` and the sequence prints one line per grant: `GrantRewardSet: granting 'SimpleQuest.Reward.Currency.Gold' (recipient: targeted)`, then the component announcing it received it.

<img width="861" height="431" alt="The Output Log filtered to LogSimpleQuestActivation across the second beacon: the four grants in order, each followed by the recipient's line" src="https://github.com/user-attachments/assets/9b491f77-6ca7-4bd5-8f47-c3fa874e2d07" />

Above: On Reward Granted - the single surface through which Rewards are granted. See `SimpleQuest Content/ExampleBlueprints/Actors/BP_QuestPlayerExample` to view the `Apply Reward` event handler, which is an example of how to increment attributes that in turn notify the HUD that they were changed.

---

## Gotchas

**Compile the master, not just the chapter.** Chapter 2 runs as a placement inside `QL_QuickStart`, and the placement is compiled from the master graph. Editing `QL_Ch2_Rewards` and compiling it changes nothing in play until `QL_QuickStart` is compiled too - use *Compile All*. The same is true of any linked questline you author. And compile between sessions: a graph compiled while Play In Editor is running is not picked up by that session - the running session keeps the compile it started with, and the compiler says so in the message log.

**A reward set is copied at compile, not read at runtime.** Editing `QR_QuickStartRewardSet` changes nothing until every graph that references it is recompiled. *Compile All* again.

**Hang the Exit off Any Outcome.** The Outcome node here is fed by the *Any Outcome* branch for two reasons. The practical one: if it hung off *Reached*, a Step that completed *Solved* would leave the questline unable to end. The enforced one: the compiler refuses an Outcome node that is reachable from a named pin *and* from *Any Outcome* on the same Step, because the questline would resolve twice for one completion - two questline-level payouts, two resolutions, and every Grant Once and completion count reading wrong. It names the node and both pins in the error.

**Modifiers run in order, and order changes the number.** On a 40-gold reward, Scale Amount ×2 then Clamp Amount with a maximum of 30 pays 30; Clamp at 30 then Scale ×2 pays 60. The array order on the reward is the execution order.

**A modifier on the wrong payload warns and does nothing.** Scale Amount on a reward whose payload has no amount - a Generic reward carrying "start the cutscene" - is refused with a warning naming the modifier and the payload. It does not silently pass. A modifier that quietly did nothing would look exactly like one that worked.

**Nothing receives a grant unless something subscribes to it.** The log will say `granting`; the wallet will say nothing. A grant published on a reward type no Reward Recipient Component reacts to goes nowhere, and the type has to match hierarchically - `SimpleQuest.Reward.Currency` catches Gold, `SimpleQuest.Reward.Experience` does not.

**Grants are live only.** A reward is an event, not a state. It is never replayed on a save restore or to a late-joining actor, so the totals a recipient keeps are your save's responsibility - the example player persists its experience and gold through the sample's save subsystem for exactly this reason.

**Grant Once counts resolutions of that node, wherever the modifier sits.** On a node hung off a Step, it counts the Step's completions. On the questline set, it counts the questline's. Replay the room from its button and the Step's XP does not pay again, and neither does the set's - but every gold reward does.

**A silent path is only silent until the Objective changes.** The 5,000-gold node fires the moment any Step here completes *Solved*. If a later edit to `OBJ_InteractWithTarget` - or a different Objective on the Step - starts emitting *Solved*, the plant blooms.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **Rewards are not on the Step.** They are nodes on its Completion Path pins, so which reward pays is decided by which path the Objective completes on. The Step's own Details panel has no reward field at all.
- **Display Data can be per Step.** Both Steps carry their own asset, and their sidebar names come from their *Display Name* - "Granting Rewards" and "Modifiers" - not their labels. Chapter 1's chapter-level asset is still there underneath, providing the chapter's beats.
- **The Objective picks the reward.** `OBJ_InteractWithTarget` completes *Reached*, so the *Reached* branch pays and the *Solved* branch waits. The Objective's tickbox is the switch.

Still to come: a Step offered by a giver (Chapter 3), the Prerequisites pin and the gate mode (Chapter 4), an Objective you write yourself (Chapter 5), the Deactivate pins (Chapter 7), and the Config Asset (Chapter 11).

---

## Try it

This is the room to change things in. Two experiments, both reversible.

### Make the silent `Solved` node fire:

Open `OBJ_InteractWithTarget` in `SimpleQuest Content/ExampleBlueprints/Objectives`, find the tickbox the Chapter 1 comment described, and flip it so the Objective completes on *Solved*. Compile and save the Blueprint; no questline needs recompiling, because the Objective is a class the Step instantiates at play time. Press Play, press the red Unlock button, and start Chapter 2 from its own button. The first beacon still pays 20 gold - *Simple Grant*'s node is on *Any Outcome*, so it fires on *Solved* too. The second beacon pays 5,000, the 80 never appears, the XP still pays if it hasn't already, and the chapter still ends, because the Outcome node is on the one branch that fires no matter what. Everything the graph was designed to survive, it survives. Then flip the tickbox back: every other chapter wires *Reached*, and they will not end until you do.

**Find and Untick the Branch in `OBJ_InteractWithTarget`:**

<img width="340" height="370" alt="Flip this tickbox on OBJ_InteractWithTarget to fire the Solved Completion Path which grants 5000 gold" src="https://github.com/user-attachments/assets/b7d5c1f4-ff36-470e-b344-5b9f3ce1676c" />

**The Next Completion Grants 5000 Gold:**

<img width="407" height="238" alt="After the tickbox flip: the gold readout jumping by 5,000 on the second beacon" src="https://github.com/user-attachments/assets/843beda5-ac9a-41ff-9ab0-6491e9e545f8" />

### Change the amount granted without touching the Objective:

In `QL_Ch2_Rewards`, select the *Reached* node, add a Clamp Amount modifier to its Gold reward with *Max Amount* set to 30, and *Compile All*. The second beacon pays 30 instead of 80: 40 scaled to 80, then clamped. Now move the Clamp above the Scale Amount in the Modifiers array and *Compile All* again: 60, because 40 clamped is still 40, and then it doubles. Same two modifiers, different answers, and the array order is the only thing that changed. Remove the Clamp when you are done.

---

Previous: [Chapter 1 - Basic Trigger](01_BasicTrigger.md) | Next: Chapter 3 - Basic Giver.
