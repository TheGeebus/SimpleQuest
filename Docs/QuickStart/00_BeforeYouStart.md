# Before You Start

The three central ideas to focus on, what you need open, what the controls are, and how to read the rooms.

As you work through the walkthrough, pay particular attention to three central ideas:

- **Objectives** describe what the player needs to accomplish.
- **Steps** place objectives in a progression.
- **Prerequisites** describe the conditions under which progression becomes available.

These three central ideas form the foundation of how the framework expresses progression. The walkthrough introduces them gradually, so don't worry about understanding every capability immediately. In particular, watch how prerequisites allow progression to express relationships between things without necessarily forcing those things into a strict sequence.

## Open the project

Open `SimpleQuestDemo` (or any project with both plugins installed). The QuickStart map is the editor's startup map, so it is already loaded when the editor comes up; if you have navigated away, it is at `SimpleQuest Content/QuickStart/QuickStart`.

Everything the tutorial uses lives inside the SimpleQuest plugin's content folder, and the Content Browser hides plugin content by default. Click the **Settings** gear at the top right of the Content Browser and turn on **Show Plugin Content**. A `SimpleQuest Content` folder appears at the top level; `QuickStart` is inside it, and the chapters are under `QuickStart/Chapters`, one folder per room.

<img width="1200" alt="Click the Settings gear icon and enable Plugin Content to find the chapter Questline Graphs" src="https://github.com/user-attachments/assets/5d20e1fa-e8e5-4783-8ebd-c430475118a6" />

---

## Controls

| Input                   | Does                                                          |
|-------------------------|---------------------------------------------------------------|
| **W A S D** + mouse     | Move and look                                                 |
| **E**                   | Interact - the buttons on pedestals, and Chapter 11's console |
| **P**                   | Pause menu, with save and load                                |
| Arrow keys, mouse wheel | Chapter 11 only: dial the console, scroll its log             |

The glowing beacons and the quest givers do not need a key. Both fire on overlap - walk into a beacon to trigger it, walk into a giver to accept what it is offering. The buttons are the things you press E on.

---

## The HUD

Three things on screen matter:

- **The sidebar** lists the questlines and steps currently in play and ticks them off as they complete.
- **The beats** are the lines of text that print as things happen. Each one is written to land at the moment the next action is yours, and each names the lifecycle event that just fired, in capitals: ACTIVATED, STARTED, COMPLETED, and so on. Those capitalized words are the vocabulary the whole tutorial is building; the walkthrough uses them the same way.
- **Experience and gold** read out at the bottom of the screen. Completing a room the first time grants experience points. Every completion grants gold.

<img width="1200" alt="A view of the Quest Sidebar on the left with its narrative beats along with the experience bar and gold readouts at the bottom and bottom right" src="https://github.com/user-attachments/assets/68475cbc-a001-42b8-9df0-aa76c4fab8ca" />

---

## The two buttons at the start

The first room has a pedestal with a **green Start button** and a **red Unlock button** beside it.

**Green** starts the tutorial. It calls `Start Questline` on the master questline and publishes one World State fact, `SimpleQuest.Fact.QuickStart.TutorialStarted`, which is what lets Chapter 1 proceed. From there each chapter's completion satisfies the prerequisite holding the next one back, so the rooms open in order. This is the way to play the first time.

**Red** unlocks every chapter at once. It publishes `SimpleQuest.Fact.QuickStart.ChaptersUnlocked`, which does two things: it enables the start button in each room, and it disables the chapter-to-chapter chain, so completing a room no longer opens the next one. Each chapter then plays in isolation from its own button.

Every room has its own start button. It reads *Locked - finish earlier chapters* until you either reach that room in order or press Unlock. Once enabled, pressing it re-activates that chapter, which is how you replay one - Chapter 5 asks you to, to see its other path.

<img width="1200" alt="The green 'Start Tutorial' and red 'Unlock All Chapters' buttons" src="https://github.com/user-attachments/assets/a78ecf40-9241-4767-8025-1fb5254efc15" />

---

## Reading a room

Every chapter is built on three layers, and the walkthrough refers to all three:

