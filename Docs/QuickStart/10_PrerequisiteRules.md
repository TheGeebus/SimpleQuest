# Chapter 10 - Prerequisite Rules

*A condition with a name, written in one room and read in another.*

Chapter 9 crossed an asset boundary with an activation. Chapter 10 crosses a *container* boundary with a condition - and the two chapters together say the thing neither says alone: every boundary in this framework is crossed the same way, by name.

A **Prerequisite Rule** is a condition that has been given a tag. One quest writes it. Another quest reads it. Neither knows the other exists, and nothing is wired between them.

There is also something here that has not appeared in nine chapters: **a fact that can go away again.** Everything the walkthrough has taught you to look for - `Started`, `Completed`, a path fact - only ever accumulates. A rule is a live boolean, and it is the first thing in the tutorial that can stop being true.

**By the end of this chapter you can name:** a Prerequisite Rule Entry and Exit and what passes between them, why a rule is a fact rather than a wire, how a rule differs from an activation group, what Gate Progression does to a Step whose condition is unmet, and why a rule can be retracted when a completion cannot.

---

## In the room

In order:

1. Chapter 10 activated as you finished Chapter 9, with two beats:

   > A Prerequisite Rule is a condition with a tag. It is written once, and anything that needs it - a step, a door, a light - reads it by that tag without knowing where it came from.
   >
   > Two keycards. The Security Wing is straight ahead.

   > Take a look at the Chapter 10 Questline graph. The rule is written inside one quest and read inside another, and nothing connects the two but the tag.

<img width="1200" alt="The room on entry: the chapter's two beats, the facility ahead" src="https://github.com/user-attachments/assets/3a4ed149-b4e1-469a-8afe-1fccc3fc7f02" />

2. **The Security Wing** is in front of you, and its beat tells you what you can already see:

   > The Red Keycard is in the Security Wing. You can see it through a window.

   The keycard is visible and out of reach. The door wants the terminal.

<img width="1200" alt="Through the window: the Red Keycard visible and unreachable" src="https://github.com/user-attachments/assets/bb28df99-5242-47f0-933c-5d10dd321b98" />

3. **Touch the terminal now**, before anything else. It refuses, and the refusal is the chapter in one line:

   > NO POWER. The terminal reads a condition named PowerOn, and right now it is false. Nothing in this room can change that. Whatever does is somewhere else.

   Note what did *not* happen. The beacon is still lit and still armed - the Step is Live and listening, and your touch reached the Objective and was turned away at the gate. The room told you the name of the thing you are missing and sent you to look elsewhere for it.

<img width="1200" alt="The terminal touched before the power: the NO POWER refusal beat and the denied cue" src="https://github.com/user-attachments/assets/8bcc958f-a1e7-4ad0-b472-31118ab7a69b" />

4. **The Supply Room.** Its beat:

   > The Blue Keycard is in here somewhere. You'll need to look around for it.

   There is a locker, and it is shut. There is a switch.

<img width="1200" alt="The Supply Room: its beat, the power switch, the locker shut" src="https://github.com/user-attachments/assets/fb9f5d5b-066d-4faa-b4e9-a1c957e2aeb9" />

5. **Throw the switch.** One action, and the facility changes around you:

   > Power on. This switch wrote one thing: PowerOn is true. The lights read it. The locker read it. Across the facility, a terminal read it.

   The lights come up. The locker opens - and the Blue Keycard is inside it:

   > Blue Keycard acquired. The locker opened when you turned on the power.

<img width="1200" alt="The moment the switch is thrown: the completed beat, the lights up, the locker open" src="https://github.com/user-attachments/assets/38d04fbf-6a3f-4a60-8fef-adb6852142ee" />

6. **Back to the terminal.** The same touch that was refused a minute ago now works:

   > The terminal accepts. The power is on now, a condition from inside another Quest.

   The door opens, and the Red Keycard is yours:

   > Red Keycard acquired. The door needed the terminal, which needed a condition from inside another Quest.

