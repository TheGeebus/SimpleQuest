# Chapter 9 - Activation Groups

*One tag, published by a graph that has never heard of the graph listening.*

Chapter 8 ran one asset twice, and both instances lived *inside* the chapter. Chapter 9 goes the other way: a second questline runs *alongside* the chapter, owned by nobody in it, and changes the shape of it while it is already running. Neither graph contains a reference to the other. There is no wire between them, and there is no wire you could draw - that is the point of the room.

What joins them is a tag. One node publishes on it; two nodes listen on it. The publisher does not know whether anything is listening, the listeners do not know who published, and the two listeners do opposite things: one opens the far side of the bridge, and one closes the bridge.

The chapter also cleans up after itself, which is the quieter half of the lesson. The questline it launched at the start is one it shuts down at the end - by name, as a unit, from a single node.

**By the end of this chapter you can name:** the two halves of an activation group and which one has no input pin, what decides whether an Exit opens or closes, what a group tag is and how it differs from every other tag you have seen, which direction the tag hierarchy runs, what Start Questline does that a Linked Questline node does not and what closes what it opens, what a group signal carries with it, and how to see a connection that has no wire.

---

## In the room

In order:

1. Chapter 9 activated as you finished Chapter 8, and two things started at once. The chapter's two beats:

   > An Activation Group is a connection with no wire. Entry nodes publish on a tag, and Exit nodes listening on that tag carry on from there - in this graph, or in another one entirely.
   >
   > The bridge ahead is the long way across. There is a way under it too, and it belongs to a different questline.

   > Activation Groups are a powerful tool that enable you to easily direct the flow of activation across graph boundaries.
   >
   > Take a look at the Chapter 9 Questline graph to learn more about how they work.

   And then a beat that is not the chapter's at all:

   > A second questline, running alongside the chapter. It knows nothing about the bridge and offers an alternative way forward that converges with that other path.

   Look at the sidebar. There are **two questlines** on it - *Chapter 9 - Activation Groups* and *A Hidden Passage* - and only one of them is a chapter.

<img width="1200" alt="The room on entry: the chapter's two beats and the Hidden Passage's beat, the bridge ahead" src="https://github.com/user-attachments/assets/b63fa22c-21f2-4090-b197-def6af6cbc3e" />

<img width="668" alt="The sidebar with both running: Chapter 9 and A Hidden Passage as two separate questlines" src="https://github.com/user-attachments/assets/7bf4e32c-f341-4085-b6c5-6fec731da5fd" />

2. **The long way.** The bridge is four Steps: Bridge 1, Bridge 2, Bridge 3, and then Journey's End. Walk it and notice what does *not* happen - the first three say nothing at all. No beats, no commentary, just beacons going out behind you. They are deliberately silent; the room saves its narration for the moment that matters.

<img width="1200" alt="Partway across the bridge: two beacons behind you, no new beats on the HUD" src="https://github.com/user-attachments/assets/8759b1fd-9782-44a5-9c39-730bc7682599" />

   At the far end, the fourth Step finally speaks:

   > The far side beckons.

<img width="1200" alt="Journey's End activated the long way: 'The far side beckons.'" src="https://github.com/user-attachments/assets/6d49e693-2c28-4541-a7cd-2e95e53e10ba" />

3. **Or the short way.** Leave the bridge alone and find the other beacon - the one belonging to *A Hidden Passage*. Before you touch it, look back: the bridge is untouched, and its beacons are all still lit.

<img width="1200" alt="The short way instead, before touching the Underpass: the bridge untouched, all beacons lit" src="https://github.com/user-attachments/assets/0b5fdf3d-f200-404c-8335-9f3d1901ade0" />

   Now go under, and touch the Underpass:

   > The way under is open - and so is the far side of the bridge, which this questline has never heard of. It published on a tag. It never named what was listening.

   **Two things happen in that moment**, and the room is worth looking at twice. Across the room, **Journey's End is lit**, and you never walked the bridge. And the bridge's lit beacon has **gone out**. Only the first one was ever lit - a chain activates one Step at a time, so Bridge 1 was the whole of the long way you had available - and one published tag closed it and opened the far side in the same frame, from a questline that has heard of neither.

<img width="1200" alt="The moment the Underpass completes: the Hidden Passage's completed beat, and Journey's End lit across the room" src="https://github.com/user-attachments/assets/fd6b7187-5484-4436-afa2-a6af3825a664" />

<img width="1200" alt="The same moment, looking back at the bridge: the first beacon dark, the far side lit beyond it" src="https://github.com/user-attachments/assets/71480441-f1d9-4628-acf8-c29837bc6cae" />

