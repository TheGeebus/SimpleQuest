# Chapter 4 - Sequential Steps

*Activation alone does not enforce order - wiring does.*

Chapter 3 put an actor between a Step and its start. This room puts a Step between a Step and its progress. Three Steps, three beacons labeled *Step One*, *Step Two*, and *Step Three*, and one wire that carries no activation at all: a dashed line from the second Step into the third. When the first Step completes, the other two start together, and the third one is running, lit, and touchable - and refuses you until the second is done. That refusal, and where it comes from, is the chapter.

**By the end of this chapter you can name:** the Prerequisites pin, a dashed wire and what it does not carry, PROGRESS REFUSED, the two Prerequisite Gate Modes, and why a Step with an unmet prerequisite is Live anyway.

---

## In the room

In order:

1. Chapter 4 activated as you finished Chapter 3, so on the way in two beats have printed - the chapter's, then the first Step's:

   > This chapter has THREE steps. Completing each step ACTIVATES the next, and some steps may have to wait on others to complete first.

   > Step One is now STARTED. Interact with the trigger volume to start the next steps.

   One beacon is lit, the one labeled *Step One*. The other two are dark.

<img width="1200" alt="The room on entry: the chapter's beat and Step One's STARTED beat on the HUD, the Step One beacon lit, the other two dark" src="https://github.com/user-attachments/assets/ca1b06b4-ff20-4ae2-8f75-43f05d0ff2fe" />

2. Touch it. Step One completes, and two Steps start on the same touch - three beats, in this order:

   > Step One complete. Its outcome ACTIVATES both Step Two and Step Three, but Step Three is waiting on Step Two.

   > Step Two is live (STARTED). Completing it will unblock Step Three.

   > Step Three is active, but a prerequisite is blocking it. Try the trigger to see what happens, or come back after completing Step Two.

   Both remaining beacons light. Both Steps are Live. Nothing in the room says the third one is gated except the beat.

<img width="1200" alt="After Step One: its completed beat, Step Two's and Step Three's STARTED beats, both remaining beacons lit" src="https://github.com/user-attachments/assets/9caf038f-3fa9-4ccf-b3d5-814292da0169" />

3. Take the beat's dare and touch *Step Three*. The beacon plays the denied sound and turns its blocked color, and a beat you have not seen before prints:

   > Step 2 hasn't been completed yet. That step's outcome is what allows this one to proceed.

   Step off the beacon and it returns to lit. Nothing about the Step changed - it is still Live, still armed, still waiting.

<img width="1200" alt="Touching Step Three early: the PROGRESS REFUSED beat on the HUD, the beacon in its blocked color" src="https://github.com/user-attachments/assets/3ba1cd1b-5342-4285-a99c-992eda3e385f" />

4. Touch *Step Two*.

   > Step Two complete. Step Three is no longer blocked.

   Nothing visible happens to *Step Three*'s beacon. It was lit before and it is lit now. What changed is a fact.

<img width="1200" alt="After Step Two: its completed beat, Step Three's beacon still lit" src="https://github.com/user-attachments/assets/f01e91c6-860e-4d4e-a267-137701fd03fd" />

5. Touch *Step Three* again. This time it completes, and the chapter with it:

   > Step Three completed.

   > All steps complete. Chained activation lets you build branching and gated content within a single questline graph.

   Chapter 5's door opens. Chapter 5 just activated.

<img width="1200" alt="After Step Three: its completed beat and the chapter's, Chapter 5's door opening" src="https://github.com/user-attachments/assets/6962399c-7aa0-43af-99da-3388456a1f31" />

