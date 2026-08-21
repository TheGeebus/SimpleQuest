# Changelog

All notable changes to SimpleQuest will be documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [0.7.2] — 2026-08-21 — UE 5.8 Support and Honest Surfaces

SimpleQuest now runs on Unreal Engine 5.8 alongside 5.6 and 5.7.

The rest of this release is about surfaces that reported the wrong thing, which
is worse than reporting nothing because you act on it: graph debug tooling that
misread live quest state, a runtime defect that correcting it uncovered, a
compiler that claimed changes it had not made, and a compile button that never
knew whether it had anything to do.

### Added

- **Unreal Engine 5.8 is supported.** SimpleQuest and SimpleCore build and run
  on 5.8 alongside 5.6 and 5.7, with the full automation suite passing and the
  demo content and data resolver verified on it. The Electronic Nodes visual
  integration continues to activate automatically on 5.7+ when EN is installed.
  Plugins no longer declare a fixed `EngineVersion` - a single value can't
  describe a range of supported versions, and on a plugin built from source it
  mostly produced a startup prompt on newer engines. A minimum-version check at
  build time replaces it, so an unsupported engine reports what it needs instead
  of failing with unrelated compile errors.

- **Quests keep a history of refused attempts.** Interacting with a step whose
  prerequisites aren't met, or asking a giver for a quest it can't hand over,
  was already broadcast as an event - but an event only reaches whoever was
  subscribed at the moment it fired. Those refusals are now also recorded, and
  `Get Refusal History` returns them per quest with the reason and the time.
  A journal, a hint system, or a "you can't do that yet" prompt can ask what
  the player has been refused rather than having to be listening when it
  happened. The history is session-scoped and bounded, so it isn't carried in
  save games and a player who retries repeatedly can't grow it without limit.
- **A node pulses in the graph debug overlay when an attempt to advance it is
  refused.** Interacting with a step that isn't ready, or a give that can't
  proceed, flashes the node's lifecycle color and fades over about half a
  second. It pulses the lifecycle layer specifically, because a refusal is the
  absence of a state change - the pulse marks what *didn't* advance. A node
  holding no state yet flashes and fades to nothing, which is what the
  attempted transition did.
- **The graph debug overlay shows why a node can't proceed.** A node waiting on
  unsatisfied prerequisites, or explicitly blocked, now wears a ring outside its
  state color - drawn in the prerequisite wire's own color for a prerequisite
  gate, red for an explicit block. Because it's a second channel rather than a
  replacement color, a node that is running *and* gated reads as both, which the
  previous single-color treatment could not express: being blocked used to
  replace the node's lifecycle color entirely, hiding whether it was live,
  pending, or already complete. The ring also makes a prerequisite cleared by
  resettable replay visible the moment it clears, rather than when the node
  eventually runs again.

- **The compile button reports whether a compile is actually needed.** Opening a
  questline showed the status as unknown until you happened to compile it in that
  session, so the indicator told you nothing on the way in and there was no way
  to tell a questline that needed compiling from one that didn't. It now compares
  the questline's authoring graph - and, transitively, every questline it links -
  against what the compiled data was built from, and reads as up to date when
  they match. Editing anything the compiled data depends on, in this questline or
  in a linked one, returns it to unknown until you compile.

### Fixes

- **Compile All no longer crashes the editor when a questline can't be saved.**
  It saves each questline as it compiles, and a failed save brought the whole
  editor down rather than reporting the problem - so a questline still read-only
  because it hadn't been checked out of source control, or open in another
  program, or held by a second editor instance, ended the session. Compile All
  now reports which questlines could not be written and why, counts them as
  failures, and finishes the rest.
- **Compiling a questline no longer modifies assets it did not change.** A
  compile rewrote every questline it touched, whether or not anything about it
  had changed, so opening the editor and pressing Compile produced a dozen
  modified files in version control. A code-only commit could sweep content in
  alongside it, and the history could no longer tell you whether a questline had
  actually been edited. An adopter's first compile dirtied content they had never
  opened. Three separate causes, all fixed: compiled objects were named from a
  counter that kept advancing, so an unchanged questline produced differently
  named internals on every compile; the compile marked the asset as modified
  before it could know whether anything would change; and generated text was
  assigned a new localization key each time the asset was saved. Compiling an
  unchanged questline now produces byte-identical output and leaves the asset
  alone. Verified by an automation test that compiles twice and compares.
- **The generated display name file no longer changes on every compile.** Same
  underlying cause as above - display text was being re-keyed as it was written -
  so this file appeared in every diff regardless of whether any display name had
  been edited.
- **The Prerequisite Examiner reports live prerequisite state correctly.** Three
  cases were wrong, all from the same cause. A prerequisite wired from a step's
  Any Outcome pin showed no state at all. A questline opened on its own while it
  was actually running as part of a parent questline showed no state either. And
  a prerequisite cleared by resettable replay kept reading as satisfied, which is
  the case the panel exists to make visible. The panel now identifies a leaf the
  way the graph does - by its source node and the completion path it requires -
  instead of by the fact tag it happens to read, so the three resolve together.
- **The graph debug overlay no longer shows a node as completed while it is
  running again.** A node that has completed keeps that record permanently, and a
  container stays live across loop iterations, so a node on its second run
  asserts both at once. The overlay ranked completion above activity and painted
  it as done. Current activity now wins.
- **A container's Live state is readable from every tag perspective.** Quest and
  questline containers derive Live from their inner Steps, and that derived state
  was written to only one of the container's tag spellings - unlike every other
  lifecycle state, which is published across all of them. Querying whether a
  container was live through an asset-scoped alias returned false while it was
  running, and an observer bound on that perspective never saw it start. Most
  visible on a questline embedded in another as a linked node, where the
  embedded copy and the placement running it are spelled differently: the graph
  debug overlay showed such a container as completed and never as live, even
  while its inner Steps were active.

## [0.7.1] — 2026-08-17 — Clean-Install Fixes

A fix release for three things a clean install surfaces that a development
machine cannot: a build path that had never been compiled, a graph color that
only looked right locally, and a test coupled to one engine's log wording.

### Fixes

- **The editor module builds without Electronic Nodes installed.** The
  `WITH_ELECTRONIC_NODES` guard was defined only when the optional plugin was
  found, leaving it undefined rather than zero everywhere else — a warning on
  some engine versions and a hard error on others. A project without Electronic
  Nodes could fail to compile. The definition is now unconditional, and only its
  value varies with whether the plugin is present.
- **Prerequisite wires are distinct from activation wires on a fresh install.**
  The shipped default for the prerequisite wire color matched the activation
  wire color, so the two wire types — execution flow and gating contingency —
  rendered identically until the dashed-wire integration was active or the
  color was overridden locally.
- **A resolver test no longer depends on one engine's log formatting.** It
  asserted on the exact wording of a failed class lookup, which varies between
  engine versions; it now matches the stable portion.

## [0.7.0] — 2026-08-17 — The Data Resolver

Quest data has always lived inside the `.uasset` binary. That's fine while
you're authoring one questline in the graph editor, and increasingly awkward
after that: you can't diff a change, review one, bulk-edit a hundred rows,
generate content from a spreadsheet, or hand a designer's table to a build
pipeline. This release opens that up in **both** directions - a questline can
be written out as plain tables you can read, edit and version, and those tables
can be read back into a questline. Not as an export format to look at, but as a
working surface you can actually operate on.

The point isn't a file. It's that a questline stops being a thing only the
graph editor can change.

### Questlines out, questlines back in

- **Export a questline to readable tables.** You get one table per node kind
  plus a single relationship table, keyed stably so the same questline always
  writes the same rows. Diffable, reviewable, and legible without the editor
  open.
- **Import them back.** Round-tripping a questline through tables and back
  produces the same questline: the same nodes, the same wiring, the same
  configured rewards, including reward objects nested inside other data.
- **Reroute nodes stay out of your way.** Wiring routed through reroute knots
  is written as the relationship it *means*, not as the hops it takes, so
  tidying a graph's layout doesn't churn the data.
- **Nested objects keep their identity.** A reward inside a step is written under
  a stable identity of its own rather than its position in a list, with that
  position travelling alongside as ordinary data. Reorder two rewards and the
  export changes the two cells that say where they sit and nothing else - so two
  people adding a reward on separate branches merge into two rewards, instead of
  silently becoming one.

### Bring your own shape

Your data probably doesn't look like the framework's does, and shouldn't have to.

- **Map your columns to quest properties.** A reusable **Mapping** asset says
  which of your columns feed which node properties, which of your type values
  mean which kind of node, and which column carries your row keys. Your source
  keeps its own vocabulary; the Mapping does the translating.
- **Author the Mapping by picking, never typing.** Point it at a sample of your
  own data and every field is chosen from what's actually there, so a typo
  can't quietly mis-bind a column.
- **Express relationships as columns.** If your table says a step's `next` is
  `guard_post`, that's wiring - no separate edge table required. Name an
  outcome instead of a plain target and you get outcome branching straight from
  flat data.
- **Prerequisites from a column too.** `unlock_after`, `unlock_any` and
  `unlock_unless` columns build the same prerequisite gates you'd wire by hand.
- **Your keys come back.** Export through the same Mapping and you get *your*
  column names, *your* type values, and *your* row keys - a file you can diff
  against the one you started with.

### Your data can stay where it lives

- **A folder of tab-separated files, JSON, or an in-engine Data Table.** One
  unchanged Mapping reads all of them to the same result.
- **Split across files however suits you.** A source can be one table or many -
  by chapter, by author, by whatever boundary your team already has.
- **Write your own format.** The reader/writer interface and the data types it
  exchanges are public, so a studio can support an encoding the framework has 
  never heard of without forking anything, and can implement only the direction 
  it needs.

### Re-importing into a questline that already exists

The interesting case isn't building a questline from data once. It's the
tenth time, into a questline someone has since edited by hand.

- **See what would change before anything changes.** A re-import first produces
  a preview: which nodes would be updated and in which properties, what would be
  created, what the source no longer mentions, and how the wiring would differ.
  Nested values are named by path, so you see *`Rewards[0].Amount` 42 → 99*,
  not "something in this node changed."
- **Choose the format and the Mapping from the panel.** The preview reads whichever
  format you pick - the list is every provider registered with the plugin,
  including any that your own module adds - and applies an optional Mapping chosen
  the same way. Changing either re-previews immediately, so what you're looking at
  is always the reading you selected.
- **A source that can't be read says so, in the panel.** An unreadable folder, a
  refused Mapping or an incoherent source all report where you're looking rather
  than only in the log, naming the format that was actually tried - which isn't
  always the one the panel is set to, since a console import carries its own.
- **Nothing is written until you ask.** Previewing is the default; applying is a
  separate, deliberate step.
- **Decide what a blank cell means.** A column your source declares but leaves
  empty can either preserve whatever the questline currently holds or reset it
  to its default: per column, or as a default for the whole Mapping. A third
  setting refuses the import outright if a required value is missing.
- **Deleting is opt-in, and scoped.** Nodes your source no longer mentions are
  always *reported*; they're only removed if you ask. And a source that
  describes one part of a questline is never treated as speaking for the rest.
- **Match rows by a key of your choosing.** Writing into a Data Table, the row
  name belongs to you. Name a column in your Mapping and rows are matched on that
  value instead - so you can rename a row and the next import still recognizes
  it, rather than creating a duplicate beside it. A row whose *name* matches
  while its key doesn't is left alone: it's somebody else's.
- **The preview points at the graph.** Hover a row and the node it describes
  lights up; double-click and the editor goes there, opening a container's inner
  graph if that's where the node lives. Property rows behave as the node they
  belong to.
- **One undo takes it all back**, including created nodes, deleted nodes and
  nested reward values.

### Watching it work

- **Resolver output has its own log channel.** Export, import, column mapping
  and the re-import preview all report on `LogSimpleQuestResolver`, separate
  from quest activation and graph compilation. Turn it up when you want to see
  what an import did without everything else coming along for the ride.
- **Dial it from Project Settings.** *Plugins → Simple Quest → Logging →
  Resolver*, applied live without an editor restart.

### Check a whole corpus without opening the editor

If your quest data lives in files, the question worth answering on every commit
is whether it still applies cleanly to the assets it describes. That's the same
preview the panel shows, so it can run without a person in front of it.

- **Plan every questline in a folder tree, from the command line.** Point it at
  a directory and it finds each exported corpus, previews it against the
  questline that corpus names, and reports the lot. One editor start for the
  whole run, not one per questline.
- **Structured output, not scraped logs.** The run writes JSON: what each plan
  would create and change, what it refuses, what it warns about, and which
  questline and folder every result belongs to. Field names and ordering are
  stable, so two runs over unchanged data produce byte-identical files and a
  diff between commits is signal rather than noise.
- **An exit code your build can act on.** Nothing to report, findings, or the
  run itself failed. You choose whether a source merely *differing* from its
  asset fails the build or only one that genuinely can't be applied - the first
  is right when your files are the source of truth, the second when both ends
  get edited.
- **A corpus whose questline doesn't exist yet is validated, not failed.** Data
  authored before anyone builds the asset is an ordinary state: the source is
  read and checked on its own terms, and reported as such - which is a different
  answer from "the asset already matches."
- **A run that finds nothing to check refuses.** Pointing it at the wrong
  directory reports an error instead of a clean build.

### It refuses rather than guessing

- **A source it can't fully describe is refused, not partly applied.** If the
  preview couldn't make sense of the data, applying part of it would be acting
  on a description already known to be wrong.
- **Renaming a questline isn't a property write.** A questline's ID is its
  compiled tag namespace and changing it moves every tag the questline owns and
  breaks save data keyed on them. A re-import reports the difference and
  declines to make it.
- **Rebuilds are refused, not half-done.** A node whose kind changed, or that
  moved into a different container, can't be edited in place, so it's reported
  rather than partially updated.
- **Exports won't overwrite a folder they didn't write.** An export claims its
  destination and refuses one belonging to something else, so pointing it at the
  wrong directory costs you nothing. It also refuses to *convert* one: exporting
  in a different format than the folder already holds would delete every file the
  previous format wrote, so it stops and says so rather than doing it quietly.
- **An export folder says what it is.** Alongside claiming ownership, an export
  records which questline it came from, which format wrote it and which Mapping it
  was written through - so a folder arriving in someone else's checkout can be
  read back without anyone remembering how it was made. A folder you maintain
  yourself can carry that same description while declining to be overwritten.

### Keeping gameplay tags tidy

Renaming a quest node writes a gameplay tag redirect so existing content keeps
working. Those pile up, and nothing in the engine tells you when one has
outlived its purpose - or when one has quietly turned harmful.

- **Find out which redirects are still doing something.**
  `SimpleQuest.ScanTagRedirects` reads every package on disk and reports each
  redirect as retirable, still in use (naming the packages holding it), or
  *inverted*. `--prune` then deletes the retirable ones from your project
  config.
- **Inverted redirects are the ones worth knowing about.** Move a node into a
  container and back out again and you can be left with a redirect pointing
  *away* from a tag that is still live, toward one that no longer exists. Every
  reference to that node then resolves to an invalid tag: it still activates,
  but stops writing state facts and never registers for deactivation - silently.
  The engine never checks that a redirect's target exists, and a stale redirect
  actually suppresses the warning that would otherwise surface it. The scan
  names them, and tells you to delete the line rather than resave anything.
- **`SimpleCore.TraceAssetDirty`** logs a callstack whenever a matching package
  is marked dirty, by either of the two routes that can do it - so "why does
  this asset keep asking to be saved?" becomes a question you can answer.

### Fixes

- **An asset marked dirty after a tag rename now says why.** Loading an asset
  that holds a redirected tag marks it dirty so a save can persist the healed
  value - but it did so silently, and an asset that had *always* held that tag
  got marked too. The log now names the tag and the property that matched, which
  is the difference between one restart and an afternoon.
- **Questlines no longer carry a rename ledger nothing read.** Every compile
  recorded old tag names into the asset, for a deferred-propagation feature that
  was never built. They were dead weight, and they made it impossible to tell a
  tag still awaiting migration from a historical record of one that had already
  happened. Removed - assets shed the field on their next save.
- **Applying a re-import recompiles the questline.** An apply changed the graph but
  left the compiled model describing it as it was, so the runtime, the state
  subsystem and the tag registry all read stale data until something else
  triggered a compile. An apply that changes anything now recompiles the
  questline *and every questline linked to it* - a linked parent embeds the
  target's compiled data, so recompiling only the target would fix one asset and
  leave its neighbours describing nodes that no longer exist. Nothing is
  recompiled when an apply changes nothing.
- **Re-exporting no longer refuses over how you typed the path.** The console
  accepts a questline as `/Game/Path/Asset` or `/Game/Path/Asset.Asset`, but an
  export recorded whichever form it was given and compared the next one
  literally. Using the other spelling read as a *different questline* and the
  export refused, advising you to change the questline's ID - which would have
  been the wrong fix for a difference that was never real. Both sides are
  normalized now, and markers already written keep working.
- **`OnObjectiveDeactivated` now reports why it fired.** The hook has always run
  on both the completion path and the interruption paths, but couldn't tell them
  apart - an Objective wanting to release a reservation on abandon but not on
  success had to track that itself. `GetDeactivationReason()` now answers it
  during the call: `Completed` when the Objective reported an outcome,
  `Interrupted` when it was torn down without one. Non-breaking - the event
  signature is unchanged, so existing overrides keep working untouched.

## [0.6.1] — 2026-07-21 — Embedded Questline Rewards Fix

A fix release for questline-level rewards. In 0.6.0, a questline's own
completion rewards fired correctly when the questline ran on its own, but
were silently skipped when that questline was embedded inside another as a
linked node — the exact "fires on every use, standalone or embedded" case
the rewards release promised. Embedded questlines now deliver their own
completion rewards, so a reusable questline dropped into several parent
questlines pays out its authored rewards in every one.

### Fixes

- **A linked questline now grants its own completion rewards.** When a
  questline is embedded inside another and completes, the rewards authored on
  *that* questline (its whole-questline reward sets, keyed by outcome) now
  fire, crediting the completing player — matching how they already fired for
  a standalone questline. Rewards authored under a specific outcome and under
  Any Outcome both deliver.
- **Live reward queries reach embedded questlines.** Asking what a questline
  pays now returns results for a questline embedded inside another, not only
  for one running standalone.

### API

- **`Get Advertised Rewards` is renamed to `Get Advertised Rewards For Any
  Outcome`**, so it reads as the clear counterpart to `Get Advertised Rewards
  For Outcome`. The old name is redirected, so existing Blueprints retarget
  automatically with no action needed.

## [0.6.0] — 2026-07-19 — Rewards

The rewards release: granting rewards is now a first-class part of the graph,
on equal footing with objectives. Drop a reward node onto any completion path,
configure what it grants inline, and it fires when the flow reaches it. Rewards
broadcast to whoever's listening rather than being pushed at a fixed target, so
any actor can react to the grants it cares about. And you can ask a questline
what it pays *before* the player commits — the data a "do this task, get this
reward" screen needs — for a specific outcome, a whole node, or an entire
questline, live or straight off the asset.

### Reward nodes & adapters

- **Rewards are graph nodes now.** A *Grant Rewards* node wires onto any
  completion path and fires when activation reaches it. Its output continues
  the flow, so rewards drop inline anywhere — after a step, on a specific
  outcome, or on Any Outcome — with no special lifecycle to manage.
- **Rewards are self-configuring adapters.** Each reward on a node is an
  instanced object you configure in place. The framework doesn't dictate what
  a reward *is* — it's the adapter between "the flow reached here" and "apply
  this effect." Author one in C++ or Blueprint to compute loot, scale a value
  by player level, roll a table, or hand off to another system, then hand the
  result out. Reference rewards ship for the common cases (experience,
  currency, a loot table, a value scaled by the recipient, and a no-subclass
  generic reward you configure entirely in the details panel).
- **Rewards compute; they don't just hand out constants.** Because a reward is
  an adapter, it can read the completion context and look up whatever it needs
  from your game before granting — the recipient's level, difficulty, a
  data-driven table — so the granted value reflects live state, not a number
  frozen at author time.
- **Grants broadcast to recipients, not a fixed target.** When a reward fires,
  it publishes on its reward-type channel. Any actor with a *Reward Recipient*
  component that watches that type receives it and reacts — hierarchically, so
  a component watching a currency category catches every currency under it. The
  granting node doesn't need to know who's listening, and a recipient reacts
  only to the reward types it cares about.

### Advertising rewards ("do this, get this")

- **Ask what a completion pays, before it's earned.** A preview query returns
  what any content node advertises on a given outcome — the data a quest-giver
  hub, journal, or bounty board needs to show rewards up front. Ask for one
  outcome, or get the whole picture as a map of every outcome to its rewards.
- **Previews are computed live for the viewer.** A reward's advertised value is
  produced the same way it would be granted, without granting it — so a
  level-scaled reward previews the amount *this* player would actually receive,
  and re-querying tracks changes. Querying never grants.
- **Inspect an unstarted questline cold.** The same reward data is readable
  straight off a questline asset with no running game, so a catalog can show
  what a quest pays before the player has taken it.

### Questline-level rewards

- **A questline can pay out on its own completion.** Beyond rewards wired into
  the graph, a questline carries its own reward map — keyed by its final
  outcome — for "the whole questline grants this." These fire on every use of
  the questline, whether it runs on its own or embedded inside another, with no
  wrapper required. The completing player is credited automatically.
- **Author them where they belong, safely.** Questline-level rewards live on
  the questline in a dedicated Details-panel editor: pick an outcome from the
  ones the questline actually produces (or Any Outcome), and fill in the
  rewards. Change an outcome's key at any time and the rewards move with it —
  no re-authoring. If an outcome referenced by a reward ever goes away, the
  panel flags it and the compiler refuses until it's re-pointed, so a reward
  can't silently point at nothing.
- **See a linked questline's payout on the node.** When you place a questline
  inside another, its own completion rewards show on the node as a *Grants*
  summary — so a reward that fires on every use isn't hidden away on the other
  asset.

---

## [0.5.0] — 2026-07-06 — Persistence & Reusable Questlines

The persistence release: your quests now survive save/load in full. Every
running, completed, blocked, and prerequisite-waiting state restores exactly
where the player left off. And reusable questlines now run as independent
instances per placement, so the same authored questline embedded in several
contexts progresses on its own in each.

### Save / Load

