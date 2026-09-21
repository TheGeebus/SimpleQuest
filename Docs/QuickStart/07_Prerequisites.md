# Chapter 7 - Prerequisites

*A game system can run the room without owning the quest.*

Act I ends here, with the graph letting go of the level. Every room so far placed its quest actors and let the questline drive them. This one places none: a Blueprint the framework has never heard of runs the room from three data assets, deciding which scenario exists, spawning its beacons, naming them, tearing them down - while the framework keeps every decision about progress. The room's manager can start a scenario and end one. It spawns actors appropriate for each, driven by data assets that configure their Trigger components.

The mechanism underneath this chapter is the largest of the tutorial: three scenarios, each a Quest node holding a graph of its own, each with a goal Step gated by a combinator - AND, OR, NOT - over three Steps beside it, and a Prerequisite Gate in the outer graph that ends the chapter when all three are solved. The nested graphs and the orange Deactivate wires Chapter 4 left for later are here too.

**By the end of this chapter you can name:** the contract a game system uses to take part - *Activate Quest*, *Deactivate Quest*, *Add Tags to Trigger*, *Is Quest Live*, and an observer - and what that contract cannot do; the AND, OR, and NOT combinators; a Quest node and its inner graph; the breadcrumbs and the Questline Outliner; the Prerequisite Gate; the Deactivate and Deactivated pins; the Fact Tag node; and the rule that lets all of them plug into each other.

---

## In the room

In order:

1. Chapter 7 activated as you finished Chapter 6, so on the way in its beat has printed:

   > Prerequisite Expressions gate progress conditionally.
   >
   > They may be based on other quest Completion statuses directly, on Outcome tags published by any Questline graph, or on a World State fact tag - which may be published at any time by any system.

   No beacon is lit, because there is none: the room has three buttons - *AND*, *OR*, *NOT* - and four empty positions. Nothing inside the chapter has started.

<img width="1200" alt="The room on entry: the chapter's beat on the HUD, the three operator buttons, the four empty beacon positions" src="https://github.com/user-attachments/assets/764f9af4-267e-408f-91f3-96a91ef8e9b9" />

2. Press the AND button. Four beacons appear, all lit, labeled *Step 1*, *Step 2*, *Step 3*, and *Goal*, and a beat prints for the scenario:

   > The AND combinator requires that all inputs are true before the resulting Prerequisite Expression evaluates as true, allowing progress through any connected nodes.
   >
   > Complete 1, 2, and 3 to unlock the goal.

<img width="1200" alt="After pressing AND: the scenario's beat on the HUD, four beacons spawned and lit, labeled Step 1, Step 2, Step 3, and Goal" src="https://github.com/user-attachments/assets/3f92547b-642d-460e-924f-ecee5831f82d" />

3. Touch *Goal* first. The denied sound and the blocked color, and no beat at all - the Steps in this room have no lines of their own. Step off and it is lit again. It is Chapter 4's refusal with three conditions behind it instead of one.

<img width="1200" alt="Touching Goal before the inputs: the beacon in its blocked color, no beat on the HUD" src="https://github.com/user-attachments/assets/7c6b9f23-e0d8-4078-bdb0-505dd22218b2" />

4. Touch *Step 1*, *Step 2*, and *Step 3* in any order - each plays the success sound and goes dark - then *Goal*. It completes, the scenario's beat prints, and the four beacons vanish:

   > The AND combinator means "ALL of these must happen first."

<img width="1200" alt="After the three inputs and the Goal: the AND scenario's completed beat on the HUD, the beacons gone" src="https://github.com/user-attachments/assets/4f1cb32f-add6-4e7d-8b37-32c2561adf0c" />

5. Press the OR button. Four new beacons, the same labels, and the scenario's beat:

   > The OR combinator will evaluate true as long as at least one of its inputs are also true.
   >
   > Touch any of the first three steps to unlock the goal.

   Touch one input and *Goal* opens. The other two stay lit and still complete if you touch them - nothing required them, and nothing turned them off. Touch *Goal*:

   > The OR combinator means "ANY of these must happen first."

<img width="1200" alt="The OR scenario after one input: the Goal open, the other two inputs still lit" src="https://github.com/user-attachments/assets/d01e6d23-b939-4293-accd-789a0be75315" />

