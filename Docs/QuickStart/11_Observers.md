# Chapter 11 - Observers

*The thing that has been showing you everything, finally named.*

Every room so far has had something in it that reacted. A door that opened. A sconce that lit. A beacon whose aura changed under your feet, a locker that unlatched, a HUD that wrote a line the moment a Step went Live. The walkthrough has been describing those reactions for ten chapters without ever saying what performs them.

They are nearly all the same component. **Ten Blueprints in the QuickStart carry a Quest Observer**, and that is most of the in-world layer of the tutorial: the door panels, the buttons, the prerequisite room's manager, the HUD itself. Chapter 11 is where the tutorial turns around and names the thing that has been doing the showing - and names the two exceptions, because they are the interesting ones. The giver actor reports its own gives through its own events and watches nothing. The sconce watches a **fact** rather than a quest, through a different component entirely, and that one closes a gap Chapter 10 had to apologise for.

So the room is a **control center**. One console, one screen, and a dial that runs through every chapter. Lock the dial onto a chapter and the screen tells you that chapter's state - not a summary someone wrote, but the lifecycle events themselves, one line each, in the order the framework produced them. Dial a chapter you finished an hour ago and it still answers, which is the lesson this room exists for.

**By the end of this chapter you can name:** the four component roles and which one reads, the one thing all four share, what an observer can and cannot do, the difference between ACTIVATED and STARTED and when they come apart, what catch-up is and what it refuses to replay, the difference between a state event and a transient one, and how an observer picks its scope.

---

## In the room

In order:

1. Chapter 11 activated as you finished Chapter 10, with two beats that name what is in the room:

   > The screen has a Quest Observer Component: it hears quest events without affecting them.

   > It also has a Quest Trigger Component, which can participate in its own set of chosen quests.

   Ahead of you is the console, its screen already lit. Across the far half of the room is a barrier.

<img width="1200" alt="Chapter 11 on entry: the two opening beats on the HUD, the console and its lit screen ahead, the barrier across the far half of the room" src="https://github.com/user-attachments/assets/c6aa2aab-1278-4f43-b7c5-aaf7d9730024" />

2. **The screen is on, and it is not showing this room.** It has been watching **Chapter 10** since the level loaded - the chapter you just left - so Chapter 11's own opening events happened while it was looking the other way. The HUD names the first Step, *Look Up Chapter 11*:

   > The screen has been watching Chapter 10. Set it to Chapter 11 instead.

   > Stand in the glowing zone and use the arrow keys to select a chapter. Then press 'E', 'Enter', or 'Left Click' to lock in your selection.

<img width="1200" alt="The console screen as you find it, still watching Chapter 10, with Look Up Chapter 11 named on the HUD" src="https://github.com/user-attachments/assets/0fbf3c7e-6710-46dd-bb7c-1afbe80f240b" />

<img width="1200" alt="The dial open in the glowing zone, mid-scroll through the chapter list" src="https://github.com/user-attachments/assets/16e618b0-a7e5-4d9e-8f81-f2ffb0f4c373" />

3. **Lock it onto Chapter 11.** The screen clears and fills in one go, and the Step completes - the lock-in is a trigger, and *Look Up Chapter 11* was waiting for it. Then read the rows, because they are not all the same kind:

   > Everything above the timed rows is marked catch-up. The chapter began while the screen was watching 10, so it was caught back up to the present state, not shown the events live. The timed rows were events that it watched live.

   The whole idea arrives in one screen, before anyone has explained it. **A row with a time on it was witnessed. A row reading *catch-up* was answered.**

<img width="1200" alt="The screen immediately after locking onto Chapter 11: catch-up rows above, timed live rows below, and the footer's count" src="https://github.com/user-attachments/assets/9fbaffab-c220-4991-8ed1-88ed7da4e0d8" />

4. **Go and talk to the archivist.** He is holding a request and he will not hand it over:

   > The giver is advertising this quest. Until its prerequisites are satisfied, a step is ACTIVATED and nothing more - in scope, not running.

   > GIVE BLOCKED. The request exists but can't be accepted while the prerequisite is unfulfilled. A refusal is an event, and something in this room was listening for it.

   The barrier drops as he refuses you. That is the "something" - an observer in the level watching for the refusal, and the first time in eleven chapters that one has reacted to a **failure** rather than a success.

   > Note the event is amber: this signifies it is TRANSIENT. It yields no persistent state, and it WILL NOT be replayed to an actor subscribing late.

   > Transient events include ACTIVATION FAILED, GIVE BLOCKED, PROGRESS, and PROGRESS REFUSED.

