# Chapter 8 - Linked Questlines

*One authored route, two running instances, three addresses each.*

Act II opens with the same asset running twice at once. A Step starts two patrols; each patrol is a placement of one questline asset, `QL_PatrolRoute`, which knows nothing about this chapter or about the other placement. Clear the west side and the east side is exactly where you left it - same authored Steps, separate progress. That is the whole idea, and the work it takes in the graph is one node and a property.

The chapter also answers a question the earlier pages kept deferring: a Step has more than one address, and this is the room where the difference starts to matter.

**By the end of this chapter you can name:** a Linked Questline node and what makes it a placement rather than a copy, the tag prefix a placement puts on everything inside it, the three addresses one authored Step ends up with, which settings belong to the placement and which to the asset, and why two placements converge through a gate instead of racing to the Outcome node.

---

## In the room

In order:

1. Chapter 8 activated as you finished Chapter 7, so on the way in two beats have printed - the chapter's, then the Step's:

   > One Questline can be placed inside another. The asset is authored once, and each placement runs as its own instance with its own progress.
   >
   > This chapter places the same patrol route twice.
   >
   > Take a look at the Chapter 8 Questline graph to see one asset sitting in two places.

   > Touch the beacon to start two patrols - two distinct instances of one Questline.

   One beacon is lit. The rest of the room is dark.

<img width="1200" alt="The room on entry: the chapter's beat and Start the Patrol's beat on the HUD, one beacon lit and the rest of the room dark" src="https://github.com/user-attachments/assets/64d5e5a9-cf3b-4d90-bbfa-55bf64ea71d1" />

2. Touch it. Three beats, and four beacons light at once - two on the west side, two on the east:

   > One Step started two Questlines. The same route, running twice.

   > Patrol the West side of the room, touching each waypoint.

   > Patrol the East side of the room. Touch both beacons in order.

   Look at the sidebar: two entries named *Waypoint One*, live at the same time. They are not a display bug. They are two instances of one authored Step.

<img width="1200" alt="After touching the beacon: the Start the Patrol completed beat and both patrols' activated beats, four beacons lit in two pairs" src="https://github.com/user-attachments/assets/a1a42c12-7191-42f9-9d07-5279c1d801c1" />

<img width="753" alt="The sidebar with both patrols running: two entries named Waypoint One at the same time" src="https://github.com/user-attachments/assets/719666b6-3f7b-4395-a1ff-98a460a33395" />

3. Walk one side. Touch its first beacon:

   > Go to the waypoint.

   Then its second:

   > Completed.

   and the patrol reports in with a line of its own - *Western route completed* or *East side cleared*, depending which side you walked.

   Now walk to the far end of the room. The other patrol's two beacons are still lit, in the same order, waiting. Nothing you did on one side touched the other.

<img width="1200" alt="After clearing one side: that patrol's completed beat, the far side's two beacons still lit" src="https://github.com/user-attachments/assets/7605f083-797a-4d9a-a5a0-50657f23d78a" />

4. Walk the other side. Same two beats, word for word, because both patrols read them from the same asset - then that patrol's own closing line, and the chapter's:

   > Two placements, one authored route. Neither patrol ever knew the other was running.

   Chapter 9's door opens.

<img width="1200" alt="After the second side: the chapter's completed beat, Chapter 9's door opening" src="https://github.com/user-attachments/assets/e0d0a62a-f2dd-4560-99ab-3125390a3e90" />

