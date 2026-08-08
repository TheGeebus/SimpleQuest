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

Python 3, no dependencies. Run the console commands in the editor first; these read the log afterwards.

```
python capture_resolver_baseline.py save      # record the current output
python capture_resolver_baseline.py compare   # diff the current output against the recording
```

`compare` exits 0 and prints `IDENTICAL` when nothing moved, or exits 1 and prints a unified diff. `diff_plans.py`
compares the last two plan blocks in the log against each other, for when you want a before/after within one session.

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