<img width="1200" alt="The archivist refusing: GIVE BLOCKED in amber on the screen and the barrier mid-drop" src="https://github.com/user-attachments/assets/660efd92-2ce2-4fd4-8143-2a55507e1033" />

5. **Clear the barrier**, and the request becomes available:

   > GIVE ENABLED means nothing stands in the way of accepting. Every giver-gated quest fired this same event, but the screen is the first place that named it.

   Before you accept, read that row. It says ACTIVATED and GIVE ENABLED **with nothing under them**. That is the offer phase, and it is the one state in the framework you cannot see any other way.

<img width="1200" alt="The request's row before accepting: ACTIVATED and GIVE ENABLED with nothing under them" src="https://github.com/user-attachments/assets/29ba76be-6c13-4e8a-a8ab-95e1e069c11c" />

6. **Accept.** The row gains a line, timed, live:

   > STARTED landed the moment you accepted from the giver.

   > Now dial Chapter 5 and lock it in.

<img width="1200" alt="The same row after accepting, with STARTED landed and timed" src="https://github.com/user-attachments/assets/6d9297cb-927a-4b4c-bf4b-96f5fb310742" />

7. **Dial to Chapter 5.** A chapter you finished an hour ago arrives all at once - every row catch-up, and the fork's row carrying the outcome you chose:

   > COMPLETED. Chapter 5 is now on screen.

   > If you already played it, the screen has its current state anyway, the fork included - an observer arriving late is caught up to what is, not replayed through everything that happened.

   The beats cover the other case too, because a player who took the Shortcut in Chapter 9 may never have been to Chapter 5:

   > If you haven't yet played Chapter 5, consider doing it now to see the results.

   > Explore any chapters that you've finished. Their current state is reported through the same events.

<img width="1200" alt="Chapter 5 locked in: every row marked catch-up, and the fork's row carrying the outcome you chose" src="https://github.com/user-attachments/assets/795926e8-f675-4813-8321-e5bba68424a8" />

8. **Now dial back to Chapter 11.** This is the exam:

   > Switching its view to a new chapter unsubscribes the observer component on the screen from its prior channel and subscribes it to the new one. On subscription, events that already fired on that new channel are re-broadcast marked as Catch-up rather than Live. This immediately brings that observer current with the present state.

   > Back at the Chapter 11 readout. Note that the transient events from earlier - the amber lines - are gone. The observer was caught back up to the present state. Since those events don't yield a state, they weren't replayed to the Observer like the stateful ones were.

   The rows you watched arrive one at a time a few minutes ago come back together, grey, and **two things are missing**: the amber GIVE BLOCKED, and GIVE ENABLED. What is left is the state.

<img width="1200" alt="Chapter 11 locked in again: the same rows as catch-up, with the amber lines and GIVE ENABLED gone" src="https://github.com/user-attachments/assets/d028181d-3eab-4da3-aa45-747ad71f42ad" />

9. **End the chapter** at the trigger:

   > Touch the glowing trigger when you're ready to end this Chapter.

   > Quest Observer Components are extremely flexible and allow your game to easily react to any quest event. You can continue to use the console in this room to explore the flow of states and events.

<img width="1200" alt="The chapter's closing beat and the last rows arriving live" src="https://github.com/user-attachments/assets/1a86250e-9785-4917-b84e-7dc4857b86ab" />

---

## In the graph

**Open `QL_Ch11_Observers`.** Five Steps, one parallel split, and a prerequisite:

**Start → Look Up Chapter 11**, then its *Any Outcome* splits into two: **Clear the Barrier** and **Look Up Chapter 5**, both at once. *Look Up Chapter 5* carries a prerequisite on *Clear the Barrier* and is giver-gated by the archivist. Both converge on **Back to Chapter 11**, also giver-gated, and that into **Chapter End**, and out through the **Outcome node (Solved)**.