1. **Beats** - the HUD text. Player language. Fires at the moment the thing happens. Authored on a Display Data asset in the chapter's `DisplayData` folder.
2. **Graph comments** - the comment boxes inside the chapter's questline graph. Author language. Sit beside the nodes they describe and say why the graph is shaped the way it is.
3. **The in-world tell** - a door that opens, a light that comes on, a screen that logs - something in the room that visibly changes with quest state, so the concept is a physical consequence and not only text.

Play the room first. Then open its graph: `SimpleQuest Content/QuickStart/Chapters/<Num_Name>/QL_Ch<Num_Name>`. Read the comments. Then come back here for the part underneath.

---

## Keeping a graph open while you play

The graph editor is a live instrument during Play In Editor, and most of what the walkthrough points at is easier to see this way than to read about.

- **Halos.** With a questline graph open during PIE, content nodes draw a colored halo for the lifecycle state they are in - waiting on a giver, Live, Completed, or Deactivated.
- **Questline Outliner.** A tab in the graph editor listing the structure of whatever is open. Double-click an entry to jump to it. It is the fastest way around a graph with nested Quests or linked questlines.
- **Breadcrumbs.** The bar across the top of the graph panel. Clicking a graph's name goes to that graph; clicking the arrow before it goes to the node that hosts it.
- **Facts Panel.** `Window > Developer Tools > Debug > Facts Panel`. One panel with a view selector at the top, and two views to pick from:
  - **World State** - every fact currently asserted, searchable, live. The framework writes each node's lifecycle here as facts (`.Live`, `.Started`, `.Completed`, `.Blocked`), so this view answers whether something holds right now: is this Step Live, has this chapter completed. A fact is a count, not a flag - the Count column says how many times it has been asserted, and it holds until the count reaches zero. Most lifecycle facts sit at 1; `.Completed` counts every time a node resolves, which Chapter 1 puts on screen. The store itself is shared, not SimpleQuest's. Anything in your game can add, remove, and read facts here through the SimpleCore Blueprint Library - the tutorial's own start and unlock buttons do - so an inventory, a weather system, or a faction ledger can keep its state in the same place without touching the quest system, and a quest can gate on it (Chapters 7 and 10).
  - **Quest State** - the detail a yes-or-no fact can't hold. *Resolutions*: one row per completion, with the outcome, the time, and the source. *Entries*: how each node was reached - from which node, on which outcome. *Prereq Status*: what each giver-held node is waiting on. This record is the framework's own: you read it, here or through the Quest State Subsystem's query functions, and only the quest manager writes it. It is not built for anything else to share.

  Each menu invocation opens a fresh panel, so you can dock one or more of each side by side.
- **Prerequisite Examiner.** Right-click a node and choose *Examine Prerequisite Expression*. The panel lays out the node's whole expression with each condition tinted by whether it is satisfied - the place to look when a node is waiting and you want to know on what (Chapters 4 and 7).
- **Group Examiner.** Right-click an Activation Group node and choose *Examine Group Connections* to see the pairings that have no wire (Chapter 9).

<img width="1200" alt="A questline side-by-side with a PIE session - debug halos show the live state of nodes" src="https://github.com/user-attachments/assets/c47711f1-6c53-4b42-99ea-383d8bc75086" />

---

## Saving and loading

Press **P** at any point for the pause menu. Saving mid-chapter and loading it back is worth doing once early: restored state arrives through the same catch-up path the framework uses for anything that registers late - a streamed-in actor, a spawned NPC - and every event a restored observer receives is marked as catch-up rather than live. Chapter 11 puts that distinction on a screen.

---

## Conventions in the documentation

- **EVENTS** are written in capitals, the way the beats print them. **States** are capitalized words: Live, Completed, Blocked.
- Tags are in code font: `SimpleQuest.Questline.QuickStart.Chapter_1`.
- Asset names are in code font too: `QL_Ch1_BasicTrigger`, `DA_Ch1Main_BasicTrigger`.
- A **beat** is a line the HUD printed. A **comment** is a comment box in a graph. When this walkthrough quotes either, it is quoting the asset as shipped.

---

Next: [Chapter 1 - Basic Trigger](01_BasicTrigger.md)
