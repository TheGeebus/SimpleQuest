"""Capture (or compare) the resolver's console output for the three PlanSplit fixtures.

The console command is about to become a thin caller over QuestImport_RunInPlace. That refactor threads source reading,
mapping application, policy derivation and error handling through a new seam, and it is supposed to change NOTHING a
user can observe. The only way to make that claim checkable is to record the output first.

    python capture_resolver_baseline.py save     -> writes baseline_resolver.txt
    python capture_resolver_baseline.py compare  -> diffs the current log's blocks against it
"""
import difflib
import os
import re
import sys

from _paths import LOG
HERE = os.path.dirname(os.path.abspath(__file__))
BASELINE = os.path.join(HERE, "baseline_resolver.txt")

FIXTURES = ["PlanSplit_Control", "PlanSplit_MoveLegal", "PlanSplit_ClassChange"]


def strip_stamp(line):
    return re.sub(r"^\[[^\]]+\]\[\s*\d+\]", "", line).rstrip()


def latest_block(text, fixture):
    """The LAST run for this fixture: from its Cmd line to the first line that is not resolver output."""
    marker = f"ImportQuestline {LOGPATH_HINT}" if False else fixture
    start = text.rfind("Cmd: SimpleQuest.ImportQuestline")
    # walk backwards through Cmd lines until we find one naming this fixture
    while start != -1:
        line_end = text.find("\n", start)
        if fixture in text[start:line_end] and "--apply" not in text[start:line_end]:
            break
        start = text.rfind("Cmd: SimpleQuest.ImportQuestline", 0, start)
    if start == -1:
        return None

    out = []
    for line in text[start:].splitlines():
        if out and "LogSimpleQuestResolver" not in line:
            break
        out.append(strip_stamp(line))
    return out


LOGPATH_HINT = ""

text = open(LOG, encoding="utf-8", errors="ignore").read()
blocks = {}
for f in FIXTURES:
    b = latest_block(text, f)
    if b is None:
        print(f"FAIL - no run found for {f}")
        sys.exit(1)
    blocks[f] = b

if not any("already matches the source" in l for l in blocks["PlanSplit_Control"]):
    print("FAIL - PlanSplit_Control planned changes, so QL_SplitProbe is not in its pristine state.\n"
          "       Run PlanSplit_Control with --apply --delete-orphans, then re-run the three plans.")
    sys.exit(1)

if any("StructLiteral fallback" in l for l in blocks[FIXTURES[0]]):
    print("FAIL - the first block carries the once-per-process StructLiteral guard-log.\n"
          "       Run the reset apply first so it absorbs that line, then re-run the three plans.")
    sys.exit(1)

rendered = []
for f in FIXTURES:
    rendered.append(f"===== {f} =====")
    rendered.extend(blocks[f])
    rendered.append("")
current = "\n".join(rendered)

mode = sys.argv[1] if len(sys.argv) > 1 else "save"

if mode == "save":
    open(BASELINE, "w", encoding="utf-8").write(current)
    print(f"baseline written: {BASELINE}")
    for f in FIXTURES:
        print(f"  {f}: {len(blocks[f])} lines")
    sys.exit(0)

if not os.path.exists(BASELINE):
    print("no baseline to compare against - run with 'save' first")
    sys.exit(1)

before = open(BASELINE, encoding="utf-8").read().splitlines()
after = current.splitlines()
if before == after:
    print(f"IDENTICAL - {len(after)} lines across {len(FIXTURES)} fixtures. The refactor changed nothing observable.")
    sys.exit(0)

print("DIFFERS:")
for d in difflib.unified_diff(before, after, "before", "after", lineterm="", n=2):
    print(d)
sys.exit(1)