<img width="1200" alt="QL_Ch11_Observers, the whole graph: Start into Look Up Chapter 11, the split to Clear the Barrier and Look Up Chapter 5, both into Back to Chapter 11, then Chapter End and the Outcome node" src="https://github.com/user-attachments/assets/e80ae5b6-e2f6-4b64-8768-5b2ca2784a79" />

**The Step labels in the graph are not the names on the HUD, and that is on purpose.** The node is `Look_Up_Chapter_5`, which says what completes it. The display name is **Watch Givers and Triggers**, which says what it is teaching. `Back_to_Chapter_11` shows up in the log as **Catch-up Events**. The graph names the action; the display data names the lesson. Nothing enforces that - the node label and the display name are independent fields, and this chapter is where the gap between them earns its keep.

`Clear_the_Barrier` has **no display data at all**, so it writes nothing to the HUD. A Step with no display asset is silent by design; it is a beat in the room rather than a line in the log.

### Things worth clicking:

- **The console, `BP_ArchiveConsole`.** Two quest components and some furniture. `QuestObserver` reads, `QuestTrigger` advances, and `Screen` is a Widget Component rendering `WBP_ArchiveDisplay`; `InteractZone` is the box whose overlap hands you the dial. **Two components, because it does two jobs** - and the component list is the only place that says so, which is exactly why it is worth opening. An actor that reads and an actor that advances are different claims, and a reader who can see both on one actor has the chapter's thesis in one glance.

- **`QuestObserver`'s tags, and where they actually come from.** *Observed Tags* is **empty**. Nothing is authored on the component at all. On Begin Play the console calls **Add Observed Tag** and passes its own `LockedTag` variable, so the default value of `LockedTag` is the single place that decides what the screen shows when the level loads - and it is the same call the dial makes every time you lock in. Author the tag in the map as well and you would have written the same fact twice, free to drift. The dial does not change a setting on the component either; it calls **Remove Observed Tag** on the old tag and **Add Observed Tag** on the new.

<br>
  <img width="1200" alt="BP_ArchiveConsole's component list beside QuestObserver's empty Observed Tags map, with LockedTag's default value" src="https://github.com/user-attachments/assets/4983a897-45a7-4ed1-ba0c-49f721e20782" />
<br>
<br>
  <img width="1200" alt="QuestObserver's Observed Tags during play, with the dialed entry present: Routing on Descendants and the twelve event switches" src="https://github.com/user-attachments/assets/86a407b0-f26e-4681-9e14-d1450726cc21" />
<br>

- **`QuestTrigger`'s *Step Tags to Trigger*.** Both console-completed Steps are in that one list, rather than a Trigger component each. The component boundary is the capability boundary: a second Trigger would be a second copy of one role, and the lock-in would have to pick which of them to fire.

<br>
  <img width="1200" alt="QuestTrigger's Step Tags to Trigger with both console-completed Steps in the one list" src="https://github.com/user-attachments/assets/d42bf37e-432f-44d7-8e15-ac58a5aea371" />
<br>

- **`QC_RecallTarget`**, a **Config Asset**. One tag, two instances, one per Step. This is the first use of the Config Asset slot anywhere in the QuickStart - the seam has been there since 0.5 and was never exercised until this room.

<br>
  <img width="1200" alt="Look Up Chapter 5 selected: the Config Asset slot filled with a QC_RecallTarget instance, and its Prerequisites pin wired from Clear the Barrier" src="https://github.com/user-attachments/assets/13ecd39f-6a96-4652-8530-0a4ff6f5f9a3" />
<br>
<br>
  <img width="479" alt="The two QC_RecallTarget instances, one chapter tag each" src="https://github.com/user-attachments/assets/8dd907d9-4507-4964-860f-bfc56848fdf8" />
<br>

- **`OBJ_RecallChapter`**, the Objective. It compares the dialed tag against the chapter the Step expects and refuses the trigger when they disagree - so browsing the dial is allowed and does not progress anything.

- **`BP_QuestObserverDebugger`**, in the plugin's `ExampleBlueprints/Actors` - not placed in any room, and not mentioned anywhere else in this walkthrough. It is a bare actor carrying a Quest Observer that prints every event it hears. It ships watching `SimpleQuest.Questline` with Descendants routing, which is the whole project - drop it in a level, press play, and it logs everything. Narrow the tag when you want less. The *Try it* section at the end of this chapter uses it.

