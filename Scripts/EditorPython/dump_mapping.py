"""Print what a UQuestImportMapping actually holds.

The recipe is the artifact the whole resolver hangs on, and it has had no readable rendering: the .uasset is compressed
so a text search finds nothing, and the details panel is a live widget rather than a record you can paste, diff, or
attach to a note. This exists so a recipe's state can be READ instead of described.

NOT on the asset, and deliberately: the Sample Source folder + format. A recipe describes a SHAPE, not the sample it
was authored against, so those live per-user in UQuestResolverEditorMemo (EditorPerProjectUserSettings.ini), keyed by
this asset's path.

Usage: Tools -> Execute Python Script, or change ASSET below and re-run.
"""
import unreal

ASSET = "/Game/MapTest/DA_MapTest"


def field(obj, name):
    """get_editor_property raises when a property isn't exposed to Python; a dump must not die on one bad field."""
    try:
        return obj.get_editor_property(name)
    except Exception as exc:
        return "<unreadable: {}>".format(exc)


def dump_list(label, container, render):
    """Render an array of USTRUCTs. A plain USTRUCT() without BlueprintType may not wrap for Python at all, so the
    whole loop degrades to repr() rather than failing the run - a partial dump still answers most questions."""
    try:
        items = list(container)
    except Exception as exc:
        unreal.log_warning("--- {} --- not iterable ({}); raw: {!r}".format(label, exc, container))
        return

    unreal.log("--- {} ({}) ---".format(label, len(items)))
    for index, item in enumerate(items):
        try:
            unreal.log("  [{}] {}".format(index, render(item)))
        except Exception as exc:
            unreal.log("  [{}] <render failed: {}> raw: {!r}".format(index, exc, item))


def main():
    mapping = unreal.load_asset(ASSET)
    if mapping is None:
        unreal.log_error("dump_mapping: '{}' did not load.".format(ASSET))
        return

    unreal.log("=== {} ({}) ===".format(ASSET, mapping.get_class().get_name()))
    unreal.log("Discriminator column : {}".format(field(mapping, "discriminator_column")))
    unreal.log("Key column           : {}".format(field(mapping, "key_column")))
    unreal.log("Default absent policy: {}".format(field(mapping, "default_absent_policy")))
    unreal.log("Delete orphaned nodes: {}".format(field(mapping, "delete_orphaned_nodes")))

    dump_list(
        "Row Kinds",
        field(mapping, "discriminator_classes"),
        lambda k: "class={} values={} primary='{}'".format(
            field(k, "node_class"), field(k, "values"), field(k, "primary_value")),
    )

    dump_list(
        "Column Bindings",
        field(mapping, "bindings"),
        lambda b: "'{}' -> {}   absent={}".format(
            field(b, "source_column"), field(b, "target_property"), field(b, "absent_policy")),
    )

    dump_list(
        "Wire Bindings",
        field(mapping, "wire_bindings"),
        lambda w: "'{}' verb={} qualifier='{}'".format(
            field(w, "source_column"), field(w, "edge_verb"), field(w, "qualifier")),
    )


main()
