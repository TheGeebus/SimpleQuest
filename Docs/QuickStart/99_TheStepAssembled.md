# The Step node, assembled

*Eleven rooms, one node type, and the seven things they never showed you.*

Every chapter closed with a section called *The Step so far*. This page is those eleven sections put together, in the order you configure a Step, plus an honest list of what the tutorial left out.

There is no twelfth room. This page summarizes the Step node and applies to all Questline graphs.

---

## What a Step is

A Step is the only thing in SimpleQuest that completes. Containers derive their state from the Steps inside them, questlines derive theirs from their containers, and the whole framework is arranged around what one Step needs in order to start, to be fired at, to refuse, and to end.

Everything below is a property of that one node, or a thing in the world that reaches it.

---

## Identity

**The Node Label is the Step's identity.** The tag is built from it, and everything that watches or fires the Step addresses that tag. Rename the label and the tag changes - which is why Chapter 1 told you to compile before wiring an actor.

**A Step's identity is its tag, not its node.** The node is authored once; the tag is minted per placement, prefixed by the placement's label. A questline placed twice means two instances of every Step inside it, each with its own facts, its own trigger, and its own sidebar entry.

**Three addresses, one node.** The canonical address is where the state lives. The aliases are the same event seen from other graphs. A placement's address is unique to one instance; the asset's address names them all at once.

**A Step's address tells you what it is inside of.** `QuickStart.Chapter_9.Bridge_1` is a Step inside a placed chapter. `QL_Shortcut.Underpass` is a Step in a questline that is inside nothing.

**The display name and the node label are different fields for different audiences.** The log says *Catch-up Events*; the graph says `Back_to_Chapter_11`. Nothing enforces agreement, and the gap is useful: the label says what completes the Step, the display name says what it means.

---

## Completion

**The Objective Class is the completion logic, and it declares the Completion Path pins.** Change the class and the pins change with it. They are discovered from the class - its *Complete Objective With Outcome* calls, its `ObjectiveOutcome` properties, its `GetPossibleOutcomes` - and refreshed when it changes.

**A Completion Path is one way the Step can end.** The Objective picks it by the tag it completes with. The graph routes on it, or does not, pin by pin. An unwired path ends the branch and that is a legitimate shape.

**Complete Objective With Outcome ends the Step** - once per activation, and never with *Any Outcome*.

**Rewards are not on the Step.** They are nodes on its Completion Path pins, so which reward pays is decided by which path the Objective completed on. The Step's Details panel has no reward field at all.

**An activation carries its outcome.** The next Step's Objective can read which path started it, and the *Entries* tab shows it.

---

## Being started

**ACTIVATED and STARTED are two events**, and a giver is what puts time between them. The Objective is instantiated at STARTED, not before. Triggers arm at STARTED, not before. Between the two, the Step is Pending Giver.

**The Step has no giver property.** Whether it waits is decided outside the graph, by a Quest Giver Component in the world listing its tag. The node face reports what the level says.

**A Step can be started by something that never names it.** An activation group's Exit, another graph's wiring, a system calling *Activate Quest* by tag - the Step has an input, something arrived at it, and it ran.

**A Step is addressed by tag from outside.** A system that knows the tag can start it, stop it, watch it, and arm a Trigger Component against it at runtime - and cannot progress it. The framework records the difference as provenance.

**A Step's completion can be driven entirely from outside the graph.** Chapter 11's console fires a Trigger Component. The graph never knows what a dial is.

---

## Being gated

**The Prerequisites pin** is the second input on the node face: one wire, a condition rather than a flow. It takes a Completion Path pin, a Fact Tag node, a Prerequisite Rule Exit, or a combinator. AND and OR join as many as you like; NOT inverts one.

**A Step starts even when its prerequisite is unmet.** STARTED, `.Live`, the Objective instance, and the armed trigger all happen. Only progress waits. Containers are the ones that hold their activation.

**Prerequisite Gate Mode decides what an unmet condition holds back** - the next fire (*Gate Progression*) or the completion's consequences (*Gate Completion*). The tutorial uses Gate Progression everywhere, because it is the mode that talks back: the Step stays Live, the trigger stays armed, and a refused touch is an event you can narrate.

**A Step's prerequisite can name a condition instead of a node**, and a condition can stop being true. Prerequisites built on completions are one-way. Prerequisites built on rules are not, and a Step gated on one can close again after it opened.

**Nothing a Step reads has to know who wrote it.** The prerequisite is a tag, and tags have no authors.

---

## Being blocked, and being torn down

**Blocked is a state a Step can be in at any point of its lifecycle** - not started, waiting on a giver, Live - set and cleared from outside it. Nothing on the Step node configures it.