<br>
  <img width="405" alt="The status toast reporting a refused trigger after locking in on a chapter neither Step wants" src="https://github.com/user-attachments/assets/cc5b7a26-f3b3-4619-b2a9-b822db3583a7" />
<br>

---

## Under the hood

### What an observer is

One of four component roles, and the only one that cannot change anything.

- **Trigger** advances a Step.
- **Giver** offers one.
- **Reward Recipient** receives what a completion pays.
- **Observer** watches. It subscribes to a curated set of quest tags, broadcasts the full lifecycle vocabulary, and has no method that can start, complete, block, or deactivate anything.

That last clause is the design, not an omission. Observers read out; they never push in. Chapter 6's door obeyed it without the walkthrough saying so.

**The four are peers, not a hierarchy, and that is worth knowing before you place one.** None of them inherits from another, so a Trigger has no observed-tags list and a Giver has no step-trigger list. An actor that needs two roles carries two components and says so in its component list - which is what the console does, and what makes it readable. If you have used SimpleQuest before 0.9, this is a change: those surfaces used to arrive by inheritance, so a Trigger quietly *was* an Observer and a Giver was both. The events are identical; what moved is which component admits to hearing them.

All four sit on one common component, **Simple Core Observer**, which you will see in the inheritance when you open any of them. It is not a quest component: it belongs to SimpleCore, knows nothing about quests, and watches **world state facts** - any tag at all, because facts belong to whoever writes them. So the choice is a short one. Watching a **quest**, its phases and outcome and refusals, is a Quest Observer. Watching a **fact**, is this true right now, is a Simple Core Observer - and every quest component can do that too, because they all inherit it. Chapter 10's lights are the worked example.

There are two forms and they are the same mechanism. `UQuestObserverComponent` is the authored one - a map of tags to settings on an actor. **Observe Quest Lifecycle** is the ad hoc one, a Blueprint node for a graph that wants a subscription without a component. Same events, same catch-up.

### The thirteen delegates, and the twelve switches

The component exposes twelve specific events plus one catch-all:

ACTIVATED, GIVE ENABLED, GIVE DISABLED, GIVE BLOCKED, STARTED, PROGRESS, PROGRESS REFUSED, COMPLETED, DEACTIVATED, BLOCKED, UNBLOCKED, ACTIVATION FAILED - and `OnAnyQuestEvent`, which receives all of them as one report. The screen binds `QuestObserver`'s and nothing else, because it is a log.

Each of the twelve has its own checkbox in the tag's settings, so one actor can watch a chapter for completions only while another watches the same chapter for refusals. Two of the names on the screen differ from the pin names: the pins read *On Quest Enabled* and *On Quest Disabled*, and the screen says **GIVE ENABLED** and **GIVE DISABLED**, because those two only ever fire on giver-held content and the word *give* is what makes that legible beside GIVE BLOCKED.

### Catch-up is a state read, not a recording

This is the chapter's one hard idea, and the screen is built to make it visible.

When an observer subscribes to a tag, the framework immediately tells it the current state of that tag - as synthetic events, delivered to that subscriber only, flagged **CatchUp** on the payload. Nothing is published, and no other subscriber sees anything.

**It is derived, not stored.** The framework reads the node's phase and emits the events that phase implies:

| Phase         | What arrives                                                     |
|---------------|------------------------------------------------------------------|
| Never reached | nothing at all                                                   |
| Activated     | ACTIVATED, plus GIVE ENABLED if the offer is currently available |
| Started       | ACTIVATED, STARTED                                               |
| Completed     | ACTIVATED, STARTED, COMPLETED with the outcome                   |
| Deactivated   | the interrupted run if there was one, then DEACTIVATED           |

A `Blocked` fact adds BLOCKED on top of whichever of those applies - and for a node that was never reached, BLOCKED is the one thing that can arrive on its own. A node never reached and not blocked produces **nothing**, which is why dialing a chapter you skipped reads NO RECORD rather than a wall of blanks.

That distinction is worth holding onto, because the room's beat describes the same moment as events being *re-broadcast*. At the console that reads correctly - the rows do arrive. Underneath, nothing was kept and nothing was played back: the framework asked the quest what state it is in and expressed the answer in the same event vocabulary. **Which is why the amber lines cannot come back.** They were never stored, and no phase implies them.