4. Either way, touch Journey's End:

   > You reached the far side. Two routes, two unrelated questlines - and whichever one you took, the chapter closed the other behind you.

   and the chapter closes:

   > Something outside this questline opened a way through it while it was already running. No direct wire connection was needed.

   Look at the sidebar. *A Hidden Passage* is **gone**. The chapter did not only end; it shut down the questline it started, by name, on its way out.

   The way on to Chapter 10 is back through the underpass, and you cannot see it from here. That door opens whether or not you ever took the shortcut - touching the first underpass beacon opens it, and so does finishing at Journey's End. Walk the long way across, never go under at all, and the route onward is still open behind you. Two routes through the room, and the room makes certain that either one leads out of it.

<img width="1200" alt="After Journey's End: the chapter's completed beat and the sidebar with A Hidden Passage gone" src="https://github.com/user-attachments/assets/ab8801f8-48c1-4106-8511-2ca1bc885ab7" />

The in-world tell is the pair of shots at step 3: the same angle, before and after, one light going out and another coming on clear across the room. Nothing walked across. A tag was published, and two nodes that had been listening all along did opposite things with it.

Worth noticing that the two routes are **not** symmetrical. Walk the bridge and the hidden passage stays where it is, still open, still offering itself - right up until Journey's End closes it. Take the passage and the bridge is gone immediately. Both routes end with the other one shut, but by different mechanisms and at different moments: the passage closes the bridge mid-run with a group signal, and the chapter closes the passage at the end, by name.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/09_ActivationGroups/QL_Ch9_ActivationGroups`. A straight chain, and two nodes standing apart from it:

**Start → Start Questline → (Forward) → Bridge 1 → (Any Outcome) → Bridge 2 → (Any Outcome) → Bridge 3 → (Any Outcome) → Bridge 4 → Deactivate Quest → Outcome node (Reached)**.

Above the chain, an **Activation Group: Exit** reaching **Bridge 4's** *Activate*. Below and to the left, a second **Activation Group: Exit** reaching **Bridge 1's** *Deactivate* - and from there a second run of orange wires along the bottom of the chain, Bridge 1's *Deactivated* output into Bridge 2's *Deactivate*, and Bridge 2's into Bridge 3's. Neither Exit has an input pin. Both carry the same group tag.

- **Start Questline** is not a Linked Questline node. It launches `QL_Shortcut` as a questline of its own - not placed inside this one, not prefixed by it, not owned by it. Its own *Forward* pin carries the activation straight on to Bridge 1, so the chapter does not wait for anything.
- **The four Bridge Steps** are an ordinary chain of the kind Chapter 4 built. Bridge 4's *Node Label* is "Bridge 4" but its *Display Name* is **Journey's End**, which is what the sidebar shows. Bridges 1 through 3 have their deactivation pins shown, which is how the orange run is drawn at all - Chapter 7 turned that checkbox on for the same reason.
- **The two Exits** have **no input pin**. There is no way to reach either from inside this graph, and that is not an oversight - it is the whole design. They listen on a tag, and when anything anywhere publishes on that tag, activation continues out of their *Exit* pins as though a wire had arrived.
- **The pin each one lands on decides its job.** They are the same node class with the same tag. One is plugged into an *Activate* input and starts what it reaches; the other is plugged into a *Deactivate* input and tears down what it reaches. Nothing on the node itself says which it is.
- **Deactivate Quest** sits between Journey's End and the Outcome node, naming `SimpleQuest.Questline.QL_Shortcut`. It is the counterpart to Start Questline at the other end of the chain: one node opened that questline, one node closes it, and the chapter's own ending passes through on the way to the Outcome node.

<img width="1200" alt="QL_Ch9_ActivationGroups, the whole graph with both Exits, the Deactivate Quest node, the orange deactivation run, and all five comment boxes" src="https://github.com/user-attachments/assets/f4ff9d35-6fd5-4308-907b-585182a6e098" />

<img width="1200" alt="Zoomed on the two Exits together: identical titles, two wires, opposite kinds of pin" src="https://github.com/user-attachments/assets/e3b544ba-2a76-44bc-a766-373214162068" />

**Now open `QL_Shortcut`** - or select the Start Questline node and open the asset it names. Two Steps and a portal:

**Start → Shortcut Trigger → (Any Outcome) → Underpass → (Any Outcome) → Activation Group: Entry → (Forward) → Outcome node (Solved)**.