<img width="1200" alt="The terminal after the power: it accepts, and names the condition" src="https://github.com/user-attachments/assets/f8cad6e2-a1ce-46a7-a65c-62553a7843a8" />

7. With both keycards, the chapter closes:

   > The switch was in one room and the terminal in another. Nothing was wired between them. The condition had a tag, and that was enough.

<img width="1200" alt="Both keycards taken and the chapter's completed beat" src="https://github.com/user-attachments/assets/93be143f-7489-4ae0-99c6-6d356cff3ce6" />

The in-world tell is one switch changing three things at once - the lights, the locker, and a terminal in another room - with nothing running between them. The switch does not know about any of them.

Worth being precise about *how* each of them found out, because they did not all find out the same way. The terminal reads the **rule**: its prerequisite is a leaf on `SimpleQuest.PrereqRule.PowerOn`. The door and the locker are level actors with Observer components, and they watch the **Step** - `...Chapter_10.Supply_Room.Power_Switch` - reacting to its completion rather than to the condition the rule publishes. Same moment, same switch, two different things being listened to.

---

## In the graph

Open `SimpleQuest Content/QuickStart/Chapters/10_PrerequisiteRules/QL_Ch10_PrerequisiteRules`. Two containers and a gate:

**Start → Supply Room and Security Wing**, both at once; each one's *Any Outcome*, solid, into a **Prerequisite Gate**, with the gate's *Prerequisites* fed by an **AND** over both quests' *Any Outcome*; the gate's *Forward* into the **Outcome node (Solved)**.

That much is Chapter 7's vocabulary - two containers, fan out on activation, fan in on a prerequisite. What is new is inside them, and it does not appear in this graph at all.

<img width="1200" alt="QL_Ch10_PrerequisiteRules, the outer graph with all five comment boxes" src="https://github.com/user-attachments/assets/18b6f328-4e72-48fa-9675-ad983a18e3b0" />

**Double-click Supply Room.** Three Steps and something you have not seen:

**Start → Supply Entry → Power Switch and Blue Keycard**, and off to the side a **Prerequisite Rule: Entry**, with a dashed wire running into it from Power Switch. The Entry has a tag: `SimpleQuest.PrereqRule.PowerOn`.

That is the whole of the writing side. An expression goes in; a name comes out.

<img width="1200" alt="Inside the Supply Room: the Rule Entry and the dashed wire from Power Switch" src="https://github.com/user-attachments/assets/1f48f523-2bb0-46c2-a249-180f9b0c516f" />

**Now double-click Security Wing.** Three Steps and the other half:

**Start → Security Entry → Terminal → Red Keycard**, and a **Prerequisite Rule: Exit** carrying the same tag, its dashed wire running into **Terminal's *Prerequisites* pin**.

There is no wire between the two quests. There is no wire that *could* run between them - you are looking at two separate inner graphs. They meet at a tag and nowhere else.

<img width="1200" alt="Inside the Security Wing: the Rule Exit and its wire into Terminal's Prerequisites" src="https://github.com/user-attachments/assets/8264a1b7-f391-4746-a217-1cfe45f98382" />

**The comment boxes. In short:**

- *Over the two quests* - they share a condition that has no wire. The Supply Room writes a rule; the Security Wing reads it. Open either and you find one end but not the other; they meet only by name.
- *Over the gate* - a simple AND requiring both keycards in any order, at which point the questline completes.
- *Over the Quest nodes* - double-click to enter, and use the breadcrumbs or the Outliner to get back.
- *Over the Rule Entry* - it takes an expression and gives it a name. While the expression is true, `SimpleQuest.PrereqRule.PowerOn` is a fact in World State; when it goes false, the fact is removed. Here the expression is one leaf, but it can be any depth, and it re-evaluates whenever a leaf changes. And then the line this chapter turns on: **from the reader's side a rule and a fact are the same thing - a published tag.** The hub's *Unlock Chapters* button sets a fact from a Blueprint; this rule sets one from a graph; a door, a light, a Step's prerequisite, another graph's Rule Exit - none of them know or care which.
- *Over the Rule Exit* - it reads a named condition and passes it to a Prerequisites pin. The condition is written somewhere this graph cannot see, and the Exit does not know that; it knows a tag. Terminal is a plain Step, so its prerequisite gates *progression*: the beacon stays live and armed and every touch is refused with the reason. And the comparison worth having: **a group would ACTIVATE Terminal when the switch was thrown; a rule GATES it** - Terminal still has to be reached by its own quest, and have the condition as well.