**Two consequences follow, and the room demonstrates both.**

The first: **transient events never come back.** PROGRESS, PROGRESS REFUSED, GIVE BLOCKED, and ACTIVATION FAILED are moments, not states. That is the GIVE BLOCKED row vanishing when you dial back. The split is not a convention the screen invented - the eight state events are exactly what a phase can reconstruct, and the four transients are exactly what it cannot.

The second: **only the latest state survives.** Catch-up carries one outcome per node, the most recent. A chapter you replayed resolved twice, and the registry still has both - `Get Resolution History` will hand them over - but the observer is told what the quest *is*, not what it has been through.

**And the order is the authored order.** The replay walks parents before children, then follows the same authored progression the live run used, so a restored log reads in the order the player originally saw. Arrival time is the one thing it cannot reproduce, which is what the screen's time column is for: a live row stamps the quest clock, and a catch-up row has no time to show.

### ACTIVATED is not STARTED

Ten chapters could not teach this, because the two only come apart on giver-held content and the gap is invisible without an observer.

- **ACTIVATED** means in scope. The quest has been reached and is being offered.
- **STARTED** means Live. It is running.

On everything in Chapters 1 through 10 except the giver rooms, those happen in the same breath. On a giver-gated Step they do not: the Step activates, the giver takes hold of it, GIVE ENABLED fires when its prerequisites allow the offer, and **STARTED does not arrive until the player accepts**. The archivist's request sits in that gap long enough to read it off the screen - which is the whole reason this chapter gave a Step a giver it did not otherwise need.

### Scope: one tag, or everything under it

Each watched tag carries a routing setting, and it is one line with large consequences.

**Exact** hears that tag only. **Descendants** hears the tag and everything beneath it - so an observer on `...QuickStart.Chapter_5` receives the chapter's own events *and* every Step inside it. That is how the console shows a whole chapter from one subscription, and it is also how the HUD works: one subscription at the questline root, every node beneath it.

It is also where the fork receipt comes from. "Which way you went" is not recorded on Chapter 5 - it is on the fork Step inside it. Descendants routing picks it up for free, so the scope setting and the receipt are the same decision.

### The late arrival is the subscription, not the actor

The console is never spawned mid-run. It has existed since the level loaded.

What arrives late is the **subscription**. `AddObservedTag` is Blueprint-callable, and when the component has already registered it subscribes and catches up that tag inside the call - synchronously, before the function returns. A placed observer watching nothing, told mid-run what to watch, is correct on the spot. Not because an event fired. Because it asked.

That is also why the lock-in order matters and then stops mattering. The console removes the old tag, adds the new one - catch-up runs inside that call - and only then fires the trigger, so the Step's own completion arrives as a **live** row rather than buried in the replay. Reverse the two and the screen still ends up correct, which is the real point.

---

## Gotchas

**An observer with no authored tags is not broken - it is the console's actual build.** *Observed Tags* is empty on `QuestObserver`, and the first tag arrives from a Begin Play call. Registration is written for this: the component marks itself registered *before* its own empty-set check, specifically so a component that starts with nothing still accepts runtime adds. An empty map is a perfectly good starting state for anything that learns what to watch - and when the thing to watch is already named by a variable the actor owns, driving the list from that variable beats authoring it twice.

**Catch-up fires inside `AddObservedTag`, not on the next tick.** Anything the handler touches must already be built. The console clears the screen *before* adding the tag for exactly this reason - add first and the rows land in a log that is about to be wiped.

**Upgrading from before 0.9: a Trigger or a Giver with no Observer beside it has lost a surface.** The roles used to inherit from one another, so binding *On Quest Started* on a Trigger worked - it was an Observer underneath. It is not any more. If a Blueprint of yours binds a lifecycle event on a Trigger or a Giver and nothing fires, that is this: add a Quest Observer Component and list the tag there. The events still publish exactly as they did; only the component that admits to hearing them changed. A Giver reporting its own gives needs nothing added, because it has its own *On Quest Given* and *On Give Refused* now.

**A transient event seen live is gone forever.** If a refusal or a progress tick matters after the fact, something has to write it down when it happens. The framework keeps session-scoped refusal history and nothing more; a durable record is yours to make - write a fact when the thing happens and watch *that*, which does replay.

