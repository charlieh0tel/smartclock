# /// script
# requires-python = ">=3.11"
# ///
"""Lists the functions an image's link check finds missing, the work
left before its C links alone, by module, cheapest first: each one's
size, and its counterpart in the images it is diffed against
(docs/firmware/diff/IMAGE-vs-BASE.tsv) with that function's
hand-written file, so that one carried as it stands is a port of that
C.

    DECOMPILED_CFLAGS='...' CPU32_LIBGCC=... python3 tools/firmware/link_report.py \\
        third_party/firmware/decompiled/z3815a-4010/z3815a-4010.c

make report-link runs it.  Each missing function is of one class, by
its closest counterpart that a base's C writes by hand: carried
(identical or relocated, its C the base's), changed at a similarity of
SIMILAR or more, changed below it, or new (no counterpart written by
hand).  A module's rows come in that order, smallest first, and the
modules by the share of their bytes carried, highest first.  A row
marked common/ is identical or relocated in every image that shares
common/ (rewrite.md, "Shared C", rule 1), so that its C may move there
rather than be copied.  The first line counts the classes, the
functions named FUN_, and those common/ would fit.  Always exits 0
once the link has run.
"""

import collections
import json
import os
import pathlib
import shlex
import sys
import tempfile

from common_check import ANALYSIS, BASE, DIFFS, SHARED, sharing_images
from diff_figures import read_rows
from image_diff import SIMILAR_FAR
from link_check import link_required

# The threshold the seeding names a changed function by
# (seed_names.py's THRESHOLD), image_diff.py's far similarity.
SIMILAR = SIMILAR_FAR
# The classes, cheapest first.
CARRIED, NEAR, FAR, NEW = "carried", f"changed >= {SIMILAR:.2f}", f"changed < {SIMILAR:.2f}", "new"
CLASSES = (CARRIED, NEAR, FAR, NEW)
# The prefix the export names a function no name covers by.
UNNAMED = "FUN_"
# The export's prefix of an installer function's address.
INSTALLER_PREFIX = "installer::"


class Counterpart:
    """A missing function's match in a base image: the base, its name
    there, the diff's status and similarity, and the file of the base's
    C that writes it by hand, if one does."""

    def __init__(self, base: str, row: dict[str, str], rewritten: str | None):
        self.base, self.name, self.status = base, row["name"], row["status"]
        self.similarity = 1.0 if row["status"] in SHARED else float(row["similarity"] or 0)
        self.rewritten = rewritten

    def kind(self) -> str:
        if self.rewritten is None:
            return NEW
        if self.status in SHARED:
            return CARRIED
        return NEAR if self.similarity >= SIMILAR else FAR

    def __str__(self) -> str:
        how = self.status if self.status in SHARED else f"{self.status} {self.similarity:.3f}"
        return f"{self.base} {self.name} ({how}, {self.rewritten or 'not hand-written'})"


def functions(sidecar: pathlib.Path) -> dict[str, dict]:
    """An export's functions by name, from its .conventions.jsonl."""
    return {entry["name"]: entry for entry in map(json.loads, open(sidecar))}


def export_key(region: str, address: str) -> str:
    """A diff row's address as the image's export gives it."""
    key = "%08x" % int(address, 16)
    return INSTALLER_PREFIX + key if region == "installer" else key


def diff_rows(image: str, base: str) -> list[dict[str, str]]:
    return [row for row in read_rows(DIFFS / f"{image}-vs-{base}.tsv") if row["kind"] == "function"]


def shared_names(image: str) -> set[str]:
    """The names of BASE's functions identical or relocated in image."""
    found = collections.defaultdict(set)
    for row in diff_rows(image, BASE):
        found[row["name"]].add(row["status"])
    return {name for name, statuses in found.items() if statuses <= SHARED}


def modules(image: str) -> dict[str, str]:
    """The image's modules.tsv, each function's module by address."""
    path = ANALYSIS / image / "modules.tsv"
    rows = [line.rstrip("\n").split("\t") for line in open(path) if line.strip() and not line.startswith("#")]
    return {"%08x" % int(row[0], 16): row[2] for row in rows}


def report(c_path: str, missing: list[str]) -> list[str]:
    export = pathlib.Path(c_path)
    image = export.parent.name
    own = functions(export.with_suffix(".conventions.jsonl"))
    by_address = {entry["address"]: entry["name"] for entry in own.values()}
    module_of = modules(image)
    bases = sorted(path.name.removeprefix(f"{image}-vs-").removesuffix(".tsv")
                   for path in DIFFS.glob(f"{image}-vs-*.tsv"))
    size, module, counterparts = {}, {}, collections.defaultdict(list)
    common_bases = {}
    for base in bases:
        written = {name: entry.get("rewritten") for name, entry in
                   functions(export.parents[1] / base / f"{base}.conventions.jsonl").items()}
        for row in diff_rows(image, base):
            if not row["other_address"]:
                continue
            name = by_address.get(export_key(row["region"], row["other_address"]))
            if name is None:
                continue
            size.setdefault(name, int(row["other_size"]))
            module.setdefault(name, row["module"])
            if row["name"]:
                counterparts[name].append(Counterpart(base, row, written.get(row["name"])))
                if base == BASE and row["status"] in SHARED:
                    common_bases[name] = row["name"]
    sharing = sharing_images()
    shared = [shared_names(other) for other in sharing if other not in (image, BASE)]
    fits = {name for name, base_name in common_bases.items()
            if image in sharing and all(base_name in names for names in shared)}

    rows = []
    for name in missing:
        address = own[name]["address"]
        best = max(counterparts[name], key=lambda c: (c.rewritten is not None, c.similarity), default=None)
        kind = best.kind() if best else NEW
        rows.append((module_of.get(address, module.get(name, "unassigned")), kind, size.get(name, 0),
                     name, address, best, name in fits))

    by_module = collections.defaultdict(list)
    for row in rows:
        by_module[row[0]].append(row)

    def carried_share(entries):
        total = sum(entry[2] for entry in entries) or 1
        return -sum(entry[2] for entry in entries if entry[1] == CARRIED) / total, total

    counts = collections.Counter(row[1] for row in rows)
    lines = [f"{image}: {len(missing)} of {len(own)} functions missing: "
             + ", ".join(f"{counts[kind]} {kind}" for kind in CLASSES)
             + f"; {sum(row[3].startswith(UNNAMED) for row in rows)} named {UNNAMED}"
             + f", {sum(row[6] for row in rows)} that common/ would fit"]
    for name, entries in sorted(by_module.items(), key=lambda item: (carried_share(item[1]), item[0])):
        kinds = collections.Counter(entry[1] for entry in entries)
        lines.append("")
        lines.append(f"{name}: {len(entries)}, {sum(entry[2] for entry in entries)} bytes: "
                     + ", ".join(f"{kinds[kind]} {kind}" for kind in CLASSES if kinds[kind]))
        for _, kind, length, function, address, best, fit in sorted(
                entries, key=lambda entry: (CLASSES.index(entry[1]), entry[2], entry[3])):
            lines.append(f"  {kind:<15} {function} {address} {length}"
                         + (f"  {best}" if best else "") + ("  common/" if fit else ""))
    return lines


def main() -> int:
    if len(sys.argv) != 2 or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path = sys.argv[1]
    with tempfile.TemporaryDirectory() as work:
        _, _, missing = link_required(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work)
    print("\n".join(report(c_path, missing)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