The **Activation Group: Entry** is the other half, and there is only one of it. Activation arriving at its *Enter* pin publishes a signal on its group tag - to every Exit in the project carrying a matching tag, whatever those Exits do with it - and then continues locally out of *Forward*. Here *Forward* goes straight into the Outcome node, so `QL_Shortcut` resolves *Solved* in the same breath as the signal goes out. The questline's job is finished the moment it has spoken.

There is also an orange wire in this graph, and it is the one that answers Chapter 9's closing move. **Start's *Deactivated* output runs into Shortcut Trigger's *Deactivate*, and Shortcut Trigger's own *Deactivated* runs on to Underpass.** Nothing in this graph ever fires that wire; it exists for someone outside to pull. That is this questline deciding, for itself, what being shut down means.

<img width="1200" alt="QL_Shortcut, the whole graph with the orange Entry wire and all three comment boxes" src="https://github.com/user-attachments/assets/d7265f97-ad67-4e9b-ad25-229774eedca2" />

**The comment boxes. In short:**

- *In the chapter, over Start Questline* - it launches another questline asset as its own thing, not placed inside this one the way Chapter 8's patrol route was, but started alongside it, under its own tag, with its own entry in the HUD. Nothing here can wire into it and nothing in it can wire back. The only thing the two share is the room.
- *In the chapter, over Deactivate Quest* - it stops a quest in progress and fires that quest's *Deactivated* pin, which can cascade on to further nodes. And if the tag names a Quest or a Questline - anything holding an inner graph - it also fires the *Deactivated* pin on that graph's own Start node, which is how a container hands the decision inward.
- *In the chapter, over the opening Exit* - an Exit is a second entry point into a graph that has already started. It has no input pin, it listens on a tag, and groups respect the tag hierarchy: an Exit on a parent tag hears every child, so a second way across added later as `CrossingFound.Ferry` would land here without touching this node. It also points at the instrument: right-click either node and choose **Examine Group Connections**.
- *In the chapter, over the closing Exit* - activation groups can connect to *Deactivate* pins like any solid activation wire, closing a route as well as opening it.
- *In the chapter, beside the Exits* - any system can publish on a group's tag channel, not just Entry nodes. Faction reputation, dev tooling, cheats, and game-mode orchestration can all participate by publishing the tag to the Signal Subsystem directly.
- *In the shortcut, over the whole graph* - this questline knows nothing about the bridge. It was started by a node in Chapter 9, but it runs under its own tag and nothing in it refers to that graph. Add a second listener anywhere else in the project and this graph does not change.
- *In the shortcut, over the Entry* - the *Forward* pin carries this node's own input onward, here into the Outcome. It does not rebroadcast the group, and an Entry never hears other Entries. To receive a group, use an Exit.

### Things worth clicking:

<br>
  <img width="1061" alt="The opening Exit selected: Group Tag CrossingFound, and no input pin on the node" src="https://github.com/user-attachments/assets/7175d812-c158-466b-8220-da14f1a49e82" />
<br>

<br>
  <img width="953" alt="The closing Exit selected: the SAME Group Tag" src="https://github.com/user-attachments/assets/fd1aba3e-6d6d-4d33-b060-a21740c7687b" />
<br>

- **Select each Exit in turn and read the *Group Tag*.** They are identical: `SimpleQuest.ActivationGroup.CrossingFound`. That is the chapter's cleanest single point. Two nodes, one tag, one publish, and opposite outcomes - and the only thing that distinguishes them is which pin their wire ends on. Look at the nodes while the Details panel is open, too: an *Exit* pin on the right, and nothing at all on the left.

<br>
  <img width="1049" alt="The Activation Group: Entry selected in QL_Shortcut: Group Tag CrossingFound.Underpass" src="https://github.com/user-attachments/assets/ab0b3ade-1506-4b1b-81ba-36eefb7c9d9a" />
<br>

- **Select the Entry in the shortcut.** Its *Group Tag* is `SimpleQuest.ActivationGroup.CrossingFound.Underpass` - one segment *longer* than the Exits'. That is not a mismatch; it is the hierarchy doing its job, and the direction it runs is worth getting right the first time. See below.

<br>
  <img width="1023" alt="The Start Questline node selected: Graph set to QL_Shortcut" src="https://github.com/user-attachments/assets/a435f0c4-8684-4e1c-8552-b4ccd88a1d6c" />
<br>

<br>
  <img width="1074" alt="The Deactivate Quest node selected: Tags to Deactivate reading SimpleQuest.Questline.QL_Shortcut" src="https://github.com/user-attachments/assets/561cb7c6-3166-46da-b974-1852f2fbb765" />
<br>

