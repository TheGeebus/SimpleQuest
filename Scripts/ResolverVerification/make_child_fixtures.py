"""Three fixtures for instanced-child topology, built from PlanSplit_Control (the pristine QL_SplitProbe export).

Target: reward node 4B96... which owns three children across three tables —
    Rewards[0] ScaledAmountReward   (scaled_amount_reward.tsv)
    Rewards[1] CurrencyReward       (currency_reward.tsv)
    Rewards[2] LootTableReward      (loot_table_reward.tsv)

PlanChild_Remove  - drops Rewards[2].            Expect: one ChildRemoved, and apply actually removes it.
PlanChild_Add     - adds Rewards[3] (XPReward).  Expect: one ChildAdded, and apply actually creates it.
PlanChild_Silent  - drops ALL THREE.             Expect: NO child change planned at all, and apply leaves them intact.

The third is the one that matters most. ReattachInstanced replaces a property's contents wholesale when the source
DESCRIBES them and leaves them alone when it does not - "silence is not an assertion of emptiness". If that guard ever
breaks, a source that simply omits a property would silently delete authored content, which is destructive and quiet.
A fixture that only tested add and remove would never notice.
"""
import os
import shutil

from _paths import EXPORT_ROOT as ROOT
SRC = os.path.join(ROOT, "PlanSplit_Control")
OWNER = "4B9676754FA53D95F804F48B1F9483F2"


def load(path):
    data = open(path, "rb").read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16"), "utf-16"
    if data[:3] == b"\xef\xbb\xbf":
        return data.decode("utf-8-sig"), "utf-8-sig"
    return data.decode("utf-8"), "utf-8"


def save(path, text, enc):
    open(path, "wb").write(text.encode(enc))


def fresh(name):
    dst = os.path.join(ROOT, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(SRC, dst)
    return dst


def drop_edges_to(folder, key):
    """A child row is ALSO an edge endpoint - the owner carries a contains(<Prop>[i]) edge to it. Dropping the row and
    leaving the edge is a dangling endpoint, and ValidateBundle refuses the whole bundle for it (correctly). An export
    would never emit one, so a fixture must not either."""
    path = os.path.join(folder, "edges.tsv")
    text, enc = load(path)
    lines = [l for l in text.split("\n") if l.strip()]
    kept = [lines[0]] + [l for l in lines[1:] if l.split("\t")[-1] != key]
    dropped = len(lines) - len(kept)
    save(path, "\n".join(kept) + "\n", enc)
    if dropped:
        print(f"    - edges.tsv: {dropped} edge(s) to {key.split('/')[-1]}")


def drop_row(folder, filename, key):
    path = os.path.join(folder, filename)
    text, enc = load(path)
    lines = [l for l in text.split("\n") if l.strip()]
    kept = [lines[0]] + [l for l in lines[1:] if l.split("\t")[0] != key]
    if len(kept) == len(lines):
        raise SystemExit(f"  !! {filename}: no row keyed {key}")
    save(path, "\n".join(kept) + "\n", enc)
    print(f"    - {filename}: dropped {key.split('/')[-1]}")
    drop_edges_to(folder, key)


def add_row(folder, filename, cells, owner, verb):
    path = os.path.join(folder, filename)
    text, enc = load(path)
    lines = [l for l in text.split("\n") if l.strip()]
    hdr = lines[0].split("\t")
    row = [""] * len(hdr)
    for col, val in cells.items():
        row[hdr.index(col)] = val
    lines.append("\t".join(row))
    save(path, "\n".join(lines) + "\n", enc)
    print(f"    + {filename}: {cells['key'].split('/')[-1]}")

    # And the containment edge the owner would carry, so the fixture is shaped like a real export rather than merely
    # accepted. ReattachInstanced gathers children by key prefix, not by edge, so this is fidelity rather than function.
    epath = os.path.join(folder, "edges.tsv")
    etext, eenc = load(epath)
    elines = [l for l in etext.split("\n") if l.strip()]
    elines.append("\t".join([owner, verb, cells["key"]]))
    save(epath, "\n".join(elines) + "\n", eenc)
    print(f"    + edges.tsv: {verb}")


print("PlanChild_Remove")
d = fresh("PlanChild_Remove")
drop_row(d, "loot_table_reward.tsv", f"{OWNER}/Rewards[2]")

print("PlanChild_Add")
d = fresh("PlanChild_Add")
add_row(d, "xpreward.tsv", {"key": f"{OWNER}/Rewards[3]", "class": "XPReward", "Amount": "42"},
        OWNER, "contains(Rewards[3])")

print("PlanChild_Silent")
d = fresh("PlanChild_Silent")
drop_row(d, "scaled_amount_reward.tsv", f"{OWNER}/Rewards[0]")
drop_row(d, "currency_reward.tsv", f"{OWNER}/Rewards[1]")
drop_row(d, "loot_table_reward.tsv", f"{OWNER}/Rewards[2]")

print("\ndone")