6. Press the NOT button. This time the labels say more - *Step 1 - Required*, *Step 2 - Required*, *Step 3 - Locks Goal* - and so does the beat:

   > The NOT combinator inverts the value of the input expression, useful for disabling pathways based on quest progress.
   >
   > Activate 1 and 2 to unlock the Goal. Activate 3 to lock it.
   >
   > Reset the scenario using the button if needed.

   Touch *Step 1* and *Step 2*, and *Goal* opens. Touch *Step 3* instead, or as well, and *Goal* refuses you for the rest of the run: a completed Step cannot be un-completed, so a NOT over it has flipped for good. Press the NOT button again and the scenario resets - the beacons respawn, all four lit - and this time leave *Step 3* alone.

<img width="1200" alt="The NOT scenario after touching Step 3 - Locks Goal: the Goal refusing" src="https://github.com/user-attachments/assets/bc981ee2-e1d7-4d7b-8b5c-4ea42e652a77" />

7. Touch *Goal*. The scenario's beat, and then the chapter's, because that was the third of three:

   > The NOT combinator means "this CANNOT have happened."

   > By combining AND, OR, and NOT combinators, Prerequisite expressions can be as simple or as complex as needed.
   >
   > Take a look at the Chapter 7 Questline graph for more on the use of prerequisite expressions.

   Chapter 8's door opens.

<img width="1200" alt="After the NOT scenario solves: the scenario's completed beat and the chapter's on the HUD, Chapter 8's door opening" src="https://github.com/user-attachments/assets/010e234c-5400-4eb6-b433-55c6069fdb7b" />

The in-world tell is the *Goal* beacon: lit from the moment its scenario starts, refusing until an expression over the other three holds, and never changing on its own when it does. The three scenarios differ only in which combinator sits between the inputs and the goal, and the room is built so you can feel the three shapes with the same four beacons - beacons that did not exist until you pressed a button.

---

## A system that plays along

Select the room's actors in the World Outliner and count what carries a quest tag: nothing but the buttons, two doors, and one actor, `BP_PrereqSpawnManager`. No giver, no beacon. Every beacon you touched was spawned by the manager when you pressed a button and destroyed by it when the scenario solved. The manager is not part of SimpleQuest - it is a Blueprint in the chapter's folder, built the way any game's own system would be built - and it is the reason this chapter is different in kind from the six before it.

**What the manager owns.** Three data assets in the chapter's *Scenarios* folder - `DO_And`, `DO_Or`, `DO_Not` - each holding a Quest's tag, three inputs with a label and a Step tag apiece, and a goal. From those the manager decides which scenario is live, spawns a beacon per Step at one of its four arrow components, labels it, colors it, and later destroys it. Labels, positions, colors, which scenario, when: the manager's.

**What it never touches.** Progress. It has no way to complete a Step, satisfy a condition, write a path fact, or end the chapter. Its whole vocabulary toward the framework is five things: *Activate Quest* and *Deactivate Quest* by tag, *Add Tags to Trigger* on a beacon it spawned, *Is Quest Live* at startup, and a Quest Observer on the scenario's tag. Everything that happens to the goal - the refusals, the moment it opens, the Solved that reaches the gate - is the graph's, exactly as it would be with placed actors.

**How the two meet.** Press a button and the manager tears down whatever scenario is running, reads the chosen data asset, starts observing the Quest's tag, and calls *Activate Quest* on it. The framework activates the Quest and its four Steps, and records on the *Entries* tab that a system did it - provenance *ExternalAPI*. STARTED comes back through the observer, and the manager spawns the beacons and hands each its Step's tag; a Trigger Component joining a Step that is already Live catches up, arming as if it had been there from the start. COMPLETED comes back, and the manager destroys the beacons and deactivates the Quest. On a fresh BeginPlay it asks *Is Quest Live* for each scenario and rebuilds whichever is running - so the room after a save and load comes back with its beacons, from state alone.

That is the whole integration. The manager is one possible shape of it - a demo's shape, with arrows for spawn points and labels in data - and the tutorial does not offer it as the way to build yours. What it proves is narrower and worth more: a data-driven system can create and destroy the things a quest is played with, at runtime, and participate in the progression through the same tags and events the placed actors used, without owning any of it.