- **Select Start Questline, then Deactivate Quest.** One opens a questline and one closes it, and they are the only two nodes in the chapter that name `QL_Shortcut` at all. Start Questline carries a *Graph* property - an asset reference. Deactivate Quest carries a *tag*, which is why it can name a Step, a container, or a whole questline without caring which. Compare both with Chapter 8's Linked Questline node, whose *Linked Graph* produced output pins, a tag prefix, and a place in the Outliner. Neither of these produces any of those.

<br>
  <img width="1200" alt="Bridge 1 selected with its deactivation pins shown: the orange wire arriving at Deactivate, its Deactivated output leaving for Bridge 2" src="https://github.com/user-attachments/assets/ed66ad98-d943-41dd-a650-5fae8f12e6a8" />
<br>

- **Select Bridge 1 and look at its two extra pins.** *Deactivate* is an input like *Activate*, and the closing Exit is wired into it. *Deactivated* is an output that fires when this Step is torn down, and it runs on to Bridge 2's *Deactivate*. That is how one arrival takes down three Steps: the Exit only reaches Bridge 1, and Bridge 1 passes it along.

<br>
  <img width="1048" alt="The Group Examiner pinned on CrossingFound: Setters (1) and Getters (2)" src="https://github.com/user-attachments/assets/4edbc189-8342-4461-b716-c433ebb095d8" />
<br>

- **Right-click any of the three group nodes and choose *Examine Group Connections*.** The Group Examiner opens, pinned to that tag, and scans every Questline asset in the project for nodes carrying it - **Setters** (the Entries that publish) and **Getters** (the Exits that listen), each with the asset it lives in and what it connects to on its own side. Here it reads **Setters (1)** and **Getters (2)**, which is the whole topology of this chapter in two numbers. This is the only view in the editor that shows this connection, because there is no wire to draw and no single graph that contains both ends. An endpoint whose own tag differs from the pinned one is marked with the tag it actually carries, which is how you confirm at a glance that the parent/child pairing is what you intended.

<br>
  <img width="489" alt="The Questline Outliner with Chapter 9 open: four Bridge Steps, no Hidden Passage" src="https://github.com/user-attachments/assets/b409739d-4acd-4305-bec3-cee4333b4a59" />
<br>

- **Open the Questline Outliner** with the chapter open. Chapter 9 holds four Bridge Steps and nothing else. *A Hidden Passage* is not under it, is not under the master, and is not anywhere in this tree - because it is not inside anything. Chapter 8's placements appeared in the Outliner; this questline never will.

---

## Under the hood

### Two ways to run another questline

Chapter 8's Linked Questline node and Chapter 9's Start Questline node both cause a second graph to run, and they are not variants of one idea:

| | **Linked Questline** (Chapter 8) | **Start Questline** (Chapter 9) |
|---|---|---|
| Relationship | the inner graph is **placed inside** this one | the other graph is **started alongside** |
| Tags | inner nodes get this placement's prefix | the other graph keeps its own tags |
| Pins | the inner graph's Outcome nodes become pins on the node | one *Forward* pin, fired immediately |
| Ending | the placement resolves, and the outer graph can route on it | nothing reports back; the two are unrelated |
| Outliner | appears as a branch under the placing node | does not appear |

The level settles the argument without reading a line of code. Open any bridge beacon and its trigger lists `SimpleQuest.Questline.QuickStart.Chapter_9.Bridge_1` - the chapter's placement address inside the master questline, exactly as Chapter 8 taught. Open the Underpass beacon and its trigger lists `SimpleQuest.Questline.QL_Shortcut.Underpass`. No `QuickStart`, no `Chapter_9`, no prefix at all. **That address is what "not inside anything" looks like.**

This also means the two questlines have no shared ending. `QL_Shortcut` resolves *Solved* on its own; Chapter 9 resolves *Reached* when Journey's End completes. Neither waits for the other, and neither can route on the other's outcome. The only traffic between them is the group signal - and, at the end, one node naming the other by tag.

### The two halves

At compile, the Entry becomes an `ActivationGroupSetterNode` and each Exit becomes an `ActivationGroupListenerNode`, and they do opposite jobs:

- **The Entry publishes.** When activation reaches its *Enter* pin, it publishes a transient signal on the group tag's channel and then forwards locally. It publishes whether or not anything is listening, and it has no way to find out.
- **An Exit subscribes.** It subscribes **when its instance is registered**, not when anything activates it, and it stays subscribed for the instance's whole lifetime. The Exits are armed before you ever reach the room, and they stay armed after the graph around them has finished.

