"""Rebuild /Game/MapTest/DA_MapTest into the recipe the MapTest fixtures were authored against.

WHY THIS EXISTS: the file fixtures are reproducible (Scripts/ResolverVerification/make_*.py) and the ASSET fixtures are
not, so DA_MapTest was quietly repurposed as a scratchpad during UI work and its proven state was lost with nothing to
restore it from. A recipe that can be rebuilt from a script is a recipe that cannot drift silently.

THE SHAPE IS DERIVED FROM THE FIXTURES THEMSELVES, not from memory - Saved/QuestExport/MapTest_{,Wire,Branch}Source
carry columns key/graph/type/label/goal/result/next/on_solved/on_reached and discriminator values entered/objective/
finish. 'key' and 'graph' are STRUCTURAL (row identity and owning container), so neither is bound as a property.

Run: Tools -> Execute Python Script. Re-runnable; it overwrites rather than appends.
"""
import unreal

ASSET = "/Game/MapTest/DA_MapTest"

KEY_COLUMN = "key"
DISCRIMINATOR_COLUMN = "type"

# Discriminator value -> node class. Values come from the fixtures' 'type' column.
ROW_KINDS = [
    ("entered",   "/Script/SimpleQuestEditor.QuestlineNode_Entry"),
    ("objective", "/Script/SimpleQuestEditor.QuestlineNode_Step"),
    ("finish",    "/Script/SimpleQuestEditor.QuestlineNode_Exit"),
]

# Source column -> node property. Absent policy left at the recipe default (Preserve) except where a fixture relies on
# a reset; 'result' carries the Exit's outcome tag and is the one the branch fixture varies.
BINDINGS = [
    ("label",  "NodeLabel",     unreal.QuestAbsentFieldPolicy.PRESERVE),
    ("goal",   "ObjectiveClass", unreal.QuestAbsentFieldPolicy.PRESERVE),
    ("result", "OutcomeTag",    unreal.QuestAbsentFieldPolicy.RESET),
]

# Row-adjacent relationships. An empty qualifier means "this node's default output for that verb", which is what lets
# one 'next' column wire an Entry and a Step alike; a named qualifier targets a specific outcome pin.
WIRE_BINDINGS = [
    ("next",       "activates", ""),
    ("on_solved",  "outcome",   "Solved"),
    ("on_reached", "outcome",   "Reached"),
]


def load_node_class(path):
    """A TSoftClassPtr property nativizes from a UClass OBJECT, not from a path wrapper - handing it a SoftClassPath
    fails with 'Cannot nativize SoftClassPath as Class'. Several loaders reach a /Script/ class depending on engine
    version, so try them in order and report which one answered rather than failing opaquely."""
    for loader in (lambda: unreal.load_class(None, path),
                   lambda: unreal.load_object(None, path),
                   lambda: getattr(unreal, path.rsplit(".", 1)[-1], None)):
        try:
            found = loader()
        except Exception:
            found = None
        if found is not None:
            return found
    raise RuntimeError("could not resolve node class '{}'".format(path))


def main():
    mapping = unreal.load_asset(ASSET)
    if mapping is None:
        unreal.log_error("make_maptest_recipe: '{}' did not load.".format(ASSET))
        return

    mapping.set_editor_property("key_column", KEY_COLUMN)
    mapping.set_editor_property("discriminator_column", DISCRIMINATOR_COLUMN)
    mapping.set_editor_property("default_absent_policy", unreal.QuestAbsentFieldPolicy.PRESERVE)
    mapping.set_editor_property("delete_orphaned_nodes", False)

    kinds = []
    for value, class_path in ROW_KINDS:
        kind = unreal.QuestDiscriminatorClass()
        kind.set_editor_property("node_class", load_node_class(class_path))
        kind.set_editor_property("values", [value])
        kind.set_editor_property("primary_value", value)
        kinds.append(kind)
    mapping.set_editor_property("discriminator_classes", kinds)

    bindings = []
    for column, prop, policy in BINDINGS:
        binding = unreal.QuestColumnBinding()
        binding.set_editor_property("source_column", column)
        binding.set_editor_property("target_property", prop)
        binding.set_editor_property("absent_policy", policy)
        bindings.append(binding)
    mapping.set_editor_property("bindings", bindings)

    wires = []
    for column, verb, qualifier in WIRE_BINDINGS:
        wire = unreal.QuestWireBinding()
        wire.set_editor_property("source_column", column)
        wire.set_editor_property("edge_verb", verb)
        wire.set_editor_property("qualifier", qualifier)
        wires.append(wire)
    mapping.set_editor_property("wire_bindings", wires)

    unreal.EditorAssetLibrary.save_asset(ASSET)
    unreal.log("make_maptest_recipe: rebuilt {} - {} row kind(s), {} binding(s), {} wire binding(s). "
               "Run dump_mapping.py to read it back.".format(ASSET, len(kinds), len(bindings), len(wires)))


main()
