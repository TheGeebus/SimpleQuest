"""Three fixtures for the bStructuralChange split.

CONTROL   - unmodified export. Proves the level comparison did not change meaning: must still plan clean.
CLASS     - one row's class cell changed to a different REAL node class. Must produce a plan-level Refusal, which is
            whole-plan fatal, so apply performs nothing.
MOVE      - the Step's graph cell changed from 'root' to the Quest container's key. Must plan as bMoved and, until
            move is implemented, report a NAMED skip on apply rather than an anonymous deferral.
"""
import os
import shutil

from _paths import EXPORT_ROOT as ROOT
# Built from QL_SplitProbe's OWN export, not QuickStart's. Duplicating an asset re-mints every QuestGuid, so fixtures
# made from the original export would match nothing and every row would plan as a create.
SRC = os.path.join(ROOT, "QL_SplitProbe")

QUEST_KEY = "9856BE0744F0BC8CA9286FA075636EFF"   # the QuestlineNode_Quest container, currently at graph=root
STEP_KEY = "EEC37AE94B181E668C5D239F52739203"    # the QuestlineNode_Step, currently at graph=root


def load(path):
    data = open(path, "rb").read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16"), "utf-16"
    if data[:3] == b"\xef\xbb\xbf":
        return data.decode("utf-8-sig"), "utf-8-sig"
    return data.decode("utf-8"), "utf-8"


def edit_cell(folder, filename, row_key, column, new_value):
    path = os.path.join(folder, filename)
    text, enc = load(path)
    lines = text.split("\n")
    header = lines[0].rstrip("\r").split("\t")
    col = header.index(column)
    key_col = header.index("key")
    hits = 0
    for n in range(1, len(lines)):
        if not lines[n].strip():
            continue
        cr = lines[n].endswith("\r")
        cells = lines[n].rstrip("\r").split("\t")
        if cells[key_col].strip() != row_key:
            continue
        old = cells[col]
        cells[col] = new_value
        lines[n] = "\t".join(cells) + ("\r" if cr else "")
        print(f"    {filename} [{row_key[:8]}] {column}: '{old}' -> '{new_value}'")
        hits += 1
    open(path, "wb").write("\n".join(lines).encode(enc))
    if hits != 1:
        raise SystemExit(f"expected 1 row, matched {hits} - fixture not built")


def fresh(name):
    dst = os.path.join(ROOT, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(SRC, dst)
    print(f"  {name}")
    return dst


print("building fixtures from the QL_SplitProbe export:")
fresh("PlanSplit_Control")

d = fresh("PlanSplit_ClassChange")
edit_cell(d, "step.tsv", STEP_KEY, "class", "QuestlineNode_Quest")

d = fresh("PlanSplit_Move")
edit_cell(d, "step.tsv", STEP_KEY, "graph", QUEST_KEY)

print("\ndone")