That second point is the one that makes the room work. Chapter 9's Exits are not waiting for their turn in the chain - there is no wire putting them in a chain. They are simply listening, from the moment the chapter's instances exist.

**The signal is transient.** It is a message on a bus, not a fact in the world. Nothing records that it was sent, nothing can ask later whether it was, and there is no queue: a listener that does not exist at the moment of the publish has missed it, permanently. Chapter 6's Blocked flag and Chapter 7's prerequisites were state you could inspect. This is not.

### One tag, two listeners, opposite jobs

Both Exits compile to the same class and hold the same tag, so one publish reaches both. What differs is where the compiler put their destinations:

- The Exit wired to an *Activate* input contributes **Bridge 4** to its forward-activation list.
- The Exit wired to a *Deactivate* input contributes **Bridge 1** to its forward-*deactivation* list.

Nothing about the node declares which it is. The pin the wire lands on is the whole distinction - which is the same unified-signal rule Chapter 7 used when it fed a completion into a *Deactivate* input, and the reason the schema accepts any output signal at any signal-consuming input.

**The teardown cascades, and that is the only reason three Steps go dark.** The closing Exit reaches exactly one node. Bridge 1 then carries a deactivation wire to Bridge 2, and Bridge 2 to Bridge 3, so the arrival walks the chain link by link.

**Deactivation passes through a Step that has already finished.** This is the part worth holding on to. Walk Bridge 1 and Bridge 2, then take the underpass: those two are Completed, not Live, so there is no lifecycle of their own to interrupt. The cascade reaches them anyway, writes no fact, publishes no DEACTIVATED event, fires no activate-on-deactivation wire - and **forwards regardless**, so it still arrives at Bridge 3 and tears down the one Step that actually was live. An inactive node relays the teardown without claiming a transition it never made. The cycle guard is what stops that relaying from looping, not the inactivity.

**The cascade stops one Step short, deliberately.** Bridge 3 has no deactivation wire out of it. If it did, the teardown would continue into Bridge 4 - the Step the other Exit is opening in the same moment. Leaving that last wire off is load-bearing, not tidiness, and the last Try it below is what happens when you add it.

**Ordering, precisely.** Within a *single* node, forward teardown runs before forward activation, and the framework does that on purpose: a utility that closes one route and opens another means the opening to be the end state, so tearing down first and starting second leaves what the author drew. Across *two separate* Exit nodes there is no such guarantee - they are two independent subscribers to one publish, and nothing sequences them. In this room it does not matter, because the two sides touch disjoint Steps. Draw a graph where they overlap and the order is not yours to assume.

### Closing what it started

The chapter's last act is the one that has no group in it at all. Journey's End completes, and the activation runs into **Deactivate Quest**, which names `SimpleQuest.Questline.QL_Shortcut` and then forwards to the Outcome node that resolves the chapter.

Naming a whole questline works because of what happens next, and it is worth following once:

1. The request reaches the manager, which deactivates that tag. A questline identity has facts and publishes events, but it mints **no runtime node** - there is nothing there to dispatch through.
2. So the deactivation reaches the asset instead, and fires the routing hanging off that graph's own **Start node's *Deactivated* output**. In `QL_Shortcut` that wire runs into Shortcut Trigger's *Deactivate*. The same thing happens one level down for a Quest container: the node's own *Deactivated* pin fires for whatever is wired beside it, **and** the Start node of its inner graph fires for whatever is wired inside it. Chapter 7's orange chain was that second half, seen from within.
3. Shortcut Trigger relays to Underpass, the same way Bridge 1 relays to Bridge 2. Whichever of the two is live comes down; the other passes the cascade through.
4. Nobody clears the questline's own running state, because **it is derived rather than stored** - the moment no Step of the asset is active, the asset stops reading as running, and the sidebar row goes with it.

The shape to take away: **the graph being closed decides what closing it means.** Chapter 9 names a questline and nothing more. If `QL_Shortcut` wanted a shutdown that left one Step standing, or that lit something on the way out, that would be authored in `QL_Shortcut` - on the orange wire out of its Start node - and Chapter 9 would not change by a pixel.

That is also why this is a *Deactivate Quest* rather than a *Set Blocked*. Blocking is a gate that stays shut; this questline is not forbidden, it is finished. Deactivated, it can be started again the next time the chapter runs - which is exactly what a replay needs.

### The hierarchy runs one way

The Exits listen on `...ActivationGroup.CrossingFound`. The Entry publishes on `...ActivationGroup.CrossingFound.Underpass`. The signal arrives because **a subscription receives its own channel and every descendant of it** - that is the bus's default routing, and it is what SimpleCore is for.

