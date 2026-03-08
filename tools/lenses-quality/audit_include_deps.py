#!/usr/bin/env python3
"""Audit include relationships for first-party Lenses source/header files."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable

SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}
EXCLUDED_PATH_PARTS = {
    ".deps_vendor",
    "build",
    "__pycache__",
}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--plugin-root",
        default="plugins/lenses",
        help="Path to the Lenses plugin root (default: plugins/lenses).",
    )
    parser.add_argument(
        "--output-json",
        default="",
        help="Optional path for JSON output.",
    )
    return parser.parse_args()


def should_skip(path: Path) -> bool:
    return any(part in EXCLUDED_PATH_PARTS for part in path.parts)


def iter_source_files(plugin_root: Path) -> Iterable[Path]:
    for path in sorted(plugin_root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix not in SOURCE_EXTENSIONS:
            continue
        if should_skip(path):
            continue
        yield path


def display_path(path: Path) -> str:
    try:
        return path.relative_to(Path.cwd()).as_posix()
    except ValueError:
        return path.as_posix()


def source_module_for(rel_path: Path) -> str:
    parts = rel_path.parts
    if len(parts) >= 2 and parts[0] == "src":
        return f"src:{parts[1]}"
    if len(parts) >= 3 and parts[0] == "include" and parts[1] == "lenses":
        return f"include:{parts[2]}"
    if len(parts) >= 1 and parts[0] == "include":
        return "include:other"
    return "other"


def target_module_for(include_target: str, delimiter: str) -> str:
    if delimiter == "<":
        return "system"

    normalized = include_target.strip()
    if normalized.startswith("lenses/"):
        parts = normalized.split("/")
        if len(parts) >= 2:
            return f"include:{parts[1]}"
        return "include:other"

    first = normalized.split("/", 1)[0]
    if first in {"ai", "core", "filter", "io", "pipeline"}:
        return f"src:{first}"
    return "local:other"


def build_report(plugin_root: Path) -> dict:
    module_edges: dict[str, Counter[str]] = defaultdict(Counter)
    file_include_counts: dict[str, dict[str, int]] = {}
    unmatched_local_includes: dict[str, list[str]] = defaultdict(list)
    include_frequency: Counter[str] = Counter()

    for source_path in iter_source_files(plugin_root):
        rel_path = source_path.relative_to(plugin_root)
        source_module = source_module_for(rel_path)

        local_count = 0
        system_count = 0
        includes: list[tuple[str, str]] = []

        with source_path.open("r", encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                match = INCLUDE_RE.match(line)
                if not match:
                    continue
                delimiter, include_target = match.groups()
                includes.append((delimiter, include_target))
                include_frequency[include_target] += 1
                if delimiter == "<":
                    system_count += 1
                else:
                    local_count += 1

        for delimiter, include_target in includes:
            target_module = target_module_for(include_target, delimiter)
            module_edges[source_module][target_module] += 1

            if delimiter == '"' and target_module == "local:other":
                unmatched_local_includes[source_module].append(include_target)

        file_include_counts[display_path(source_path)] = {
            "local": local_count,
            "system": system_count,
            "total": local_count + system_count,
        }

    edge_rows = []
    for src_mod in sorted(module_edges):
        for dst_mod, count in sorted(module_edges[src_mod].items()):
            edge_rows.append({"from": src_mod, "to": dst_mod, "count": count})

    hot_includes = [
        {"include": include_name, "count": count}
        for include_name, count in include_frequency.most_common(80)
    ]

    unresolved_rows = []
    for source_module, targets in sorted(unmatched_local_includes.items()):
        unique_targets = sorted(set(targets))
        unresolved_rows.append(
            {
                "source_module": source_module,
                "count": len(targets),
                "unique_targets": unique_targets,
            }
        )

    return {
        "plugin_root": display_path(plugin_root),
        "edge_count": len(edge_rows),
        "module_edges": edge_rows,
        "file_include_counts": file_include_counts,
        "include_frequency": hot_includes,
        "unclassified_local_includes": unresolved_rows,
    }


def main() -> int:
    args = parse_args()
    plugin_root = Path(args.plugin_root)
    if not plugin_root.exists():
        raise SystemExit(f"plugin root not found: {plugin_root}")

    report = build_report(plugin_root)
    rendered = json.dumps(report, indent=2, sort_keys=False)
    print(rendered)

    if args.output_json:
        output_path = Path(args.output_json)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered + "\n", encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