**The manager, and everything the room places:**

<img width="1200" alt="BP_PrereqSpawnManager selected in the level: its four arrow components, with nothing else placed for the chapter but the buttons and the doors" src="https://github.com/user-attachments/assets/f50a3e47-d537-4c30-9dbb-5659673461df" />

**One scenario, as data:**

<img width="665" height="934" alt="DO_Not open: the scenario's Quest tag, the three inputs with their labels and Step tags, and the goal" src="https://github.com/user-attachments/assets/b5f5c4f5-ddb0-4eef-9476-d4ce51a21a99" />

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/07_Prerequisites/QL_Ch7_Prerequisites`. The outer graph is short, and its Start node is wired to nothing:

**Start** (disconnected) - **AND, OR, NOT**, three Quest nodes - each one's *Solved* pin, dashed, into an **AND combinator**, whose output runs into a **Prerequisite Gate**'s *Prerequisites* pin - each one's *Any Outcome*, solid, into the gate's *Enter* - the gate's *Forward* into the **Outcome node (Solved)**.

- A **Quest node** is a container: a single-use group of nodes with a graph of its own inside. Double-click one to enter it; the breadcrumbs above the graph and the Questline Outliner panel bring you back. Its face shows the outcome pins its inner graph's Outcome nodes declare - here one, *Solved* - plus *Any Outcome*, exactly like a Step.
- The three Quest nodes have nothing wired into their *Activate* pins, and Start is disconnected on purpose: the manager starts each one by tag, and a wired Start would activate all three the moment the chapter did. The pacing is the system's; the graph keeps the rest.
- The **Prerequisite Gate** is a utility node with three pins: *Enter*, *Forward*, and *Prerequisites*. It holds a wire, not a node: activation arriving at *Enter* is forwarded only when the expression holds, and deferred until it does. Here the expression is *AND(AND Solved, OR Solved, NOT Solved)*, so the chapter ends when the last scenario does.
- The Outcome node names *Solved*. Chapter 8's prerequisite is wired from this chapter's *Any Outcome*, as every chapter's is.

<img width="1200" alt="QL_Ch7_Prerequisites, the outer graph: Start disconnected, the three Quest nodes, their Solved pins into the AND combinator and its output into the Prerequisite Gate's Prerequisites pin, their Any Outcome wires into the gate's Enter, and Forward into the Outcome node" src="https://github.com/user-attachments/assets/82750221-b887-4d91-936d-ba1dff83abd9" />

**Inside a scenario.** Double-click the AND node. Its graph is the room you just played:

**Start → *Entered* → AndInput1, AndInput2, AndInput3, and AndGoal**, all four at once; each input's *Any Outcome*, dashed, into an **AND combinator**; the AND's output into **AndGoal's *Prerequisites***; AndGoal's *Any Outcome* into the **Outcome node (Solved)**. And in orange, a chain: **Start's *Deactivated* → AndInput1's *Deactivate***, AndInput1's *Deactivated* → AndInput2's *Deactivate*, and on down to AndGoal.

- Every Step here has *Show Deactivation Pins* ticked, and so does Start. That is where the orange pins come from - Chapter 4 promised them.
- The OR scenario is the same graph with an **OR combinator**. The NOT scenario is the same graph with NotInput3's *Any Outcome* into a **NOT**, and the NOT's output into an **AND** alongside NotInput1 and NotInput2 - a combinator feeding a combinator, which is the whole point of the chapter.
- The Outcome node in each inner graph names *Solved*, which is why the Quest node outside shows a *Solved* pin.

**Inside the AND scenario:**

<img width="1200" alt="Inside the AND scenario: Start's Entered into the four Steps, the orange chain from Start's Deactivated down through them, the three Any Outcome wires into the AND, the AND into AndGoal's Prerequisites, and AndGoal into the Outcome node" src="https://github.com/user-attachments/assets/4ad5dbf0-2a60-47a2-a057-f5c35b092c42" />

**Inside the NOT scenario:**

<img width="1200" alt="Inside the NOT scenario: NotInput3's Any Outcome into the NOT, the NOT and the other two inputs into the AND, and the AND into NotGoal's Prerequisites" src="https://github.com/user-attachments/assets/3712bea1-f398-433d-93f7-e3af7c9a7da6" />

**The comment boxes. In short:**

- *Over Start, outer graph* - the room manager actor activates each scenario using the Blueprint-callable *Activate Quest*. A wired Start would activate its connections automatically, which is not wanted here, so it stays disconnected.
- *Over the Quest nodes* - a Quest node is a single-use container holding a group of nodes; double-click to enter, and use the Outliner or the breadcrumbs to navigate. Quests can be gated by a giver just like Steps, and fire their own lifecycle events. Their tag is `SimpleQuest.Questline.<QuestlineID>.<QuestNodeLabel>`, with the nodes inside as children.
- *Over the gate* - a Prerequisite Gate defers activation on any wire into its *Enter* until the expression holds. It gates a wire without an inline content node.
- *Over the AND combinator* - a combinator takes any number of prerequisite inputs and produces one result, and its output is itself a prerequisite, so one can feed another. The result goes to a content node's *Prerequisites* pin or to a gate.
- *Over the Step, outer graph* - the two Prerequisite Gate Modes, as Chapter 4 explained them.
- *Inside each scenario, "Navigating Between Quest Graphs"* - the breadcrumbs above the graph: click a name to open that graph, click the arrow before a name to jump to the node that hosts it; or double-click an entry in the Questline Outliner.
- *Inside the AND scenario, over the orange wires* - Start's *Deactivated* fires when the Quest is deactivated, and each wire into a Step's *Deactivate* takes that Step down with it. Deactivating a Quest does not automatically stop what is inside it: without these wires the Quest goes inactive while its Steps keep listening. Activation and deactivation wires may both connect to either kind of input, so a completion can deactivate something and a deactivation can activate something.
- *Inside the OR scenario* - OR opens on the first satisfied input; the others stay live and still respond. If a scenario should shut the others down the moment one path is taken, that is a deactivation wire, not a prerequisite.
- *Inside the NOT scenario* - NOT wraps a single input and inverts it. Its result feeds an AND alongside two plain inputs, so the goal opens when the first two are solved and the third is not.

### Things worth clicking:

<br>
  <img width="1200" alt="The breadcrumb bar above the graph with the AND scenario open, and the Questline Outliner beside it" src="https://github.com/user-attachments/assets/d511d576-c6b8-444f-9756-cf1acdb583a0" />
<br>

- **Open the Questline Outliner** and use it. With the AND scenario open, the breadcrumb bar above the graph reads the path down to it - the master, the chapter asset, the AND node - with an arrow before each name: click a name to open that graph, click the arrow before it to select the node that hosts it. The Outliner lists every graph and node in the tree; double-click to jump. The tutorial's graphs are three deep, and yours may be deeper.

<br>
  <img width="1200" alt="QL_QuickStart: the two Fact Tag nodes, TutorialStarted and ChaptersUnlocked, with their comment boxes" src="https://github.com/user-attachments/assets/23bd9b12-57cf-47d1-84c3-8d7273e9d34c" />
<br>

<br>
  <img width="820" height="366" alt="The wire from the ChaptersUnlocked Fact Tag node through the NOT, joined by Chapter 6's Any Outcome in the AND, into the Chapter 7 node's Prerequisites pin" src="https://github.com/user-attachments/assets/81da3241-c6ca-4f9c-8aff-9c6e2b8c79be" />
<br>

- **Click up to `QL_QuickStart` and find the Chapter 7 node.** Its *Prerequisites* pin is fed by an AND with two inputs: Chapter 6's *Any Outcome*, and a NOT wrapping a **Fact Tag node** that names `SimpleQuest.Fact.QuickStart.ChaptersUnlocked` - the fact the red pedestal button publishes. Every chapter from 2 on is gated the same way, from the same NOT. You have been playing under an AND / NOT expression since Chapter 1, and this is the first page that shows it: *the previous chapter ended, and the chapters were not unlocked*.

- **Select a goal Step and examine it.** Right-click AndGoal, *Examine Prerequisite Expression*: three condition boxes under an AND, one per input, each *Outcome: Any Outcome*. Do the same on the gate in the outer graph: three boxes, one per scenario, each *Outcome: Solved*. During play both panels tint live.

- **Open `BP_PrereqSpawnManager` if you are curious,** and read it as an example, not a pattern. Its functions are named for what they do - *Configure Scenario*, *Spawn Trigger For Step* - and every call it makes into the framework is one of the five above.

### What you just played

All three scenarios were built with the same shape: three Steps whose Completion Paths combine to gate the fourth. The only difference was the expression gating the goal Step.

A prerequisite is not a special relationship between Steps. It is an expression made of one or more assertions of a fact. A Step's completed path is a fact asserted by the framework. A World State fact tag is also a fact, asserted by an external system that adds or removes it. A combinator takes in one or more of these assertions and produces another fact-like assertion, which can itself become an input to another combinator.

That is why the three scenarios can be expressed by the same machinery: AND, OR, and NOT are operators over conditions, not special cases of progression.

The rest of this chapter looks at how that machinery connects to the graph and to the game system that is using it.

---

## Under the hood

### The contract, from the framework's side

Nothing the manager calls is special to it. *Activate Quest* publishes an activation request on the framework's bus; the manager subsystem resolves the tag and activates the node with provenance *ExternalAPI*, logging `HandleActivationRequest: '...Chapter_7.AND' - resolved to 1 canonical(s) to try (CustomData empty)`. From there it is the cascade every earlier chapter ran: the container's ACTIVATED and STARTED, its four entry Steps activated with *ChainCascade* and no source, AndGoal Live and gated from its first instant. *Deactivate Quest* publishes a deactivate request, and the manager subsystem runs the same deactivation the orange wires run. *Is Quest Live* reads the World State fact. *Add Tags to Trigger* is the runtime half of the Trigger Component's authored list: the component subscribes to the Step as if the tag had been there at load, and if the Step is already Live it replays the activation to itself - the same catch-up a late subscriber gets, with no giver attached, because nothing gave this quest.

Two things follow. First, the framework does not distinguish a system's activation from a wire's except in the record: the Quest's row on the *Entries* tab says *ExternalAPI* where a wired chapter's says *ChainCascade*, and everything downstream is identical. Second, the system's only leverage is at the edges - start and stop - and even those go through the same guards: activate a Quest that is already Live and the re-entry is a no-op, deactivate one that is not running and the cascade passes through. The second *Try it* leans on exactly that.

### The composition rule

Chapter 4's dashed wire ran from a Completion Path pin into a *Prerequisites* pin. This chapter adds three nodes between the two ends, and one rule covers all of them: **a prerequisite is anything with a prerequisite output, and it can be wired into anything with a prerequisite input.** Outputs: a Step's or Quest's Completion Path pins (yellow, or *Any Outcome*), a Fact Tag node, a combinator's result. Inputs: a content node's *Prerequisites* pin, a Prerequisite Gate's *Prerequisites* pin, a combinator's own inputs. AND and OR take as many inputs as you give them - two to start, and *Add Condition Pin* on the right-click menu for each one more; NOT takes one. The compiler folds whatever you drew into one expression tree per consumer, and the runtime evaluates the tree.

The three leaf kinds under the combinators are the three things the chapter's beat named:

- **A path.** A yellow pin means "that node completed on that path"; *Any Outcome* means "on any of its paths." Read from the per-run path fact when the source is resettable, as Chapter 4 explained, or from the resolution record when it is not.
- **An outcome, anywhere.** An Outcome node wired as a prerequisite reads "some questline resolved with this outcome," with no node named - the tutorial never wires one, but the Examiner will show you one if you do.
- **A fact.** A Fact Tag node reads a World State fact - any fact, written by anything: a Set Blocked node writes one, Chapter 6's red button wrote one with *Add Fact*, and the pedestal's red button wrote `ChaptersUnlocked`. A fact leaf is the only kind that can go false again, since facts are removed as freely as they are added; NOT over a fact is therefore a condition that can close after it opened. It is also the leaf a system like the manager could feed the graph through, had this room needed one: a fact written from Blueprint is a prerequisite the moment a Fact Tag names it.

### Where a condition bites

The same expression does a different job depending on what consumes it, and this room has all three consumers:

- **On a Step** - the goal Steps. The Step activates anyway and the condition gates its progress (or its completion, by *Prerequisite Gate Mode*). AndGoal is Live from the moment the scenario starts, its beacon lit, its fires refused with PROGRESS REFUSED and the unmet leaves listed.
- **On a Quest** - the chapters in the master graph. A container with an unmet prerequisite defers its activation; nothing inside it activates until the condition holds. Chapter 7 waited on Chapter 6 that way.
- **On a gate** - the outer graph's Prerequisite Gate. A gate gates a *wire*: activation arrives at *Enter*, and if the expression does not hold the gate subscribes to the expression's leaves and forwards the moment it flips true. The first scenario to finish parks an activation at the gate; the third scenario's completion both satisfies the last leaf and arrives at *Enter* again, and the gate fires once - it remembers which cascade it last fired for.

Chapter 4's rule is the one underneath: a prerequisite gates the earliest thing that can be gated. A Step is already running, so it gates the next fire; a container has not activated, so it gates activation; a gate has nothing but the wire.

### What a NOT is over

A path leaf never goes false inside a run - the per-run fact stays until a reset, the resolution record forever - so an AND or an OR over paths, once true, stays true. NOT is different in kind. NOT over a *path* is a condition that starts true and can close once, which is what the NOT scenario shows: touch *Step 3* and NotGoal is locked for the run, with nothing in the room able to reopen it. NOT over a *fact* can open and close as often as the fact is written and removed. The master graph's `NOT(ChaptersUnlocked)` is the second kind, and *Before You Start* described its effect from the room: press the red button and every chapter's prerequisite closes at once, permanently for that session, which is exactly why the chapter buttons then call *Activate Quest* - which bypasses prerequisites - instead of waiting on them.

### Nested graphs and their tags

A Quest node's inner graph is compiled under the Quest's tag: `SimpleQuest.Questline.QuickStart.Chapter_7.AND` for the container, `...Chapter_7.AND.AndGoal` and the three inputs beneath it. Inside the inner graph, Start's *Entered* pin is the container's activation arriving; the inner Outcome node is how the container resolves - *Solved* here, which is why the Quest node outside shows a *Solved* pin and why `...Chapter_7.AND.Path.Solved` is the fact the outer AND reads. A container's Live is derived from its Steps: the AND node's halo comes on when its first Step goes Live and off when the last one ends, and its own STARTED and COMPLETED publish on its tag, which is how the scenario's beats print with no display data on any Step - and how the manager hears them.

The tag tells you where a node lives, and the tools follow it: the Outliner is the tree of tags, the breadcrumbs are the path to the one you are in, and the Facts Panel filtered to `Chapter_7.AND` shows the container and its four Steps together. The manager's data assets name those same tags, which is the entire coupling between the system and the graph: a system that knows a tag can start, stop, watch, and arm against the thing it names, and nothing more.

### What deactivation does, and does not do

Deactivating a node ends its lifecycle without completing it: `.Live` is removed, `.Deactivated` is written, **DEACTIVATED** publishes, and for a Step the trigger-side wrap goes out so its beacons disarm. It does not touch the node's neighbors - and that includes a container's own Steps. Deactivate the AND Quest and the Quest is marked deactivated while AndInput1 keeps listening - and the Quest's own Live, derived from its Steps, stays on - unless a wire says otherwise.

The orange wires are those wires. A *Deactivated* output fires when its node deactivates; a *Deactivate* input deactivates its node. The scenario chains them, Start down to the goal, so one deactivation of the Quest takes the whole graph down in order. Two details matter:

- A node that is not running passes the cascade through without claiming anything - no fact, no event - so the chain works whatever state its links are in. Deactivating the solved AND scenario walks four completed Steps silently.
- The orange pins accept both kinds of wire. A Completion Path pin wired into a *Deactivate* input means "when this completes, stop that" - the OR comment's suggestion, and the first *Try it*.

The chain exists in this graph because of the manager. Its teardown is one *Deactivate Quest* on the container, and the graph's author had to decide what that should mean for the Steps inside - which is the right place for the decision. The system says "stop this scenario"; the graph says what stopping is. Without the chain the beacons would still vanish, because the manager owns them, but the Steps would still be Live in the World State and the AND node's halo would still be on. The second *Try it* shows that, and what it costs.

### Resets, and who does them

Pressing a scenario's button while it is running does the teardown and the activation both, which is the reset the NOT beat mentions. The manager does nothing else to make that a reset: every node under the master graph is resettable, so as each completed Step re-activates it clears its own per-run path fact, and a NOT that had flipped is open again. A replay of the whole chapter from the room's button does the same one level up - the chapter re-activates, and the three containers' `.Path.Solved` facts are cleared before it runs, so the gate re-gates. Chapter 4 introduced the mechanism; this is where a system relies on it without knowing it exists.

### What is watching

The outer graph during play: the three Quest nodes wear halos as their Steps run, and the gate wears the purple gating ring until all three have solved. Right-click the gate and examine it - three boxes, *Outcome: Solved*, grey for a scenario that has not run, amber while it runs, green once solved - and examine a goal Step inside a scenario for the same view over its inputs. The World State filtered to `Chapter_7.AND` shows the container's `.Live` come and go with its Steps, `.Path.Solved` when the goal lands, and `.Deactivated` on whichever Steps the manager's teardown found running. The *Entries* tab shows each scenario's row with provenance *ExternalAPI* - the manager's *Activate Quest* - and four rows for its Steps with *ChainCascade* and no source: a container's Start names nobody.

**The outer graph after the AND scenario:**

<img width="1200" alt="The outer graph during PIE after the AND scenario: the AND node with the Completed halo, OR and NOT unlit, the gate with its gating ring" src="https://github.com/user-attachments/assets/6d5c0653-4ae3-49ba-8f97-39f715206b8c" />

**The gate examined after the AND scenario:**

<img width="921" height="307" alt="The Prerequisite Examiner on the gate after the AND scenario: three boxes, Outcome Solved, AND green and OR and NOT grey" src="https://github.com/user-attachments/assets/3bfa7520-38e7-4233-ac69-ff2e08dc186c" />

**Inside a running scenario:**

<img width="1200" alt="Inside a running scenario during PIE: the goal Step with the Live halo and the gating ring, the inputs with their halos" src="https://github.com/user-attachments/assets/22c959cd-0e01-473f-a86a-21e80880466c" />

**The AND container's facts after solving:**

<img width="1200" alt="World State view filtered to Chapter_7.AND after the scenario: .Path.Solved beside .Completed on the container, and the four Steps' facts" src="https://github.com/user-attachments/assets/bb546d84-be4e-4685-b971-a4f343313ad9" />

**The framework's record of who started it:**

<img width="1200" alt="Quest State view, Entries tab: the scenario's row with Provenance ExternalAPI, and the four Step rows with ChainCascade and no source" src="https://github.com/user-attachments/assets/429aaf9d-4bc4-4214-9cac-749c0bcb14fb" />

---

## Gotchas

**A system can start and stop; it cannot progress.** There is no call that completes a Step or satisfies a condition from outside, and that is the design. If a system of yours needs to *influence* progress, it writes a fact and the graph reads it through a Fact Tag; the decision stays in the graph, where the Examiner can see it.

**A NOT over a path cannot reopen.** Completing is permanent within a run, so a NOT that has flipped false stays false until the node is reset. If a design needs "not yet" rather than "never," gate on a fact and remove the fact, or reset the node - the scenario button does the second.

**A container does not stop its contents.** Deactivating a Quest ends the Quest and nothing else. Wire the orange chain, or the Steps inside keep answering triggers from inside a container that reads as inactive - and the container's halo stays on, because its Live is derived from theirs.

**A gate gates a wire, not a node.** Nothing downstream of the gate exists until it forwards; nothing upstream is held. If you want the *scenarios* to wait, the condition belongs on their *Prerequisites* pins; the gate is for what comes after them.

**Any number in, one out.** AND and OR grow by *Add Condition Pin*, as many as you need. A second wire into a *Prerequisites* pin is still refused - the combinator is the join, and the schema will tell you so.

**A Fact Tag reads the fact now.** Not when it was written, not whether it was ever written: whether it holds. The Set Blocked node and *Add Fact* both write facts a Fact Tag can read, and both can be undone - `ChaptersUnlocked` is never removed in the tutorial, which is what makes the red button permanent for a session.

**The Start of an inner graph is the container's Activate.** Nothing else activates a container's Steps. If a Quest's Steps never start, look at what is wired into the Quest, not into Start.

**The Steps here have no beats.** The scenario's Quest node carries the narration, so a goal's refusal is silent on the HUD. A room of yours built this way should decide on purpose which node speaks.

---

## The Step so far

Act I ends here. Seven rooms have shown the Step node from every side the level can reach: its Objective and the pins the Objective declares, the triggers and givers that reach it by tag, the facts and events it publishes, the prerequisite that gates its fire, the switch that blocks it, and now the wires that end it and the system that spawns what it is played with. Act II turns from the level to the graphs themselves - questlines placed twice, questlines opening each other, conditions with names.

What this chapter added to your picture of the Step node and its Objective:

- **A Step is addressed by tag from outside.** A system that knows the tag can start a container, stop it, watch it, and arm a Trigger Component against a Step at runtime - and cannot progress it. The framework records the difference as provenance.
- **The Prerequisites pin** takes one wire from any prerequisite output: a Completion Path pin, a Fact Tag node, or a combinator. AND and OR join as many as you like; NOT inverts one; and a combinator's output is a prerequisite, so they nest.
- **Show Deactivation Pins** reveals *Deactivate* and *Deactivated*. Deactivation ends a Step without completing it - `.Live` off, `.Deactivated` on, the beacons disarmed - and travels only where you wire it.
- **A Step inside a Quest** is addressed under the Quest's tag and started by the Quest's own Start node. Its lifecycle is its own; the container's is derived from it.
- **Prerequisite Gate Mode** is the Step's half of the story. The other two consumers - a Quest's activation and a gate's wire - are the chapter's; a Step gates its fire.

Still to come: one questline placed twice (Chapter 8), a questline opening something it knows nothing about (Chapter 9), a named condition read by everything that needs it (Chapter 10), and the Config Asset (Chapter 11).

---

## Try it

### Let the first choice close the others:

Open the OR scenario's graph and wire OrInput1's *Any Outcome* into the *Deactivate* inputs of OrInput2 and OrInput3 - a Completion Path pin into an orange input, which the schema allows. *Compile All*. Play the room from its button, press OR, and touch *Step 1*: the other two beacons go dark, `.Deactivated` is written on both Steps, and *Goal* is open, because OR only needed the one. Touch *Step 2* or *Step 3* now and nothing happens - a fire at a Step that is not Live is dropped, Chapter 1's third fate. That is the OR comment's advice in practice: a prerequisite says what must have happened; a deactivation wire says what may no longer. It is also a decision the graph made, not the manager - the system that spawned those beacons had no say in it. Remove the two wires and *Compile All*.

**The OR scenario with the two deactivation wires:**

<img width="1200" alt="The OR scenario with OrInput1's Any Outcome wired into the Deactivate inputs of OrInput2 and OrInput3, and in play the two beacons dark after Step 1" src="https://github.com/user-attachments/assets/64868503-9b81-45b8-a2a7-7befc7a4112f" />

### Cut the chain:

In the AND scenario's graph, disconnect the orange wire from Start's *Deactivated* into AndInput1's *Deactivate*, and *Compile All*. Play, press AND, touch one input, and then press OR. The four AND beacons vanish - the manager destroyed them - but filter the World State to `Chapter_7.AND` and the Steps you did not touch are still Live, and the AND node in the outer graph still wears its halo: the manager's *Deactivate Quest* marked the container deactivated, and with the chain cut nothing reached the Steps. Now press AND again, and nothing appears. The container was still Live when the manager re-activated it, so the re-entry was a no-op that publishes no STARTED, and STARTED is what the manager spawns on. This is the limit of a system's leverage, seen from the wrong side of it: it asked for a stop and a start, the graph honored both literally, and the room the system thought it had reset was never reset at all. Reconnect the wire and *Compile All*: the same sequence ends every Step in one call, and the button works again.

**The AND container after a teardown with the chain cut:**

<img width="1200" alt="World State view after switching scenarios with the AND chain cut: the untouched AndInput Steps still Live" src="https://github.com/user-attachments/assets/a5c9edfa-a508-414b-a715-da2347507ed2" />

---

Previous: [Chapter 6 - Blocking](06_Blocking.md) | Next: Chapter 8 - Linked Questlines