The practical value is the one the comment box names: a second way across the gap, added later on `CrossingFound.Ferry`, would reach both Exits without anybody editing Chapter 9. They named a category, not a sender.

**It does not work in the other direction.** A subscriber on a child tag does *not* hear a publish on the parent. The ancestor walk is defined in the routing modes and explicitly reserved for later; it is not implemented. So the rule to carry out of this room is:

> **Listen broad, publish narrow.** The listener's tag should be the same length or shorter than the publisher's, never longer.

Get it backwards and nothing errors, nothing warns, and nothing happens. The Group Examiner is how you catch it, because it lists both ends and tells you which tag each one actually carries.

### A group tag is not a node tag

Everything else in this walkthrough has been a tag the compiler minted for you, and Chapter 8 made a whole room out of what happens when a node's tag gets a placement's prefix. **Group tags get no prefix.** An Exit's tag is `SimpleQuest.ActivationGroup.CrossingFound` when the chapter is compiled on its own, and it is still exactly that when the chapter is compiled as a placement inside the master questline - while the Step beside it goes from `QL_Ch9_ActivationGroups.Bridge_4` to `QuickStart.Chapter_9.Bridge_4`.

You author a group tag; you do not get given one. It is a name in a shared namespace, like a radio frequency, and it means the same thing everywhere in the project.

The consequence follows directly from Chapter 8: **place a graph containing an Exit twice, and both placements' Exits hear the same signal.** Two instances, two listeners, one tag, both fire. Sometimes that is exactly right - one broadcast waking every copy. When it is not, the group tag is the wrong tool, because a group has no notion of *which* instance you meant. A wire does; that is what wires are for.

### What rides along

A group is transparent to everything the framework tracks about a cascade. The signal carries the activation params, the origin chain, and the originating event ID that reached the Entry, and an Exit stamps all three onto whatever it activates, unchanged - it does not append itself to the chain or mint a new event identity. The signal also carries a source tag, taken from the last link of the inbound chain, and that one is informational: it is there for logging and for you, not for routing.

So Bridge 4, activated across an asset boundary by a questline it has never heard of, arrives with provenance intact. On the *Entries* tab it is an ordinary cascade arrival, and anything downstream that cares about instigators or event identity behaves exactly as it would have if a wire had done it. **The portal is a shortcut through the graph, not a break in the record.**

### What is watching

The group signal itself leaves nothing to look at. There is no fact to filter for, no row in the Resolutions tab, no count that goes up. The instruments here are different from the previous chapters':

- **The Group Examiner** is the authoring-time instrument. It answers "where does this group come from, anywhere in the project" by scanning the assets rather than the running game, so it works with PIE stopped. Setters and Getters counted separately is what makes a two-listener topology legible at all.
- **The pin tooltip** on either Exit's output states the contract in one line: it fires when any Entry with a matching group tag publishes, from this graph or any other. Note that it says nothing about opening or closing - it cannot, because that depends on where you wire it.
- **World State** shows the *consequence* rather than the signal, and the clearest part of it is what is missing. Take the underpass and filter to `Chapter_9`: Bridge 1 reads `Started` and `Deactivated`, Bridge 4 reads `Started` and `Live` - and **Bridge 2 and Bridge 3 have no rows at all**. The teardown passed straight through them, because a Step that was never activated has no lifecycle to interrupt and writes nothing on the way past. Six facts for a room with four Steps in it: the ones that ran, and nothing for the ones that did not.
- **The sidebar is the instrument for the ending.** It is the only place the questline teardown is visible without opening a panel, which is why the chapter spends a beat on it: two rows at the start, one at the end, and nothing in between announced the change.
- **Verbose logging** on `LogSimpleQuestActivation` is the closest thing to watching the signal itself. The Entry logs its publish with the chain depth and event ID; each Exit logs its receipt with the same. The deactivation side logs louder - the forward teardown logs each target at Log level, and the pass-through logs at Verbose with the reason, so a chain of three reads as three lines and tells you which ones had anything to tear down.

<img width="727" alt="Hovering the closing Exit's output pin: the tooltip about any matching Entry, from this graph or any other" src="https://github.com/user-attachments/assets/87025037-fa49-4e95-843c-06336ab4518d" />

<img width="1200" alt="World State filtered to Chapter_9 after the underpass: Bridge_1 Started and Deactivated, Bridge_4 Started and Live, no rows for Bridge_2 or Bridge_3" src="https://github.com/user-attachments/assets/47682577-3dec-4ada-b058-15520e43e056" />