Full quest progression saves and restores with one call each way, dropped
into whatever save game your project already uses. The plugin ships no
`USaveGame` of its own to adopt. SimpleQuest packs the complete state of
your quests — everything running, completed, blocked, and waiting on a
prerequisite, plus each objective's in-progress detail — into a single
serializable snapshot you embed in your own save, and reconstitutes it on
load exactly where the player left off.

The integration is struct-only. Capture the snapshot, drop it into your
monolithic save alongside the rest of your game's data, and write it
however you like. The snapshot is a plain value copy with no live object
references, so it hands cleanly to a background (async) save with no
game-thread hitch.

- **One call to save, one to load.** *Capture Quest State* returns the
  snapshot to embed in your save object. On load, a single *Restore Quest
  State* takes it back and reconstitutes everything: running quests,
  completed ones, in-flight objectives — and you never enumerate which
  questlines were active, because the save records that itself. The apply and
  graph-rebuild steps are also exposed separately (*Apply Quest Snapshot* /
  *Restore Quest Graphs*) for when you want to split them across a level
  transition — see the load flow below.
- **State resumes, not just "which quests are open."** A quest caught
  mid-objective returns mid-objective. A completed quest stays completed
  and its fact-driven consequences (opened doors, unlocks, journal
  entries) re-derive on load. A quest still waiting on a prerequisite
  keeps waiting and fires when the prerequisite is finally met. A
  counting objective comes back at its exact count. 
- **Resolution history persists, not just current state.** The full
  record of how every quest and step resolved — each outcome and authored 
  path, and each time it was entered — is captured and restored, so history 
  and outcome queries return the complete pre-save story on load, and 
  prerequisite gates that read that history stay satisfied exactly as 
  they were.
- **Objectives persist their own progress.** A custom objective type
  opts in by overriding a capture / restore pair, and the framework keys each
  objective's saved state to its stable identity and re-applies
  it after the objective rebuilds on load. The built-in counting
  objective already does this, and your own objectives carry whatever
  state they define.
- **Replayed events are marked as such.** The events that reconstruct
  quest state on load arrive flagged as catch-up rather than live, so an
  actor or UI that would normally play a transition — a patrol, an
  animation, a sound — can recognize the replay and jump straight to the
  settled state instead of acting out history.
- **Reconstructed events carry the full payload.** Before, a save/load
  restore — or any observer that binds after a quest is already running —
  delivered its catch-up events with only the quest tag filled in. Now the
  rich context rides along: the display name, the instigating actor, your
  `CustomData`, and the origin lineage, all rehydrated from the persisted
  registries. An actor or UI that reads those fields on live events sees
  the same data on the first post-load event volley, not a stub.
- **The load flow is level-transition-shaped.** The recommended sequence
  — load the save, apply the snapshot, then open your gameplay level —
  restores the data before the level's actors register, so givers,
  triggers, and HUD catch up to the restored state as they spawn, with no
  per-level wiring. Pass *Restore On Next Level Load* to *Apply Quest
  Snapshot* and the questline graphs rebuild themselves the moment your
  gameplay level opens. The whole load becomes apply-then-open with no
  second call in the destination level.
- **Starting a questline is idempotent.** Calling *Start Questline* on a
  questline that's already running — or one just restored from a save — does
  nothing, so you can fire it unconditionally from a level, GameMode, or
  PlayerController `BeginPlay` without a fresh start racing or overwriting a
  loaded game. A new game starts it once; a loaded game lets the restore
  reconstruct it.

### Reusable questlines run as independent instances

A questline embedded in another graph (a LinkedQuestline node) now runs as
its own independent progression *per placement*, instead of every embedding
of the same sub-questline collapsing into one shared run. Reuse a generic
"escort" or "delivery" questline in three places and you get three
independent runs, each with its own state.

- **Two addresses per instance.** Every instance publishes on its specific
  contextual tag (*this* placement) and on the sub-questline's class tag
  (*any* instance of the template). An observer binds "this escort" or "every
  escort" by choosing which tag it watches; activation always targets a
  specific instance.
- **Behavioral change.** Multi-embedding previously shared a single runtime
  progression. If you were relying on that convergence, make it explicit —
  funnel the placements together with an Activation Group (the signal portal)
  rather than depending on an implicit merge.

### Quest display data available from the first frame

A quest's authored display data — `DisplayName`, `Description`, and its
`DisplayData` asset — is now queryable by tag from game startup, without the
questline having to be active first. The compiler writes a compiled display
index alongside the tag data; the subsystem parses it at initialization and
eager-loads the referenced display assets, so `Get Display Name` /
`Get Display Description` / `Get Display Data` return authored content on
frame one.

- A nameplate, world-map marker, or chapter-select screen can show a quest's
  title before the player has reached it — no activation, no tick-delay
  workaround. The prior "query returns empty until the questline starts" race
  is gone.
- Generated per-project data (this index and the compiled tags) now lives in
  your project's `Config/`, not inside the plugin — so it survives PIE / Live
  Coding tag-tree rebuilds, and an engine-installed (read-only) plugin copy
  works.

### Blueprint log helper

- **`Log SimpleQuest Message`** — a BlueprintCallable on the SimpleQuest
  Blueprint library that writes to the `LogSimpleQuest` category at a chosen
  verbosity (`Error` / `Warning` / `Display` / `Verbose`). Blueprint-only
  projects can emit into the framework's log channel without a C++ shim.

### Breaking changes