### Things worth clicking:

<br>
  <img width="1157" alt="The Rule Entry selected: Group Tag SimpleQuest.PrereqRule.PowerOn" src="https://github.com/user-attachments/assets/ea0b27f2-c3a7-4fd8-becc-d5dcae2c1836" />
<br>

<br>
  <img width="1090" alt="The Rule Exit selected: the SAME Group Tag" src="https://github.com/user-attachments/assets/1c4b4d30-61df-4fc8-9c5a-e1a94d7377d2" />
<br>

- **Select the Rule Entry, then the Rule Exit.** One property each, and it is the same value: `SimpleQuest.PrereqRule.PowerOn`. That pair of panels is the chapter, the way Chapter 9's two identical Exits were. The difference is that these two are in *different graphs*, and neither panel gives you any way to find the other.

<br>
  <img width="1169" alt="Terminal selected: Prerequisite Gate Mode on Gate Progression" src="https://github.com/user-attachments/assets/cccc6873-c0fd-4471-84f6-43eb2aff51b4" />
<br>

- **Select Terminal.** *Prerequisite Gate Mode* is on **Gate Progression**, which is why the refusal reads the way it does. Chapter 7 introduced the setting and never showed the other half of it; this is it, with a condition the player has to leave the room to satisfy.

<br>
  <img width="1200" alt="The Prerequisite Gate examined: an AND over two boxes, each Source quest / Outcome Any Outcome" src="https://github.com/user-attachments/assets/97b17cad-21a3-4c53-8e70-6bb85a7116fd" />
<br>

- **Right-click the gate in the outer graph and examine it.** Two condition boxes, one per quest, and both read *Outcome: Any Outcome* - the gate does not care **how** each quest ended, only that it did. Each of these quests has exactly one ending, so the distinction costs nothing here and saves an edit the day one of them grows a second. During play the boxes tint as each keycard lands.

<br>
  <img width="412" alt="The Questline Outliner with Chapter 10 expanded: both Quests and three Steps under each" src="https://github.com/user-attachments/assets/f4508864-b8ec-493a-bdae-dbe87bd45517" />
<br>

- **Open the Questline Outliner.** Chapter 10 holds two Quests, each with three Steps. The rule appears nowhere in it - the Outliner lists nodes that have tags of their own, and a rule's tag names a *condition*, not a place in the tree.

---

## Under the hood

### A rule is a fact with a publisher

At compile, the Rule Entry becomes a `QuestPrereqRuleNode` holding two things: the tag, and the expression. At runtime it subscribes to every leaf its expression mentions and re-evaluates whenever one of them changes. The whole of its behavior is four lines:

- expression true and the fact is not present → **add the fact**
- expression false and the fact is present → **remove the fact**
- otherwise → do nothing

So `SimpleQuest.PrereqRule.PowerOn` is an ordinary World State fact, written by the framework instead of by a Blueprint. And the Rule Exit compiles to nothing exotic either: Terminal's prerequisite expression ends up holding a plain **fact leaf** on that tag - the same kind of leaf Chapter 7's Fact Tag node produces. By the time the compiler is done, there is no "rule" left in the data at all. There is a node that writes a fact and a Step that reads one.

That is why the comment box can say a rule and a fact are the same thing from the reading side. It is not a simplification for the reader; it is what the compiled graph contains.

### The first fact that can be taken back