<img width="1200" alt="Quest State view, Entries tab for Bridge_4: where the activation came from" src="https://github.com/user-attachments/assets/86b7ddf1-4f32-4a8e-a120-de6aa19811a0" />

---

## Gotchas

**An Exit cannot be wired to.** It has no input pin by design. If you find yourself wanting to trigger one from inside its own graph, you want a wire, not a group - the group exists for the case where the wire is impossible.

**Nothing on an Exit says whether it opens or closes.** Two Exits on one tag can do opposite things, and the Details panel looks identical on both. The destination pin is the only place that information lives, so a graph with several Exits on one tag is a graph to read by following wires, not by reading nodes.

**A deactivation does not stop at a finished Step.** It passes through anything with no active lifecycle and keeps going. That is what makes the cascade work when the player is halfway across the bridge, and it is also why a chain can reach further than you expected. If you want the teardown to stop somewhere, leave the wire off - do not rely on the node being done.

**Stop the cascade yourself.** A deactivation chain runs as far as the wires go, and the cycle guard is the only automatic brake. Where a chain converges on something you are also opening, the last link is the one to leave out.

**One Exit orders its own work; two do not.** A single node's teardown runs before its own activation, by design. Two separate Exits reacting to one publish are two independent subscribers with nothing sequencing them. Keep their targets disjoint, or put both wires on one Exit.

**Deactivating a questline that authored no teardown does nothing visible.** Naming a questline tag fires that graph's Entry-node *Deactivated* routing - and if the graph has no such wire, there is nothing to fire. The questline stops reading as running only once its Steps stop being active, so a graph that never wired its own shutdown will sit there with live Steps and no way in. Author the orange wire in the graph that may be closed, not in the graph that closes it.

**An Entry's Forward pin is local only.** It carries that Entry's own input onward. It does not re-emit the group, and an Entry never hears other Entries - including one on the same tag two nodes away. To receive a group signal you need an Exit, always.

**Listen broad, publish narrow.** A parent-tagged listener hears child publishes. A child-tagged listener does *not* hear parent publishes. Nothing warns you about the second case; the room simply never opens.

**Group tags are global.** No placement prefix, no asset scoping, no namespacing you did not type yourself. Two graphs that both use `SimpleQuest.ActivationGroup.Unlock` are wired together whether or not anyone intended it, and placing one graph twice gives you two listeners on one tag. Name them the way you would name a save key.

**The signal does not wait.** It is transient and unqueued. A listener whose instance does not yet exist at the moment of the publish never receives it, and nothing will tell you it was missed. If a group has to survive the gap between "sent" and "ready to hear it," what you actually want is a fact - which is Chapter 10's subject.

**Nothing catches a typo.** The two halves live in different assets, so the compiler cannot pair them up and has nothing to complain about: a misspelled tag on either end compiles clean and does nothing forever. Open the Examiner and look at the counts. `Setters (1)` and `Getters (0)` is the shape of a typo.

**Start Questline reports nothing back.** It is a fire-and-forget request, resolved asynchronously; its *Forward* pin fires immediately and carries no news about the questline it launched. If the outer graph needs to know how the other one ended, a group signal back is one answer, and Chapter 10's named condition is the other.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **A Step can be started, or torn down, by something that never names it.** Bridge 4 has no idea an activation group exists; neither does Bridge 1. Each has an input, something arrived at it, and everything after that is identical to a wire arriving.
- **A Step's *Deactivate* and *Deactivated* pins are ordinary signal pins.** The input accepts any output signal, the output fires on teardown and can drive the next one. A deactivation chain is built exactly like an activation chain - and the same two pins on a graph's *Start* node are how a whole questline says what shutting it down should mean.
- **A finished Step is not a wall.** A deactivation relays through a Step that has already completed, without disturbing it, and reaches whatever is wired beyond.
- **Activation arrives with its provenance intact regardless of route.** Params, origin chain, and event identity cross the portal unchanged, so a Step cannot tell - and does not need to tell - whether it was reached by wire or by signal.
- **A Step's address tells you what it is inside of.** `QuickStart.Chapter_9.Bridge_1` is a Step inside a placed chapter; `QL_Shortcut.Underpass` is a Step in a questline that is inside nothing. Read the tag and you have read the containment.
- **A questline is a thing you can close, not only a thing you start.** One node names it, that graph's own wiring decides what the teardown reaches, and its running state follows from its Steps rather than from any bookkeeping of yours.
- **Silence is a choice.** Bridges 1 through 3 carry no Display Data at all, and the room is better for it. Beats belong on the moments worth narrating, not on every Step you author.

