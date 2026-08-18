"""Compare the last two in-place PLAN blocks in the editor log.

PlanInPlace sorts OutPlan.Entries, then each entry's Changes. Before that, both walks came off TMaps and two runs over
identical input rendered in whatever order the hash landed in. Identical blocks = the sort holds.

THE DETECTOR HAS TO REFUSE A VACUOUS PASS. A plan with no changed entries logs two lines that are trivially identical
between runs, which looks exactly like success and proves nothing - that is how the first attempt at this passed while
the fixture was silently a no-op. So the ordering claim is only made when there is enough material to have shuffled.
"""
import difflib
import re
import sys

from _paths import LOG
MIN_ENTRIES = 2   # below this, line order carries no information

text = open(LOG, encoding="utf-8", errors="ignore").read()
starts = [m.start() for m in re.finditer(r"ImportQuestline: in-place PLAN for ", text)]
if len(starts) < 2:
    print(f"FAIL - found {len(starts)} plan block(s); run the import twice")
    sys.exit(1)


def block(at):
    out = []
    for line in text[at:].splitlines():
        if out and "LogSimpleQuestResolver" not in line:
            break
        out.append(re.sub(r"^\[[^\]]+\]\[\s*\d+\]", "", line).rstrip())
    return out


a, b = block(starts[-2]), block(starts[-1])

# Count what actually got rendered: one [UPDATE]/[CREATE]/[ORPHAN] line per entry the plan chose to print.
entry_re = re.compile(r"\[(UPDATE|CREATE|ORPHAN)\]")
change_re = re.compile(r":\s+[^\s:]+: '.*' -> '.*'$")   # log lines carry a category prefix; the property name holds no colon
entries = [len(entry_re.findall(l)) for l in a].count(1)
changes = sum(1 for l in a if change_re.search(l))
multi = re.search(r"(\d+) update\(s\) \((\d+) with changes\)", a[0] if a else "")

print(f"blocks: {len(a)} vs {len(b)} lines | rendered entries: {entries} | property changes: {changes}")
if multi:
    print(f"summary line: {multi.group(1)} update(s), {multi.group(2)} with changes")

if entries < MIN_ENTRIES:
    print(f"\nVACUOUS - only {entries} entry(s) rendered. Two identical near-empty plans prove nothing about")
    print("ordering. The fixture is not producing changes; fix it before trusting any result here.")
    sys.exit(2)

if a != b:
    print("\nDIFFER:")
    for d in difflib.unified_diff(a, b, "run 1", "run 2", lineterm="", n=1):
        print(d)
    if sorted(a) == sorted(b):
        print("\n>>> same lines, different ORDER - this is exactly what the sort is meant to prevent")
    sys.exit(1)

print(f"\nPASS - {entries} entries / {changes} property changes rendered identically across both runs.")
if changes < entries:
    print("note: fewer changes than entries, so the per-entry Changes sort is only partly exercised.")
print("\n--- plan ---")
for line in a:
    print(line)