Every fact the walkthrough has shown you so far accumulates. `Started` is an append-only anchor. `Completed` is append-only. A path fact is written when a node resolves and stays written. Chapter 1 taught you to read a fact as a count, and every count so far has only gone up.

**A rule is the exception, and it is deliberate.** The node checks the expression both ways, and retracts the fact when the expression stops holding. The source comment on the subscription explains why the subscriptions stay live for the rule's whole lifetime rather than unsubscribing once satisfied: a `NOT` expression needs re-evaluating whenever a leaf transitions, in either direction.

This is worth sitting with, because it changes what a prerequisite *is*. A prerequisite built on `Completed` can only ever become true. A prerequisite built on a rule can become true, then false, then true again - a door that locks when the power fails, a vendor that closes at night, a path that is only open while you are carrying something. The tutorial's rule happens to be monotonic, because Power Switch completing is permanent. The mechanism is not.

### Why a rule and not an activation group

Both cross a boundary by name. They are not interchangeable, and the comment box draws the line exactly:

|                        | **Activation Group** (Chapter 9)            | **Prerequisite Rule** (Chapter 10)                    |
|------------------------|---------------------------------------------|-------------------------------------------------------|
| What crosses           | an activation - a thing happening           | a condition - a thing being true                      |
| Effect on the far side | **starts** the node it reaches              | **gates** a node that still has to be reached         |
| Lifetime               | transient; a listener not present misses it | a fact; anything can read it at any later moment      |
| Reversible             | no - an activation has happened             | yes - the fact is retracted when the expression fails |

Put a group where this chapter puts a rule and the terminal would activate the moment the switch was thrown, whether or not the player had ever found the Security Wing. The rule leaves the quest's own structure in charge of *reaching* Terminal and adds a second condition on top.

### What the player is told, and why

Terminal's *Prerequisite Gate Mode* is **Gate Progression**, so the Step stays Live and its trigger stays armed. A touch reaches the Objective, the gate refuses it, and the refusal carries the reason and the unsatisfied leaf - which is what lets the room put a *Progress Refused* beat on the HUD naming the condition by name.

That is a deliberate piece of design worth copying: the player is not told "nothing happens here." They are told which condition is false, and that it is false somewhere else. Chapter 7's prerequisites gated things the player could see; this one gates on something in another room, so the refusal has to carry the information or the room becomes a guessing game.

### What is watching

<br>
  <img width="1200" alt="World State filtered to PrereqRule before the switch: no rows at all" src="https://github.com/user-attachments/assets/14bfe693-9b36-40f2-837e-c66cef40ed70" />
<br>
  <img width="1200" alt="World State filtered to PrereqRule after the switch: PowerOn, Count 1" src="https://github.com/user-attachments/assets/8428a6ea-752d-4946-827b-fe5533dc52a8" />
<br>

- **World State**, filtered to `PrereqRule`, is the clearest instrument in the chapter: **no rows at all before the switch, and one row after.** The tag does not exist until the rule publishes it. These two shots show the same filter before and after the power switch. This is the whole mechanism underneath a Prerequisite Rule.

<br>
  <img width="414" alt="The Prerequisite Examiner pinned to Terminal before the power: the PowerOn leaf red" src="https://github.com/user-attachments/assets/662fa627-0997-419c-bdf8-73d692848d22" />
<br>
  <img width="414" alt="The same Examiner after the power: the leaf green" src="https://github.com/user-attachments/assets/a5c9816b-25a5-43bc-867f-61a9c2929eed" />
<br>

- **The Prerequisite Examiner** pinned to Terminal shows the PowerOn leaf red before the switch and green after - one condition flipping, with nothing else in the panel changing.

<br>
  <img width="1200" alt="The graph during PIE before the power: Terminal with its Live halo and the purple gating ring" src="https://github.com/user-attachments/assets/0131c8ab-2238-489e-9ae1-76982bb25f37" />
<br>

