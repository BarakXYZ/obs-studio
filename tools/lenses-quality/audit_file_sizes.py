#!/usr/bin/env python3
"""Audit first-party Lenses C/C++ file sizes."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}
EXCLUDED_PATH_PARTS = {
    ".deps_vendor",
    "build",
    "__pycache__",
}


@dataclass(frozen=True)
class FileStat:
    path: str
    lines: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--plugin-root",
        default="plugins/lenses",
        help="Path to the Lenses plugin root (default: plugins/lenses).",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=40,
        help="Number of largest files to include in top list (default: 40).",
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


def count_lines(path: Path) -> int:
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        return sum(1 for _ in handle)


def display_path(path: Path) -> str:
    try:
        return path.relative_to(Path.cwd()).as_posix()
    except ValueError:
        return path.as_posix()


def build_report(plugin_root: Path, top_n: int) -> dict:
    stats: list[FileStat] = []
    for source_path in iter_source_files(plugin_root):
        stats.append(FileStat(path=display_path(source_path), lines=count_lines(source_path)))

    ordered = sorted(stats, key=lambda item: item.lines, reverse=True)
    total_lines = sum(item.lines for item in ordered)
    average_lines = (total_lines / len(ordered)) if ordered else 0.0

    return {
        "plugin_root": display_path(plugin_root),
        "file_count": len(ordered),
        "total_lines": total_lines,
        "average_lines": average_lines,
        "top_files": [asdict(item) for item in ordered[: max(0, top_n)]],
        "all_files": [asdict(item) for item in ordered],
    }


def main() -> int:
    args = parse_args()
    plugin_root = Path(args.plugin_root)
    if not plugin_root.exists():
        raise SystemExit(f"plugin root not found: {plugin_root}")

    report = build_report(plugin_root, args.top)
    rendered = json.dumps(report, indent=2, sort_keys=False)
    print(rendered)

    if args.output_json:
        output_path = Path(args.output_json)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered + "\n", encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
