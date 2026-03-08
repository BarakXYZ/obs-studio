#!/usr/bin/env python3
"""Generate a markdown summary from Lenses quality audit JSON files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file-sizes", required=True, help="Path to file-sizes.json")
    parser.add_argument("--function-sizes", required=True, help="Path to function-sizes.json")
    parser.add_argument("--include-deps", required=True, help="Path to include-deps.json")
    parser.add_argument("--output-md", required=True, help="Path to summary markdown output")
    return parser.parse_args()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def render_table(headers: list[str], rows: list[list[str]]) -> str:
    out = []
    out.append("| " + " | ".join(headers) + " |")
    out.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        out.append("| " + " | ".join(row) + " |")
    return "\n".join(out)


def main() -> int:
    args = parse_args()
    file_sizes = load_json(Path(args.file_sizes))
    function_sizes = load_json(Path(args.function_sizes))
    include_deps = load_json(Path(args.include_deps))

    top_files = file_sizes.get("top_files", [])[:10]
    top_functions = function_sizes.get("top_functions", [])[:12]
    edges = sorted(include_deps.get("module_edges", []), key=lambda item: item["count"], reverse=True)[:12]
    top_includes = include_deps.get("include_frequency", [])[:12]

    md_parts: list[str] = []
    md_parts.append("# Lenses Audit Summary")
    md_parts.append("")
    md_parts.append("## Totals")
    md_parts.append("")
    md_parts.append(f"- File count: **{file_sizes.get('file_count', 0)}**")
    md_parts.append(f"- Source LOC (audited set): **{file_sizes.get('total_lines', 0)}**")
    md_parts.append(f"- Parsed function count: **{function_sizes.get('function_count', 0)}**")
    md_parts.append("")

    md_parts.append("## Top Files by LOC")
    md_parts.append("")
    md_parts.append(
        render_table(
            ["File", "LOC"],
            [[item["path"], str(item["lines"])] for item in top_files],
        )
    )
    md_parts.append("")

    md_parts.append("## Top Functions by Length")
    md_parts.append("")
    md_parts.append(
        render_table(
            ["Function", "File", "Lines", "Start"],
            [
                [
                    item["name"],
                    item["file"],
                    str(item["lines"]),
                    str(item["start_line"]),
                ]
                for item in top_functions
            ],
        )
    )
    md_parts.append("")

    md_parts.append("## Top Module Include Edges")
    md_parts.append("")
    md_parts.append(
        render_table(
            ["From", "To", "Count"],
            [[item["from"], item["to"], str(item["count"])] for item in edges],
        )
    )
    md_parts.append("")

    md_parts.append("## Most Frequent Includes")
    md_parts.append("")
    md_parts.append(
        render_table(
            ["Include", "Count"],
            [[item["include"], str(item["count"])] for item in top_includes],
        )
    )
    md_parts.append("")

    Path(args.output_md).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output_md).write_text("\n".join(md_parts).strip() + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