- **The graph overlay** before the power shows Terminal wearing its Live halo *and* the purple gating ring at once, which is the visual statement of Gate Progression: running, and refusing.
- **The rule itself does log**, once you ask for it. Run `Log LogSimpleQuestActivation Verbose` in the console, then throw the switch: `QuestPrereqRule 'SimpleQuest.PrereqRule.PowerOn' published (expression satisfied)`. Should the expression ever go false again you get the matching `retracted (expression no longer satisfied)`. Both lines fire on the **transition** only - a rule that is already published stays quiet, which is why the log is a record of the two moments that matter rather than a running commentary.

<br>
  <img width="1099" alt="The Output Log on a refused touch at Verbose: the refusal line naming Terminal and the unsatisfied PowerOn leaf" src="https://github.com/user-attachments/assets/06b5dfcc-17fb-485f-8a2f-256a7760b71c" />
<br>

- **The refused touch logs too, and names what is missing.** With that same category on, walk into Terminal before the power: `CheckQuestObjectives: '...Security_Wing.Terminal' refused a trigger - prerequisite gate is unmet. Unsatisfied leaves: [SimpleQuest.PrereqRule.PowerOn]`. That is the whole room in one line - which Step refused, why, and the exact tag that would unblock it. The same refusal also rides out as an event carrying those leaf tags, which is what lets the HUD beat name the condition instead of just saying no; an **Observer Component** with *Observe Progress Refused* ticked receives it.

---

## Gotchas

**A rule's tag is global, exactly like a group's.** `SimpleQuest.PrereqRule.PowerOn` means the same thing everywhere in the project, with no questline prefix and no scoping you did not type. Two rules writing the same tag are two publishers of one fact, and the last one to evaluate wins the argument. Name them as carefully as you would name a save key.

**Nothing pairs the two ends for you.** The Entry and the Exit are in different graphs and neither knows the other exists. A misspelling on either side compiles clean and does nothing forever - the Exit's condition is simply never satisfied, and the Step it gates refuses every touch with a reason that looks correct. Check the fact in World State, not the wires.

**A rule is only as live as its node.** The `QuestPrereqRuleNode` subscribes when its instance is registered, so a rule inside a questline that has not started is not watching anything and its fact is not published. If a condition has to be true before its own graph runs, a rule is the wrong tool; a Blueprint writing the fact is the right one.

**Gate Progression keeps the beacon armed.** A Step gated this way looks exactly like a Step that is ready. That is the point - the player should be able to try it and be told why not - but it means an unmet prerequisite does not read as "locked" in the world unless you give it a cue of its own. This room spends a refusal beat on it.

**Retraction is real, and nothing warns you.** If a rule's expression can go false, anything gating on it can go from passable back to blocked while the player is standing in front of it. That is a feature, and it is also the one way a rule can surprise you in a way a `Completed` anchor never will.

**A rule does not activate anything.** It is a condition and nothing else. If the far side should *start* when the near side finishes, you want Chapter 9's activation group; if the far side should be *allowed* when the near side holds, you want this.

**The Observer component cannot watch a rule tag.** Its *Observed Tags* picker is filtered to `SimpleQuest.Questline`, so a rule's tag is not in the list - an actor wired up that way has to watch the Step that drives the rule instead, which is exactly what this room's door and locker do. The things that *can* read a rule directly are a Step's *Prerequisites* pin, another graph's Rule Exit, and any Blueprint subscribing to the World State fact itself. The comment box's "a door, a light, a Step's prerequisite" is true of the fact; it is not true of that one component's tag picker, which is worth knowing before you plan a door around it.

---

## The Step so far

What this chapter added to your picture of the Step node and its Objective:

- **A Step's prerequisite can name a condition instead of a node.** Everything through Chapter 7 built prerequisites out of other nodes' outcomes. A rule leaf names a tag, and the thing that writes it may be in another graph, another asset, or a Blueprint.
- **Gate Progression is the mode that talks back.** The Step stays Live, the trigger stays armed, and a refused touch is an event you can narrate. Use it when the player can reasonably try and deserves an answer.
- **A condition can stop being true.** Prerequisites built on completions are one-way. Prerequisites built on rules are not, and a Step gated on one can close again after it opened.
- **Nothing a Step reads has to know who wrote it.** The prerequisite is a tag, and tags have no authors.

Still to come: the Config Asset, and a Step that is driven entirely from outside the graph (Chapter 11).

---

## Try it

### Read the rule from somewhere else:

Open any other chapter's graph - `QL_Ch9_ActivationGroups` will do - drag in a **Prerequisite Rule: Exit**, set its *Group Tag* to `SimpleQuest.PrereqRule.PowerOn`, and wire it into the *Prerequisites* pin of one of its Steps. *Compile All*.

Play from Chapter 9 and watch that Step refuse everything, then walk into Chapter 10 and throw the switch. The Step you gated two chapters earlier becomes passable, from a condition written in a quest it has never heard of. Nothing was added to the Supply Room to make that work: it was already publishing a fact, and facts do not care how many readers they have.

Undo the edit and *Compile All*.

<img width="914" alt="A second Rule Exit on PowerOn added to another chapter's graph" src="https://github.com/user-attachments/assets/00ade20d-3661-4b2f-8474-394b4c37503f" />

### Take the fact away:

This is the one experiment that shows a rule doing what a completion cannot, and it costs a single Blueprint node. In `BP_QuestPlayerExample`, wire a **Debug Key** event to a **Remove Fact** node with its *Tag* set to `SimpleQuest.PrereqRule.PowerOn`. No graph edit, no new tag, nothing to undo afterward.

<img width="982" alt="The debug key in BP_QuestPlayerExample: Debug Key 1 Pressed into Remove Fact, Tag SimpleQuest.PrereqRule.PowerOn" src="https://github.com/user-attachments/assets/82d0bc5d-e5ad-410c-aef3-047527ce0d89" />

Play to the Supply Room, throw the switch, confirm `PowerOn` is in World State, then press the key. **The gate closes again.** Walk back to Terminal and it refuses exactly as it did before the power, naming the same unsatisfied leaf in the log. Nothing was un-completed to make that happen: Power Switch keeps its anchor, the Supply Room stays finished, and the only thing that changed is a tag that is no longer there.

**The Examiner now shows something it cannot show any other way.** Pinned to Terminal, the rule's header goes red while the condition underneath it stays green. The two rows answer different questions - the header asks whether the fact is present, the body asks what the rule's own condition says - and they can only disagree when something reached around the rule and deleted its tag. Red over green is the signature of exactly that, and it is worth being able to read: a rule's fact was removed by hand, and the rule has not noticed.

<img width="414" alt="The Prereq Examiner on Terminal after the key: the rule header red over its condition still green" src="https://github.com/user-attachments/assets/ffba8237-f36f-4f7b-894d-512e605b40b1" />

It has not noticed because it has no reason to look. A rule re-evaluates when one of its leaves changes, and this rule's only leaf is Power Switch's Any Outcome - a completion, append-only, which will never change again. The expression is still true. The fact is simply gone.

**Now load a save taken after the switch was thrown.** `PowerOn` is back, the gate is open, and you did nothing. A rule re-evaluates the moment it is registered, so when the questline comes back it asks its condition again, gets the same answer it always gets, and publishes the tag a second time.

<img width="414" alt="After loading a save past the switch: the Examiner green again, with nothing touched" src="https://github.com/user-attachments/assets/4288f990-423c-4c95-a5af-b8d2bdbb6539" />

That is the whole difference between a rule and an anchor. A completion is a thing that happened, and you can read it forever because nothing can take it back. A rule is a standing claim about the present, and it reasserts itself every time it is asked. You can delete its fact. You cannot make it forget.

---

Previous: [Chapter 9 - Activation Groups](09_ActivationGroups.md) | Next: Chapter 11 - Observers
