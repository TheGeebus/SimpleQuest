# Resolver verification harness

Development scripts for checking that a change to the data resolver's **import pipeline** changed nothing a user can
observe. They complement the automation tests (`Automation RunTests SimpleQuest`) rather than replacing them, and they
catch a different class of thing:

| | asserts |
|---|---|
| Automation tests | semantics somebody thought to assert — this plan has N entries, this cell was refused, undo reverts |
| This harness | the **exact console output is unchanged**, including wording, ordering, and warnings nobody wrote an assertion for |

The second is what makes a pure-motion refactor checkable. The failure mode of moving code is rarely "the behaviour
broke" — it is "the behaviour is fine but a log line now reads differently," and only a byte comparison sees that.

## Running it

Python 3, no dependencies. Four commands in the editor console, then one in a terminal. **Run them in this order** —
the first one is not optional, for two reasons given below.

```
SimpleQuest.ImportQuestline <ExportDir>/PlanSplit_Control      --in-place=/Game/SplitTest/QL_SplitProbe --apply --delete-orphans
SimpleQuest.ImportQuestline <ExportDir>/PlanSplit_Control      --in-place=/Game/SplitTest/QL_SplitProbe
SimpleQuest.ImportQuestline <ExportDir>/PlanSplit_MoveLegal    --in-place=/Game/SplitTest/QL_SplitProbe
SimpleQuest.ImportQuestline <ExportDir>/PlanSplit_ClassChange  --in-place=/Game/SplitTest/QL_SplitProbe
```

```
python capture_resolver_baseline.py compare   # diff against the recording  (exit 0 = IDENTICAL)
python capture_resolver_baseline.py save      # re-record, once you have decided a difference was intended
```

Reach for `compare` first, always. `save` overwrites the recording, which destroys the evidence of whether anything
drifted. `diff_plans.py` compares the last two plan blocks in the log against each other, for a before/after inside one
session.

### Why the first command matters

The three capture runs are read-only, so what they print is a **diff against whatever state the probe asset is in**. That
makes the output a statement about the environment as much as the code, and there are two ways for the environment to
poison it. The leading apply closes both at once:

- **It resets the probe to pristine.** Otherwise a plan describes some earlier apply's leftovers. A previous baseline was
  recorded with the asset mid-`MoveLegal`, which quietly swapped which fixture exercised the clean case.
- **It absorbs the once-per-process guard-log.** `StructLiteral fallback for InstancedStruct::Payload` is emitted once per
  `(struct, property)` per editor session, so it attaches to whichever import runs first and is missing from every later
  one. Recorded, it pins a property of the session rather than of the code.

`capture_resolver_baseline.py` refuses to save if either condition is violated, so a mistake here fails loudly instead of
banking a bad recording. Both refusals name the fix.

## Regenerating the fixtures

`Saved/` is gitignored, so the fixture folders are **not** in the repository — the generators are, and the fixtures come
back from them. The chain runs:

```
Content/SplitTest/QL_SplitProbe.uasset
  -> SimpleQuest.ExportQuestline    -> Saved/QuestExport/QL_SplitProbe
     -> make_split_fixtures.py      -> PlanSplit_Control          (unmodified; the baseline's clean case)
                                       PlanSplit_ClassChange      (a row names a different real node class)
                                       PlanSplit_Move             (relocation leaving links across the boundary)
        -> make_legal_move_fixture.py  -> PlanSplit_MoveLegal     (same relocation, wiring restated)
        -> make_child_fixtures.py      -> PlanChild_Add / _Remove / _Silent
```

The baseline records the first, second and fourth of those. `PlanSplit_Move` is the known-bad counterpart the
cross-boundary guard refuses, and `make_fixture.py` is unrelated to the baseline entirely — it perturbs a QuickStart
export to probe plan ordering.

Each generator's docstring says what its fixture proves. The child fixtures are worth reading before editing any of
them — `PlanChild_Silent` exists to prove a **negative**, that a source which simply omits a property does not delete
authored content, and a fixture set covering only add and remove would never notice if that guard broke.

### Synthetic node keys

A fixture that invents a node needs a key, and the key decides which code path runs: a row key that parses as a GUID is
adopted verbatim as the node's identity, while anything else mints a deterministic GUID and records the original string
as `ImportSourceKey`. A readable key like `NewExit` would therefore test the *minting* path and produce a node unlike
its neighbours, so synthetic keys here are GUID-shaped on purpose.

They use a recognizable prefix and a counter, so a fixture-invented node is obvious at a glance in log output next to
real exported GUIDs:

```
FEEDFACE000040008000000000000001    make_legal_move_fixture.py - the invented exit
```

Increment the last digit for the next one. Only the 32-hex-character shape is required — `FGuid` is four `uint32`s and
does not check UUID version or variant nibbles.

Note that applying such a fixture puts the synthetic GUID into the probe asset, so it propagates into anything later
derived from a re-export of it.

## What it does not cover

Worth knowing before trusting a green run:

- **Three fixtures, 33 lines.** It is a tripwire, not a proof.
- **Only `SimpleQuest.ImportQuestline`.** Export, `DumpCompiled` and the round-trip harness are not observed. In
  particular `DumpCompiled` reports its node count only in its log line and never in the file it writes, so a change
  there is invisible to any file comparison.
- **Nothing that fails silently.** `GEngine->Exec` on an unregistered console command returns without complaint, so a
  change that drops a command registration passes both this harness and the automation tests.
- **The probe asset is not in the repository.** `/Content/` is gitignored, so `QL_SplitProbe` — the root of the chain
  above — travels with the working copy, not the clone.