**Also Deactivate is the line between a block and an interruption.** Off, nothing in flight is touched.

**Show Deactivation Pins reveals *Deactivate* and *Deactivated*.** Deactivation ends a Step without completing it: `.Live` off, `.Deactivated` on, the triggers disarmed. Those pins are ordinary signal pins - the input accepts any output signal, and the output can drive the next teardown.

**A finished Step is not a wall.** A deactivation relays through a Step that has already completed, without disturbing it, and reaches whatever is wired beyond.

**A Step has a phase, not a pile of booleans.** Activated, Started, Completed, Deactivated - one answer, which is exactly what an observer's catch-up can rebuild.

---

## Talking to the Objective

**The trigger context is how the world talks to the Objective** - the actor, the counts, a *Custom Tag*, and *Custom Data*, none of which the framework interprets.

**A give carries a runtime context into the Objective**: the instigator, plus any custom data the giver attaches, merged with the Step's own defaults and delivered when the Objective activates.

**The Config Asset is how a Step carries data its Objective needs.** Two instances of one Config class give two Steps the same Objective with different expectations, and no Blueprint branching per Step.

**The Objective sees nothing on a refused fire.** *Try Complete Objective* runs only when the manager lets the fire through, so an Objective can assume any fire it receives was allowed.

**Refuse Trigger is the Objective's own no**: a *Refused* response to the actor that fired, with a reason tag you chose, and the Step still Live.

---

## Narration

**Display Name, Description, and Display Data are UI text, kept apart from identity.** A Step can carry its own or inherit the questline's.

**Display Data has one array per event**, and the ones you leave empty print nothing. *Completed Beats By Outcome* is keyed by the outcome tag, with *Completed Beats Default* behind it. *Give Blocked*, *Blocked*, and *Unblocked* have their own.

**Display Data is asset-level unless the placement overrides it.** Shared text has to read correctly from every placement.

**Silence is a choice.** Chapter 9's bridges carry no Display Data at all and the room is better for it. Beats belong on the moments worth narrating, not on every Step you author.

---

## Additional Concepts

The tutorial is extensive, but it is not a comprehensive look at every feature. 

The concepts below are not covered by a specific tutorial chapter. They are listed here so you know they exist and where to read about them.

- **Description.** The long-form text field beside *Display Name*. Every room's beats are short enough to live in Display Data, so nothing here ever filled it.

- **Target Actors.** An Objective can be handed specific actors to care about rather than discovering them. See [Reference implementations](../../README.md#reference-implementations) - `UInteractAllTargetsObjective` auto-discovers instead, which is what every room uses.

- **Target Classes.** The same idea by class rather than by instance. Nothing in the tutorial needed "any three of these."

- **Number of Elements**, and with it **counting objectives** and **Report Progress**. `UCountingQuestObjective` keeps a `CurrentElements` / `MaxElements` counter initialized from the Step's authored `NumElementsRequired`. Because no room has a counting objective, **no room ever fires a PROGRESS event either** - Chapter 11's screen names PROGRESS as a transient event and never shows one arriving. See [Progress and refusal](../../README.md#progress-and-refusal); the framework never auto-fires progress, so an Objective reports it deliberately.

- **Gate Completion mode.** The other half of *Prerequisite Gate Mode*. Chapter 4's *Try it* has you switch a Step to it and watch the difference, so you can meet it - but no room ships that way.

- **Custom Data.** Arbitrary payload riding a trigger context or a give, which the framework carries and never interprets. See [Completion and trigger-context forwarding](../../README.md#completion-and-trigger-context-forwarding).

- **Forward Params.** The same mechanism on the way out: `TriggeredActor`, `Instigator`, and `CustomData` auto-forward from the last inbound trigger context to the completion event unless you pass them explicitly. Same section as above.

---

## Where to go next

[Building your first progression](../../README.md#building-your-first-progression) walks the seven steps every room here was built with, in a project of your own.

[Objectives](../../README.md#objectives) is the one class you will subclass first - [the lifecycle contract](../../README.md#the-lifecycle-contract) for what the framework guarantees about when your code runs, and [when to write a custom Objective vs use a reference](../../README.md#when-to-write-a-custom-objective-vs-use-a-reference) for whether you need to at all.

[Reacting to quest events](../../README.md#reacting-to-quest-events) is Chapter 11's subject in reference form.

For questions, support, and more information about things the rooms did not answer, please go to the [Simple Quest Discord](https://discord.gg/PN9kzPypeS).

---

Previous: [Chapter 11 - Observers](11_Observers.md)
<br>
Index: [All chapters](README.md)