The in-world tell is the far side of the room: four beacons in two pairs, and finishing one pair changes nothing about the other. The two waypoint beats are identical because they come from one Display Data asset; the two patrol beats differ because each placement carries its own.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/08_LinkedQuestlines/QL_Ch8_LinkedQuestlines`. One Step, two placements, and a gate:

**Start → Start the Patrol → (Any Outcome) → West Patrol and East Patrol**, both at once; each one's *Solved* pin, dashed, into an **AND combinator**, and the AND into a **Prerequisite Gate**'s *Prerequisites* pin; each one's *Any Outcome*, solid, into the gate's *Enter*; the gate's *Forward* into the **Outcome node (Solved)**.

- **West Patrol** and **East Patrol** are *Linked Questline* nodes. Each has one property that matters - *Linked Graph* - and both are set to the same asset, `QL_PatrolRoute`. From the outside they behave like the Quest containers of Chapter 7: one node, its own tag, its own lifecycle events, gateable by a giver or a prerequisite. The difference is where the inner graph lives and who else may use it.
- Their output pins come from the linked asset. `QL_PatrolRoute` resolves *Solved*, so every placement of it shows a *Solved* pin, plus *Any Outcome*. Add an Outcome node to the route and a pin appears on both placements; remove one and both lose it, along with whatever was wired to it.
- The ending goes through a gate for a reason the comment box states plainly: each placement publishes its own *Solved*, so wiring either one straight to the Outcome node would let the first patrol home end the chapter for both. The AND joins them, and the gate holds the activation until both have solved.

<img width="1200" alt="QL_Ch8_LinkedQuestlines, the whole graph: Start into Start the Patrol, its Any Outcome into both Linked Questline nodes, each node's Solved into the AND, the AND into the gate's Prerequisites, and Forward into the Outcome node" src="https://github.com/user-attachments/assets/a7281bec-5a6b-4e40-94ab-46db49d18832" />

**Now open `QL_PatrolRoute`** - or double-click either placement, which opens the same asset. Two Steps:

**Start → Waypoint 1 → (Any Outcome) → Waypoint 2 → (Any Outcome) → Outcome (Solved)**.

That is the entire route. It is an ordinary questline asset: it compiles on its own, it can be started on its own tag, and it contains no reference to Chapter 8, to West, or to East. The placements point at it; it does not point back.

<img width="1200" alt="QL_PatrolRoute, the whole graph: Start into Waypoint 1 into Waypoint 2 into the Outcome node tagged Solved" src="https://github.com/user-attachments/assets/d375c8e1-58cc-4565-b467-a6293ae221ef" />

**The comment boxes. In short:**

- *Over the placements* - a Linked Questline node places another questline asset inside this one, and it is a placement, not a copy: the graph lives in `QL_PatrolRoute`, and every placement reads from that one asset, so editing the route once changes both patrols. The placement's path prefixes everything inside it, so one authored Step lands on a different tag in each placement - the box spells out Waypoint 1's three addresses, which the next section walks through. Anything set on the placement node belongs to the placement; anything inside the route belongs to the asset.
- *Over the convergence* - the two patrols share an asset and nothing else. Each placement gets its own instance of every node inside the route, on its own tag, so clearing West Patrol's waypoints leaves East Patrol where it was. And the reason for the AND and the gate: either placement alone reaching the Outcome node would resolve the chapter early.
- *Inside the route, over the Steps* - this asset does not know where it is placed. Everything in the graph runs once per placement, so during Chapter 8 there are two live instances of Waypoint 1 and two of Waypoint 2 at the same time, each with its own state. Anything authored here has to read correctly in every placement, which is why the shared Display Data names no side of the room.
- *Inside the route, over the Outcome node* - the Outcome nodes in this graph become the output pins on every Linked Questline node that places it, and a placement's *Any Outcome* fires for whichever outcome resolves, the same as on a Step.

### Things worth clicking:

<br>
  <img width="1200" alt="West Patrol selected: the Details panel with Linked Graph set to QL_PatrolRoute, Node Label West Patrol, Display Name West Patrol, and Display Data DA_PatrolRoute_West" src="https://github.com/user-attachments/assets/a285fb64-1ab2-4d40-befd-e7b074e427b6" />
<br>

<br>
  <img width="1200" alt="East Patrol selected: the same Linked Graph, with a different Node Label, Display Name, and Display Data" src="https://github.com/user-attachments/assets/9130f404-bd7c-4adb-9005-5d9ffdad05ad" />
<br>

- **Select West Patrol, then East Patrol.** *Linked Graph* is the same asset on both. Everything else differs: *Node Label* (which becomes the tag segment), *Display Name*, and *Display Data* - `DA_PatrolRoute_West` on one, `DA_PatrolRoute_East` on the other, which is why their narration differs while the waypoints' does not. A placement left with empty Display fields falls back to the linked asset's own values.

<br>
  <img width="1200" alt="Waypoint 1 selected inside QL_PatrolRoute: Display Data DA_PatrolRoute_Waypoint, shared by every placement" src="https://github.com/user-attachments/assets/5f757158-48a9-4b2d-b4b9-4fd1a8762ea5" />
<br>

- **Select Waypoint 1 inside the route.** Its *Display Data* is `DA_PatrolRoute_Waypoint`, and both placements' Waypoint 1 *and* both placements' Waypoint 2 use it - one asset, four running Steps, one set of beats. Read them with that in mind: "Go to the waypoint" is deliberately free of any side, any order, and any chapter.

<br>
  <img width="437" alt="The Questline Outliner with Chapter 8 expanded: both placements and the four Steps beneath them" src="https://github.com/user-attachments/assets/559e1af8-613d-49d4-9422-a68662d91fb6" />
<br>

- **Open the Questline Outliner** with the chapter graph open. Chapter 8 holds Start the Patrol and two placements; each placement holds Waypoint 1 and Waypoint 2. Four Step entries, two authored Steps. The Outliner is showing you instances, which is the clearest picture of what a placement is.

- **Right-click the gate and examine it.** Two condition boxes, one per placement, each *Outcome: Solved*. During play they tint as the patrols finish.

---

## Under the hood

### A placement is a prefix

At compile, a Linked Questline node does not copy the route's nodes into this graph. It compiles the route's graph again under its own path. West Patrol's segment is `West_Patrol`, so the route's `Waypoint_1` compiles to `...Chapter_8.West_Patrol.Waypoint_1`; East Patrol's compiles to `...Chapter_8.East_Patrol.Waypoint_1`. Two instances, two tags, two sets of facts, two entries in the sidebar - from one authored Step.

Everything follows from that. The instances have separate `.Live`, `.Started`, and `.Completed` facts, separate path facts, separate resolution rows, and separate entries on the *Entries* tab. Their triggers are separate too: the level places four beacons, one per instance tag, and each beacon only ever hears its own.

The route's own wiring compiles per placement as well. Waypoint 1's *Any Outcome* into Waypoint 2 becomes two edges - West's into West's, East's into East's - because the compiler is walking the route once per placement with a different prefix each time. Nothing crosses.

### One Step, three addresses

Chapter 1 mentioned a Step has two addresses and moved on. Here it has three, and the comment box over the placements lists them:

| Address                                                                | Whose perspective                   |
|------------------------------------------------------------------------|-------------------------------------|
| `SimpleQuest.Questline.QL_PatrolRoute.Waypoint_1`                      | the route asset, authored alone     |
| `SimpleQuest.Questline.QL_Ch8_LinkedQuestlines.West_Patrol.Waypoint_1` | the chapter asset, authored alone   |
| `SimpleQuest.Questline.QuickStart.Chapter_8.West_Patrol.Waypoint_1`    | played inside the master questline  |

The last one is **canonical**: it is the address the runtime writes facts at, the one the Quest State view shows, and the one that identifies the node in every event's payload. The other two are **aliases**, and the framework publishes every one of that node's events on all three channels at once, as a single publish. Subscribe to whichever perspective your subscriber knows about and you hear the same event; a subscriber bound to a broad ancestor still hears it exactly once, because it is one publish across several channels rather than several publishes.

There is one asymmetry worth holding on to, and it is the reason this chapter exists: **the placement addresses are unique, and the asset address is not.** `...QuickStart.Chapter_8.West_Patrol.Waypoint_1` names one instance. `...QL_PatrolRoute.Waypoint_1` names *both* - it is the alias of two different running Steps at once. A subscriber bound to it hears west and east alike. A beacon listing it advances **both** patrols on one touch: the Trigger Component resolves the alias to every canonical behind it and publishes its fire on all of them, and each Live instance's own subscription receives it. That is occasionally what you want - one analytics hook, one actor that services any placement - and it is a bug when you meant one side of the room.

The editor reads the shared address too. A Step's halo in the route asset is resolved against `...QL_PatrolRoute.Waypoint_1`, so by default it shows both instances at once rather than either of them - which is why the graph editor's toolbar has a **placement picker**, described below.

The rule that falls out: **address a placement when you mean this one, address the asset when you mean any of them.** Chapter 6's Set Blocked node named the placed address for exactly this reason - it meant that Step, in that chapter.

### What belongs to whom

Placement or asset is the question to ask about every field in this chapter:

- **The placement owns** its *Node Label* (and therefore the tag segment), its *Display Name*, its *Display Data*, its *Prerequisites* pin, its Resettable Replay setting, and its wiring in the outer graph. West and East differ in every one of these that is set.
- **The asset owns** the Steps, their Objectives, their wiring, their Display Data, and the Outcome nodes that become the placement's pins. Change any of them once and every placement changes.

The chapter uses both sides on purpose. The waypoint beats are identical in both patrols because they come from the asset. The patrol beats differ because they come from the placements. If you find yourself wanting one placement's Waypoint 1 to say something the other's should not, the text has stopped being a property of the route and become a property of the placement - which is the signal to move it out, not to fork the asset.

### Two endings, one chapter

Start the Patrol's *Any Outcome* activates both placements in one cascade. Each placement activates its own Waypoint 1, and each route runs to its own Waypoint 2, whose completion resolves its placement with *Solved* - the boundary completion the inner Outcome node compiles to.

That resolution publishes on all three of the placement's perspectives, and writes `...Chapter_8.West_Patrol.Path.Solved`. The gate is subscribed to both placements' path facts through the AND, so the first patrol home flips one leaf and the gate keeps waiting; the second flips the other, the expression holds, and the gate forwards the activation it has been holding to the Outcome node. The chapter resolves *Solved*, and Chapter 9's prerequisite - an OR over Chapter 8's paths, under the same `NOT(ChaptersUnlocked)` every chapter carries - is satisfied.

Without the gate this room would end on whichever patrol finished first, with the other still lit and running. That is the shape to remember whenever two branches must both land: **fan out on activation, fan in on a prerequisite.** The graph has no "wait for both" wire, and does not need one.

### What is watching

During the patrols the two placement nodes wear Live halos, and they wear them separately: they are two nodes in this graph, with two compiled tags.

Open the route itself and there is one picture, not two. The overlay resolves an editor node to the tag of the asset it is sitting in - `...QL_PatrolRoute.Waypoint_1` - and that address belongs to both instances, so the halos read merged: a Step is Live when either patrol is on it. Entering the route through West Patrol rather than East makes no difference, because it is the same node in the same asset either way; the path you took to open it is not part of the question.

The **placement picker** at the right-hand end of the graph editor's toolbar is how you ask a narrower question. It lists the placements of the open asset that are running right now - here *West Patrol* and *East Patrol* - and picking one narrows every reading in that graph to that instance: the halos, the gating rings, the refusal pulses, and the Prerequisite Examiner's tints. It reads *All placements* by default, which is the merged view and the only thing an asset placed once can mean, and it resets when play ends. It is the same instrument as the Blueprint editor's debug object dropdown, asking the same question: which of these running copies am I looking at?

**Three readings of one moment.** From a fresh run, touch the beacon to start both patrols, then touch **the first west beacon and nothing else** - one waypoint, on one side. West Patrol is now on Waypoint 2 and East Patrol is still on Waypoint 1, and the room will hold there until you touch something. Open `QL_PatrolRoute` and change nothing but the picker:

- ***All placements*** - **both** waypoints wear Live halos at the same time. The route cannot be in that state: it runs Waypoint 1 *then* Waypoint 2, and nothing walks two Steps at once. You are looking at East's Waypoint 1 and West's Waypoint 2, painted onto the same two nodes, because the asset address belongs to both instances and Live outranks Completed when the merge has to pick one.
- ***West Patrol*** - Waypoint 1 Completed, Waypoint 2 Live. The sequential route, exactly as authored.
- ***East Patrol*** - Waypoint 1 Live, Waypoint 2 dark. The same two nodes, one waypoint behind.

Two of those three are a picture of something real. The default is the one that isn't, and that is the whole argument for the dropdown.

The World State view filtered to `Waypoint_1` says the same thing in numbers. Take it at the symmetric moment - right after Start the Patrol, both patrols on Waypoint 1: both instances' facts at their placement addresses, and the `QL_PatrolRoute.Waypoint_1` spellings beside them with a **Count of 2**. Chapter 1's "a fact is a count, not a flag" pays off here - the count at the asset address is the number of placements currently asserting it, so the moment the three readings above are taken at has `...QL_PatrolRoute.Waypoint_1.Live` down to 1, with `...Started` still at 2. That is the merged halo in numbers: one instance still on the Step, one already past it, one address holding both.

The *Entries* tab shows both placements starting from the same source, `Start_the_Patrol`, on the same outcome. The *Resolutions* tab at the end shows each patrol's *Solved*, plus the route asset's own identity resolving twice - once per placement. Type `Solved` into the panel's filter to see it clearly: the filter reads the Outcome column as well as the Quest column, so an outcome name isolates the **boundary** completions - the two placements, the chapter, and `QL_PatrolRoute` twice - while the Steps inside, which resolve on their Objectives' own outcomes, drop out. Filter by a tag fragment instead and you isolate an address family; filter by an outcome and you isolate an ending.

**The outer graph during the patrols:**

<img width="1200" alt="The outer graph during PIE with both patrols running: both Linked Questline nodes wearing Live halos" src="https://github.com/user-attachments/assets/8341c40b-c5bb-45d9-8609-a66683acaa5f" />

**The route asset with the picker on its default, one waypoint into the west patrol - both waypoints Live at once:**

<img width="1200" alt="QL_PatrolRoute with the picker on All placements, after the first west beacon only: both waypoints wearing Live halos at once" src="https://github.com/user-attachments/assets/f0c847c4-b704-4926-bb96-2ec199bb008d" />

**The same graph, the same moment, West Patrol picked:**

<img width="1200" alt="The same graph with West Patrol picked: Waypoint 1 Completed, Waypoint 2 Live" src="https://github.com/user-attachments/assets/7dc0321d-3b9a-4c04-8082-5cea0b719c8d" />

**The same graph, the same moment, East Patrol picked - the only thing that moved is the dropdown:**

<img width="1200" alt="The same graph with East Patrol picked: Waypoint 1 Live, Waypoint 2 dark" src="https://github.com/user-attachments/assets/7f2ca4bc-4f8a-4458-9353-e52827efda29" />

**Both instances of one authored Step:** - note that both the West and East Patrols have their own states - the West Patrol records a completion on Waypoint 1 and the East Patrol does not. The QL_PatrolRoute questline also recorded two independent STARTED events: one for each placement.

<img width="1200" alt="World State view filtered to Waypoint_1: both placements' facts side by side, and the QL_PatrolRoute.Waypoint_1 spellings beside them with a Count of 2" src="https://github.com/user-attachments/assets/178b2b3b-cb31-4c45-a52a-21b33f08d530" />

**Where each patrol came from:**

<img width="1200" alt="Quest State view, Entries tab: the two placement rows, both with Source Start_the_Patrol" src="https://github.com/user-attachments/assets/1172bab4-2961-4337-8643-b2db5dbef7ff" />

**How each patrol ended:**

<img width="1200" alt="Quest State view, Resolutions tab filtered to Solved: both patrols' Solved rows, and the QL_PatrolRoute rows beside them resolving twice on one tag" src="https://github.com/user-attachments/assets/f06bdc2d-4e7d-4bd8-9d4c-e46be9311e8e" />

---

## Gotchas

**Editing the route edits every placement.** That is the point of a placement, and it is the thing to remember before "just tweaking" a linked asset used in six rooms. If a change is right for one placement only, it belongs on the placement - a Display Data override, a different prerequisite, a different label - or the route needs splitting.

**The asset address names every instance at once.** A trigger, a giver, or an observer pointed at `...QL_PatrolRoute.Waypoint_1` speaks to both patrols - and in the trigger's case advances both, from one touch. Useful deliberately, confusing by accident. When a component of yours reacts twice, or advances a Step on the far side of the room, check which perspective its tag is. The debug halos read that address as well, so a linked asset opened during play shows its placements merged until you pick one in the toolbar.

**A questline cannot place itself.** Directly or through a chain, a cycle fails the compile with the whole chain named in the message - the error says the closing link is valid in isolation and any link in the chain must go. For a runtime loop across assets, that is what Chapter 9's activation groups are for.

**Two placements finishing is not one event.** Each publishes its own completion. Anything that should happen once when both are done needs the fan-in this room uses - a combinator into a gate, or into the Prerequisites pin of whatever comes next.

**The sidebar will show the same name twice.** Two instances of one authored Step carry the same Display Name, because the name is on the asset. If your UI needs to tell them apart, the distinguishing text is on the placement, not on the Step - or the UI reads the tag rather than the name.

**A placement's pins come from the linked asset's Outcome nodes.** Delete an Outcome node in the route and every placement loses that pin and the wire attached to it, in every graph that places it - including ones you are not looking at. The compile will tell you, but the wires do not come back.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **A Step can be running more than once.** Two placements of one asset means two instances of every Step inside it, each with its own facts, its own trigger, its own sidebar entry, and its own place in the graph's halos.
- **A Step's identity is its tag, not its node.** The node is authored once; the tag is minted per placement, prefixed by the placement's label. Everything the framework records is keyed on the tag.
- **Three addresses, one node.** The canonical one is where the state lives; the aliases are the other perspectives the same event is published on. The placement's address is unique to one instance; the asset's names them all.
- **Display Data is asset-level unless the placement overrides it.** Shared text has to read correctly from every placement.

Still to come: a questline opening something it knows nothing about (Chapter 9), a named condition read by everything that needs it (Chapter 10), and the Config Asset (Chapter 11).

---

## Try it

### Place the route a third time:

Open `QL_Ch8_LinkedQuestlines`, drag in a Linked Questline node, set its *Linked Graph* to `QL_PatrolRoute`, and give it a Node Label - *North Patrol* will do. Wire Start the Patrol's *Any Outcome* into its *Activate*, add a third pin to the AND with *Add Condition Pin*, and wire the new placement's *Solved* into it and its *Any Outcome* into the gate's *Enter*. *Compile All*.

Play the room and touch the first beacon. The chapter now starts three patrols - and the third has no beacons, because nothing in the level lists its tags. Open the World State view and filter to `North_Patrol`: `Waypoint_1` is Live, waiting for a trigger that does not exist. The chapter cannot end, because the gate is waiting on a patrol nobody can walk.

That is worth seeing once: a placement is complete the moment you make it, and the level is a separate question. Undo the edit and *Compile All*.

**A third placement, wired in:**

<img width="1200" alt="A third Linked Questline node placing QL_PatrolRoute, wired from Start the Patrol and into the AND's third pin" src="https://github.com/user-attachments/assets/6384b803-aca2-4617-ba18-7a735e3e3430" />

### Make a cycle on purpose:

In `QL_PatrolRoute`, drag in a Linked Questline node and set its *Linked Graph* to `QL_Ch8_LinkedQuestlines` - the graph that places it. *Compile All* and read the Message Log: the compile fails, and the error names the whole chain rather than blaming the node you just added, because a cycle is a property of the chain. Delete the node and *Compile All*.

**The compiler refusing the cycle:**

<img width="1200" alt="The compiler's cycle error in the Message Log, naming the whole chain" src="https://github.com/user-attachments/assets/5ce20759-ce0c-417c-86ea-bf3838cd9a2a" />

---

Previous: [Chapter 7 - Prerequisites](07_Prerequisites.md) | Next: Chapter 9 - Activation Groups
