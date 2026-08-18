"""Rebuild Saved/QuestExport/PlanOrderProbe as a perturbed copy of the QuickStart export.

The first attempt appended text AFTER the closing paren of an NSLOCTEXT cell, which the FText buffer parser discards -
the value round-tripped identical and the plan came back empty. FTextProperty::Identical_Implementation compares
DISPLAY STRINGS when GIsEditor, so the perturbation has to land inside the third NSLOCTEXT argument.
"""
import os
import re
import shutil

from _paths import EXPORT_ROOT as ROOT
SRC = os.path.join(ROOT, "QuickStart")
DST = os.path.join(ROOT, "PlanOrderProbe")

# NSLOCTEXT("ns", "key", "display") -- capture around the display string so only it moves.
QUOTED = r'"(?:[^"\\]|\\.)*"'
NSLOC = re.compile(r'(NSLOCTEXT\(\s*' + QUOTED + r'\s*,\s*' + QUOTED + r'\s*,\s*")((?:[^"\\]|\\.)*)("\s*\))')

TARGET_COLUMNS = ("NodeLabel", "DisplayName")
TARGET_FILES = ("linked_questline.tsv", "quest.tsv", "step.tsv")

# A SECOND change on the same rows, so each plan entry carries two property changes and the per-entry Changes sort is
# exercised too - one change per entry would only ever prove the entry-level sort. EResettableReplay is a plain enum
# (Inherit / Enabled / Disabled) with no structural consequence, unlike a pin count or a class swap.
ENUM_COLUMN = "ResettableReplay"
ENUM_FLIP = {"Enabled": "Disabled", "Disabled": "Enabled"}


def load(path):
    data = open(path, "rb").read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16"), "utf-16"
    if data[:3] == b"\xef\xbb\xbf":
        return data.decode("utf-8-sig"), "utf-8-sig"
    return data.decode("utf-8"), "utf-8"


def perturb(cell):
    if not cell.strip():
        return cell, False
    if NSLOC.search(cell):
        return NSLOC.sub(lambda m: m.group(1) + m.group(2) + " PROBE" + m.group(3), cell, count=1), True
    return cell + " PROBE", True


if os.path.isdir(DST):
    shutil.rmtree(DST)
shutil.copytree(SRC, DST)

total = 0
for name in TARGET_FILES:
    path = os.path.join(DST, name)
    text, enc = load(path)
    lines = text.split("\n")
    header = lines[0].rstrip("\r").split("\t")
    cols = [header.index(c) for c in TARGET_COLUMNS if c in header]
    enum_col = header.index(ENUM_COLUMN) if ENUM_COLUMN in header else -1
    hits = 0
    for n in range(1, len(lines)):
        if not lines[n].strip():
            continue
        cr = lines[n].endswith("\r")
        cells = lines[n].rstrip("\r").split("\t")
        for i in cols:
            if i < len(cells):
                cells[i], changed = perturb(cells[i])
                hits += changed
        if 0 <= enum_col < len(cells) and cells[enum_col].strip() in ENUM_FLIP:
            cells[enum_col] = ENUM_FLIP[cells[enum_col].strip()]
            hits += 1
        lines[n] = "\t".join(cells) + ("\r" if cr else "")
    open(path, "wb").write("\n".join(lines).encode(enc))
    print(f"  {name}: {hits} cell(s) across columns {cols}")
    total += hits

print(f"\n{total} cells perturbed in {DST}")
text, _ = load(os.path.join(DST, "linked_questline.tsv"))
print("\nsanity - first two perturbed NodeLabel cells:")
for row in text.split("\n")[1:3]:
    print("   ", row.split("\t")[4])