The in-world tell is the refusal: a lit beacon that answers with the denied sound and its blocked color, then goes back to lit when you step off. The beats say "blocking" and "unblock" in the everyday sense; the framework has a state actually called Blocked, and it is a different thing, with its own room (Chapter 6). What holds Step Three here is a prerequisite.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/04_SequentialSteps/QL_Ch4_SequentialSteps`. Three Steps and an Outcome:

**Start → First → (Any Outcome) → Second and Third, together → Third → Outcome (Reached)**, plus one dashed wire: **Second's *Any Outcome* → Third's *Prerequisites* pin**.

- All three Steps host `OBJ_InteractWithTarget`; the beacons complete them on *Reached*.
- First's *Any Outcome* has two solid wires out of it, into Second's *Activate* and Third's *Activate*. Both start when First completes.
- Second's *Any Outcome* also has a wire out, and it is dashed. It runs into the *Prerequisites* input on Third - the pin under *Activate* that Chapters 1 through 3 never used. A dashed wire carries no activation - it adds a condition.
- Third continues from *Any Outcome* into the Outcome node. Second continues nowhere: its completion is consumed entirely by the condition it feeds.

<img width="1200" alt="QL_Ch4_SequentialSteps: the whole graph with both comment boxes, First's two solid wires, the dashed wire from Second's Any Outcome into Third's Prerequisites pin" src="https://github.com/user-attachments/assets/185bad47-f82e-4ed9-a516-e25978134028" />

**Two comment boxes. In short:**

- *Over the graph's left half* - what a connection means in a Questline Graph is decided by the input pin, not the output. Every content node's outputs are Completion Path pins - wired to an activation input they make a solid wire that carries activation, and the yellow ones are mutually exclusive while the white *Any Outcome* always fires.
- *Over the right half* - Steps can gate one another's progress. Second and Third both activate when First completes - the dashed wire from Second into Third's *Prerequisites* pin forms a relationship that carries no activation and adds a condition: Second must complete, on any outcome, before Third can progress. Then the two Prerequisite Gate Modes, explained below.

### Things worth clicking:

<br>
  <img width="1017" alt="Third selected: the Details panel with Prerequisite Gate Mode on Gate Progression" src="https://github.com/user-attachments/assets/0d14a007-0753-4d3a-8d8d-a5d7e54d55da" />
<br>

- **Select Third.** In the Details panel, *Prerequisite Gate Mode* reads *Gate Progression*, and it is what it reads on every Step in the tutorial - the other setting is described in the comment and explained below, and the *Try it* switches to it.

<br>
  <img width="1019" alt="Second selected: Resettable Replay set to Enabled" src="https://github.com/user-attachments/assets/2858655f-3131-4e7d-be5f-2529385c31fa" />
<br>

- **Select Second.** *Resettable Replay* is set to *Enabled* rather than *Inherit*. Second is the node whose completion the dashed wire reads, and being resettable is what makes that reading per-run, so the gate closes again when the room is replayed. The master graph also declares itself resettable, and every child may choose to simply inherit its parent's setting. This override is not generally needed unless the parent and child must differ. 

<br>
  <img width="921" alt="DA_Ch4_Third: Started Beats, Progress Refused Beats, and Completed Beats Default filled" src="https://github.com/user-attachments/assets/52c750fe-ac72-496a-8a21-b94d5f222bb2" />
<br>

- **Open `DA_Ch4_Third`.** Three arrays: *Started Beats*, *Completed Beats Default*, and one new one, *Progress Refused Beats* - the line that printed when the beacon refused you. `DA_Ch4_First` and `DA_Ch4_Second` fill only Started and Completed, and none of the three fills *Activated Beats* - for a Step with no giver, ACTIVATED and STARTED land in the same instant, and one line is enough.

- **Drag a second wire into Third's *Prerequisites* pin.** It refuses: *This prerequisite input already has a connection. Use an AND or OR node to combine conditions.* Those nodes, and NOT, are Chapter 7.

---

## Under the hood

### A Step with an unmet prerequisite starts anyway

Chapter 1 said the master graph activates every chapter at once and holds each behind a prerequisite. That held Chapter 2 *back*: it did not start until Chapter 1 completed. The prerequisite in this room does something different. Third starts the moment First completes - `.Live`, `.Started`, STARTED, its Objective instantiated, its beacon armed - with the prerequisite unmet.

The difference is what kind of node the prerequisite is on. A Quest or Linked Questline node with an unmet prerequisite defers its activation: nothing inside it activates until the condition holds. A Step with no giver activates regardless and lets the prerequisite gate what comes next - its progress or its completion, by its *Prerequisite Gate Mode*. A Step waiting on a giver is the exception: there the prerequisite gates the give (Chapter 3's ENABLED), and the manager skips the progression gate because the give already checked. The rule underneath all three: a prerequisite gates the earliest thing that can be gated. On a container that is activation. On a Step that is already running, it is the next fire.

### What the dashed wire compiles to

Wiring Second's *Any Outcome* into Third's *Prerequisites* pin does not produce a condition named "any outcome." It produces an **OR over every Completion Path Second declares** - one condition per yellow pin, three here, because `OBJ_InteractWithTarget` declares *Reached*, *Solved*, and *Procedural*:

| Condition                          | Reads the fact                                                  |
|------------------------------------|-----------------------------------------------------------------|
| Second completed on *Procedural*   | `SimpleQuest.State.QuickStart.Chapter_4.Second.Path.Procedural` |
| Second completed on *Reached*      | `SimpleQuest.State.QuickStart.Chapter_4.Second.Path.Reached`    |
| Second completed on *Solved*       | `SimpleQuest.State.QuickStart.Chapter_4.Second.Path.Solved`     |

You never see the three as three: the graph shows one wire, and the Prerequisite Examiner shows one condition, because both present what you authored and the compiler does the expanding. Wire a single yellow pin into *Prerequisites* instead and the compiled condition is one: that node completing on *that* path and no other. The `.Path.<Outcome>` facts are a fourth kind of lifecycle fact, written when a resettable node resolves, one for the path it resolved on. They sit in the World State view beside `.Live`, `.Started`, and `.Completed`, and because Second is resettable, its path fact is per-run: replaying the room clears it, and the gate on Third closes again. A node that is not resettable has no per-run fact to clear, and a condition on it reads the permanent resolution record instead - satisfied forever once it has happened once.

### The fire that gets refused

Chapter 1 gave the three fates of a fire: reaches the Objective, refused, or dropped. This room is the middle one. Touching *Step Three* before *Step Two*:

1. The beacon's Trigger Component finds the Step Live and publishes the fire on the Step's tag, exactly as it did for a free Step.
2. The quest manager, which sits between the fire and the Objective, checks the Step before handing it over: is it Blocked, and - for a Step on *Gate Progression* with a prerequisite - does the prerequisite hold? Here it does not. The manager publishes **PROGRESS REFUSED** on the Step's tag, with the reason (*Prereq Unmet*) and the list of unsatisfied conditions, and stops. The Objective never sees the fire. Nothing is written, and nothing counts.
3. The HUD hears it and prints the Step's *Progress Refused Beats*.
4. The beacon hears it on its own *On Quest Trigger Blocked* - filtered to its own fire, so a second beacon watching the same Step would stay quiet - and plays the denied sound and shows its blocked color. Stepping off runs its end-overlap logic, which puts an unsatisfied beacon back to lit.
5. The refusal is also recorded in the Quest State Subsystem's refusal history, which is what the graph's PIE overlay reads to flash the node.

Touch *Step Two* and its completion writes `...Second.Path.Reached`. Nothing else happens to Third - no event, no beat - but the next fire at *Step Three* passes the check and reaches the Objective, which completes it on *Reached* as in every room so far. From there the completion runs as Chapter 1 described: *Any Outcome* into the Outcome node, the chapter resolving with `Reached`, and in the master graph the Chapter 4 node's *Any Outcome* satisfying Chapter 5's prerequisite.

### The two gate modes

**Gate Progression** is what every Step in the tutorial uses and what you just watched: while the prerequisite is unmet, fires are refused before the Objective, with PROGRESS REFUSED going back to whoever fired. The player has to fire again after the condition is met. Nothing accumulates.

**Gate Completion** lets the fire through. The Objective processes it normally - it can count, progress, satisfy the trigger, and complete - and only the *consequences* of completing wait: the Step's `.Completed` fact, its COMPLETED event, its Outcome node and Completion Path wires, all held until the prerequisite holds, then released at once. If the Objective has already completed when the condition lands, the Step completes in that instant, with nobody touching anything. It is the mode for "collect the pieces in any order, but the door opens only after the guard leaves": the collecting is real when it happens, the payoff waits.

The setting is per Step, in its Details panel, and it only means anything on a Step that has a prerequisite and no giver.

### What is watching

In the graph, during PIE, Third shows three things at once after First completes: the Live halo, because it is running; a **gating ring** outside the halo in the prerequisite wire's own color, because its condition is unmet; and, for a moment after a refused fire, a red pulse. The ring is evaluated live from the Step's expression, not read from any fact - which is why a Step gated this way shows one even though the *Prereq Status* tab in the Quest State view, which lists only Steps waiting on a giver, has no row for it.

Right-click Third and choose *Examine Prerequisite Expression*. The panel shows the expression the way you wired it: one condition box, its source Second and its outcome *Any Outcome*, with a faint tint for its live state - grey before Second has activated, amber while Second is running and the outcome is undecided, green once any of Second's paths has landed, and rust if Second ended without producing one. The three-way OR the compiler built from that wire stays underneath; the panel correlates on the source node and the pin you connected. Wire a named pin instead and the box names that path, and turns rust if Second ends on a different one. When a Step of yours refuses fires and you cannot see why, this is the panel.

**Where the Examiner is opened from:**
<img width="1200" alt="The right-click menu on Third, with Examine Prerequisite Expression under its Prerequisite section" src="https://github.com/user-attachments/assets/95dd718b-a218-4fbf-b4b0-18f2a529be67" />

**The graph after Step One - two Live halos, one gating ring:**
<img width="1200" alt="The graph during PIE after Step One: Second and Third with Live halos, Third with the gating ring outside its halo" src="https://github.com/user-attachments/assets/ad81e2fd-2616-438f-9465-e101dfea1c8c" />

**The Prerequisite Examiner while Second is running:**
<img width="1200" alt="The Prerequisite Examiner pinned on Third while Second is running: one condition box, Source Second, Outcome Any Outcome, in its in-progress tint" src="https://github.com/user-attachments/assets/07ff5eeb-1b34-48ba-9ce4-333ea1887452" />
*(note that 'Third' shows as pinned in the Prerequisite Examiner panel)*

**The Prerequisite Examiner after Second completes:**
<img width="1200" alt="The Prerequisite Examiner after Second completes: the same box in its satisfied tint" src="https://github.com/user-attachments/assets/d3b0386c-fe85-496c-a604-6b1f47500390" />

**The fact the gate reads:**
<img width="1200" alt="World State view filtered to Chapter_4.Second after Step Two: the .Path.Reached fact beside .Completed and .Started" src="https://github.com/user-attachments/assets/85fadd26-336c-42a3-9445-37cf8550dea9" />

---

## Gotchas

**Activating two Steps together does not order them.** First's *Any Outcome* starts Second and Third in the same instant, and without the dashed wire the room would accept them in either order. Activation flow says *when a node starts*; it says nothing about which of two started nodes must finish first. Order is a prerequisite, and a prerequisite is a wire into the *Prerequisites* pin. If two Steps of yours are being completed out of order, look for the dashed wire that is not there.

**A dashed wire out of a node does not mean the node goes anywhere.** Second's only outgoing wire is the dashed one, so Second's completion activates nothing. That is correct here - Third was already started by First - but a node whose *only* exit is a prerequisite wire is a node that ends its own branch. Wire its *Any Outcome* to something if you expected it to continue.

**The Objective is not consulted on a refused fire.** Under *Gate Progression* the manager refuses before *Try Complete Objective* runs. An Objective that counts fires counts nothing; one that logs on every fire logs nothing. If your Objective "isn't seeing the trigger," check for a gating ring before you debug the Blueprint.

**The refusal goes to the actor that fired, and to the tag.** The beacon reacts because it is the one that fired. Every observer of the Step's tag hears PROGRESS REFUSED too - the HUD prints from it - and it carries the unsatisfied conditions, so a UI can say what is missing instead of only that something is. The example HUD prints the authored line.

**The condition is on the path, not on the Step.** "Second completed" and "Second completed on *Reached*" are different conditions, and the dashed wire's source pin decides which you authored. From *Any Outcome* the gate opens on any of Second's paths. From the *Reached* pin it opens only on that one, and a Second that ends on *Solved* would leave Third refusing forever, with no wire in the graph pointing at the reason. Chapter 5 is where an Objective ends more than one way; read its Path pins with this in mind.

**Replay reads the per-run fact.** Play the room again from its button and Third refuses again until Second completes again. That is Second's *Resettable Replay* at work: its `.Path.Reached` is cleared when the chapter re-activates. A prerequisite on a node that is not resettable reads the permanent record, and a condition that was met once stays met - which is what you want for "has the player ever done this," and not what you want for "has the player done this *this time*."

**One wire per Prerequisites pin.** The pin refuses a second connection. Two conditions are an AND node or an OR node with its output wired into the pin, and those nodes take as many inputs as you like (Chapter 7).

**The beats' "blocked" is not the framework's Blocked.** The chapter's text says Step Three is blocked and then unblocked, in the plain sense of the word. The Blocked state - `Set Quest Blocked`, `.Blocked`, the *Blocked* reason on a refusal - is a separate mechanism, and Chapter 6 shows it. If you read the beats as naming the state, the next room will confuse you.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **The Prerequisites pin** is the second input on the node face, under *Activate*: one wire, a condition rather than a flow. A yellow pin wired into it means "that node completed on that path", and *Any Outcome* wired into it means "that node completed at all," as an OR of its paths.
- **Prerequisite Gate Mode** decides what an unmet condition holds back on a running Step - the next fire (*Gate Progression*, the tutorial's setting everywhere) or the completion's consequences (*Gate Completion*). It means nothing on a Step with no prerequisite, and a giver-gated Step ignores it, because the give already checked.
- **A Step starts even when its prerequisite is unmet.** STARTED, `.Live`, the Objective instance, and the armed trigger all happen - only progress waits. Containers are the ones that hold their activation.
- **The Objective sees nothing on a refused fire.** Its *Try Complete Objective* runs only when the manager lets the fire through, so an Objective can assume that any fire it receives was allowed.

Still to come: an Objective you write yourself (Chapter 5), the Blocked state and a refused give (Chapter 6), the Deactivate pins and AND / OR / NOT (Chapter 7), and the Config Asset (Chapter 11).

---

## Try it

### Switch Third to Gate Completion:

Select Third in `QL_Ch4_SequentialSteps`, set *Prerequisite Gate Mode* to *Gate Completion*, and *Compile All* - the master too, as always. Play the room from its button, touch *Step One*, and then touch *Step Three* before *Step Two*. The beacon does not refuse you: it plays the success sound and shows its satisfied color, because the fire reached the Objective and the Objective completed. But no COMPLETED beat prints, Third's halo stays Live, and the chapter does not end. The completion happened and its consequences are being held. Now touch *Step Two*. Three beats land at once, and read them in order: *Step Three*'s first, then the chapter's, and only then *Step Two*'s. The held completion is released the instant `...Second.Path.Reached` is written, and that happens inside Second's own resolution, before Second's COMPLETED event goes out - the cascade is synchronous all the way down. Set the mode back to *Gate Progression* and *Compile All* when you are done.

The Output Log filtered to `LogSimpleQuestActivation` narrates it: `DeferChainToNextNodes: '...Chapter_4.Third' outcome='SimpleQuest.Outcome.Reached' ... - subscribed to 3 prereq channel(s)` when the early fire lands, then `TryFireDeferredCompletion: '...Chapter_4.Third' - prereqs satisfied, resuming chain` when *Step Two* completes.

**Step Three touched first under Gate Completion:**

<img width="1200" alt="Step Three touched before Step Two under Gate Completion: the beacon satisfied, no COMPLETED beat, Third's halo still Live" src="https://github.com/user-attachments/assets/ab17ae72-6188-468d-9a25-c3c67dd4f21d" />

**The log across the held completion:**

<img width="1200" alt="The Output Log filtered to LogSimpleQuestActivation: the DeferChainToNextNodes line, then TryFireDeferredCompletion when Step Two lands" src="https://github.com/user-attachments/assets/6edfa5dd-f7b2-4be2-a644-ee24b156bb38" />

### Gate on one path instead of any:

Undo the first experiment first. Then disconnect the dashed wire at Second's *Any Outcome* and reconnect it from Second's *Reached* pin into Third's *Prerequisites* pin, and *Compile All*. Play: nothing changes, because Second completes on *Reached*. Open the Prerequisite Examiner on Third and the difference is one word - *Outcome: Reached* where it read *Any Outcome*. Underneath, the compiled condition went from three paths to one, and that is invisible until an Objective ends on a path you did not gate on. Put the wire back on *Any Outcome* and *Compile All*.

---

Previous: [Chapter 3 - Basic Giver](03_BasicGiver.md) | Next: [Chapter 5 - Named Outcomes](05_NamedOutcomes.md)