- **`UQuestObjective::OnObjectiveActivated` is now a two-parameter event.**
  The single nested `FQuestObjectiveActivationContext` parameter is replaced
  by two: `const FQuestObjectiveAuthoredConfig& Authored` (the objective's
  authored configuration) and `const FQuestObjectiveRuntimeContext& Runtime`
  (the caller's runtime input — instigator, custom data, origin lineage). The
  0.4.1 deprecation notice telegraphed this; it has now landed.
  - **Adopter action:** any objective subclass overriding `OnObjectiveActivated`
    (a C++ `_Implementation` or a Blueprint override) must update its signature
    to the two parameters. Field semantics are unchanged — data that was on the
    nested context now reads from `Authored` (config) or `Runtime` (instigator /
    `CustomData` / origin); only the delivery shape changed. Signature changes
    can't be auto-redirected, so there's no CoreRedirect for this — update the
    override by hand.
  - The C++ dispatch entry point `DispatchOnObjectiveActivated` widened to match
    (`Authored`, `Runtime`, `FGameplayTag InOwningStepTag`).

### Fixed

- **Placed quest givers gate deterministically at startup.** A giver-gated
  quest activated at game start could skip its gate and go straight to Live
  instead of waiting in the giver's offer state. The gate check reads the
  "this quest has a giver" set, but a placed giver only populated that set at
  its deferred (next-tick) registration — which raced the synchronous startup
  activation (a placed actor's `QuestTagsToGive` is per-instance level config,
  invisible to the asset-registry scan, so only runtime registration saw it).
  Givers now *declare* themselves synchronously at component initialization —
  before any `BeginPlay` starts a questline — while keeping their live-state
  catch-up on the deferred tick. Placed givers gate reliably with no manual
  ordering, per-instance authoring intact. (A runtime-spawned or late-streamed
  giver still needs its retroactive re-gate — tracked separately.)

### Upcoming

- **Quest rewards.** Runtime and editor framework for creating 
  and delivering rewards. A standalone questline graph node allows 
  rewards to be issued under any circumstance. By following the
  same adapater pattern as the `UQuestObjective`, they maintain
  maximum flexibility to serve any game.
    

---

## [0.4.1] — 2026-06-17 — Authoring Primitives + Subscriber Routing

Patch release adding new authoring primitives (the **Prereq Gate**
utility node, an **Add / Remove / Clear Facts** node trio for direct
WorldState participation from questline graphs, and a **Resettable
Replay** node setting that makes pin-wired prerequisites re-gate when
replayable content runs again), giving
subscribers a way to opt out of the bus's hierarchical-delivery
default, and closing several latent gaps in the quest-resolution
attribution chain that surfaced once the new primitives were
exercised against real authoring patterns. It also broadens the
observer surface — a catch-all `OnAnyQuestEvent` delegate and a new
run-phase `ProgressRefused` event that completes the refusal-event
family — and adds a typed `CustomTag` designer channel on the shared
context base. Quest components (observer / trigger / giver) also gain
runtime add and remove of their watched-tag sets, so a spawned or
reconfigured actor can join an in-progress quest and catch up on the
spot.

### Prereq Gate utility node

A new graph primitive that gates an activation cascade on a
prerequisite expression. Wire the **Enter** input from any upstream
cascade source, the **Prerequisites** input from a prereq expression
(leaves through AND / OR / NOT combinators), and the **Forward**
output to whatever should fire once the conditions hold.

The gate handles the "complete these conditions in any order, then
proceed" pattern as a graph-visible primitive — no more wiring
phantom downstream Quests just to leverage their built-in prereq
gating. Sibling to Set Blocked, Clear Blocked, and Start Questline
under the Flow Control context-menu category.

- **Per-leaf-kind semantics done right.** Fact-keyed prereqs (raw
  WorldState facts — mutable state) wake on both addition and
  removal, so `NOT(Fact)` reads naturally. Path-keyed prereqs
  (quest resolutions, entries, named outcomes — append-only
  historical record) inherit monotonic behavior automatically.
  No designer-side toggle — the right semantic drops out of the
  leaf kind the designer wired.
- **Deduplicate against duplicate cascade arrival.** When a Step's
  completion BOTH satisfies the gate's last prereq AND cascades
  directly into the gate's Enter pin, the gate fires once for that
  cascade event — not once per arrival path. Achieved by per-
  cascade-event identity check; ResetTransientState clears the
  deduplication state on PIE re-entry.
- **Per-pin labels + tooltips + node-level tooltip** so the gate's
  three-pin geometry reads cleanly in the graph.

### Resettable Replay — prerequisites that re-gate on replay

Pin-wired prerequisites read a quest's permanent resolution history,
so once one is satisfied it stays satisfied for the rest of the
session. That's right for one-way progression, but it makes
repeatable content hollow: replay a chapter and its gates are already
open, because the upstream steps still count as resolved from the
first run.

The new **Resettable Replay** setting fixes that without touching the
historical record. It's a tri-state — **Inherit / On / Off** — on
every content node and on the questline graph asset itself. The
default is **Off**, the permanent behavior above, unchanged.

- **Set it On where a gate should reset.** The setting goes on the
  node being gated *on* — the one whose outcome pin feeds a
  Prerequisites input — not the node that holds the prerequisite.
  With that node On, a prerequisite wired from it re-gates whenever
  the content replays: the moment the node re-activates after
  completing, its satisfaction resets and the gate must be opened again.
- **The resolution history is never altered.** On adds a clearable
  per-run view alongside the permanent record. Replay clears the
  view, but the record stays intact. "Has this quest ever resolved this
  way" queries are unaffected — only the gate's per-run state resets.
- **Inherits down the graph.** Set Resettable Replay = On for the
  questline asset (or a container node) and the content inside
  inherits it. Override any node back to Off or On individually. Turn
  a whole chapter replayable from one place, or scope it to a single
  branch.
- **Resets automatically, or on demand.** A completed node clears its
  gate when it re-activates for another run. `Activate Quest` with
  **Bypass Prerequisites** resets it explicitly. For a reset *without*
  reactivating, **`Reset Quest Run State`** (SimpleQuest Blueprint
  library) clears a quest's per-run gate state directly — every
  resettable path mirror the quest has resolved through is cleared,
  while the permanent resolution history and the Completed anchor are
  left intact. It no-ops on a currently-Live quest (an in-flight run's
  mirrors are load-bearing). Use it to re-arm a set of gates before a
  manual replay.

### Fact-manipulation utility nodes

Three new utility nodes for direct WorldState fact manipulation from
the questline graph: **Add Facts**, **Remove Facts**, and **Clear
Facts**. Sibling to the Prereq Gate, Set Blocked, Clear Blocked, and
Start Questline entries under the Flow Control context-menu category.

Each node carries a `Facts` tag container (designer authors any
number of fact tags per node) and forwards activation immediately
after processing them.

- **Add Facts.** Asserts each tag into the WorldState subsystem,
  incrementing its ref-count. A `BroadcastMode` property chooses
  how the per-fact `FWorldStateFactAddedEvent` fires: `BoundaryOnly`
  (default; fires on the 0→1 transition only), `Always` (fires
  every call), or `Suppress` (never fires).
- **Remove Facts.** Decrements the ref-count for each tag. Same
  `BroadcastMode` semantics for the corresponding
  `FWorldStateFactRemovedEvent` — `BoundaryOnly` fires on the 1→0
  transition.
- **Clear Facts.** Hard-removes each fact regardless of ref-count.
  A `bSuppressBroadcast` flag chooses whether to fire the removal
  event (defaults to firing).

Use Add / Remove for tag-counted states that multiple authored
sources contribute to. Use Clear when authoring a definitive reset
point. Together with the Prereq Gate, these nodes let questline
graphs participate fully in the quest-agnostic fact system —
adopters can publish facts that listeners outside the quest system
react to, and gate quest activations on those same facts via the
gate.

### Subscriber-side routing mode

`USignalSubsystem`'s Subscribe API gains a `Routing` parameter that
lets subscribers pick the directional scope of their subscription.
The parameter is a plain enum (dropdown in the Details panel and on
K2 node pins) with four modes: **Exact Match**, **Ancestors**,
**Descendants**, **Bidirectional**.

- **Exact Match** — receive only direct publishes on the exact
  subscribed channel.
- **Ancestors** — also receive publishes on ancestor channels
  (reserved for future; currently a no-op on the subscribe side until
  publish-side dispatch lands).
- **Descendants** (default) — also receive publishes on descendant
  channels via the bus's hierarchical walk. Preserves prior behavior;
  every existing call site compiles and behaves unchanged.
- **Bidirectional** — both Ancestors and Descendants walks active.

Per-subscription narrowing:

- `UQuestGiverComponent` subscribes Exact Match for its
  `QuestTagsToGive`. An Observer or Giver bound to "Quest X" no
  longer receives noise from Quest X's inner-Step lifecycle events
  — handler-side filtering for that case is no longer necessary.
- `UQuestObserverComponent` author-facing `ObservedTags` entries
  gain a `Routing` dropdown in the Details panel. Default Descendants;
  designers can opt individual entries to Exact Match.
- `Observe Quest Lifecycle` K2 node gains a `Routing` pin. Designer picks
  per-call scope; default Descendants preserves prior behavior.
- BP-callable `SubscribeMessage` / `SubscribeMessageOfType` on the
  SimpleCore Blueprint library gain the same parameter.

### Quest-resolution attribution fixes

Three related framework gaps that the Prereq Gate exercised and
that this release fixes. All of them previously latent because the
typical Step's pin name happens to match the downstream Exit's
`OutcomeTag`; gaps surface when the two intentionally differ.

- **Exit's authored OutcomeTag now drives the questline-level
  resolution.** When a cascade terminates at an Exit / Outcome
  terminal at an asset's root scope, the questline resolves with
  the Exit's `OutcomeTag` — not the upstream Step's pin-name
  outcome that happened to lead there. The Step's pin name remains
  the routing-internal path identity; the Exit's tag is the
  public-facing outcome.
- **Utility-node Forward outputs participate in questline
  resolution.** A Prereq Gate (or any future utility node) whose
  Forward output reaches an Exit / Outcome terminal at the asset
  root scope correctly resolves the enclosing questline. Previously
  the cascade dead-ended silently — the resolution attribution
  was only captured for content-node outcome pins.
- **Questline-level lifecycle event published on the questline's
  own tag.** `FQuestEndedEvent` now fires on the questline's tag
  channel when the questline resolves, in addition to the Step's
  tag channel. Subscribers bound at the questline tag — including
  Exact Match subscribers, who otherwise would receive nothing —
  now receive a direct questline-level lifecycle event.

### Prereq deferral correctness — subscription cleanup

Internal fix to `UQuestNodeBase::DeferActivation`: re-deferring a
node that's already waiting on prereqs now correctly unsubscribes
its existing leaf handles before re-subscribing. Previously the
re-subscribe would orphan the prior handles in the SignalSubsystem
(still firing, no longer tracked, can't be cleaned up via
`UnsubscribeAll`), producing spurious wake-up firings on future
fact events. Affects any node whose Activate is called multiple
times while still in the deferred state — most visibly the Prereq
Gate, but content-node prereq deferral inherits the fix too.

### Activate Quest — prerequisite bypass

`Activate Quest` gains an optional **Bypass Prerequisites** input. When
set, the quest activates immediately: its prerequisite expression is
skipped, and any prerequisite wait the quest was already holding is
cancelled. Default off leaves the normal prereq-gated behavior unchanged.

For flows that deliberately start a quest out of its gated order — a
chapter-select / unlock screen, a debug "jump to here" control, or
save-restore reconstituting a quest that was already past its gate. The
wait-cancellation is what makes the unlock case safe: a quest that was
armed and waiting on a prerequisite won't later re-activate on its own
once that prerequisite is belatedly satisfied, so jumping ahead and then
completing the content you skipped doesn't double-fire the quest you
jumped to.

### UI display data — Phase 1 (preview)

Per-node `DisplayName` and `Description` properties on quest content
nodes, plus a marker `UQuestDisplayData` base class for adopter-
authored richer UI metadata schemas. Queryable via
`UQuestStateSubsystem::GetDisplayName` / `GetDisplayDescription` /
`GetDisplayData` with derived-fallback defaults (e.g., a formatted
leaf name when `DisplayName` is unset). Phase 2 — UI display data
sourced through the adopter-pluggable resolver pattern — lands
in 0.5.0.

### Runtime watched-tag mutation on quest components

The watched-tag containers on the quest components — `ObservedTags`
(Observer), `StepTagsToTrigger` (Trigger), `QuestTagsToGive` (Giver) —
are edit-time configuration, read once when the component registers.
New BlueprintCallable APIs change what a component watches **at runtime**:

- **Observer** — `AddObservedTag` / `RemoveObservedTag`
- **Trigger** — `AddTagsToTrigger` / `RemoveTagsFromTrigger`
- **Giver** — `AddTagsToGive` / `RemoveTagsFromGive`

Add a tag whose quest is already in progress and the component catches
up on the spot — the observer's lifecycle events replay, a trigger
activates (and `SendTriggerEvent` begins participating), a giver's
availability set updates. Remove a tag and it unsubscribes cleanly; if
the component was mid-activation on that tag, the closing half of the
pair fires (`OnQuestTriggerDeactivated`, or a giver availability
change) so bound UI tears down instead of stranding "on".

Enables components whose watched set changes during play — an NPC giver
whose offered quests shift with world state, or a trigger volume
re-pointed at different steps without respawning the actor.

### Source query API

A new runtime channel exposing which Trigger / Giver / Observer
components in the world handle a given quest tag. Closes the
blocker-introspection loop — adopters with a quest tag in hand (from
an `FQuestActivationBlocker` payload, a UI list, or anywhere else)
can ask the framework "which actor offers this quest?" without
maintaining a parallel tag → actor registry on their side.

- **`Get Active Triggers For Tag`**, **`Get Active Givers For Tag`**,
  **`Get Active Observers For Tag`** — BP-callable functions on the
  SimpleQuest Blueprint library, returning an array of
  `FQuestRoleSourceInfo` entries. Each entry carries the live
  component reference, the live actor reference, and the tag the
  query resolved through. Designers drive nav hints, world-map
  markers, "head to X" arrows, and similar follow-ups directly from
  these references.
- **`Get Active Objective For Tag`** — returns the currently-live
  `UQuestObjective` instance for a given Step tag. Useful for UI that
  needs to bind directly to the Step's objective during its Live
  window (progress UI, hint panels, etc.).
- **Alias-aware queries.** A query against either the standalone
  form of a LinkedQuestline-inlined Step or the contextual form
  returns the same result. Designers don't need to know which
  compile context a Step is currently active under — the answer is
  consistent across every alias.
- **Self-registration.** Trigger / Giver / Observer components
  self-register at `BeginPlay` and unregister at `EndPlay` — no
  designer wiring required. Stale entries from destroyed actors
  filter out automatically.

### Objective self-introspection

`UQuestObjective` gains a small set of accessors letting an objective
discover its own position in the framework's tag address space and
find the actors targeting its Step — without manual outer-chain walks
or adopter-side side-registries.

- **`Get Owning Step Tag`** — the contextual tag of the Step that
  hosts this objective. Valid from `OnObjectiveActivated` onward.
- **`Get Owning Step Alias Tags`** — every additional perspective
  tag the framework has registered for the Step (typically empty
  outside the LinkedQuestline-inlined case).
- **`Get Triggers Targeting This Step`** / **`Get Givers Targeting
  This Step`** — convenience accessors chaining the owning-step tag
  into the source query API above. Lets an objective enumerate the
  actors authored to target it, e.g. to cache a trigger set at
  activation and present "X of M complete" UI per-event.

### Multi-target Objective surface

New framework primitive for per-target satisfaction signalling plus a
turnkey example Objective exercising the surface end-to-end. Closes the
"interact with all N target actors, disable each as they tick off"
authoring pattern without requiring adopters to maintain a parallel
tag → actor registry on their side.

- **`Publish Trigger Satisfied`** — new BP-callable on
  `UQuestObjective`. Pass an `FQuestObjectiveTriggerContext` whose
  `TriggeredActor` names the specific actor whose contribution was
  consumed. The framework publishes `FQuestTriggerSatisfiedEvent` on
  the step's tag channel (with multi-channel alias fan-out); Trigger
  Components on the satisfied actor filter by own-actor and react
  via their `OnQuestTriggerSatisfied` delegate.
- **`On Quest Trigger Satisfied`** — new multicast delegate on
  `UQuestTriggerComponent`. Carries `(QuestTag, MatchedChannel,
  FQuestTriggerSatisfiedEvent)`. Adopters bind to disable trigger
  visuals / collision / interaction prompts per-target while the
  step continues for the remaining targets. Own-fire filter
  (`SatisfiedActor == GetOwner()`) applied internally before broadcast.
- **`UInteractAllTargetsObjective`** — new example Objective subclass
  under `Objectives/Examples/`. Discovers its target set at activation
  via `Get Triggers Targeting This Step` (no authoring on the Step
  beyond assigning the objective class). Tracks per-actor satisfaction;
  publishes the new event per-tick; completes with the new outcome
  `SimpleQuest.Outcome.AllTargetsInteracted` when every target has
  fired. Designed as a reference shape for adopter multi-target
  objectives.

Adopter wiring on each target actor:
1. Add a `UQuestTriggerComponent` with `Step Tags To Trigger`
   including the Step's tag.
2. Call `SendTriggerEvent` from interaction code (overlap, interact
   key, etc.).
3. Optionally bind `OnQuestTriggerSatisfied` to drive per-target
   visual disable.

### Lifecycle event coverage — Activated event broadening

`FQuestActivatedEvent` now fires from every content node activation
path, not just giver-gated quests. Adopters building lifecycle-aware
UI no longer have to mix-and-match Activated and Started bindings
to cover both giver and non-giver authoring patterns.

- **Every content node fires Activated when execution reaches it.**
  Giver-gated, non-giver-gated, and asset-level scope entry all
  emit `FQuestActivatedEvent` on the node's tag channel.
- **Asset-level publish.** Starting a questline now publishes
  `FQuestActivatedEvent` and `FQuestStartedEvent` on the questline's
  own tag at scope entry, plus writes the asset-level **Live** state
  fact. Adopters binding `Observe Quest Lifecycle` to the questline tag
  receive the asset-level activation alongside per-content-node
  events. The questline's `DisplayName` / `Description` /
  `DisplayData` are also registered against the questline tag at
  graph-registration time, so the same subscriber can query authored
  display content for the asset-level event.
- **Activated and Started carry distinct semantics now.** Activated
  fires when execution reaches a node ("show the player what's
  starting"); Started fires when the node is Live with objectives
  bound and progress tickable. Both fire for every node; UI elements
  generally want Activated, gameplay reactors (triggers, completion
  watchers) generally want Started.
- **Catch-up reconstruction extended.** Late subscribers binding mid-
  session via `Observe Quest Lifecycle` now reconstruct `Activated` from
  either the **Live** or **PendingGiver** state facts. Reconstruction
  is routing-aware (Exact Match returns only the subscribed tag;
  Descendants fans out across every known descendant) and parent-
  first ordered so a subscriber bound on a questline tag receives
  the asset-level events before per-content-node events — same
  ordering the live cascade naturally produces.
- **Symmetric with §4.36 close-out.** The asset-level Live fact +
  questline-tag publish at activation mirrors the Completed fact +
  questline-tag publish at resolution that landed in §4.36. Adopters
  see consistent asset-level lifecycle signal at both ends.

### Lifecycle display data sample — `UQuestLifecycleDisplayData`

A new `UDataAsset` subclass shipping under `Display/Samples/` that
gives adopters a turnkey shape for per-lifecycle-event narrative
text. Designers populate beat arrays once on the data asset;
lifecycle subscribers consume them per event type.

- **Per-event narrative beats.** `Activated Beats`, `Enabled Beats`,
  `Started Beats`, `Disabled Beats`, `Deactivated Beats`, `Blocked
  Beats`, `Unblocked Beats`, `Activation Failed Beats`, and `Progress
  Refused Beats` are each an `FQuestNarrativeBeats` wrapper carrying a
  `TArray<FText>`. Designer authors any number of beat strings per
  event; UI iterates them at delivery time. The set now covers every
  observable lifecycle event, including the offer-phase and run-phase
  refusals.
- **Outcome-keyed completion beats.** `Completed Beats By Outcome` is
  a `TMap<FGameplayTag, FQuestNarrativeBeats>` keyed by outcome tag.
  Different outcomes can carry different completion text — `Victory`
  beats vs. `Defeat` beats, etc.
- **`Get Completed Beats` accessor with fallback chain.** Blueprints
  access completion beats through `GetCompletedBeats(OutcomeTag)`
  which returns the exact-outcome entry if present, falls back to a
  shared all-outcomes entry if authored, returns empty otherwise.
  The raw map is protected — adopters can't accidentally bypass the
  fallback by binding the map directly.
- **Extends `UQuestDisplayData`** — the marker base class introduced
  in §4.34 Phase 1. Adopter subclasses with different schema shapes
  (richer text, audio refs, etc.) compose alongside this sample
  via the same base.
- **Assignable on every content node.** Pick the data asset in the
  node's `Display Data` UPROPERTY; queryable at runtime via `Get
  Display Data` and cast to `UQuestLifecycleDisplayData`.

### SimpleUI — Typewriter Text Block FText migration

The Typewriter Text widget's queue API migrates from `FString` to
`FText` throughout, removing the `.ToString()` losses adopters were
hitting when piping localized strings into the widget.

- **All queue API methods take `FText`.** `Queue Text`, `Queue
  Texts`, `Insert Text At`, `Replace Text At`, `Get Queued Texts`
  (renamed from `Get Queued Strings`) — every input and the
  collection getter operate on `FText` directly.
- **Localization-respecting.** `FText` carries its namespace +
  source-string-key context through every queue operation;
  per-language displays update automatically when the project's
  active culture changes.
- **Empty-`FText` no-op semantic.** An empty `FText` queued via any
  entry path silently no-ops — the widget's internal guards check
  `IsEmpty` (not `IsEmptyOrWhitespace`), so whitespace-only entries
  are honored as legitimate designer-authored padding beats ("wait
  two seconds, show nothing, advance").
- **Per-character internals unchanged.** The internal display state
  is still `FString` (per-character revealing happens in string
  space); the `OnCharAdded` delegate still receives `FString`
  chunks. The migration is at the queue input/output boundary —
  exactly where adopters interact with the widget.

### Picker categories extensibility

K2 node Outcome and Questline pickers gain a settings-driven
extensibility point. Projects bridging quest outcomes / identities
with their own tag trees can now extend the picker filters without
forking source.

- **`Project Settings → Plugins → Simple Quest → Authoring`** —
  two new arrays: `Additional Outcome Picker Categories` and
  `Additional Questline Picker Categories`. Each entry is a tag
  prefix (e.g., `Game.Outcome`, `MyStudio.Quest`) whose descendants
  will appear in the picker alongside the framework default.
- **Affects the K2 node pickers** for the Complete Objective
  (Outcome) and Observe Quest Lifecycle (Questline) nodes. Designers
  see their own tag tree alongside `SimpleQuest.Outcome` /
  `SimpleQuest.Questline` without scrolling through unrelated
  project tags.
- **Live refresh.** Settings change refreshes open Blueprints
  automatically — no close-and-reopen needed to see the new
  categories in pickers.
- **UPROPERTY-bound pickers** (Observer's `ObservedTags`, Giver's
  `QuestTagsToGive`, etc.) still show only the SimpleQuest namespace
  in their pickers; UPROPERTY meta is compile-time baked. Settings-
  driven extension of those surfaces is planned for a future
  release if adopter demand surfaces.
- **Two compose-time rules enforced** (violations dropped with a
  warning log):
  - Entries under the framework-owned SimpleQuest namespace are
    rejected — extend with your own namespace.
  - Entries that overlap each other (duplicates, parent/child
    pairs) are pruned to one — the underlying gameplay-tag picker
    can't render overlapping namespace roots without crashing.

### Tag picker — click the label to open

The prefix-stripped tag pickers on quest graph nodes (Exit outcome chips,
Activation Group and Prerequisite Rule tag chips) now open the tag menu
when you click the tag label itself, not only the small dropdown arrow
beside it — matching the click behavior of the standard engine tag picker.

### Activation context restructure — deprecation notice (0.5.0)

`UQuestObjective::OnObjectiveActivated`'s single-parameter signature
is scheduled for restructure in 0.5.0 alongside save/load. The new
shape splits Authored/Runtime context onto the function signature
itself (two parameters — `FQuestObjectiveAuthoredConfig& Authored`,
`FQuestObjectiveRuntimeContext& Runtime`) instead of the current
nested struct. Adopter override sites will need to update their
signature when 0.5.0 ships (FunctionRedirects can't auto-fix
signature changes). Payload data and field semantics remain intact;
only the delivery container changes.

Doc-comment `UPCOMING CHANGE` blocks have been added to
`UQuestObjective::OnObjectiveActivated`,
`FQuestObjectiveActivationContext`, and its `Authored` / `Dynamic`
sub-struct fields telegraphing the change for adopters reading the
headers (IDE quick-docs or auto-generated API docs).

### Catch-all lifecycle delegate — `OnAnyQuestEvent`

`UQuestObserverComponent` gains a single catch-all delegate that fires
for every lifecycle event the observer receives, packaged into one
`FQuestLifecycleEventReport`. Broad-audience consumers — quest-log
HUDs, audio routers, telemetry — bind once and branch on event type,
instead of binding all ten per-type delegates and fanning them into
the same handler.

- **`FQuestLifecycleEventReport`** carries `QuestTag`, `MatchedChannel`,
  `EventType`, `Payload`, `OutcomeTag` (populated on Completed), and
  `GiverActor` (populated on Started, Give Blocked, and Progress
  Refused). The common fields a broad consumer needs; consumers wanting
  richer event-specific data (progress counts, blocker arrays) still
  bind the corresponding narrow delegate.
- **`EQuestLifecycleEventType`** — a `uint8` `BlueprintType` enum
  identifying which event arrived (the single-value companion to the
  multi-select `EQuestEventTypes` exposure bitmask), usable as a
  Blueprint `Switch` operand in the catch-all handler. Includes a
  `None` value for configuration use ("no event selected"); runtime
  dispatch never emits `None`.
- **Gated by the same per-tag opt-in flags** as the narrow delegates
  (`FObservedQuestEventSettings`) — events whose per-tag flag is off
  don't fire the catch-all. Bind `OnAnyQuestEvent` *or* the narrow
  delegates, not both, unless you want the same event delivered twice
  (there's no framework-side cross-subscription dedup).
- **Stays a thin fan-out, not a discriminating firehose** because the
  bus's subscribe-time filtering (per-tag flags, outcome filter,
  routing mode) has already narrowed delivery by the time it fires.

### Run-phase refusal event — `FQuestProgressRefusedEvent`

The refusal-event family now spans all three lifecycle phases: offer
(`FQuestActivationFailedEvent`), give (`FQuestGiveBlockedEvent`), and —
new — run (`FQuestProgressRefusedEvent`). It fires when an interaction
(typically a trigger) tries to progress a step that's structurally
reachable but can't advance: a prerequisite isn't met, or the step is
Blocked.

- **Observable through the standard surfaces.** New
  `UQuestObserverComponent::OnQuestProgressRefused` delegate plus a
  `bObserveProgressRefused` per-tag opt-in flag, and it flows through
  `OnAnyQuestEvent` automatically. Carries the blocker array and the
  originating trigger context, so adopter UI can surface contextual
  "can't do that yet, because X" feedback.
- **`ProgressRefused`** added to both `EQuestEventTypes` (exposure
  bitmask) and `EQuestLifecycleEventType` (arrival identifier).
- The struct is the former `FQuestTriggerBlockedEvent`, renamed and
  broadened — see Breaking changes for the migration.

### Per-context `CustomTag` — typed single-tag designer channel

`FQuestContextBase` (the shared base for trigger contexts, activation
contexts, and event payloads) gains a `CustomTag` `FGameplayTag` field
— a lightweight typed complement to the existing `CustomData`
(`FInstancedStruct`) for the common case where the game-specific data a
source carries is just a single tag (outcome routing, category, variant
selector).

- **No category filter.** Adopter-defined and framework-agnostic,
  symmetric with `CustomData`'s zero type restriction. The framework
  never interprets it; consumers read and branch on it.
- **Flows through the full pipeline.** A trigger's `CustomTag` rides
  through the trigger → objective boundary (readable in
  `TryCompleteObjective`) and back out onto completion / progress
  event payloads, the same as `CustomData`. Lets one trigger among
  several tell a shared objective which named outcome to resolve,
  without packing a one-field struct into `CustomData`.

### `Get Direct Parent Tag` — SimpleCore tag helper

A small `BlueprintPure` helper on the SimpleCore Blueprint library
returning a gameplay tag's direct parent (`X.Y.Z` → `X.Y`; an invalid
tag when the input is root-level or invalid). Exposes
`FGameplayTag::RequestDirectParent` to Blueprint graphs that walk tag
hierarchies — e.g., resolving a parent entry from a child's tag.

### Block now gates trigger progress

A Blocked step now refuses trigger-driven progress, not just gives and
(re)activation. Previously a manually Blocked step (the **Set Blocked**
node or `Set Quest Blocked`) would still complete when its trigger fired —
Block was enforced at the giver and at activation, but the trigger →
objective path checked only prerequisites, never the Blocked state. Firing
a trigger against a Blocked step now publishes a progress-refused event
(with the blocker reason set to **Blocked**, distinct from a prerequisite
refusal) and the step holds until it's cleared. Block still leaves the
trigger active and interactable — it refuses progress without disabling the
target, the same way it leaves a giver visible.

### `Is Quest Blocked` — lifecycle-state query

A `BlueprintPure` **Is Quest Blocked** on the SimpleQuest Blueprint library,
completing the lifecycle-state set alongside `Is Quest Live`, `Is Quest
Completed`, and `Is Quest Pending Giver`. Returns whether a quest or step
currently holds the Blocked state — for driving UI enablement, interactable
affordances, or any logic that needs to branch on whether a target is
blocked.

### Components join an in-progress quest cleanly

Quest components (observers, triggers, givers) now finish their registration
and catch-up one tick after `BeginPlay` instead of during it. A component's
`BeginPlay` runs before its owning actor's, so a component coming online into
an already-running quest could previously fire its catch-up — a Started cue,
an availability change, a trigger's activation — before the actor had finished
its own setup (built its materials, bound its delegates). Deferring a single
frame guarantees the actor is fully initialized first: spawn a trigger or
giver at runtime into a quest that's already live and it now syncs to the
current state with no setup-order workarounds. A trigger spawned onto an
already-live step also receives its activation event, which it previously
missed.

### Observer components replay *Activated* when joining a running quest

An observer component bound to a quest's `Activated` event now receives it
when the component joins a quest that's already live — including a quest that
started at game launch. Previously the component's catch-up reconstructed only
`Started` for a running non-giver quest, so a binding on `Activated` saw
nothing until the next live event. The `Observe Quest Lifecycle` Blueprint node
already reconstructed `Activated` from live state (see *Activated event
broadening* above); the component now matches it, so both observer surfaces
replay identically on join.

### Deactivation cascades pass through inactive nodes

A node's **Deactivated → Deactivate** wiring now relays the teardown to its
downstream nodes even when the node the cascade reaches has nothing of its own to
deactivate — one that's already resolved, or was never activated. Previously the
cascade halted at the first such node, so a teardown chain routed through a
completed node never reached anything past it.

An authored deactivation chain now tears down everything downstream regardless of
which nodes along the way have already finished — useful when the chain runs
through steps the player may have completed in any order. A node still emits its
own `Deactivated` event (to the components and observers bound to it) only when it
genuinely transitioned from active to deactivated, so passing the cascade through
an already-inactive node never fires a spurious deactivation. Cyclic and fan-in
teardown wiring is guarded against re-entry.

### Breaking changes (compiled-data + signatures)

- **`FQuestPathNodeList::ExitedGraphTags`** (and
  `UQuestNodeBase::ExitedGraphTagsOnAnyOutcome`) replaced by
  `ResolvedGraphs` / `ResolvedGraphsOnAnyOutcome` carrying the new
  `FQuestGraphResolution { GraphTag, OutcomeTag }` struct. **Recompile
  every questline asset after upgrade** — older compiled data drops
  the asset-resolution attribution silently (questline-level
  resolutions stop firing).
- **`UQuestObserverComponent::GetImplicitlyObservedTags`** return
  type changed from `FGameplayTagContainer` to
  `TArray<FQuestObservedTagSpec>`. Adopters who subclassed Observer
  with their own bridge override need to update the return type
  and wrap each tag in `FQuestObservedTagSpec { Tag, Routing }`.
  Default routing preserves prior hierarchical behavior.
- **`FQuestResolutionRecordedEvent` and `FQuestEntryRecordedEvent`**
  gain an `OriginatingEventID` field. New constructor overloads
  added; existing constructors preserved. Subscribers reading the
  events are unaffected (the field's just there to read).
- **`UQuestManagerSubsystem::SetQuestResolved`** /
  `UQuestStateSubsystem::RecordResolution` / `RecordEntry` gain a
  trailing optional `FOriginatingEventID` parameter. Existing call
  sites compile unchanged via the default argument; cascade-driven
  callers should pass the cascade's `OriginatingEventID` to enable
  per-event dedup downstream.
- **`UQuestObjective::DispatchOnObjectiveActivated`** signature
  widened to take a trailing `FGameplayTag InOwningStepTag`
  parameter. Only direct C++ caller is `UQuestStep::ActivateInternal`
  internally — adopter Blueprint objective subclasses and standard
  C++ overrides of `OnObjectiveActivated_Implementation` are
  unaffected.
- **SimpleCore subsystem header paths.** `USignalSubsystem` and
  `UWorldStateSubsystem` headers moved from `Signals/` and
  `WorldState/` into a flat `Subsystems/` directory, matching the
  convention SimpleQuest already uses. Adopter C++ code that
  `#include`s these directly needs to update the path:
  - `#include "Signals/SignalSubsystem.h"` → `#include "Subsystems/SignalSubsystem.h"`
  - `#include "WorldState/WorldStateSubsystem.h"` → `#include "Subsystems/WorldStateSubsystem.h"`
  No source-level API change; just file location. BP-level usage
  (Observe Quest Lifecycle, Add/Remove/Clear Fact utility nodes, the
  SimpleQuest BP library queries) is unaffected.
- **`UQuestTriggerComponent::SetActivated` and `SetTriggerSatisfied`
  removed.** Both were `BlueprintNativeEvent`s with no-op default
  impls. The framework's per-step lifecycle signals are now exclusively
  the rich delegates `OnQuestTriggerActivated`, `OnQuestTriggerDeactivated`,
  `OnQuestTriggerSatisfied`, `OnQuestTriggerResponded`, and
  `OnQuestTriggerBlocked`, each carrying the full event payload.
  Adopters previously overriding these BlueprintNativeEvents via
  component subclassing should bind the corresponding delegates
  instead — the rich delegates fire on the same lifecycle transitions
  with strictly more information.
- **`UQuestTriggerComponent::OnQuestTriggerActivated` delegate
  signature widened.** Previously `(bool bIsActivated)`; now
  `(FGameplayTag QuestTag, FGameplayTag MatchedChannel,
  FQuestStartedEvent Event)` matching the rich-payload pattern of
  the other trigger delegates. Adopter bindings need to update the
  handler signature; the payload carries the original `Event.QuestTag`
  identity plus the full started-event context.
- **`ESignalRoutingFlags` renamed and reshaped to `ESignalRoutingMode`.**
  The bitmask-style `UENUM(Flags)` is replaced by a plain
  `UENUM(BlueprintType)` with four discrete modes: `ExactMatch`,
  `Ancestors`, `Descendants`, `Bidirectional`. Routing pins / fields
  now render as a dropdown instead of a bitmask checkbox set. C++
  call sites pass the enum value directly (no `static_cast` from
  `int32`); UPROPERTY meta `Bitmask` + `BitmaskEnum` annotations
  removed at every site. A CoreRedirect entry handles the type +
  value renames for enum-typed properties; Blueprint K2 nodes that
  manually overrode the previous bitmask `int32` `Routing` pin may
  reset to the default `Descendants` on first load (re-pick the
  intended mode if needed). Helpers `FSignalRoutingDefaults::
  IncludesDescendants` and `IncludesAncestors` replace the prior
  `EnumHasAnyFlags(...)` checks at dispatch sites.
- **SimpleUI Typewriter Text Block queue API migrated `FString` →
  `FText`.** `Queue Text`, `Queue Texts`, `Insert Text At`, `Replace
  Text At`, and `Get Queued Texts` (renamed from `Get Queued
  Strings`) now take/return `FText`. Adopters with C++ or BP call
  sites passing `FString` need to wrap with `FText::FromString(...)`
  at the call boundary (or migrate the upstream value to `FText`
  directly to preserve localization context). The per-character
  internals and `OnCharAdded` delegate still operate in `FString`.
- **Compiler no longer substitutes `NodeLabel` for empty
  `DisplayName`.** Content nodes with an empty authored `DisplayName`
  now produce an empty runtime `DisplayName` (was: silent fallback
  to the editor-visible `NodeLabel`). Adopters who were relying on
  the implicit fallback for any content node should either author
  the desired `DisplayName` explicitly or pipe `NodeLabel` through
  themselves at the consumer.
- **`Bind To Quest Event` renamed to `Observe Quest Lifecycle`.** The
  K2 async-action node, its underlying class, and the BP library
  factory function all rename for naming honesty — the node is a
  latent observer of lifecycle state, not an explicit
  delegate-binding operation, and the new name reflects the
  designer-facing role rather than the subscription mechanism
  underneath. CoreRedirects (`ClassRedirect` for the async-action
  class, `ClassRedirect` for the K2 node class, `FunctionRedirect`
  for the factory function) auto-migrate saved Blueprint graphs and
  any C++ code referencing the names by reflected path. Specifics
  for adopters touching these directly:
  - K2 node display name `Bind To Quest Event` → `Observe Quest
    Lifecycle`.
  - Async-action class `UQuestEventSubscription` →
    `UQuestLifecycleObserver`.
  - K2 node class `UK2Node_BindToQuestEvent` →
    `UK2Node_ObserveQuestLifecycle`.
  - BP library factory `USimpleQuestBlueprintLibrary::BindToQuestEvent`
    → `USimpleQuestBlueprintLibrary::ObserveQuestLifecycle`.
  - The `ExposedAsyncProxy` reference output on the K2 node renames
    from `Subscription` to `Observer`. Existing graphs auto-update
    via the class redirect; saved variable pin names in BP graphs
    that captured the old `Subscription` name will need a one-time
    re-wire (the var-pin's underlying type updates correctly, only
    its label is stale until re-touched).
  - C++ headers move alongside their classes:
    `BlueprintAsync/QuestEventSubscription.h` →
    `BlueprintAsync/QuestLifecycleObserver.h`, and
    `K2Nodes/K2Node_BindToQuestEvent.h` →
    `K2Nodes/K2Node_ObserveQuestLifecycle.h`. Adopter C++ code
    `#include`ing these directly updates the path. C++ adopter code
    referencing the class name itself auto-resolves via the
    reflection redirect; direct C++ symbol references update at
    next recompile.

- **`FQuestTriggerBlockedEvent` renamed to `FQuestProgressRefusedEvent`.**
  The struct was always semantically a "progress refused" signal; the
  rename aligns the name with the meaning and broadens it onto the
  lifecycle observation surface (the new `OnQuestProgressRefused`
  delegate, `bObserveProgressRefused` flag, and `OnAnyQuestEvent`
  coverage — see *Run-phase refusal event* above). `UQuestTrigger-
  Component`'s own `OnQuestTriggerBlocked` delegate keeps its name for
  now and references the renamed struct. A `CoreRedirect` (`Struct`
  rename) auto-migrates saved Blueprint data and reflected C++
  references; direct C++ `#include` / symbol references update at next
  recompile.

### Known limitations

- The Prereq Gate's per-cascade dedup covers Path-keyed prereqs
  (Quest Resolution, Entry, Path, Outcome leaves) but not raw Fact-
  keyed prereqs — `FWorldStateFactAddedEvent` /
  `FactRemovedEvent` are SimpleCore primitives and don't carry the
  SimpleQuest-specific `OriginatingEventID`. A gate whose prereqs
  include raw Fact leaves can double-fire under the same authoring
  pattern. Rare in practice; flag if it surfaces.

### Fixed

- **Tag rename propagation no longer suppressed by unrelated compile
  errors.** Renaming a node while another node in the same compile
  has an unrelated error (e.g., an unset Objective on a different
  Step) used to silently drop the rename — the picker would update
  but the `OldName → NewName` redirect never landed, leaving loaded
  actor instances stuck on the old tag with nothing in the redirect
  map to heal them. Both compile entry points (per-graph editor
  button + Compile All toolbar command) now capture rename intent
  regardless of compile success; the rename gets propagated even
  when other errors fire elsewhere in the graph.
- **Reused-tag rebinding prevention.** Renaming a content node to
  a label that was previously renamed away from another node is
  now refused at the rename gate with a helpful error. Previously,
  the cross-chain redirect surgery during compile would silently
  free the old label as a fresh target, and unloaded assets
  (sublevels, data tables, BP CDOs) still referencing the old
  label would silently re-bind to the new owner on next load.
  The new check consults the GameplayTag redirect map at rename
  time and blocks reuse with a message pointing at
  `Project Settings → GameplayTags → Gameplay Tag Redirects` for
  the cleanup affordance when the reuse is intentional.
- **Display data queries now return authored content.** The compiler
  previously wasn't copying authored `DisplayName` / `Description` /
  `DisplayData` UPROPERTYs from content nodes onto their runtime
  instances, so `Get Display Name` / `Get Display Description` /
  `Get Display Data` on `UQuestStateSubsystem` always saw empty
  registry records. `Get Display Name` additionally hid the bug by
  silently falling back to a derived leaf-name reformat of the tag
  (e.g., `"Mayor.Tea.Time"` → `"Mayor Tea Time"`), making it look
  like authored content was working. Three-part fix:
  - **Compiler now copies the UPROPERTYs at compile time** so the
    runtime registry records carry the authored values.
  - **`Get Display Name` / `Get Display Description` / `Get Display
    Data` now return empty / null on missing records and log a
    `Warning` on `LogSimpleQuestState`.** The previous derived-leaf-
    name fallback in `Get Display Name` is removed. Loud failure
    surfaces missing-data bugs (registration gaps, compile drift)
    where the silent fallback masked them. Adopters who want auto-
    derived names can call `FQuestTagComposer::FormatTagForDisplay`
    explicitly; they're not forced into it as the framework default.
  - **NodeLabel fallback for empty `DisplayName` is removed.** An
    empty authored `DisplayName` is now treated as the designer's
    explicit "don't pipeline anything to display" signal — the
    compiler no longer substitutes the editor-visible `NodeLabel`.
    Paired with the loud-failure mode above and the Typewriter
    widget's empty-`FText` no-op (this release), the signal is
    clean end-to-end: empty author → empty registry → empty query
    → silent widget. Adopters who want to pipe the node label
    explicitly can read it via `NodeLabel` and queue it themselves.
- **`OnQuestObjectiveProgress` no longer auto-fires from inside
  `CompleteObjectiveWithOutcome`.** Previously, completing an
  objective implicitly broadcast a final Progress event before the
  Completion event, regardless of whether the objective had called
  `ReportProgress` on the same trigger. This caused duplicate
  Progress events for objectives that called `ReportProgress`
  themselves before completing, and produced spurious Progress
  events for objectives that have no per-fire progress semantic
  at all (e.g., binary "interacted yes/no" objectives). Progress
  is now purely explicit: the framework never auto-fires Progress.
  Objectives that want a final X/X tick before completion call
  `ReportProgress` explicitly first, then `CompleteObjectiveWithOutcome`.
  `UCountingQuestObjective::AddProgress` was updated cooperatively
  to preserve its existing observable "final X/X tick before
  completion" behavior — Counting-objective adopters see no
  behavior change. Listeners subscribing to Progress no longer
  receive events from objectives that never call `ReportProgress`,
  removing the need for defensive event-shape filtering.
- **`UTypewriterTextBlock::IsIdle` now correctly reports idle from
  inside `OnFinalDisplayDelayEnd` handlers.** Previously the
  `NextTextDelay` timer handle was still considered active by UE's
  TimerManager during the broadcast, so listeners querying
  `IsIdle()` (e.g., to decide whether to advance a queued display
  pipeline from a HUD-level joint-idle gate) saw the widget as
  still-not-idle. Fixed by explicitly clearing `NextTextDelayHandle`
  before the broadcast. Adopters relying on `IsIdle()` from within
  `OnFinalDisplayDelayEnd` handlers will now see the expected
  post-natural-completion state.
- **Activation Group node tooltips + doc comments corrected to
  reference SignalSubsystem rather than WorldState.** Three sites
  carried stale "WorldState" references from an earlier
  implementation that has since migrated to the transient signal
  channel approach: the Activation Group Entry node's input pin
  tooltip (designer-facing), the Activation Group Exit node's pin
  allocation source comment, and the `UActivationGroupSetterNode`
  runtime class doc comment (which still described the migration
  as future-tense after it had completed). Activation Groups
  publish `FQuestActivationGroupTriggeredEvent` on the GroupTag's
  SignalSubsystem channel; Listeners subscribe to that channel.
  Designer-facing tooltip and source documentation now accurately
  describe the publish mechanism. Adopters reading source for
  cross-system bridge patterns (any system can publish on a group's
  tag channel — Setter nodes are one publisher among potentially
  many) see the correct subsystem reference.
- **LinkedQuestline node display fields fall back to the linked
  asset's defaults.** A LinkedQuestline node's `DisplayName` /
  `Description` / `DisplayData` now fall back per-field to the linked
  questline asset's own authored class-default values when the outer
  node leaves them empty. Previously the asset-level defaults were
  silently dropped on the inline path, surfacing only when the
  questline was used at root level. Outer-node authoring still
  overrides per-field where present.
- **Questline asset-level display registration honors an empty
  `DisplayName`.** The runtime registry write for a questline's
  asset-level display data no longer substitutes the asset's short
  name when the authored `DisplayName` is empty — the "empty means
  don't surface this in UI" convention now holds at the questline
  level, matching content nodes. Editor surfaces (outliner, tooltips)
  still show the asset short name for readability; only the runtime
  registry write changed, via a new accessor that returns the raw
  authored field rather than the editor-convenience fallback.

### SimpleUI — Typewriter Text Block expansion

The Typewriter Text widget (introduced in 0.4.0's SimpleUI plugin
spin-up) gains a full set of designer-facing controls for the
dialogue-pacing use cases adopters typically need.

- **Skip controls.** `SkipToEndOfCurrentString` reveals the rest of
  the currently-displaying string immediately and starts the
  configured between-strings dwell. `SkipToNextString` does the same
  but also skips the dwell and advances to the next queued string.
  `SkipAllRemaining` drains the entire queue, firing the Start/End
  event pairs for every skipped string so adopter-side per-string
  telemetry or persistence stays consistent.
- **Pause and resume.** `PauseTypewriter` freezes the typewriter or
  dwell at its current point, saving the remaining timer duration.
  `ResumeTypewriter` picks up exactly where it left off. Useful for
  modal interruptions — open a sub-menu, dialogue pauses; close it,
  dialogue continues from the same character.
- **State queries.** `IsDisplaying` / `IsInDwell` / `IsPaused` /
  `IsIdle` describe the widget's current state for input handlers
  ("is there something to skip?"). `GetCurrentDisplayProgress`
  returns a 0–1 progress value over the currently-displaying string,
  suitable for driving a progress bar or skip-prompt visibility.
- **Queue manipulation.** `InsertTextAt`, `RemoveTextAt`, and
  `ReplaceTextAt` give narrative middleware finer control than the
  existing append / front-of-queue / clear primitives. Index 0
  (the currently-displaying string) interacts cleanly: replacing it
  restarts display with the new content; removing it advances to
  the next queued string.
- **Live-applied configuration.** `SetDisplayDelayMinMax`,
  `SetNextStringDelay`, and `SetUseTypewriterEffect` re-schedule the
  active timer when called mid-display so a designer can speed up,
  slow down, or skip-to-end via property change during playback —
  e.g., the player holding a fast-forward button can scale the
  per-character delay live without waiting for the current
  character's scheduled tick to complete.
- **Paused-state-aware integrations.** All skip functions, queue
  manipulation, and interrupt paths recognize a paused-in-flight
  string as still-active for event-pairing purposes. Aborting a
  paused string via `ClearTextQueue(true)` or interrupt fires its
  pending `OnDisplayStringEnd`; skipping a paused string reveals
  the remainder and advances normally.


---

## [0.4.0] — 2026-05-20 — Architectural Cohesion + Adopter Ergonomics

Where v0.3 began to reveal the shape of the framework, v0.4 fully describes it.

### Component model — Observer / Trigger / Giver linear inheritance

`UQuestObserverComponent` is now the base of a linear inheritance
chain. `UQuestTriggerComponent` inherits Observer's full lifecycle
event surface; `UQuestGiverComponent` inherits both. An actor that
gives quests AND responds to trigger events drops a single Giver
component instead of stacking a Giver plus a separate Observer.

- Tags configured on a derived component (Giver's `QuestTagsToGive`,
  Trigger's `StepTagsToTrigger`) automatically wire up to the
  inherited event surface. You don't author a parallel
  `ObservedTags` map — the component does the bridging for you.
- `UQuestGiverComponent` exposes a single rich-payload
  `OnGiveAvailabilityChanged` delegate, replacing the prior bundle
  of per-event Giver delegates. It fires when the set of offer-able
  or accept-ready quests on this giver changes, carrying a delta
  payload that describes what just became available, unavailable,
  enabled, or disabled. Per-give success and refusal still come
  from the inherited `OnQuestStarted` and `OnQuestGiveBlocked`
  delegates — filter by `GiverActor == GetOwner()` to scope them
  to your giver instance.
- Action APIs widened. `GiveQuest`, `ActivateQuest`, `ResolveQuest`,
  `SetQuestBlocked`, `ClearQuestBlocked`, `StartQuestline`, and the
  Trigger-send entry points all accept context payloads that
  propagate through to outbound lifecycle events. Adopter code can
  attach `Instigator`, `OriginTag`, `OriginChain`, and a custom
  `FInstancedStruct` payload at any write site; downstream
  subscribers receive them as part of the standard `FQuestEventPayload`.

### Path / Outcome architectural separation

Pin-wired prereqs now distinguish *which authored output path* fired
the upstream resolution, not just *which outcome*. Closes a real
prereq-wiring gap where two parallel paths on a content node sharing
the same outcome tag would both satisfy a downstream prereq wired to
just one of them — silently dropping the designer's authored intent.
After this change, wiring a prereq into a specific output pin means
only that pin's path satisfies it.

Underneath, the prereq expression schema gained two new leaf types
and two new authoring nodes built on top of them:

- `Leaf_Path` — emitted by the compiler whenever a prereq is wired
  from a specific output pin on a content node. Satisfies only when
  the source quest resolved through that exact authored path
  (matches the pin's `PathIdentity`, an `FName`). Queries
  `UQuestStateSubsystem::HasResolvedAtPath`.
- `Leaf_Outcome` — context-free outcome leaf. Satisfies when ANY
  quest resolves with the named outcome tag (or any descendant via
  gameplay-tag hierarchy). Queries
  `UQuestStateSubsystem::HasAnyQuestResolvedWith`. Subscribes
  directly on the outcome tag channel (see "Outcome-channel
  publishing" below).

New authoring nodes:

- **Prerequisite Fact Tag** — gates on the presence of any World
  State fact tag. Decoupled from graph topology entirely; lets you
  gate quests on systemic state from anywhere in the project (faction
  reputation, inventory facts, system-level flags) without routing
  it through the quest graph.
- **Prerequisite Outcome** — gates on a context-free outcome tag.
  Satisfied when any quest resolves with the picked outcome, no
  matter which quest produced it. Picker filtered to
  `SimpleQuest.Outcome.*` for ergonomic narrowing.

Both new nodes render with the same visual shape as Prerequisite
Rule Exit — title bar above, inline tag picker on the same row as
the output pin. Tag-picker prereq leaves are now visually consistent
across the three variants.

Quest resolution records (`FQuestResolutionEntry`) and the broadcast
event (`FQuestResolutionRecordedEvent`) both gained a `PathIdentity`
field carrying the source pin's `FName`. The event keeps a 4-arg
constructor for back-compat; new construction sites prefer the 5-arg
form to preserve graph-authored path identity.

### Tag rename propagation through Blueprints and assets

Renaming a quest node or asset in the questline editor now flows
through every `FGameplayTag` reference shape on next compile,
across loaded and unloaded content. Closes a common friction class
where renames would silently break Blueprint subscriptions and
component bindings.

Coverage:
- Property fields on components and actors (`FGameplayTag` or
  `FGameplayTagContainer`).
- K2 node pin defaults in Blueprint graphs — the literal tag values
  authored on `Bind To Quest Event` and similar nodes that store
  tags as pin strings rather than struct values.
- Default values on Blueprint Class Defaults, including private
  (non-`Instance Editable`) variables that don't reload simply by
  reopening the asset.
- Data Assets and Data Tables. Rows in an open Data Table editor
  live-refresh on rename — no need to close and reopen the asset.
- Nested struct fields, including custom structs that wrap or embed
  `FGameplayTag`.
- Adopter custom components and asset types. No inheritance from
  any specific base class required; the framework finds matching
  fields automatically.

Assets that weren't loaded at compile time heal transparently on
next load via Unreal's standard `GameplayTagRedirects` machinery,
and the framework flags them dirty so Save All catches the healed
value to disk. Avoids the usual need to restart the editor to apply
`GameplayTagRedirects`. They are registered and take effect immediately 
on graph compilation.

Authoring-time guardrail: the editor refuses renames that would
collide with another node's current name OR last-compiled identity.
The collision is detected before the rename commits, with a clear
error message — so subscribers never silently migrate to the wrong
node because two nodes briefly shared a name across a compile
boundary.

Compile-time guardrail: any new rename operation that would create a
collision with some prior name in an existing chain of redirects is 
detected at compile time. The prior chain is broken at the point of 
collision and the downstream tag is then connected directly to all
immediate upstream tags, preserving the transitive nature of the 
pre-existing redirect chain, ensuring all tags are accurately redirected.

The compile pipeline batches all rename-redirect writes into a
single end-of-compile pass, so the per-graph **Compile** toolbar
button now runs at parity with the project-wide **Compile All
Questlines** action. No felt-slowness regression on iterative
authoring cycles.

### Activation failure observability

Quest activation refusals are now observable. When an `ActivateQuest`
call refuses — because the tag isn't registered, the quest is
already live, the quest is already waiting on a giver, or the quest
is blocked — `OnQuestActivationFailed` fires on any
`UQuestObserverComponent` subscribed to the quest's tag.

- Per-quest opt-in via the **Observe Activation Failed** checkbox
  on the observer's tag entry (off by default; debug-leaning
  feature most adopters won't need outside diagnostic flows).
- The delegate carries the attempted tag both as a registered
  `FGameplayTag` (when valid) and a raw `FName` (so designers can
  read what was attempted even when the tag isn't registered at
  all), the refusal reason as an `EQuestActivationBlocker` enum,
  and the standard `FQuestEventPayload`.
- Subscriptions bound on a parent tag receive failures from any
  descendant — even when the failed tag isn't in the gameplay tag
  manager. Combined with the raw `FName`, this means a single
  observer bound on `SimpleQuest.Questline.MyArc` can catch
  failures across the whole arc, including typo'd tags that never
  registered to begin with.

### Trigger response surface — per-fire and per-lifecycle feedback

The Trigger Component's outbound surface (`SendTriggerEvent`) now
has a matching inbound feedback surface. Designers wiring trigger
volumes to objectives get response, block, and lifecycle-wrap signals
back without authoring a parallel subscription pipeline.

Three new events with matching delegates on `UQuestTriggerComponent`:

- **`OnQuestTriggerResponded`** — per-fire response from the
  objective. `EQuestTriggerResolution` discriminates `Progress`
  (counter advanced), `Completed` (objective resolved with an
  outcome), and `Refused` (objective received the fire but declined
  to act on it, with a designer-supplied refusal reason tag). Echoes
  the originating fire's `FQuestObjectiveTriggerContext` for own-fire
  scoping.
- **`OnQuestTriggerBlocked`** — per-fire structural block. Fires
  when a trigger reaches a step that's been activated but can't make
  forward progress because of structural blockers (Blocked fact set,
  or unmet prerequisites in `GatesProgression` mode). Carries the
  same `FQuestActivationBlocker[]` payload adopters already handle
  for `OnQuestGiveBlocked` — one translator covers both surfaces.
- **`OnQuestTriggerDeactivated`** — per-lifecycle wrap. Fires once
  per watched step's lifecycle end. `EQuestTriggerEndReason`
  discriminates `Completed` (step completed normally), `Interrupted`
  (step deactivated before completion), and `Manual` (objective
  signaled trigger-side wrap explicitly via the BP-callable below).

Auto-publishes wire Response and Deactivated into the existing
lifecycle flow — adopters using the default flow get
Progress / Completed Responses and Completed / Interrupted
Deactivations for free. Refusal and Manual deactivation are explicit
objective-side operations:

- **`RefuseTrigger(RefusalReason, TriggerContext)`** — new
  Blueprint-callable on `UQuestObjective`. Call from inside
  `TryCompleteObjective` when a trigger fire doesn't satisfy game-
  logic conditions (wrong actor, missing item, wrong phase).
  Publishes a Response event with `Resolution = Refused` and the
  designer-supplied reason tag.
- **`PublishTriggerDeactivation(OutcomeTag, FinalContext)`** —
  new Blueprint-callable on `UQuestObjective`. Call when an
  objective wants to release trigger-side audiences early without
  ending the step — for example, a multi-phase objective that's
  done with one trigger volume's input but isn't yet ready to
  complete.

**Originating-component identity stays framework-stamped.** Each
fire carries a framework-managed pointer to the publishing Trigger
Component that survives any adopter mutation of the payload
(legitimate retargeting of `TriggeredActor` for game logic, etc.).
Response and Blocked deliveries filter via pointer-equality against
this stamp so the publishing component always receives feedback for
its own fires, no matter what the objective does to the payload in
between.

**Attribution auto-forwarding** — `ReportProgress`,
`CompleteObjectiveWithOutcome`, `RefuseTrigger`, and
`PublishTriggerDeactivation` auto-fill `TriggeredActor`, `Instigator`,
and `CustomData` from the originating fire when the adopter omits
them. Adopters can pass explicit context to override per-field; the
framework fills the rest. Eliminates the silent-failure mode where a
missing `TriggeredActor` in an adopter's
`CompleteObjectiveWithOutcome` call would cause downstream subscriber
filters to silently reject the Response event.

**Coverage of the `GatesProgression` silent-ignore.** Steps with
`EPrerequisiteGateMode::GatesProgression` previously dropped trigger
fires silently when prerequisites were unmet. The framework now
publishes `FQuestTriggerBlockedEvent` with the unsatisfied leaf tags
in that case, so trigger-side UI can react to "you need to satisfy
X first" without authoring its own prereq monitoring.

`FQuestObjectiveTriggered` renamed to `FQuestTriggerFiredEvent` for
naming consistency with the rest of the Quest Trigger event family.
Existing assets and Blueprint references heal via `StructRedirects`
on next load; no manual migration required.

### Outcome-channel event publishing

Quest resolutions and entries now publish on the outcome tag channel
in addition to the canonical quest channel. Subscribers can bind
directly to an outcome tag (e.g., `SimpleQuest.Outcome.Victory`) and
receive every resolution that matches — across all quests, with the
gameplay-tag hierarchy walk handled by the signal bus.

Useful for systems that key off outcomes rather than quests:
achievement trackers, reputation systems, telemetry, audio cue
layers. Replaces the per-quest subscribe-and-filter pattern with a
direct subscribe on the outcome itself.

Signal bus deduplication means subscribers bound on both the quest channel
AND the outcome channel still get exactly one callback per logical
resolution event.

### `FSignalEventBase` marker struct

A bare marker base in SimpleCore that all signal-bus event structs
inherit from. Doesn't add fields or behavior — the bus's runtime
contract still admits any USTRUCT — but provides:

- A canonical compile-time concept (`CSignalEvent`) for template
  constraints on event-handling APIs.
- A meta-struct filter target for Blueprint picker UX (used by the
  Subscribe Message Of Type node, below).

SimpleQuest's `FQuestEventBase` now derives from `FSignalEventBase`,
so all quest lifecycle events flow through the picker filter
naturally. Adopters can derive their own event structs from
`FSignalEventBase` to get the same picker integration.

### Blueprint Subscribe nodes

The signal bus's `SubscribeMessage` is now Blueprint-callable via
two nodes on `USimpleCoreBlueprintLibrary`:

- **Subscribe Message** — untyped variant. Receives every event
  published on the channel as an `FInstancedStruct` payload.
  Branch on the payload's identity inside the handler.
- **Subscribe Message Of Type** — typed-filter variant. Pass a
  `UScriptStruct*` for the payload type; only events matching that
  type (or a derived type) fire the handler. Removes the "is this
  the event I care about?" branch when you only care about one
  event shape on a busy channel.

The **Payload Type** picker on Subscribe Message Of Type is filtered
to descendants of `FSignalEventBase`, narrowing the dropdown to
event-shaped structs only rather than every USTRUCT in the project.

Pairs with **Unsubscribe Listener** (also new this release) for
single-call teardown — call from `EndPlay` and the bus clears every
subscription this listener registered.

### Signal bus convenience

`USignalSubsystem` (the gameplay-tag-routed event bus underlying
all framework event routing) gains an
`UnsubscribeListener(UObject* Listener)` convenience method. One
call from your subscriber's `EndPlay` or `BeginDestroy` clears
every subscription that listener registered, across every channel.
Replaces the per-(channel, handle) tracking pattern previously
required for clean teardown.

- Exposed to Blueprint via `USimpleCoreBlueprintLibrary::UnsubscribeListener`.
  Drop the **Unsubscribe Listener** node in your actor's `EndPlay`
  and wire `self` to the Listener pin (the WorldContext pin
  auto-fills).
- Quest components (`UQuestObserverComponent`,
  `UQuestTriggerComponent`, `UQuestGiverComponent`) now use this
  hook automatically — they clean themselves up on destruction
  without the bus accumulating stale subscriber records from
  departed actors over a long session.

### Per-channel log verbosity

`LogSimpleQuest` splits into five categories so you can dial each
concern independently. The previous all-or-nothing pattern (compile
firehose drowning the gameplay trace, or gameplay trace burying
the compile diagnostics) goes away.

| Channel | Coverage |
|---|---|
| `LogSimpleQuest` | Module startup, settings, debug overlay, anything not covered below |
| `LogSimpleQuestActivation` | Quest activation flow — starts, chain advancement, deactivation |
| `LogSimpleQuestSubscription` | Component and Blueprint subscriptions; catch-up event delivery |
| `LogSimpleQuestCompiler` | Graph compile output, native tag registration, tag rename propagation |
| `LogSimpleQuestState` | Quest history recording — resolutions, entries, tag registrations |

Set verbosity per channel under **Project Settings → Plugins →
Simple Quest → Logging** (or **Simple Core** for `LogSimpleCore`).
Changes apply live — no editor restart, no `DefaultEngine.ini`
edit. Values persist across sessions automatically.

### Settings page reorganization

Project Settings is split into three pages, sorted by audience.

- **Project Settings → Plugins → Simple Quest** — quest manager
  class designation and the five log verbosity dials. The adopter
  surface.
- **Project Settings → Plugins → Simple Core** — verbosity dial
  for `LogSimpleCore`. Separate page so SimpleCore stays usable
  standalone for projects that don't pull in SimpleQuest.
- **Editor Preferences → Plugins → Simple Quest Visuals** —
  wire, pin, node title, and debug-highlight colors used by the
  questline graph editor. Per-developer (saved to local Editor
  Preferences, not source-controlled).

The previous single Project Settings page bundled colors alongside
the adopter knobs; colors are out of that surface entirely now so
first-impression scanning stays focused on what adopters actually
configure.

### Block + deactivate convenience

The graph-driven `SetBlocked` node has had an **Also Deactivate
Targets** toggle for a while — when checked, blocking a quest also
deactivates it in the same authored step. That convenience now
exists for external callers too: `FQuestBlockRequestEvent` and the
Blueprint-callable **Set Quest Blocked** node both accept an
optional `bAlsoDeactivate` flag (default off — Block and
Deactivate stay separate concerns by default).

When checked, the framework fires the deactivate request alongside
the block — even if the quest was already blocked. So a quest
that's blocked but still live can be deactivated by the same call
that would have been a no-op on the block side alone.

### Electronic Nodes integration — stock 5.7+ marketplace plugin

The Electronic Nodes (EN) visual integration that styles questline
graph wires now uses the stock marketplace EN plugin on UE 5.7+. The
previously-required EN fork is no longer needed; adopters install
EN from the marketplace and the visual layer activates automatically
when the plugin is enabled in their project.

Behavior matrix:

- **UE 5.7+ with EN installed and enabled** → integration loads and
  renders dashed prerequisite wires across all EN wire styles
  (Default, Manhattan, Subway), through knot chains, with stable
  visual behavior under node drag.
- **UE 5.7+ without EN** → integration inert, framework otherwise
  unaffected.
- **UE 5.6 (any EN state)** → integration is gated off entirely.
  EN's 5.6 release predates the spline-rendering hook needed for
  the dash effect, so the framework runs cleanly without the EN
  visual layer on 5.6. All other framework functionality works
  identically on 5.6 and 5.7+.

The integration hooks `FENConnectionDrawingPolicy::MakeDrawSpline`
(introduced in EN's 5.7 release) and overlays a dashed-rendering
pass for wires entering a Prerequisites input. Knot-traversed prereq
chains are detected via the existing schema mixin's topology walk —
prereqs routed through one or more knots still dash correctly.

### Cleanups

- Older transitional outcome tag prefixes (`Quest.Outcome.*` from
  pre-0.4.0 and a short-lived `SimpleQuest.QuestOutcome.*` from
  mid-0.4.0 development) no longer have runtime fallback handling.
  The `GameplayTagRedirects` entries that migrate them to the
  canonical `SimpleQuest.Outcome.*` form remain in place, so
  upgrade migration still works transparently — adopters with
  older content rely on the standard redirect chain rather than
  special-case detection logic.
- Internal cleanup: the unused `FQuestTagComposer::MakeEntryPathFact`
  helper deleted (no public-API impact).


### Tag namespace migration table

Six namespace roots migrated. The 0.4.0 cycle landed the move in
two stages — an initial consolidation under `SimpleQuest.*` that
kept "Quest" as a secondary prefix, then a follow-up pass that
trimmed the redundant "Quest" substring and renamed the identity
prefix to "Questline" (matching the `UQuestlineGraph` asset
class — the prior "Quest" identity was misleading since Steps
and Prereq Rules also lived under it). Adopters of pre-0.4.0
content get redirects for both stages, so migration is one-shot
regardless of which intermediate state the content was authored
against.

| Was (pre-0.4.0) | Final (0.4.0) |
|---|---|
| `Quest.*` (quest identifiers) | `SimpleQuest.Questline.*` |
| `Quest.Outcome.*` (named outcomes) | `SimpleQuest.Outcome.*` *(promoted out of Quest — outcomes are decorators, not lifecycle events)* |
| `QuestState.*` (state facts) | `SimpleQuest.State.*` |
| `QuestPrereqRule.*` | `SimpleQuest.PrereqRule.*` |
| `QuestActivationGroup.*` | `SimpleQuest.ActivationGroup.*` |
| `Quest.Channel.*` (signal-bus channels) | `SimpleQuest.Channel.*` |

Resulting authored tag shapes:

```
SimpleQuest.Questline.MyAsset.Step1
SimpleQuest.State.MyAsset.Step1.Live
SimpleQuest.Outcome.Combat.Victory
SimpleQuest.PrereqRule.MyRule
SimpleQuest.ActivationGroup.MyGroup
SimpleQuest.Channel.Given
```

A comprehensive `GameplayTagRedirects` block in
`Config/DefaultGameplayTags.ini` covers every namespace root and
every seeded child tag. Adopter projects upgrading from pre-0.4.0
content don't need to manually rewire any tag pickers — the
redirect chain rewrites old tag references on asset load and
persists the new namespace on next save. Blueprint Class Default
references and other property-stored tag fields heal automatically;
questline asset internals heal via **Compile All Questlines** from
the editor's main menu. The pre-flight and post-flight stale-tag
scans verified byte-identical results across the migration,
confirming the redirect chain caught every reference.

Also fixed in this pass: a pre-existing bug where prerequisites
authored on LinkedQuestline nodes never made it into the compiled
runtime data. Prereq gating on LinkedQuestline nodes now works as
designers expect on next compile.

### Added

#### `SimpleQuest.*` namespace as the new tag tree root

Every plugin-introduced gameplay tag now lives under a single
`SimpleQuest.*` parent. Picker filters scope correctly to the new
tree by category — for example, `Categories="SimpleQuest.Outcome"`
on an outcome-tag UPROPERTY narrows the picker to authored outcome
tags only.

Compiler-emitted tag shapes (the runtime addresses the framework
routes events through):

- Node identifiers: `SimpleQuest.Questline.<QuestlineID>.<NodeName>`
- State facts: `SimpleQuest.State.<...>.Live` /
  `.Completed` / `.PendingGiver` / `.Deactivated` / `.Blocked`
- Per-node outcome facts: `SimpleQuest.State.<...>.Outcome.<leaf>`

#### Namespace finalization — secondary prefixes slimmed
After the initial `SimpleQuest.*` consolidation, a follow-up pass
dropped the redundant `Quest` substring from secondary prefixes and
renamed the identity prefix to `Questline` (matching `UQuestlineGraph`
the asset class — the prior `Quest` identity was misleading since
Steps and Prereq Rules lived under it too).

- Tag picker filter scopes updated to the new shape (e.g. designers
  now author against `Categories="SimpleQuest.Outcome"` rather than
  `Categories="SimpleQuest.QuestOutcome"`).
- C++ constant identifiers are unchanged — only the tag string
  values they map to. `Tag_Channel_QuestGiven` still refers to the
  same constant; its value is now `SimpleQuest.Channel.Given` rather
  than `SimpleQuest.QuestChannel.Given`. Existing source references
  compile and resolve transparently.
- Why this matters: the doubled `Quest` substring was visual noise
  in the tag picker — every leaf showed up under a `Quest.Quest...`
  path. The trimmed namespace reads cleanly for common-node
  authoring.

#### `GameplayTagRedirects` block for pre-0.4.0 migration

The project's `Config/DefaultGameplayTags.ini` ships with a
comprehensive `GameplayTagRedirects` section that migrates every
pre-0.4.0 namespace root and every seeded child tag to the final
`SimpleQuest.*` shape. Unreal's gameplay tag redirects match by
exact tag string (not by prefix), so each migrated tag gets its
own explicit entry. C++-defined channels and example outcomes are
also redirected explicitly — defensive coverage for any external
Blueprint that referenced them by string literal before the rename.

#### Compile-time guards against stale Asset Registry metadata
The compiler skips empty / `None` tag entries during native tag
registration, preventing junk entries from accumulating in
`CompiledTags.ini` when stale Asset Registry data carries malformed
tag names. Transitional safeguards also accept the legacy
`Quest.Outcome.*` prefix during compile so projects upgrading from
pre-0.4.0 content don't see compile-time noise before they recompile
their authored assets.

#### Quest event lifecycle redesign

The lifecycle event vocabulary expands to distinguish "this quest is
offerable" from "this quest is actually startable right now" — and
adds first-class refusal events so adopters can build "locked icon"
and "go talk to the innkeeper" UI without writing query boilerplate.

- **`FQuestActivatedEvent`** — fires once per giver-gated quest the
  moment its activation wire arrives. Always fires regardless of
  prerequisite state. The carried `FQuestPrereqStatus` payload
  reports overall satisfaction plus a per-condition breakdown, so
  designer-side UI can show a locked-icon indicator with concrete
  "what's missing" detail. Non-giver quests skip this event and
  proceed straight to `FQuestStartedEvent`.
- **`FQuestEnabledEvent`** — fires when a quest becomes truly
  accept-ready (both activated AND prerequisites satisfy). Fires
  the same tick as `Activated` if prereqs were already met, or
  later when the relevant facts flip. Pairs naturally with the
  "don't show give-UI until prereqs are fulfilled" pattern.
- **`FQuestDisabledEvent`** — symmetric partner to `Enabled`,
  fires when a previously-enabled quest returns to non-ready
  state (rare; covers NOT-prereq edge cases where a fact unsets
  after satisfying the expression).
- **`FQuestGiveBlockedEvent`** — refusal response when an attempted
  give fails. Carries a structured array of `FQuestActivationBlocker`
  entries (one per reason the quest currently isn't startable) plus
  the giver actor that initiated the attempt. Global subscribers
  (debug overlays, telemetry, party-shared UI) can opt in to this
  channel without coupling to the give-side flow.
- **`FQuestActivationBlocker`** — structured "why can't this start"
  entry. `EQuestActivationBlocker` enum reasons: `PrereqUnmet`
  (with the unsatisfied condition tags listed), `Blocked`,
  `AlreadyLive`, `NotPendingGiver`, `UnknownQuest`.
- **`FQuestPrereqStatus` / `FQuestPrereqLeafStatus`** — prerequisite
  snapshot structures. `FQuestPrereqStatus` reports overall
  satisfaction; the per-leaf array reports which individual
  conditions evaluated true or false. Available wherever an event
  payload carries it, and queryable via `GetQuestPrereqStatus` on
  the state subsystem.
- **`GiverActor` field on `FQuestStartedEvent`** — giver-initiated
  quests carry the offering actor in their start event payload so
  subscribers can correlate which NPC kicked off the chain.
  Catch-up subscribers receive the same field by reading it from
  the quest history records (see two-layer state below).

#### Manager / state subsystem split

Quest state read access moves out of the manager subsystem onto a
new public read API, matching how `UWorldStateSubsystem` works on
the SimpleCore side.

- **`UQuestStateSubsystem`** (renamed from `UQuestResolutionSubsystem`).
  The naming convention — `...StateSubsystem` — signals "externally
  accessible fact registry with potentially limited write access"
  across the suite. Future suite plugins (dialogue, progression)
  will follow the same convention for their own state queries.
- **`QueryQuestActivationBlockers(QuestTag)`** — Blueprint-callable
  query returning an array of `FQuestActivationBlocker` entries.
  Empty array means the quest is currently startable; non-empty
  lists every distinct reason it isn't. Same underlying logic
  feeds this query and the `FQuestGiveBlockedEvent` refusal
  publication, so synchronous query and reactive event paths can
  never drift apart.
- **`GetQuestPrereqStatus(QuestTag)`** — Blueprint-callable accessor
  for the prerequisite snapshot recorded at activation time.
- All writes still flow through `UQuestManagerSubsystem`. The state
  subsystem is read-only from adopter code; the manager owns the
  black-box write surface.

#### `UQuestGiverComponent` lifecycle event surface

`UQuestGiverComponent` initially exposed six per-event delegates
matching the new lifecycle granularity (`OnQuestActivated`,
`OnQuestEnabled`, `OnQuestDisabled`, `OnQuestStarted`,
`OnQuestDeactivated`, `OnQuestGiveBlocked`). The late-0.4.0 sprint
later consolidated these into a single `OnGiveAvailabilityChanged`
delegate carrying a delta payload (see **Late 0.4.0 sprint** above
for the final shape). Per-give success and refusal notifications
come from the inherited `UQuestObserverComponent` delegates with
`GiverActor == GetOwner()` filtering.

- **`ActivatedQuestTags`** read-only container on the component,
  parallel to `EnabledQuestTags`, surfacing the proactive set
  ("which of my quests could be offered if their prereqs were met")
  for UI iteration.
- **`QueryActivationBlockers(QuestTag)`** convenience method on
  the component that delegates to `UQuestStateSubsystem`.

#### `Bind To Quest Event` — full lifecycle exposure with per-instance pin control

The async Blueprint node now exposes the full lifecycle event surface
with per-placement pin customization, so designers can configure each
instance to listen to only the events they care about.

- **9 lifecycle event pins** in lifecycle order: `On Activated`,
  `On Enabled`, `On Give Blocked`, `On Started`, `On Progress`,
  `On Completed`, `On Deactivated`, `On Blocked`. Plus the always-
  present `Subscription` reference output.
- **Per-instance pin toggling** via the node's right-click context
  menu, grouped into Offer / Run / End sub-sections by lifecycle
  phase. A node that only listens for `On Completed` shows just
  that pin; toggling pins off and back on preserves existing wire
  connections by name.
- **Subscription cost matches exposure** — a node with only
  `On Started` exposed only subscribes to that event. No extra
  delegate bindings for events you don't use.
- The proxy reference output pin renames from `AsyncTask` to
  `Subscription` (existing nodes orphan the old wire on first
  reload — minor migration cost).
- Outcome / prerequisite / blocker / giver actor pins appear
  whenever at least one relevant lifecycle pin is exposed, so they
  remain discoverable regardless of which subset of events you've
  enabled.
- Pin tooltips rewritten to Unreal's standard Pin Label / Type
  Description / Description format for consistency with engine
  Blueprint conventions.

#### Two-layer state pattern — quest history records

Per-resolution outcome detail and per-cascade entry attribution move
out of the World State boolean-fact surface and into queryable
rich-record registries on `UQuestStateSubsystem`. World State still
answers "did this quest happen?" with boolean facts; the state
subsystem now answers "with what specific outcomes, on which entry,
from where in the cascade?" with structured records.

- **`HasResolvedWith(QuestTag, OutcomeTag)`** and
  **`HasEnteredWith(QuestTag, OutcomeTag)`** — Blueprint-callable
  predicate queries against the records. The existing
  `IsQuestResolvedWith` library function routes through the new
  query so adopter Blueprints keep working unchanged.
- **`FQuestResolutionRecord`** and **`FQuestEntryRecord`** —
  append-only per-quest history. Each entry carries timestamps,
  and for entries the source quest and incoming outcome that
  caused the activation (information the prior boolean-fact
  representation couldn't preserve).
- **`FQuestResolutionRecordedEvent`** and **`FQuestEntryRecordedEvent`**
  broadcast on the quest's tag channel on each record write.
  Pre-flight for save/load (0.5.0) and the upcoming Quest State
  inspector view.
- Prerequisite expressions can now branch on entry-pin vs
  outcome-pin distinctions — runtime evaluation dispatches to the
  matching query and per-condition subscriptions hook the matching
  event channel.

#### Container vs Step state model split

Quest container nodes (the parent grouping shape) and Step nodes
(the running-and-completing unit) now play distinct roles in the
lifecycle. Container Live state is **derived** from inner Step
state — a container is Live if any of its Steps are Live, and
transitions back to non-Live when all its Steps finish. Steps own
intrinsic state; containers reflect their contents.

This closes a class of inconsistencies where re-activating a partly-
complete container would behave erratically. Activating a container
whose inner Steps are mixed-state now routes through the activation
cascade normally, with per-Step idempotency guards preventing
already-Live Steps from re-running. Loop-back wires, fan-in
patterns, and external re-activation all behave consistently.

- **`IsStepNode()` / `IsContainerNode()`** virtual predicates on
  `UQuestNodeBase` distinguish the two roles. Most adopter custom
  nodes won't need to override these — the framework's default
  Quest and Step classes set the right values.
- **Path-aware giver gate** — when a quest container has a giver
  on one of its activation pins, the gate now considers which
  Steps that specific pin actually reaches. If those Steps are
  already Live (the player is already on this path), the gate
  skips — there's no work for the giver to enable.
- The previous `EQuestActivationBlocker::AlreadyLive` reason now
  only reports for Step nodes (containers handle Live re-entry
  via the derivation model above). The unused `Deactivated`
  blocker value is removed; the activation flow clears that
  state on entry, so reporting it as a blocker was contradictory.

#### Hierarchical catch-up parity + richer quest history snapshots

Catch-up for late subscribers now matches live subscription
semantics. Subscribing on a parent tag (e.g.
`SimpleQuest.Questline.MyArc`) fans out catch-up across every
descendant tag that has any recorded history — mirroring how the
live bus delivers descendant events to parent subscribers.
Previously, parent-prefix subscriptions only received catch-up if
the literal subscribed tag had recorded state; descendants with
recorded history were invisible at bind time.

The history records themselves expand to carry the full activation
context at start time — sufficient for save/load (0.5.0) to
reconstitute live questline state from the registry alone.

- Every Quest start now writes a history entry containing how it
  was activated (giver-gate / cascade / external API / initial
  entry), the actor that initiated it (when giver-gated), and the
  merged-final activation parameters delivered to the objective
  (target actors / classes, element counts, origin tag and chain,
  custom payload).
- **`UQuestStateSubsystem::GetQuestTagsUnderPrefix(Prefix)`** —
  Blueprint-callable. Returns every quest tag the manager has
  registered this session that matches or descends from the given
  prefix. The catch-up fan-out uses this; adopters can call it
  directly for UI work that needs the same "what quests exist
  under this arc?" answer.
- **`IsKnownQuestTag(QuestTag)`** and **`GetKnownQuestTagCount()`** —
  companion predicates for adopter code that needs to check tag
  registration before issuing queries.
- **Per-field history accessors** on `UQuestStateSubsystem`:
  `GetLastGiverActor`, `GetLastActivationProvenance`,
  `GetLastActivationParamsSnapshot`, `GetLastPathIdentity`.
  Blueprint-callable; each returns data from the most recent
  recorded entry for the given quest.
- **Quest State Facts Panel — new columns** on the Entries tab:
  `Provenance`, `Giver`, `Path`. Sortable, included in the
  substring filter, gated to skip filtering on unpopulated values
  so a designer-authored "None" tag retains literal meaning when
  typed.

#### Plugin tag travel — zero-config adoption

Compiled and authored tags now ship with the plugin rather than the
project. Adopters copying the plugin folder into a fresh project
inherit the demo's compiled tags and the framework's default tag set
automatically — no manual tag registration required.

- Compiled tags persist to `Plugins/SimpleQuest/Config/Tags/SimpleQuestCompiledTags.ini`.
  Existing adopters with the prior project-side path get an
  automatic cleanup of the old file on next editor launch.
- The framework's default authored tag set (example activation
  groups, prereq rule groups, and named outcomes the demo content
  references) ships in
  `Plugins/SimpleQuest/Config/Tags/SimpleQuestAuthoredTags.ini`.
  Registered at module startup via Unreal's native gameplay tag
  registration hook.
- `GameplayTagRedirects` for pre-0.4.0 content migration stay in
  the project's `Config/DefaultGameplayTags.ini` rather than the
  plugin — Unreal needs redirects available at engine boot, which
  happens before plugin configs load. Fresh adopters never see
  these redirects; they're provided as a reference for projects
  upgrading from pre-0.4.0 content.

#### UE 5.7 compatibility verified

Build target settings now use the auto-tracking
`BuildSettingsVersion.Latest` sentinel, which resolves to the
appropriate version under both UE 5.6 and UE 5.7 without
hardcoding a version-tied value. UE 5.6 stays the primary target;
UE 5.7 pull-forward verified via full build and PIE walkthrough on
demo content with no other source changes required.

#### Internal — compiler + runtime infrastructure refactors

Implementation cleanup that doesn't change adopter-visible behavior
but simplifies internal extension points:

- **Prerequisite expression construction** unified onto a single
  builder API with one entry point per leaf kind. Adding a new
  prerequisite leaf type (a new way for prereqs to check facts)
  is now a one-line addition in three places (builder, evaluator,
  subscription dispatch) rather than a multi-site refactor.
- **`FQuestTagComposer`** centralizes every tag namespace prefix,
  compose / decompose helper, and tag-kind classifier. Adopters
  who want to author their own tag-aware extensions (for example,
  custom save/load that filters by tag kind) can use this class's
  public methods rather than re-implementing the namespace logic.

### Changed

Tag namespace consolidation flows through every authored tag-picker
filter, every compiler-emitted tag composition, and every internal
state-fact-handling code path — adopters with pre-0.4.0 content rely
on the `GameplayTagRedirects` to translate references transparently
on load; adopters authoring fresh content target the final
`SimpleQuest.*` namespace directly.

#### Active → Live state rename

The "running" state of a quest is now called **Live** instead of
**Active** across the framework. This frees the word "activation"
to mean "the moment of becoming offerable" (the new
`FQuestActivatedEvent`), which is distinct from "the moment of
running" (the existing `FQuestStartedEvent`). "Live" is also a
natural metaphor for a node in a graph — a live wire carries
current.

Affected adopter-facing surfaces:
- World State fact tags: `SimpleQuest.State.<X>.Active` →
  `SimpleQuest.State.<X>.Live`.
- `USimpleQuestBlueprintLibrary::IsQuestActive` → `IsQuestLive`.
- `UQuestStep::ActiveObjective` field → `LiveObjective`; matching
  `GetActiveObjective` accessor → `GetLiveObjective`.
- `EQuestNodeDebugState::Active` enum value → `::Live`.

Internal-only renames (no adopter impact) covered the manager's
write methods and editor debug enums for consistency.

#### State subsystem rename + read API split

- **`UQuestResolutionSubsystem` → `UQuestStateSubsystem`.** The
  rename signals "externally accessible fact registry with limited
  write access" — read API public for adopter queries, write API
  reserved to the manager. Mirrors `UWorldStateSubsystem` for
  consistency across the suite. Future suite plugins (dialogue,
  progression) will follow the same `...StateSubsystem` convention.
- `UQuestManagerSubsystem` no longer has a public read API.
  Adopters query the state subsystem instead for resolution
  records, prerequisite status, and activation blockers. The
  manager is a write-only orchestrator from adopter code.
- **`FQuestEnabledEvent` semantic refinement** — fires when a
  quest becomes truly accept-ready (activated AND prerequisites
  satisfied) rather than on first wire arrival. The "first wire
  arrival" semantics move to the new `FQuestActivatedEvent`.
  Adopters who used `OnEnabled` as their "first notice" hook
  should rewire to `OnActivated`.

#### Plugin visual defaults travel with the plugin

Wire, pin, and node color defaults now ship in the plugin's own
config rather than the project's. Adopters copying the plugin
folder into a fresh project inherit the maintainer's color choices
automatically; project-level overrides still work normally.

### Fixed

- **Prerequisites on LinkedQuestline nodes were silently ignored at
  runtime.** A pre-existing compiler bug surfaced during 0.4.0
  testing: prereq expressions authored on LinkedQuestline nodes
  never made it into the compiled runtime data, so the runtime
  treated every LinkedQuestline node as having no prereqs. The
  compile pipeline now populates the prereq expression for
  LinkedQuestline nodes correctly; existing assets pick up the
  fix on next compile.

- **Giver-gated quests bypassed prerequisite evaluation entirely.**
  Three independent runtime paths collectively skipped prereq
  checks for giver-gated quests, so a quest's giver would offer
  it even when prereqs were unmet. The lifecycle redesign closes
  this gap structurally: the giver lights up immediately on first
  wire arrival regardless of prereq state (via the new
  `FQuestActivatedEvent` carrying the prereq snapshot), and
  acceptance is the gate that protects "started" — the manager
  refuses give attempts when blockers exist and publishes
  `FQuestGiveBlockedEvent` with the structured reason, rather than
  silently starting the quest.

### Removed

- **`UQuestManagerSubsystem::InitialQuestlines` array and
  `StartInitialQuests` event — adopter migration required (breaking
  change).** The startup pathway that drove questline activation
  via designer-configured arrays on a Blueprint subclass of the
  manager is gone. Adopters now drive activation exclusively
  through `Start Questline` from any startup hook of their
  choice — player pawn `BeginPlay`, GameMode `BeginPlay`, a custom
  GameInstance subsystem, a dialogue trigger, save/load
  rehydration. One pattern serves static startup, procedural
  orchestration, and dynamic activation alike.

  `UQuestManagerSubsystem` is no longer abstract — the native
  class auto-instantiates by default. Adopters who want to
  customize manager behavior subclass it and designate the
  subclass under **Project Settings → Plugins → Simple Quest →
  Quest Manager**. The default (empty / native class) works
  without any configuration.

  **Migration:** move your project's startup questline list from
  the manager Blueprint's `InitialQuestlines` array to a
  `BeginPlay` handler that calls `Start Questline` per entry. The
  plugin's demo content shows the recommended pattern
  (`BP_QuestPlayerExample::BeginPlay → Start Questline(QL_Main)`).
  The previous demo's `BP_QuestManagerDemo` Blueprint is removed;
  the demo now runs against the native manager class via the
  default-empty `QuestManagerClass` setting.

  Side benefit: the legacy "BP recompile during PIE crashes the
  editor" failure mode common when adopters subclass the manager
  in Blueprints no longer affects projects that use the native
  class. Adopters who do subclass the manager in C++ aren't
  affected; the crash specifically came from the Blueprint
  reinstancing path under PIE.

### Known Limitations

- **Inert legacy tag string fragments persist in some questline
  `.uasset` binaries.** Functionally harmless — the compiled tag
  registry and runtime tag manager are both fully migrated to the
  `SimpleQuest.*` namespace, and clean-test-project verification
  (dropping the plugin folder into a fresh project) confirms the
  demo runs zero-config. The leftover bytes appear in stale string
  tables inside `.uasset` binaries because Unreal's asset
  serialization sometimes appends rather than replaces. They show
  up in raw `grep` results but don't affect runtime behavior, and
  natural authoring activity compresses them out over time.

- **GitHub "Download ZIP" produces broken assets.** This repository
  uses Git LFS for `.uasset` and `.umap` binaries. GitHub's archive
  endpoint (the green "Code → Download ZIP" button) doesn't resolve
  LFS pointers — the download contains ~130-byte stub files instead
  of the real binaries. Clone with `git-lfs` installed, or use a
  Release-based artifact once those land. See the README's
  Installation section.

### Internal — namespace audit playbook

For maintainers performing future namespace operations: grep for
`UE_DEFINE_GAMEPLAY_TAG` macros explicitly (not just string
literals) and for length-dependent string-strip operations near
tag handling. Both classes of site were missed during the initial
0.4.0 audit and surfaced as user-visible breakage; the audit
playbook now lists both as required checks.

---

## [0.3.5] — 2026-04-25 — Stale Quest Tags Polish

A polish pass on the Stale Quest Tags panel that shipped a few hours
earlier in 0.3.4, plus designer-facing log clarity improvements across
`BindToQuestEvent`, the resolution subsystem, and the panel's own UX
failure paths. The panel gains **multi-row mass-clear** with a per-
source confirmation breakdown and atomic undo (single transaction
wraps the batch), a **sortable + filterable Level column** with per-
source semantics, and **near-instant undo** on cleared instance
entries — was multi-second on Full Project Scan history; now sub-
millisecond regardless of scan history. The Refresh button is
renamed **Scan Loaded** for symmetric pairing with Full Project Scan,
and the Full Project Scan progress bar now advances on completion
instead of jumping to 100% before the work begins.

### Added

#### Stale Quest Tags panel — multi-row mass-clear
- **Multi-row selection** (`ESelectionMode::Multi`) on the SListView.
  Standard Ctrl+Click / Shift+Click multi-select gestures
- New header button **"Clear Selected (N)"** with live label binding
  on `ListView->GetNumItemsSelected()` and `IsEnabled` bound to the
  same count. Disabled when nothing is selected
- Confirmation dialog with **per-source breakdown**: shows count of
  Loaded / BP CDO / Unloaded entries in the selection plus the
  number of unique packages affected, before any mutation
- **Single `FScopedTransaction`** wraps the entire batch — Ctrl+Z
  restores all Loaded + Unloaded clears as one atomic operation
- `ClearOneEntry` helper extracted from `HandleClearClicked` so
  single-row and bulk paths share the source-specific dirty-resolution
  logic (Loaded → `MarkPackageDirty` on actor; BP CDO → outer-chain
  walk to UBlueprint + `MarkBlueprintAsModified`; Unloaded →
  `MarkPackageDirty` on the unloaded-level actor)

#### Stale Quest Tags panel — Level column
- New sortable + filterable **Level** column between Source and
  Actor (140px fixed width)
- Per-source display: leaf umap name (via `FPackageName::GetShortName`)
  with full-path tooltip on hover for Loaded / Unloaded entries; em-
  dash `—` with muted color and "Not applicable" tooltip for BP CDO
  entries (BP defaults aren't level-bound)
- Underlying sort/filter value is the full umap path for Loaded /
  Unloaded and empty for BP CDO — typing partial path text matches
  level rows but never matches BP CDO rows; em-dash entries cluster
  at one end of any Level sort
- Backend reuses `FStaleQuestTagEntry::PackagePath` (already populated
  for all three sources by the Tier 2 scanner); no schema change needed

#### Stale Quest Tags panel — fast PostUndo via per-actor targeted rescan
- New `ActorsTouchedByClear` map (per-actor `Source` + `PackagePath`)
  populated on every Clear (single + bulk). BP CDO entries opt out —
  their "actor" is a CDO, not a level instance
- New `UpdateFromAffectedActors` method drops `AllEntries` for tracked
  actors then re-emits via `ScanActorForStaleTags`
- `PostUndo` / `PostRedo` call `UpdateFromAffectedActors` instead of
  `Refresh(LastScope)`. Net effect: undo of a cleared instance entry
  is now sub-millisecond regardless of `LastScope`. Sibling entries
  on the same actor are preserved automatically because the rescan
  emits all stale tags fresh with valid weak ptrs

#### Stale Quest Tags panel — designer-visible BP CDO permanence warnings
- **Per-row Clear tooltip** on BP CDO entries calls out "Cannot be
  undone — the mutation propagates permanently to the Blueprint's
  class state. Use the Blueprint editor to manually re-add the tag if
  needed."
- **Clear Selected (N) header button tooltip** distinguishes Loaded /
  Unloaded undo (Ctrl+Z works, single transaction wraps the batch)
  from BP CDO permanence (Ctrl+Z is a no-op for those rows)
- **Confirmation dialog** conditionally appends a `[Warning]` block
  when the selection contains any BP CDO entries — clean dialog
  otherwise so the warning isn't always present

#### `FSimpleQuestEditorUtilities::ScanActorForStaleTags` (new public utility)
- Promoted from anonymous-namespace helper to public static method
  on `FSimpleQuestEditorUtilities`. Walks an actor's components,
  dispatches by component type (Giver / Target / Observer), emits
  one `FStaleQuestTagEntry` per stale tag found. Useful for any
  targeted recovery / scan flow that wants to re-derive entries
  for a specific actor without re-walking the entire project

### Changed

- **"Refresh" button renamed "Scan Loaded"** for symmetric pairing
  with Full Project Scan. Method renamed `HandleRefreshClicked` →
  `HandleScanLoadedClicked` for internal consistency. Tooltip
  clarifies the Tier-1 scope and references Full Project Scan as
  the alternative
- **Slow-task progress bar** in Full Project Scan now advances on
  completion (`SlowTask.EnterProgressFrame(1.f)` moved to after
  `Refresh`) instead of jumping to 100% before the scan begins.
  UE's secondary asset-loading bar continues to show real-time
  progress during sync-loads
- **`UQuestResolutionSubsystem` resolution-recording log** promoted
  Verbose → Log. Quest resolution is a high-signal event; designers
  debugging quest flow now see "recorded" entries in the Output Log
  without enabling Verbose
- **`SStaleQuestTagsPanel` actor-not-found-post-load log** promoted
  Verbose → Log with more actionable wording: "actor may have been
  renamed or removed since the scan." Class-prefix added for
  consistency with other panel logs
- **`UQuestEventSubscription` subsystem-resolution failure Warning**
  gained a "Common causes:" hint — `BindToQuestEvent` fired pre-init,
  or the `WorldContextObject` pin is wired to an actor whose UWorld
  isn't valid

### Removed

- **Anonymous-namespace `ScanActorForStaleTags`** in
  `SimpleQuestEditorUtils.cpp` — replaced by the public version above
- **`SimpleQuest.Debug.ScanBlueprintCDOs` and
  `SimpleQuest.Debug.ScanUnloadedLevels` console commands** — original
  author's note explicitly slated these for removal once Phase 4's
  panel button was in place. Phase 4 + Phase 5 (commandlet) both
  shipped in 0.3.4; the console commands were strictly inferior to
  the panel + commandlet surfaces

### Known Limitations

- **BP CDO undo is a no-op.** Clear on a BP CDO row persists
  permanently across save (the underlying mutation propagates to
  the Blueprint's class state). Ctrl+Z does not visually restore
  the row, and the underlying tag stays cleared on the BP. Two
  approaches were tried during development and both failed —
  force-recompile in `PostUndo` (didn't fix the visibility issue
  and added several seconds of lag per touched BP) and shadow-of-
  cleared-entries (the predicate's component weak-ptr check failed
  post-undo, dropping sibling rows on unrelated actors). Root
  cause is the BP-side template / CDO propagation contract — needs
  a refactor that routes through `FProperty::PreEditChange` /
  `PostEditChange` flow rather than direct container mutation.
  Slated for a focused investigation post-0.4.0; not blocking.
  Designer-visible warnings landed alongside on the per-row tooltip,
  Clear Selected (N) header tooltip, and bulk-clear confirmation
  dialog so designers see the limitation BEFORE the click rather
  than discovering it after
- **Slow-task progress bar lacks per-step granularity.** Outer bar
  stays at 0% during the scan and advances to 100% on completion;
  UE's secondary asset-loading bar shows real-time progress during
  sync-loads. Cosmetic. Plumbing per-step progress through
  `ScanActorBlueprintCDOs` / `ScanUnloadedLevels` /
  `ScanWorldPartitionActors` requires threading a callback through
  the scan internals — minor lift but no immediate user-pain driver

---

## [0.3.4] — 2026-04-25 — Stale Quest Tags Tier 2 (Project-Wide Scanning)

Project-wide stale quest-tag scanning. The Stale Quest Tags panel and a
new headless commandlet now cover the full project surface — loaded
levels (Tier 1, unchanged), Actor Blueprint defaults, and unloaded
levels including World Partition. Designers click **Full Project Scan**
in the panel to surface stale references that a normal pass wouldn't
catch; ship pipelines and CI runs invoke the commandlet directly via a
Windows `.bat` helper or a `UnrealEditor-Cmd.exe -run=StaleQuestTagsScan`
line, get structured JSON output, and gate on exit code: `0` clean, `1`
stale references found, `2` infra failure (couldn't init, JSON write
failed, etc.). Designed as the pre-flight + post-flight validator for
tag-identity work — particularly the `SimpleQuest.*` root namespace
consolidation slated for 0.4.0 — but useful any time a project ships.

The new scan tiers are opt-in and source-aware. The panel keeps its
sub-second Tier 1 refresh as the default; clicking Full Project Scan
fans out to Blueprint CDOs and unloaded levels (with a comprehensive-
vs-class-filtered World Partition toggle) wrapped in a slow-task with
progress notifications. Each row carries a Source badge — *Loaded* /
*BP CDO* / *Unloaded* — and the navigation affordance morphs to match:
Find frames the actor in its viewport for loaded entries, **Open BP**
opens the Blueprint editor for CDO entries, **Open Level** loads the
containing umap for unloaded entries. Per-row Clear works on all three
sources; affected packages roll into a panel-header **Save All
Modified (N)** button so the designer can review and save in bulk.

### Added

#### Stale Quest Tags panel — Tier 2 surfaces
- **Full Project Scan** button alongside the existing Refresh control.
  Refresh stays at Tier 1 (loaded levels — sub-second); Full Project
  Scan fans out to Tiers 1+2+3 wrapped in `FScopedSlowTask` with
  progress notifications. The panel caches the last-used scope so
  `PostUndo` / `PostRedo` re-scan against the same view (stops Ctrl+Z
  from silently narrowing the scope back to Tier 1 and dropping any
  Tier 2 rows the designer pulled in)
- **Source-aware row morphing**: per-row icon + a Source column with
  *Loaded* / *BP CDO* / *Unloaded* badges. Find button morphs into
  **Open BP** for CDO entries (opens the Blueprint editor) and **Open
  Level** for unloaded entries (loads the containing umap)
- **Save All Modified (N)** button in the panel header. Per-row Clear
  marks the affected package dirty and surfaces it here as a
  `TSet<TWeakObjectPtr<UPackage>>`; one click saves all of them and
  drops them from the tracking set. Stale entries pointing at packages
  that have been re-loaded since the last scan are dropped cleanly via
  the weak pointer
- Comprehensive-vs-class-filtered **World Partition scan mode** toggle
  (default: comprehensive — loads every WP actor; class-filtered:
  loads only descriptors whose actor class is in the quest-component
  class set, much faster at the cost of missing per-instance component
  additions)

#### Stale Quest Tags scan commandlet
- `UStaleQuestTagsScanCommandlet` (`UCommandlet` subclass; `IsEditor=
  true`, `IsClient/IsServer=false`, `LogToConsole=true`,
  `ShowErrorCount=true`). Run via
  `UnrealEditor-Cmd.exe <project>.uproject -run=StaleQuestTagsScan`
- Args:
  - `-OutputJson=<path>` writes structured output. JSON shape:
    `{ totalCount, openCount, bpCDOCount, unloadedCount, entries: [{
    source, actor, component, field, tag, package }] }`
  - `-FastWP` runs WP iteration in class-filtered mode (skips actor
    descriptors whose class can't carry a quest component); default
    is comprehensive (loads every WP actor)
- **Exit codes** for CI gating:
  - `0` — no stale references found
  - `1` — one or more stale references found
  - `2` — commandlet itself failed (init error, JSON write failure,
    Asset Registry timeout, etc.)
- Asset Registry is primed at the top of `Main` via
  `FAssetRegistryModule::Get().SearchAllAssets(/*synchronous*/ true)`
  before any scan runs — without this the freshly-spawned commandlet
  editor reports zero AR entries and every scan finds nothing
- Per-source summary line on stdout
  (`StaleQuestTagsScan: summary — Open=N, BPCDOs=M, Unloaded=K,
  Total=T`) plus one Warning-verbosity log line per stale entry
  (`StaleQuestTagsScan: [<Source>] actor=X component=Y field=Z
  tag=Q.R.S package=/Game/Foo`) so log-only runs without
  `-OutputJson` are still actionable

#### Windows `.bat` helper
- `SimpleQuestDemo/Scripts/RunStaleQuestTagsScan.bat` — resolves
  `UE_PATH` (inline assignment in the script OR the env var), finds
  the project's `.uproject`, invokes `UnrealEditor-Cmd.exe` with
  `-unattended -nopause -stdout` and forwards any extra args to the
  commandlet (e.g. `RunStaleQuestTagsScan.bat
  -OutputJson=stale-tags.json -FastWP`). Forwards the commandlet's
  exit code to the caller for CI / make-script consumption

#### Backend scan surfaces
- `FSimpleQuestEditorUtilities::FStaleTagScanScope` — boolean flag
  struct (`bLoadedLevels` / `bActorBlueprintCDOs` / `bUnloadedLevels`
  + `bComprehensiveWPScan`). Default-constructed scope =
  `{bLoadedLevels=true}`, preserving Tier 1 caller behavior bit-for-
  bit
- `FSimpleQuestEditorUtilities::EStaleQuestTagSource` — `Loaded` /
  `ActorBlueprintCDO` / `UnloadedLevelInstance`. Carried on every
  `FStaleQuestTagEntry` so the panel and commandlet output can
  attribute each stale reference to its discovery surface
- `ScanActorForStaleTags` — single helper consolidating the per-
  component walk logic shared across all three scan surfaces.
  Replaces the duplicated walk that previously lived inline in the
  Tier 1 scan path
- `ScanActorBlueprintCDOs` — walks every `UBlueprint` asset via
  Asset Registry filter, hydrates each generated class's CDO, runs
  `ScanActorForStaleTags` against actor-derived CDOs only.
  Non-actor BPs short-circuit before generated-class load
- `ScanUnloadedLevels` — iterates every `UWorld` asset in the AR,
  builds a skip set from currently-loaded editor world packages
  (Tier 1's territory), sync-loads each remaining umap, and
  dispatches by world type:
  - Non-WP world: walks `PersistentLevel->Actors` directly
  - WP world: hands off to `ScanWorldPartitionActors`
- `ScanWorldPartitionActors` — uses
  `FWorldPartitionHelpers::ForEachActorWithLoading` (the cooker's
  per-actor load/unload iteration pattern) wrapped in
  `FScopedEditorWorld` for commandlet-mode lifecycle discipline.
  Optional class-filter set built lazily from
  `BuildQuestComponentClassSet` (FastWP mode); default is
  comprehensive (loads every WP actor)

### Changed

- Stale-tag scan summary log lines (per-tier and overall) bumped from
  `Verbose` to `Display` verbosity. The commandlet's stdout pipeline
  now produces actionable per-world descriptor counts and final
  per-source totals without needing a `-LogCmds="LogSimpleQuest
  Verbose"` override
- The `Stale Quest Tags` panel's status line summarizes both the
  current visible-row count AND the last scope used (so it's clear
  at a glance whether the panel reflects a Tier-1-only refresh or a
  Full Project Scan)

### Fixed

- **Commandlet-mode World Partition iteration crashed during the
  helper's per-batch GC.**
  `FWorldPartitionHelpers::DoCollectGarbage` calls `CollectGarbage(
  IsRunningCommandlet() ? RF_NoFlags : GARBAGE_COLLECTION_KEEPFLAGS,
  true)` — in commandlet mode it passes `RF_NoFlags` as `KeepFlags`,
  which means *only root-set objects survive the GC pass*.
  `RF_Standalone` doesn't protect, asset-package ownership doesn't
  protect. A manually sync-loaded `UWorld` therefore becomes
  unreachable mid-iteration; `UWorldPartition::BeginDestroy` then
  asserts because the WP we just initialized isn't in `Uninitialized`
  state, and the sibling `UWorldPartitionSubsystem` (a tickable world
  subsystem) ensures because it was destroyed while still
  initialized. The fix: route every sync-loaded world through
  `FScopedEditorWorld` (engine's RAII helper at
  `Editor/UnrealEd/Public/EditorWorldUtils.h`, used internally by
  the WP convert commandlet). Construction handles `AddToRoot`,
  `GWorld` + `EditorWorldContext` swap, `InitWorld`,
  `UpdateModelComponents`, `UpdateWorldComponents`,
  `UpdateLevelStreaming`. Destruction handles `GEditor->Cleanse`,
  `DestroyWorld` (which routes through `CleanupWorld` + per-
  subsystem `Deinitialize` + `WP::Uninitialize`), `RemoveFromRoot`,
  `GWorld` + `EditorWorldContext` restore. The dispatch branches on
  `bIsWorldInitialized` so resident-from-prior-run worlds and
  externally-owned worlds (which the helper would assert against)
  scan directly without the wrapper

---

## [0.3.3] — 2026-04-25 — Catch-Up Outcome Recovery + Two-Layer State Foundations

A targeted release that closes the catch-up outcome recovery gap left by
0.3.2's BindToQuestEvent work. Subscriptions and observers that bind to an
already-resolved quest now recover the actual `OutcomeTag` — not the
previous `EmptyTag` placeholder — by reading from a new
`UQuestResolutionSubsystem` rich-record store keyed by quest tag. This
release also formalizes a two-layer state-architecture pattern: WorldState
remains the fast boolean-fact layer ("did X happen?" in O(1)); per-plugin
subsystems hold typed rich-record state ("what are the details?" in O(1)).

Three follow-on `BindToQuestEvent` reliability fixes ship in the same
release because they're inseparable from the catch-up behavior contract:
catch-up deferral to the next tick (fixes Accessed-None on user-cached
proxy references), per-phase duplicate-broadcast guards (closes the
one-tick race window opened by the deferral), and `RegisterWithGameInstance`
on the factory (canonical lifetime anchor; removes fragile dependency on
caller-side BP variable references).

Also folds in a tactile graph-ergonomics improvement: pin-precise drag-create
alignment in the questline graph editor — the connecting pin's connector nub
now lands exactly under the cursor on drag-from-pin spawns, regardless of
node type, content size, or which pin connects.

### Added

#### Two-Layer State Architecture (MVP)
- `UQuestResolutionSubsystem` — new `UGameInstanceSubsystem` exposing a
  read-only public API for quest resolution post-mortems:
  `GetQuestResolution(QuestTag)`, `HasResolved(QuestTag)`,
  `GetResolutionCount(QuestTag)`. Writes are private and gated by
  `friend class UQuestManagerSubsystem` — consumers physically can't
  mutate the registry. Preserves the manager's black-box doctrine:
  the manager remains the sole owner of orchestration; rich-data
  queries route through specialized read-only subsystems
- `FQuestResolutionRecord` — `USTRUCT(BlueprintType)` with three
  `BlueprintReadOnly` fields: `OutcomeTag`, `ResolutionTime` (double,
  world time at resolution), `ResolutionCount` (per-session repeat
  counter — subsumes the prior `QuestCompletionCounts` map)
- `UQuestManagerSubsystem::SetQuestResolved` writes the WorldState
  boolean fact AND the registry record atomically — single choke point
  preserves the two-layer write invariant. Never touch one layer
  without the other
- Lifetime is GameInstance-scoped, mirroring `UWorldStateSubsystem` and
  `UQuestManagerSubsystem`. Records reset naturally on PIE transitions

#### Catch-Up Outcome Recovery
- `UQuestEventSubscription::RunCatchUp` queries the registry for the
  recovered `OutcomeTag` instead of broadcasting `FGameplayTag::EmptyTag`
  on the no-filter path. Listeners that bind to an already-resolved
  quest now receive the actual outcome on `OnCompleted`
- `UQuestObserverComponent::RegisterQuestObserver`'s `bWatchEnd` catch-up
  block replaces its dual-path WorldState probing (per-filter-tag
  probes when filter set / EmptyTag fallback when not) with a single
  registry lookup followed by post-hoc `OutcomeFilter` matching.
  Mirrors the live `WatchedQuestCompletedEvent` decision path; the
  EmptyTag fallback is gone

### Fixed

- **BindToQuestEvent: Accessed None on cached proxy from catch-up.**
  `UK2Node_AsyncAction`'s standard expansion calls `Activate()` before
  firing the user's `Then` exec output. Designers wiring the AsyncTask
  pin into a `Set` off the primary `Then` chain hadn't cached it yet
  at the moment Activate ran. If `Activate()` fired a lifecycle delegate
  synchronously inside `RunCatchUp` (quest already resolved), the
  designer's downstream chain (e.g. `Print → Cancel(<var>)`) read a
  null reference. Fixed by deferring `RunCatchUp` to next tick via
  `SetTimerForNextTick` with a weak-pointer-protected lambda — same
  pattern as engine async tasks like `UAsyncTaskDownloadImage`. The
  K2 node's standard expansion now reliably completes (Activate
  returns → ThenOut fires → user's Set node runs) before any catch-up
  delegate fires
- **BindToQuestEvent: duplicate broadcast in deferral window.** The
  one-tick deferral introduced a narrow window during which a live
  signal could fire and, on next tick, catch-up could observe the
  same WorldState fact and broadcast the same lifecycle phase a
  second time. Closed via per-phase `bSawLive*` flags on
  `UQuestEventSubscription` set inside each `Handle*` after the
  `bCancelled` early-out and checked in `RunCatchUp` before each
  phase's broadcast. Listeners now receive exactly one broadcast per
  state transition. Documented edge case: a parent-tag subscription
  with the parent's *own* `Completed` fact also set during a child's
  live completion will suppress catch-up for the parent (the listener
  already received an `OnCompleted` for the child and would inspect
  `QuestTag` to differentiate). Accepted tradeoff vs. double-broadcast
- **BindToQuestEvent: missing `RegisterWithGameInstance` on factory.**
  `USimpleQuestBlueprintLibrary::BindToQuestEvent` constructed the
  proxy with `NewObject<UQuestEventSubscription>()` but never called
  `RegisterWithGameInstance(WorldContextObject)` — the canonical
  `UBlueprintAsyncActionBase` lifetime anchor. Without it, the
  action's lifetime depended on whatever strong references happened
  to exist (BP member variable, exec stack mid-fire), `SetReadyToDestroy`
  was a no-op, and fire-and-forget patterns risked premature GC.
  Fix: factory now anchors via `RegisterWithGameInstance`. `GameInstance`
  owns the strong reference until `SetReadyToDestroy` (called by
  `Cancel`); PIE exit cleans up automatically when the GameInstance
  tears down

### Changed

- `UQuestManagerSubsystem::QuestCompletionCounts` removed —
  `UQuestResolutionSubsystem::GetResolutionCount` is the new
  authoritative count source. `GetQuestCompletionCount` on the manager
  delegates to the subsystem (back-compat for any internal callers;
  external code should switch to `GetResolutionCount` directly)

#### Pin-precise drag-create alignment in the questline graph editor
- Replaces the previous `GetPinAlignmentOffset` heuristic (a fixed
  per-node-type offset assuming a "title bar ~24 + half pin-row ~12"
  layout) with a deferred Slate-geometry correction. Drag-from-pin
  spawns now land the connecting pin's connector nub exactly at cursor
  regardless of node type (Step / Quest / LinkedQuestline /
  combinator / group / etc.) or content-driven height variance
- Two placement paths covered behind a single panel-side queue:
  - QWERX hotkey path (`SQuestlineGraphPanel::OnPreviewKeyDown`) —
    enqueues alignment immediately after the spawn + autowire pass
  - Right-click action-menu path —
    `SQuestlineGraphPanel::OnGraphAddedNodeNotify` hooks
    `UEdGraph::OnGraphChanged` Add events and queues a pin-lookup
    that runs at the next Tick, after `FEdGraphSchemaAction_NewNode::PerformAction`
    has set `NodePosX/Y` to the action's drop location and
    `AutowireNewNode` has made the connection
- Centers on the connector glyph specifically (5.5px inset from the
  outer pin-widget edge per `FAppStyle`'s 11×11 `Graph.Pin.Connected`
  brush), not the pin+label widget center — corrects an early-version
  half-label-width offset
- Action-menu drop anchor is read from `Node->NodePosX/Y` at Tick
  time, not from `FSlateApplication::Get().GetCursorPos()` —
  preserves the original wire-drop point even if the user moved the
  cursor across the action menu before clicking
- `UQuestlineNode_Knot` blacklisted from the alignment queue — the
  schema's `TryCreateConnection` self-loop logic spawns knots at
  specific arch coordinates that should not be overridden
- Key+click no-drag spawn path keeps the existing heuristic offset
  (no FromPin to align against)

---

## [0.3.2] — 2026-04-24 — Authoring Diagnostics + Runtime Hardening

A full authoring-diagnostics pass plus the runtime safety net that
surfaced the need for it. Started from a user-reported ensure in a
Blueprint overlap handler — a stale gameplay tag tripped UE's
`FGameplayTag::MatchesAny` container iteration and hung the editor for
~10 seconds via `FDebug::EnsureFailed`. The fix layered out into
sanitized component getters, defense-in-depth publish/subscribe guards,
and a shared `IsTagRegisteredInRuntime` helper. Surfacing and cleaning
up the stale references needed its own tools, so the release also ships
a project-wide prereq validator and a component-side stale-tag cleanup
panel.

Rounded out with four additional authoring + runtime conveniences:
a duplicate-Outcome-routing compile warning, comment block support on
the questline graphs, a blueprint async action for subscribing to
quest lifecycle events, and a `FEditorUndoClient` hook on the graph
editor that fixes undo for any third-party node type (discovered while
investigating comment-node undo specifically).

### Added

#### Prereq Tag Validator (new toolbar action)
- `Validate Tags` toolbar button on the Questline Graph Editor —
  scans every `UQuestlineGraph` in the project, emits tokenized
  diagnostics to a new `QuestValidator` MessageLog listing with
  clickable per-node navigation. Read-only; never modifies assets
- Four diagnostic categories:
  - **Error**: prereq leaf references a missing fact tag
  - **Error**: Rule Exit with no GroupTag set
  - **Error**: Rule Exit references a rule that no Rule Entry in the
    project provides (authoring cross-reference; catches both
    unregistered tags and stale-registered orphans)
  - **Warning**: unused Rule Entry — no Rule Exit references it;
    message points at the Stale Quest Tags panel as the sweep path
- Validation is independent of the compiler: flags cross-graph drift
  and authoring hygiene the per-graph compile can't see. Validates
  against the runtime tag manager rather than the Asset Registry's
  `CompiledQuestTags` cache — catches `.Completed` state facts and
  `QuestPrereqRule.*` group tags that don't serialize into the AR tag

#### Stale Quest Tags panel (new nomad tab)
- Window → Developer Tools → Debug → Stale Quest Tags — sibling to
  the World State Facts panel. Lists quest-component tag references
  whose target isn't registered in the runtime tag manager. Pull-
  based; never auto-runs
- Scans loaded editor worlds, walks every
  `UQuestGiverComponent` / `UQuestTriggerComponent` /
  `UQuestObserverComponent` across `GEditor->GetWorldContexts`. One row
  per stale tag reference
- Per-row surfaces: Find (magnifying-glass icon, selects + frames the
  actor in its level viewport) and Clear (removes the stale tag from
  the component, marks actor dirty)
- Filter bar with case-insensitive substring match + live highlighting
  across Actor / Component / Field / Stale Tag columns
- Per-column sortable (ascending/descending toggle via header arrows)
- Alternating zebra row backgrounds + vertical-centered text
- Tier 1 scope: loaded levels only. Tier 2 (unloaded + Actor Blueprint
  CDOs, commandlet-capable) logged as follow-up

#### Runtime Helpers
- `FQuestStateTagUtils::IsTagRegisteredInRuntime(Tag)` — true iff
  `Tag` is well-formed AND currently registered in
  `UGameplayTagsManager`. Foundation for every stale-tag check across
  the runtime and editor surfaces
- `FQuestStateTagUtils::FilterToRegisteredTags(Container, ContextLabel)`
  — returns a copy of `Container` with unregistered tags stripped, with
  Warning logs per stale tag naming the context. Used by the new BP-
  facing sanitized getters
- `UQuestComponentBase::RemoveTags(TagsToRemove)` — new virtual,
  parallels `ApplyTagRenames`. Concrete overrides on giver / target /
  observer remove matching tags from authored containers and mark the
  owning actor dirty. Powers the Stale Quest Tags panel's Clear action

#### BP-Safe Sanitized Getters
- `UQuestGiverComponent::GetRegisteredQuestTagsToGive()` — registration-
  filtered view of `QuestTagsToGive`. Safe to pass to tag-library
  `Filter` / `HasAny` / `MatchesAny` calls that assert on stale entries
- `UQuestTriggerComponent::GetRegisteredStepTagsToWatch()` — same pattern
  for `StepTagsToWatch`
- `UQuestObserverComponent::GetRegisteredWatchedStepTags()` and
  `GetRegisteredWatchedQuestKeys()` — for `WatchedStepTags` and the keys
  of the `WatchedTags` TMap
- Raw accessors on `UQuestObserverComponent` (`GetWatchedStepTags` /
  `GetWatchedTags`) — const-ref views of the authored containers, for
  the editor-side stale-tag scan

#### Duplicate-Outcome-Routing Compile Warning
- The compiler now emits a tokenized warning when a single content-node
  output pin reaches two or more distinct Outcome terminals that share
  an `OutcomeTag`. Authoring is ambiguous in that configuration — the
  compiler accepts the union of reached destinations, but one outcome
  should route through exactly one terminal. Navigation tokens on the
  source node and every duplicate terminal
- `FQuestlineGraphCompiler::ResolvePinToTags` threads an optional
  per-tag visited-exits collector through the forward walk (defaults to
  `nullptr`; existing call sites unchanged). The outcome-routing loop
  passes a collector, inspects it post-walk for duplicates, emits one
  warning per (OutcomeTag, set-of-Exits) group

#### Comment Blocks
- `UEdGraphNode_Comment` support on all questline graph tiers (top-level,
  Quest inner, LinkedQuestline view). Press `C` with nodes selected to
  wrap them in a comment box with standard 50px padding; press `C` with
  no selection to drop a blank comment at the cursor. Right-click the
  graph background → "Add Comment…" in the action palette as an
  alternative entry point
- `FGraphEditorCommands::CreateComment` mapped on the questline graph
  editor's `GraphEditorCommands` list; schema contributes the palette
  entry via `GetGraphContextActions` (suppressed when dragging from a
  pin since comments don't participate in wiring)

#### Bind To Quest Event (BP async action + C++ helpers)
- `UQuestEventSubscription` — new `UBlueprintAsyncActionBase` subclass
  with four output delegates: `OnActivated`, `OnStarted`, `OnCompleted`
  (carrying the `OutcomeTag`), `OnDeactivated`. Subscribes to all four
  lifecycle event channels on a single quest tag and stays bound until
  `Cancel()` is called or the `UGameInstance` tears down. Designed for
  hierarchical tag subscriptions — subscribing on a parent tag
  (e.g. `Quest.MyLine`) receives events for every descendant quest
- Catch-up on activation: any already-asserted quest-state fact fires
  the corresponding pin immediately, mirroring
  `UQuestObserverComponent::RegisterQuestObserver`
- BP factory `UQuestEventSubscription::BindToQuestEvent(WorldContext, QuestTag)`
  — DisplayName "Bind To Quest Event", `BlueprintInternalUseOnly` +
  `HidePin`/`DefaultToSelf` on `WorldContextObject` so the pin is
  auto-wired to Self in BP graphs
- C++ one-liner template on the BP library for direct handle-based
  subscriptions: `USimpleQuestBlueprintLibrary::SubscribeToQuestEvent<TEvent>`
  resolves the signal subsystem, guards the tag against
  `IsTagRegisteredInRuntime`, and returns an `FDelegateHandle`.
  Companion `UnsubscribeFromQuestEvent(WorldContext, QuestTag, Handle)`
  for teardown

### Fixed

- Undo failure for `UEdGraphNode_Comment` placement — `FQuestlineGraphEditor`
  now inherits `FEditorUndoClient` and forces `NotifyGraphChanged` on
  the current graph from `PostUndo` / `PostRedo`. `UK2Node_AsyncAction`
  and other third-party node types whose `PostEditUndo` doesn't
  broadcast graph-change now get a reliable post-undo refresh. Covers
  every node type uniformly, not just comments

- Runtime ensure hang on stale-tag Blueprint iteration — UE's
  `FGameplayTag::MatchesAny` ensures when iterating a container
  holding an unregistered tag. Demo `BP_QuestGiverActor`'s overlap
  handler was passing the giver's raw `QuestTagsToGive` into
  `FGameplayTagContainer::Filter`, producing a ~10 second `EnsureFailed`
  hang (stack walk + crash report). Fixed via the sanitized getters
  above + BP node swap on the demo actor. No more ensure; stale tags
  skipped silently with a Warning log pointing at the Stale Quest Tags
  panel
- `UQuestNodeBase::ResolveContextualTag` was calling `RequestGameplayTag`
  without `ErrorIfNotFound=false` — the one outlier across the whole
  plugin. Latent foot-gun that would ensure on any path passing an
  unregistered `TagName`. Now passes `false` explicitly with a Warning
  log + early return on invalid

### Changed

- `UQuestGiverComponent::GiveQuestByTag` and `RegisterQuestGiver` loop
  guards upgraded from `IsValid` to `IsTagRegisteredInRuntime` — stale
  tags skipped with a Warning log naming the stale tag and the actor
- `UQuestTriggerComponent::BeginPlay` subscribe loop — same upgrade
- `UQuestObserverComponent::RegisterQuestObserver` subscribe loop — same
  upgrade

#### Soft class references across authoring + runtime
- `UQuestlineNode_Step::ObjectiveClass`, `::RewardClass`, `::TargetClasses`
  flipped from `TSubclassOf` / `TSet<TSubclassOf<>>` to `TSoftClassPtr` /
  `TSet<TSoftClassPtr<>>`. The runtime counterparts
  (`UQuestStep::QuestObjective`, `UQuestNodeBase::Reward`) were already
  `TSoftClassPtr`; runtime `UQuestStep::TargetClasses`,
  `FQuestObjectiveActivationContext::TargetClasses`, and
  `UQuestObjective::TargetClasses` all flipped to match
- Questline asset packages no longer record hard dependencies on
  designer-authored Objective / Reward / target Actor BP classes.
  Measured impact: a populated test questline dropped from ~500 MB
  to ~54 KiB package footprint
- Soft→hard resolution happens at well-defined boundaries:
  `UQuestObjective::EnableQuestTargetClasses` and
  `UQuestManagerSubsystem::ActivateNodeByTag`'s Step branch call
  `LoadSynchronous` at step activation time; the already-loaded
  `UClass*` is cached in the runtime `ClassFilteredSteps` multimap so
  event-dispatch checks stay fast
- Slate widget display in `SGraphNode_QuestlineStep` uses
  `TSoftClassPtr::GetAssetName()` for class-name rendering without
  forcing the class asset to load
- Existing assets migrate on resave — `TSubclassOf` and `TSoftClassPtr`
  share the same `FSoftObjectPath` serialization shape, so UE
  transparently reinterprets older data. Resave each affected
  questline to drop the stale hard-dep records from its package

---

## [0.3.1] — 2026-04-23 — Objective Activation Lifecycle + Structured Payloads

Dominant feature: a restructuring of the objective activation surface.
Activation now delivers a typed `FQuestObjectiveActivationContext`
struct to objectives, with named fields + `FInstancedStruct CustomData`
extension — symmetric with the existing `FQuestObjectiveTriggerContext` on
the completion side. Four entry points feed the struct (authored step
defaults, external event bus, quest giver components, step-to-step
handoff), all merging additively. New `OriginTag` + `OriginChain` give
objectives first-class "who activated me" tagging and full activation-
history awareness across cascades, Quest containers, and
LinkedQuestline boundaries.

Also bundled: the graph editor polish pass — cross-graph giver display,
auto-compile for linked questlines, copy/paste/duplicate/cut command
wiring, Quest inner-graph deep-copy on paste, toolbar and picker
conveniences, plus a batch of rename- and compile-refresh fixes.

### Added

#### Activation Params Struct (dominant feature)
- `FQuestObjectiveActivationContext` — named activation-time fields
  (`TargetActors`, `TargetClasses`, `NumElementsRequired`,
  `ActivationSource`, `OriginTag`, `OriginChain`) plus
  `FInstancedStruct CustomData` for game-specific runtime extension.
  Symmetric with `FQuestObjectiveTriggerContext` on the completion side
- `UQuestObjective::OnObjectiveActivated` replaces `SetObjectiveTarget`
  — `BlueprintNativeEvent` taking the full params struct, accessed
  via `BlueprintProtected` + public `DispatchOnObjectiveActivated`
  wrapper. Subclasses override to read authored fields or typed
  `CustomData`

#### External Activation Entry Point 
- `FQuestActivationRequestEvent` published on
  `Tag_Channel_QuestActivationRequest` — programmatic activation
  entry for procedural generators, dialogue systems, save/load
  rehydration, test harnesses. Manager subscribes and routes without
  exposing a new public method on the subsystem (black-box preserved)

#### Giver-Authored Params 
- `UQuestGiverComponent::ActivationParams` — designer-authored
  `FQuestObjectiveActivationContext` carried with every give. Placed
  world singletons (shrines, dungeon-entrance actors) author their
  specific `TargetActors`, counts, `CustomData`, `OriginTag` directly
  in the Details panel
- `UQuestGiverComponent::GiveQuestByTag(QuestTag, Params)` promoted
  signature — optional runtime `Params` arg (`AutoCreateRefTerm`
  makes the BP pin truly optional). Merges additively with the
  component's authored `ActivationParams` using the same rules as
  the step-side merge
- `ActivationSource` defaults to `GetOwner()` when neither authored
  nor caller sets it; designer-authored `OriginTag` seeds the
  initial `OriginChain`

#### Step-to-Step Forward Params 
- `UQuestObjective::CompleteObjectiveWithOutcome` gains optional
  `InForwardParams` arg (`AutoCreateRefTerm`) — completing objective
  specifies an `FQuestObjectiveActivationContext` to carry forward
  into the next step's activation. Merges additively with the
  downstream step's authored defaults. Both `InCompletionContext` and
  `InForwardParams` are BP-optional via `AutoCreateRefTerm`
- `K2Node_CompleteObjectiveWithOutcome` — new `Forward Params`
  input pin with per-pin tooltip explaining the additive merge
  rules + common uses. `Completion Data` pin now also carries a
  full per-pin tooltip. Node-level tooltip rewritten to cover all
  three authored inputs

#### Chain Propagation
- `OriginTag` — immediate-origin tag stamped onto the activating
  node; designer escape hatch for "who activated me?" BP branching
- `OriginChain` — full activation history array, oldest-first.
  Extended at every hop: step-to-step via `ChainToNextNodes`, across
  `UQuest` boundaries via `ActivateNodeByTag`'s Quest branch,
  across `LinkedQuestline` boundaries automatically (LinkedQuestline
  inlines as a `UQuest` at compile time). Every hop contributes
  exactly one entry — no gaps, no duplicates across boundaries

#### Graph Editor Polish
- Copy / paste / duplicate / cut command wiring on the questline
  graph editor (`FGenericCommands::Copy` / `Paste` / `Duplicate` /
  `Cut` + handlers modeled on `FBlueprintEditor`, clipboard via
  `FPlatformApplicationMisc` with `ApplicationCore` module dep)
- Quest inner-graph deep-copy on paste — pasted Quest nodes carry
  a full deep copy of their inner graph, labels + topology + pin
  connections intact, with fresh compiler-level identities
  (`RegenerateInnerGraphIdentitiesRecursive` + `PostPasteNode`)
- Graph Defaults toolbar button — jumps to the graph's root
  properties (FriendlyName, metadata) without hunting in the
  Details panel
- Outcome node inline tag picker — outcome tag pickable directly
  on the Outcome node widget rather than via the Details panel
- Auto-compile linked questlines — compiling a graph automatically
  recompiles any graphs that link into it, keeping cross-graph
  state consistent without manual compile cycles
- Cross-graph contextual giver display on all content nodes —
  Quest / LinkedQuestline / Step nodes show their associated giver
  actors with context-aware resolution across linked graph
  boundaries; `bGiversExpanded` lifted to component base
- `FriendlyName` `FText` on `UQuestlineGraph` — preferred over
  asset name in titles, tooltips, and outliner root display
- LinkedQuestline title format + inline asset picker (title lock
  when an asset is picked)

### Changed

#### Struct Promotions
- `PendingActivationParams` promoted from `UQuestStep` to
  `UQuestNodeBase` so any node type can be pre-stamped by cascade
  routing — unblocks `UQuest` / LinkedQuestline boundary chain
  preservation

#### Authoring
- Details-panel rename propagation — editing a node's name from
  the Details panel triggers the same propagation + compile-
  invalidation flow as in-graph rename
- Container rename propagation — renaming a Quest container
  updates all inner-graph-contained node tags and downstream
  references
- Stale-warning banner generalization in the Details panel —
  previously hardcoded to a single case; now covers all content-
  node kinds uniformly

### Removed
- `UQuestStep::TargetVector` + `UQuestlineNode_Step::TargetVector`
  — positional data now routes through `CustomData` (vectors have
  no sensible additive merge semantic)

### Fixed
- Compile-status icon regression after auto-compile-linked landed
  — neighbor-broadcast refreshes were resetting status to Unknown;
  fixed with `bSuppressDirtyOnGraphChange` guard across
  `RefreshAllNodeWidgets`
- Cross-asset compile refresh — when a linked questline recompiles,
  parent assets that reference it now refresh their compile status
  without a manual re-open
- Refresh recursion — graph-change notification loops through
  nested Quest inner graphs without duplicate work
- Objective BP visibility lockdown — internal objective lifecycle
  methods (`TryCompleteObjective`, `ReportProgress` et al.)
  properly `BlueprintProtected` + public C++ dispatcher wrappers;
  no longer leak into arbitrary BP call menus
- Linked-questline runtime participation — previously a
  LinkedQuestline node could fail to wire into the runtime graph
  depending on load order; fixed by normalizing the compile-time
  inline handoff
- Prerequisite expression compilation dropped leaves on multi-input
  AND / OR when the AnyOutcome branch fired — a chained
  `OutExpression.Nodes[Idx].ChildIndices.Add(OutExpression.Nodes.Add(...))`
  held a dangling reference when the `TArray` reallocated mid-loop,
  silently losing every child after the first. Symptoms: AND(Left, Right)
  prereqs that only checked Left; unreleasable steps in deep graphs.
  Fix sequences the `Nodes.Add` before the back-index
- Utility node (ActivationGroup Entry/Exit, GroupSetter/Getter,
  PrereqRule monitor) output-pin wiring resolved with the wrong
  `TagPrefix` when compile recursion unwound — Pass 2b iterated the
  shared `UtilityNodeKeyMap` unconditionally and rewrote nested
  utility nodes' `NextNodesOnForward` using each outer graph's prefix.
  Symptoms: group-entry cascades targeting shallow tags like
  `Quest.NewTest.Left_Two` when destinations lived at
  `Quest.NewTest.Secret_Level.Near.Left_Two`. Pass 2b now iterates
  only utility nodes belonging to the current graph
- `UQuestManagerSubsystem` subclass arbitration — every concrete
  subclass was being instantiated by UE's default
  `ShouldCreateSubsystem`, not just the one designated in project
  settings. Designated quest manager class now gates creation
  (`ShouldCreateSubsystem` override checks `USimpleQuestSettings`);
  other subclasses are silently suppressed so signal-bus handlers
  don't double-fire
- Prerequisite Rule monitors latched permanently on first
  satisfaction, breaking any expression with a `NOT` — the rule
  would publish immediately at activation time if the negated leaf
  wasn't yet asserted, then unsubscribe and never re-evaluate when
  the leaf actually fired. Rule monitors are now dynamic: they
  stay subscribed for the rule's lifetime and mirror the
  expression's current truth value, adding or retracting the
  group fact as leaves transition
- Cross-PIE-session state leak on compiled node instances — the
  instances live on the `UQuestlineGraph` asset and survive PIE
  transitions, but their subscription handles, deferred-prereq
  state, giver-gated flags, and Piece D scratch slots were from
  the prior session's dead subsystems. Session 2 would skip
  re-subscription (handles map already populated), silently
  disconnecting rule monitors from the live signal bus. New
  `UQuestNodeBase::ResetTransientState` virtual wipes this state;
  called per compiled node by `ActivateQuestlineGraph` before any
  other wiring
- Linked-asset PIE debug visualization was blind to cross-graph
  context — halos and the Prerequisite Examiner both queried
  `WorldState` with the standalone-compile tag (e.g.
  `Quest.SideQuestQL.Near.Left`) while live facts were nested
  under the active parent (`Quest.NewTest.Secret_Level.Near.Left`).
  Viewing a linked questline asset during PIE showed no feedback
  on any inner node. `FQuestPIEDebugChannel::ResolveRuntimeTag`
  and `QueryLeafState` now fall back to contextual tags via a new
  `CollectContextualNodeTagsForEditorNode` Asset Registry walk
  (extracted from the existing contextual-giver/observer machinery)
- Diagnostic-log volume under `LogSimpleQuest VeryVerbose` —
  `FindCompiledTagForNode` was printing a per-slot iteration dump
  on every call from editor paints, reaching ~260 lines per tick.
  `IsContentNodeTagCurrent` and `ReconstructNodeTag` similarly
  logged per-invocation on the hot path. All three are now silent
  on the success path; misses retain their Warning/Verbose logs

---

## [0.3.0] — 2026-04-26 — Compiler, Portal Vocabulary, Inspection Surfaces, PIE Debug Overlay

A major design iteration centered on the visual graph editor. The graph
now compiles into runtime quest data. Designers author nested
prerequisite expressions, named outcomes, reusable prerequisite rules,
and hierarchical activation groups — all visually. Four inspection
surfaces expose runtime state directly in the editor during PIE.
SimpleCore graduates to a multi-module plugin with its own editor-side
inspector.

### Added

#### Compiler + Runtime Bridge
- `FQuestlineGraphCompiler` translates authored graphs into runtime
  nodes and compiled Gameplay Tags, registered as native via
  `FNativeGameplayTag`
- Compile toolbar action (single graph) + Compile All menu command
- Compiled tags persisted to `Config/SimpleQuest/CompiledTags.ini` for
  startup availability before the Asset Registry finishes loading
- Asset Registry scan registers tags from all questline assets at game
  start
- `FMessageLog("QuestCompiler")` replaces `UE_LOG` for compiler
  diagnostics — clickable toasts open a dedicated log page on failure
- Cross-graph parallel-path compile-time warning

#### Graph Authoring
- Named outcomes replace binary Success / Failure — designers author
  any number of named outcome tags per quest/objective and wire each
  to its own downstream path
- Prerequisite expression system with full AND / OR / NOT combinators
  supporting arbitrary nesting
- Reusable Prerequisite Rules — Entry/Exit portal pair letting a named
  rule be evaluated from multiple sites without duplication
- Activation Groups — Entry/Exit portal pair providing many-to-many
  node activation topology without per-wire bookkeeping
- Utility nodes: `SetBlocked` / `ClearBlocked` with tag-container
  picker for designer-authored quest gating
- Step-level nodes — quests composed of ordered Step nodes carrying
  inline objective class pickers
- `LinkedQuestline` node embedding a referenced questline asset, with
  outcome pins synced from the linked asset
- Custom Slate widgets for combinator, group, utility, and step nodes
  — compact layouts, inline tag pickers, dynamic pin UI, "Add pin"
  controls where applicable
- Double-click on a content / portal node navigates to that node in
  the graph editor (cross-graph navigation supported)
- Tagged content handles: every content node carries a stable
  `QuestGuid` surviving rename; outliner + examiner panels use
  GUID-based references so navigation survives rename operations

#### Inspection Surfaces
- **Questline Outliner** — nomad panel tab listing every content and
  portal node in the open questline with double-click navigation
- **Entry Source panel** — Details customization on Entry nodes
  showing every effective source reaching the Entry via a cross-graph
  walk
- **Group Examiner** — panel listing Activation and Prerequisite
  groups with member counts, expansion, navigation to source nodes
- **Prereq Expression Examiner** — nested algebraic box layout of the
  pinned prerequisite expression; per-operator color inheritance
  (AND green, OR cyan, NOT red, RuleRef amber); collapsible
  combinators and RuleRefs; cross-editor hover halos that highlight
  the corresponding graph nodes
- **PIE Graph Debug Overlay (Tier 1)** — per-state colored halos on
  content-node widgets in the graph editor while PIE is active,
  driven by the new `FQuestPIEDebugChannel` reading `QuestState.*`
  facts from the PIE world's `UWorldStateSubsystem`
- **Prereq Examiner PIE coloring (Tier 2)** — live leaf satisfaction
  tints with AND / OR / NOT roll-up on every layer of the expression
  tree; hover emphasis model decouples fill (follows cursor) from
  border (emphasizes inputs)
- **WorldState Facts panel** — new `SimpleCoreEditor` module; nomad
  tab listing all asserted `WorldState` facts during PIE with
  case-insensitive substring filter, alphabetical sort, live per-tick
  refresh

#### Quest Event Context Model
- `FQuestObjectiveTriggerContext` struct carrying `TriggeredActor`,
  `Instigator`, counter state, and `FInstancedStruct CustomData` for
  game-specific extension
- `FQuestNodeInfo` struct carrying compiled display metadata
  (`QuestTag`, `DisplayName` `FText`) baked by the compiler
- `FQuestEventPayload` wrapping `FQuestNodeInfo` + optional
  `FQuestObjectiveTriggerContext` for outbound events
- All outbound events (Started, Ended, Enabled, Deactivated,
  Progress) carry `FQuestEventPayload`
- `UQuestTriggerComponent::Send*` methods accept optional `CustomData`
  via `AutoCreateRefTerm` (BP pin optional, C++ default-constructs)
- `FQuestObjectiveTriggered` / `Killed` / `Interacted` gain
  `CustomData` fields plumbed through to subsystem handlers

#### Runtime
- `UCountingQuestObjective` subclass — counter state
  (`CurrentElements` / `MaxElements`, `SetCurrentElements`,
  `AddProgress`) extracted from base `UQuestObjective`; non-counting
  objectives inherit cleanly from the minimal base
- `FQuestProgressEvent` fires per trigger (not just on completion) for
  UI reactivity (e.g. kill-counter updates from 3/5 to 4/5)
- Quest state facts in the `QuestState.<Tag>.*` namespace:
  `.Live`, `.Completed`, `.PendingGiver`, `.Deactivated`, `.Blocked`
- Autowire: rule-aware priority walker with Deactivation pin
  auto-expansion (dragging from a `Deactivated` output auto-expands
  the target node's deactivation pins before routing the wire)

#### SimpleCore
- `UWorldStateSubsystem::GetAllFacts()` — read-only const-ref
  accessor for inspection surfaces (not a UFUNCTION; mutation still
  exclusively through `AddFact` / `RemoveFact` / `ClearFact`)
- `SubscribeRawMessage<T>` / `PublishRawMessage` — `FInstancedStruct`
  delivery without type-slicing, supporting generic forwarding
  (e.g. SimpleCore ↔ GameplayMessageRouter bridges)
- New module: **`SimpleCoreEditor`** — editor-side PIE debug channel
  (`FSimpleCorePIEDebugChannel`) and `WorldStateFacts` nomad tab,
  usable by any SimpleCore consumer regardless of whether SimpleQuest
  is present

#### Plugin Presentation
- `SimpleQuestStyle` Slate style set with SVG class icons (64px, 16px)
- Plugin icon applied to the asset type thumbnail, K2 Complete
  Objective node, and Questline Outliner tab

#### K2 Nodes
- `K2Node_CompleteObjectiveWithOutcome` — custom K2 node with
  outcome tag pin, typed Context pin, and integrated Slate widget

### Changed

#### Vocabulary + Renames
- Terminal nodes: **Entry → Start**, **Exit → Outcome** (user-facing)
- Portal nodes: **Group Setter / Getter → Entry / Exit** (activation
  and prerequisite both use the portal metaphor)
- WorldState namespace: `Quest.State.*` → `QuestState.*`
  (`QuestStateTagUtils::Namespace` as the single source of truth)
- Runtime class renames to match portal vocabulary:
  `GroupSignalSetterNode` → `ActivationGroupSetterNode`,
  `GroupSignalGetterNode` → `ActivationGroupGetterNode`

#### Graph Schema
- Unified signal-identity model: every wire carries a Signal; Signal
  identity is `(source node, outcome)` with `AnyOutcome` absorbing
  specifics on the same node
- `CanCreateConnection` / `TryCreateConnection` collapsed onto a
  shared comparator (`PinsRepresentSameSignal`, `SignalSetsCollide`)
- Duplicate-source dedupe and parallel-path detection unified across
  previously separate code paths
- Self-loop rules tightened: only `QuestOutcome` and
  `QuestActivation` outputs may loop back to the same node's
  `Activate`; all other self-connections blocked
- `UQuestlineNodeBase::AutowireNewNode` replaces the schema-level
  hook — per-node priority walker with role-based pin categorization

#### Event Payloads
- `FSignalEventBase` dissolved — all events are plain USTRUCTs with
  no base-class requirement
- Routing tag is an explicit `PublishMessage` parameter (no longer
  embedded in payload)
- `SignalSubsystem` walks tag hierarchy on publish — a subscriber on
  `Quest.Questline1` receives events from every quest within

#### Class Hierarchy
- `UQuestNodeBase` (runtime) and `UQuestlineNodeBase` (editor)
  restructured as base classes; content nodes descend from
  `UQuestlineNode_ContentBase`
- `UQuestlineGraph` gains `FriendlyName` `FText` — preferred over
  asset name in titles, tooltips, outliner root display
- `UQuestlineNodeBase::PostEditUndo` broadcasts `NotifyGraphChanged`
  so dynamic-pin widgets rebuild after undo operations

#### Extensibility
- `ISimpleQuestEditorModule` formalized with registration-based
  extensibility tiers:
  - Tier 1: self-describing types (subclass + override classification
    virtuals)
  - Tier 2: replaceable policies (`FQuestlineGraphTraversalPolicy`)
  - Tier 3: factory-registered algorithms (`FQuestlineGraphCompiler`)

### Removed

#### Legacy Events
- `FQuestPrerequisiteCheckFailed`
- `FQuestRegistrationEvent`
- `FQuestRewardEvent` (reward data now carried as a field on
  completion events)
- `FQuestStepCompletedEvent`, `FQuestStepPrereqCheckFailed`,
  `FQuestStepStartedEvent`
- `FQuestTryStartEvent`
- `FQuestlineEndedEvent` (vestige of old two-level model)
- `FSignalEventBase` base class

#### Legacy Nodes
- `UQuestlineNode_Exit_Success` / `UQuestlineNode_Exit_Failure` —
  replaced by a single `UQuestlineNode_Exit` (Outcome) with a
  designer-picked `OutcomeTag` (any outcome; Success / Failure are
  just conventional names)

#### Dead Code
- `PossibleOutcomes` UPROPERTY (discovery via K2 + UPROPERTY
  reflection supersedes)
- `UQuestSignalSubsystem` — renamed to `USignalSubsystem` and
  extracted into SimpleCore
- `SignalTypes.h`, `SignalUtilities.h`
- Legacy `Config/Tags/SimpleQuestCompiledTags.ini` location —
  relocated to `Config/SimpleQuest/CompiledTags.ini` with automatic
  migration delete on editor startup

### Fixed
- Undo / redo crash on dynamic-pin operations (Import Outcome Pins
  and other pin-rebuild flows) — `PostEditUndo` now broadcasts
  `NotifyGraphChanged` so Slate pin widgets reconstruct against the
  restored `Pins` array
- Self-loop connection validation rejected legitimate loops and
  accepted some illegal combinations (e.g. `Deactivated → Activate`);
  guard moved before the deactivation block
- Auto-knot insertion fired for illegal connections
  (same root cause)
- Stale tag cleanup on recompile — registry key mismatch between
  startup path (`GetObjectPathString`) and compile path
  (`GetPackage()->GetName()`) left two entries per graph, old one
  never replaced. Normalized to `PackageName` across the board
- Group setter forward-reach dedupe — colliding signals reached a
  setter via getter + direct path; new `CheckGroupSetterForwardReach`
  catches the retroactive hole
- Hover halo off-screen paint — graph-space viewport culling added
  to all halo passes in `SQuestlineGraphPanel::OnPaint`
- Group Examiner pin-role drift after portal rename — stale FName
  literals ("Activate" / "Forward") replaced with `EQuestPinRole`
  role-based lookup
- PIE debug channel subsystem resolution for Simulate In Editor —
  primary path now uses `GEditor->PlayWorld` (works for both PIE
  and SIE); world-context iteration retained as fallback
- Compile toolbar button visually present but silently did nothing
  — `CompileQuestlineGraph` command was never mapped to an action;
  `MapAction` added in `BindGraphCommands`

---

## [0.2.0] — 2026-03-28 — Visual Graph Editor: Schema & Connections

### Added
- Full connection validation in `UQuestlineGraphSchema::CanCreateConnection`:
    - Prevents duplicate signal paths from the same source quest node
    - Prevents parallel paths to the same destination through reroute nodes
    - Enforces exit node rules: a quest node may not route multiple outputs
      to the same exit node, directly or through reroutes
    - Enforces that a single output pin leads to at most one exit node
- Custom wire rendering via `FQuestlineConnectionDrawingPolicy`:
    - Green wires for Quest Success paths
    - Red wires for Quest Failure paths
    - White wires for Any Outcome and Activation paths
    - Dashed wire rendering for Prerequisites connections
    - Spline hover detection for all wire types
- Double-clicking a wire now inserts a Reroute node in-place
- Hotkey node placement matching standard Blueprint conventions:
    - `Q + click` — place Quest node
    - `S + click` — place Quest Success exit node
    - `F + click` — place Quest Failure exit node
    - `R + click` — place Reroute node
    - Pressing a valid key while dragging a wire places and connects
      the corresponding node immediately at the cursor
        - Node placement by hotkey aligns the cursor with the node's
          relevant input pin, matching standard Blueprint behavior
- `SQuestlineGraphPanel` wrapper widget providing graph-aware input
  handling with correct Slate focus lifecycle management

---

## [0.1.0] — 2026-03-25 — Visual Graph Editor: Scaffolding

### Added
- `UQuestlineGraphSchema` — custom graph schema for the questline editor
- Editor node types:
    - `UQuestlineNode_Entry` — questline start node (non-deletable)
    - `UQuestlineNode_Quest` — represents a single quest; pins: Activate,
      Prerequisites (input), Success, Failure, Any Outcome (output)
    - `UQuestlineNode_Exit_Success` / `UQuestlineNode_Exit_Failure` — terminal
      outcome nodes
    - `UQuestlineNode_Knot` — reroute/passthrough node with dynamic
      type propagation matching the first connected wire
- `FQuestlineGraphEditor` — asset editor toolkit hosting the graph viewport
- `UQuestlineGraph` — editor graph asset containing the `UEdGraph`
- Basic node auto-wiring (`AutowireNewNode`) on all node types:
    - Output pin drags connect to Activate by default on Quest nodes
    - Input pin drags connect from Any Outcome by default on Quest nodes
    - Exit nodes only accept connections from output pins

---

## [0.0.1] — 2025-10-22 — Event Bus

### Added
- Runtime event bus supporting decoupled quest state communication

---

## [0.0.0] — 2025-05-30 — Initial Prototype (GameDev.tv Game Jam — USS Proteus)

### Added
- Core DAG data structure for representing questline graphs at runtime
- Quest node data model with Success, Failure, and Any Outcome resolution
- Initial proof-of-concept during game jam development
