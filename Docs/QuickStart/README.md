# The QuickStart, Chapter by Chapter

A walkthrough of the eleven-room tutorial that ships with SimpleQuest. The rooms teach by playing: each one narrates itself on the HUD, carries author-facing comments in its graph, and has something in the world that visibly reacts to quest state. This walkthrough is the third pass over the same rooms - the one that goes underneath. For each chapter it says what you saw, what is in the graph behind it, how the framework actually did it, and what tends to go wrong when you build the same thing yourself.

It assumes you have played the room before you read its chapter. If you haven't, start with [Before you start](00_BeforeYouStart.md) - it covers the controls, the HUD, the two buttons at the start, and how to keep a graph open while you play.

If you take one thing away from the eleven rooms, it should be a confident grip on the **Step node and its Objective** - what each property on the Step does, and what an Objective decides. Every chapter adds a piece of that picture, no chapter tries to give you all of it, and the closing page assembles the whole thing in one place.

## How each chapter is laid out

Every chapter file has the same five parts, so you can read them straight through or jump to the part you want:

| Part                | What it answers                                                                                        |
|---------------------|--------------------------------------------------------------------------------------------------------|
| **In the room**     | What happened as you played, in order, and what in the world reacted                                   |
| **In the graph**    | Which asset to open, what each node does, what to click and expand                                     |
| **Under the hood**  | Which tags were minted, which events fired in which order, where the state lives                       |
| **Gotchas**         | The mistakes the room's design steers you around, spelled out so you can avoid them on your own graphs |
| **The Step so far** | What this chapter added to your picture of the Step node and its Objective, and what is still to come  |

Some chapters add a **Try it** - a change you can make to the room's graph that won't break the tutorial.

## The chapters

Act I is authoring one questline. Act II is what happens when one graph is not enough.

|              | Chapter                                          | Teaches                                                                                                                  |
|--------------|--------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| **Prologue** | [Intro - Before You Start](00_BeforeYouStart.md) | Basic controls, HUD layout, how to start the tutorial and follow along in a graph                                        |
| **Act I**    | [1 Basic Trigger](01_BasicTrigger.md)            | A single step, a single trigger, a single ending                                                                         |
|              | [2 Rewards](02_Rewards.md)                       | Grant Rewards nodes, the experience bar and gold readout                                                                 |
|              | [3 Basic Giver](03_BasicGiver.md)                | A quest offered by an actor: activated is not started                                                                    |
|              | 4 Sequential Steps                               | Activation alone does not enforce order - wiring does                                                                    |
|              | 5 Named Outcomes                                 | A fork resolved with `Left` or `Right`, and what routes on each                                                          |
|              | 6 Blocking                                       | The Blocked state, a refused give, and a door that reads it                                                              |
|              | 7 Prerequisites                                  | AND / OR / NOT composition, spawned as three scenarios in one room                                                       |
| **Act II**   | 8 Linked Questlines                              | One questline placed twice, with separate progress                                                                       |
|              | 9 Activation Groups                              | A second questline opening a bridge it knows nothing about                                                               |
|              | 10 Prerequisite Rules                            | A named condition - the power - read by everything that needs it                                                         |
|              | 11 Observers                                     | An archive console that logs what an observer hears, live and caught up                                                  |
| **Closing**  | The Step node, assembled                         | Every Step property and Objective override in one place, with the chapter that taught it - and what the rooms don't show |

Chapters without a link are not written yet.

## After the last room

The tutorial ends where authoring begins. When you have finished Chapter 11, the [Building your first progression](../../README.md#building-your-first-progression) section of the README walks the seven steps every chapter here was built with, and [Objectives](../../README.md#objectives) covers the one class you will subclass first. Questions, and things the rooms didn't answer, go to the [Simple Quest Discord](https://discord.gg/PN9kzPypeS).