**You can watch the namespace root. You just cannot ask it about itself.** Subscribing to `SimpleQuest.Questline` with Descendants works exactly as it looks - it is a real registered tag, parent tags being registered along with their children, and every quest event in the project arrives. `BP_QuestObserverDebugger` ships doing precisely that. What the root has no answer for is any question about *itself*: *Get Quest Phase* on it warns and returns NotReached, and it has no display name, because it is a namespace and not a node. **That is the distinction worth carrying out of this chapter - the tag you SUBSCRIBE on and the tag you can QUERY are not required to be the same kind of thing.** The dial's "Everything" entry targets the master questline one level down for that reason and no other: the dial prints the name of what it is watching, and a namespace has none. A log that only prints what arrives can sit on the root quite happily.

**Descendants routing on a deep tag is a lot of rows.** One subscription at a questline root fans out to every node beneath it, and every one of them that has been reached replays. That is what you want for a HUD and a log. It is not what you want on a door.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **A Step's completion can be driven entirely from outside the graph.** *Look Up Chapter 11* is completed by a console in the level firing a Trigger component. The graph never knows what a dial is.
- **The Config Asset is how a Step carries data its Objective needs.** Two instances of one Config class give two Steps the same Objective with different expectations, and no Blueprint branching per Step.
- **An Objective can refuse a trigger and say why.** `OBJ_RecallChapter` compares the dialed tag to the expected one and refuses on a mismatch, which is what makes browsing harmless.
- **A Step has a phase, not a pile of booleans.** Activated, Started, Completed, Deactivated - one answer, which is also exactly what catch-up can rebuild.
- **The display name and the node label are different fields for different audiences.** The log says *Catch-up Events*; the graph says `Back_to_Chapter_11`.

That is the Step node, assembled. Eleven chapters, one node type, and everything else in the framework arranged around what it needs.

---

## Try it

### Watch a chapter you have not played:

Press the red **Unlock** button in the first room, skip ahead, and dial a chapter you never entered. The screen reads **NO RECORD** - not an empty log, not a row of blanks. A node that was never reached has no phase to express, so catch-up produces nothing at all for it.

<img width="1200" alt="A chapter never entered, dialed in: the screen reading NO RECORD" src="https://github.com/user-attachments/assets/0fb398ae-63d4-46e8-8741-ad872f57204f" />

### Get a completion log for the whole tutorial, without building anything:

There is already an actor for this and nothing in the walkthrough has mentioned it until now. **Drag `BP_QuestObserverDebugger` into the level** - it is in the plugin's `ExampleBlueprints/Actors` - and press play. It ships with one *Observed Tags* entry on `SimpleQuest.Questline`, **Descendants** routing, and *Observe Completed* as the only box ticked, so it needs no configuration at all: that tag is the whole project, and every completion anywhere under it arrives. Narrow it to `SimpleQuest.Questline.QuickStart` if you want the tutorial alone. Walk the whole tutorial and you have a completion log for all eleven chapters, from one tag and one checkbox.

That is the cheapest useful subscription in the framework, and it is worth seeing how little it takes: one component, one entry, one flag. Nothing was wired per chapter, and nothing has to be maintained when chapters are added - Descendants routing means a new node anywhere beneath that tag shows up in the log the first time it completes.

<img width="1200" alt="The placed Quest Observer Debugger's Details panel: one entry on the QuickStart tag, Routing on Descendants, and Observe Completed the only box ticked" src="https://github.com/user-attachments/assets/812ddf6c-5af2-4a84-8c5f-e2882c2b2f3e" />

### Catch yourself up on purpose:

Call **Add Observed Tag** from a debug key on an actor that is watching nothing, passing a chapter you have already finished. Everything arrives in that one call. Then call **Remove Observed Tag** and call Add again - it all arrives a second time, because nothing was consumed. Catch-up reads state, and state does not run out.

### Prove the transient split:

Walk into a gated Step and let it refuse you, so PROGRESS REFUSED lands live. Now dial away and dial back. The refusal is not there. Then check the refusal history for the same Step - still there, session-scoped, because the registry wrote it down and the observer did not.

---

Previous: [Chapter 10 - Prerequisite Rules](10_PrerequisiteRules.md) | Next: The Step node, assembled
<br>
Index: [All chapters](README.md)