Still to come: a condition with a name that anything can read (Chapter 10), and the Config Asset (Chapter 11).

---

## Try it

### Listen from somewhere else entirely:

Open `QL_Ch8_LinkedQuestlines` - the *finished* chapter, two rooms back. Drag in an **Activation Group: Exit**, set its *Group Tag* to `SimpleQuest.ActivationGroup.CrossingFound`, and wire its *Exit* pin into Start the Patrol's *Activate*. *Compile All*.

Play through to Chapter 9 and take the underpass. Chapter 8's Step starts again - in a chapter you completed, from a questline that has never heard of it, with no edit to the sender at all. That is the always-armed listener and the anonymous publisher in one move, and it is worth seeing once precisely because it looks like it should not be allowed.

Undo the edit and *Compile All*.

<img width="968" alt="A second Exit added to Chapter 8's finished graph on the same tag, firing when the underpass is taken" src="https://github.com/user-attachments/assets/48612795-1baa-42db-b6fb-d1cc437e26a5" />

### Invert the hierarchy:

Two property edits. In `QL_Shortcut`, change the Entry's *Group Tag* from `...CrossingFound.Underpass` to `...CrossingFound`. In `QL_Ch9_ActivationGroups`, change both Exits' *Group Tag* from `...CrossingFound` to `...CrossingFound.Underpass`. Both halves still name tags in the same family, and the pairing now runs parent-to-child instead of child-to-parent. *Compile All*.

Play the room and take the underpass. The Hidden Passage completes and says its line - it published, exactly as before - and **nothing happens**: Journey's End stays dark and the bridge stays lit. No error, no warning, no log line saying anything went wrong. Open the Group Examiner on either tag and you can see why: the endpoints are on different tags, and the delivery only runs downward.

Put all three tags back and *Compile All*.

<img width="502" alt="The graph with the Entry on the parent tag and both Exits on the child" src="https://github.com/user-attachments/assets/b9a80f66-5c41-401f-89e9-e9151cb4802f" />

<img width="1200" alt="The Underpass completed, Journey's End dark, and no Journey's End beat on the HUD" src="https://github.com/user-attachments/assets/7df9372f-ab67-41c0-9029-0f2d8a7f1442" />

### Let the cascade run one link too far:

In `QL_Ch9_ActivationGroups`, wire Bridge 3's *Deactivated* output into Bridge 4's *Deactivate* input - the one wire the chapter deliberately leaves off - so the teardown chain runs the whole length of the bridge. *Compile All*.

Play the room and take the underpass. Now one publish both opens Bridge 4 and, by way of three relays, closes it - and the result is worth looking at carefully, because **the HUD and the room end up telling you different things**.

The *"The far side beckons"* beat **prints**. Journey's End genuinely activated: the opening Exit reached it, it went Live, and a beat fires on that transition. Then the teardown arrived, and it arrived last - so the beacon stands dark and will not take a touch. The room is closed and the HUD is announcing it as open.

That is not a display bug. The beat is an honest record of a transition that really happened and was then undone a moment later, by a second subscriber to the same signal with nothing sequencing the two. Compare it with the previous Try it, where the beacon is also dark but the HUD says nothing at all: there, nothing was ever delivered. Here, everything was delivered, twice, in an order the graph does not fix.

The graph says so too, if you leave it open while you play: Bridge 4 ends the exchange wearing the **grey halo**, which is the overlay reading its `Deactivated` fact. The orange wire you just drew runs into it. Two instruments, one answer - and a HUD line insisting otherwise a few inches away.

Watch `LogSimpleQuestActivation` at Verbose and you can read the whole thing - the publish, both receipts, the three teardowns, the activation - and see the order for yourself.

That is the argument for the missing wire, and for the gotcha above it: keep two Exits' targets disjoint, or hang both wires off one Exit, where the framework guarantees the teardown goes first and the opening is what survives.

Delete the wire and *Compile All*.

<img width="1200" alt="During play - Bridge 3's Deactivated wired into Bridge 4's Deactivate, and Bridge 4 wearing the grey Deactivated halo" src="https://github.com/user-attachments/assets/47cefc89-11e5-4e80-a2d6-ea50ebb22b73" />

<img width="1200" alt="The Journey's End beat on the HUD with the beacon dark beneath it" src="https://github.com/user-attachments/assets/1dbc7457-7b96-4a49-b0c0-6f1751640423" />

---

Previous: [Chapter 8 - Linked Questlines](08_LinkedQuestlines.md) | Next: Chapter 10 - Prerequisite Rules
