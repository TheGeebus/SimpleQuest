"""PlanSplit_MoveLegal - a move INTO a container that a designer could actually have authored.

PlanSplit_Move is the known-bad: it relocates the Step and leaves both its links crossing the boundary, which the
guard now refuses. This is the legal counterpart - the same relocation, but with the wiring restated so every link
joins two nodes in one graph:

    BEFORE                                   AFTER
    rootEntry --activates--> Step            rootEntry  --activates--> SampleQuest
    Step --outcome(Reached)--> Chapter_1     innerEntry --activates--> Step          (inside SampleQuest)
                                             Step       --outcome(Reached)--> newExit (inside SampleQuest)
                                             SampleQuest--outcome(Reached)--> Chapter_1

It therefore exercises a MOVE, a CREATE landing INSIDE a container, and an edge rewire in one apply.
"""
import os
import shutil

from _paths import EXPORT_ROOT as ROOT
SRC = os.path.join(ROOT, "PlanSplit_Control")
DST = os.path.join(ROOT, "PlanSplit_MoveLegal")

QUEST = "9856BE0744F0BC8CA9286FA075636EFF"   # the SampleQuest container, at root
STEP = "EEC37AE94B181E668C5D239F52739203"    # the Step being relocated
ROOT_ENTRY = "A8925E824B49F6DD8B2DF69958CC5D22"
INNER_ENTRY = "FC7376F7435B352CDE269EB8561FF2DA"
CHAPTER1 = "A2E70194427C55AD9246D8A3E8A938BB"
NEW_EXIT = "FEEDFACE000040008000000000000001"   # synthetic but GUID-shaped, so identity is adopted verbatim
OUTCOME = "outcome(SimpleQuest.Outcome.Reached)"


def load(path):
    data = open(path, "rb").read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16"), "utf-16"
    if data[:3] == b"\xef\xbb\xbf":
        return data.decode("utf-8-sig"), "utf-8-sig"
    return data.decode("utf-8"), "utf-8"


def save(path, text, enc):
    open(path, "wb").write(text.encode(enc))


if os.path.isdir(DST):
    shutil.rmtree(DST)
shutil.copytree(SRC, DST)

# 1. relocate the Step
path = os.path.join(DST, "step.tsv")
text, enc = load(path)
lines = text.split("\n")
hdr = lines[0].rstrip("\r").split("\t")
ki, gi = hdr.index("key"), hdr.index("graph")
for n in range(1, len(lines)):
    if not lines[n].strip():
        continue
    cells = lines[n].rstrip("\r").split("\t")
    if cells[ki] == STEP:
        cells[gi] = QUEST
        lines[n] = "\t".join(cells)
        print(f"  step.tsv: graph root -> {QUEST[:8]}")
save(path, "\n".join(lines), enc)

# 2. declare an Exit INSIDE the container, so the container has an outcome to route out of
path = os.path.join(DST, "exit.tsv")
text, enc = load(path)
lines = [l for l in text.split("\n") if l.strip()]
hdr = lines[0].rstrip("\r").split("\t")
row = [""] * len(hdr)
row[hdr.index("key")] = NEW_EXIT
row[hdr.index("class")] = "QuestlineNode_Exit"
row[hdr.index("graph")] = QUEST
row[hdr.index("OutcomeTag")] = '(TagName="SimpleQuest.Outcome.Reached")'
lines.append("\t".join(row))
save(path, "\n".join(lines) + "\n", enc)
print(f"  exit.tsv: + {NEW_EXIT[:8]} inside {QUEST[:8]}")

# 3. restate the wiring
path = os.path.join(DST, "edges.tsv")
text, enc = load(path)
lines = [l for l in text.split("\n") if l.strip()]
drop = {
    (ROOT_ENTRY, "activates(Entered)", STEP),          # the Step is no longer reachable from the outer graph
    (STEP, OUTCOME, CHAPTER1),                          # ...nor does it route out of the container directly
}
kept = [lines[0]]
for l in lines[1:]:
    c = l.rstrip("\r").split("\t")
    if len(c) >= 3 and (c[0], c[1], c[2]) in drop:
        print(f"  edges.tsv: - {c[0][:8]} {c[1]} {c[2][:8]}")
        continue
    kept.append(l)

add = [
    (ROOT_ENTRY, "activates(Entered)", QUEST),          # the container is what the outer graph now activates
    (INNER_ENTRY, "activates(Entered)", STEP),          # ...and the container's own entry drives the Step
    (STEP, OUTCOME, NEW_EXIT),                          # the Step resolves into the container's exit
    (QUEST, OUTCOME, CHAPTER1),                         # ...which becomes the container's outcome, routed onward
    (QUEST, "contains(InnerGraph)", STEP),              # containment mirrors what an export would emit for an
    (QUEST, "contains(InnerGraph)", NEW_EXIT),          # inner node; the importer derives it from the graph cell
]
for a in add:
    kept.append("\t".join(a))
    print(f"  edges.tsv: + {a[0][:8]} {a[1]} {a[2][:8]}")
save(path, "\n".join(kept) + "\n", enc)

print(f"\nbuilt {DST}")
