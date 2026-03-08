#!/usr/bin/env python3
"""Heuristic function-size audit for first-party Lenses C/C++ files."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}
EXCLUDED_PATH_PARTS = {
    ".deps_vendor",
    "build",
    "__pycache__",
}
CONTROL_KEYWORDS = {"if", "for", "while", "switch", "catch", "else", "do"}


@dataclass(frozen=True)
class FunctionStat:
    file: str
    name: str
    start_line: int
    end_line: int
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
        default=80,
        help="Number of largest functions to include in top list (default: 80).",
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


def strip_comments_keep_lines(text: str) -> str:
    out: list[str] = []
    i = 0
    in_block = False
    in_line = False
    in_string = False
    in_char = False
    escaped = False

    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if in_line:
            if ch == "\n":
                in_line = False
                out.append(ch)
            else:
                out.append(" ")
            i += 1
            continue

        if in_block:
            if ch == "*" and nxt == "/":
                in_block = False
                out.append(" ")
                out.append(" ")
                i += 2
                continue
            if ch == "\n":
                out.append("\n")
            else:
                out.append(" ")
            i += 1
            continue

        if in_string:
            out.append(" " if ch != "\n" else "\n")
            if ch == '"' and not escaped:
                in_string = False
            escaped = (ch == "\\") and not escaped
            i += 1
            continue

        if in_char:
            out.append(" " if ch != "\n" else "\n")
            if ch == "'" and not escaped:
                in_char = False
            escaped = (ch == "\\") and not escaped
            i += 1
            continue

        escaped = False
        if ch == "/" and nxt == "/":
            in_line = True
            out.append(" ")
            out.append(" ")
            i += 2
            continue
        if ch == "/" and nxt == "*":
            in_block = True
            out.append(" ")
            out.append(" ")
            i += 2
            continue
        if ch == '"':
            in_string = True
            out.append(" ")
            i += 1
            continue
        if ch == "'":
            in_char = True
            out.append(" ")
            i += 1
            continue

        out.append(ch)
        i += 1

    return "".join(out)


def compact_signature(text: str) -> str:
    return " ".join(text.replace("\n", " ").split())


def extract_function_name(signature: str) -> str:
    signature = signature.strip()
    if "(" not in signature:
        return ""
    prefix = signature.split("(", 1)[0].strip()
    match = re.search(r"([A-Za-z_~][A-Za-z0-9_:~]*)\s*$", prefix)
    if not match:
        return ""
    candidate = match.group(1)
    if candidate in CONTROL_KEYWORDS:
        return ""
    return candidate


def is_probable_function_signature(signature: str) -> bool:
    normalized = compact_signature(signature)
    if not normalized:
        return False
    if "(" not in normalized or ")" not in normalized:
        return False
    head = normalized.lstrip()
    for keyword in CONTROL_KEYWORDS:
        if head.startswith(keyword + " "):
            return False
        if head.startswith(keyword + "("):
            return False
    if normalized.endswith(";"):
        return False
    if normalized.startswith("#"):
        return False
    if normalized.startswith("return "):
        return False
    if "=" in normalized and normalized.find("=") < normalized.find("("):
        return False
    return extract_function_name(normalized) != ""


def brace_delta(line: str) -> int:
    return line.count("{") - line.count("}")


def display_path(path: Path) -> str:
    try:
        return path.relative_to(Path.cwd()).as_posix()
    except ValueError:
        return path.as_posix()


def parse_functions(path: Path) -> list[FunctionStat]:
    source = path.read_text(encoding="utf-8", errors="ignore")
    cleaned = strip_comments_keep_lines(source)
    lines = cleaned.splitlines()

    results: list[FunctionStat] = []
    pending_lines: list[str] = []
    pending_start = 0
    in_function = False
    fn_name = ""
    fn_start = 0
    depth = 0

    for line_number, raw_line in enumerate(lines, start=1):
        line = raw_line.rstrip()
        stripped = line.strip()

        if in_function:
            depth += brace_delta(line)
            if depth <= 0:
                end_line = line_number
                results.append(
                    FunctionStat(
                        file=display_path(path),
                        name=fn_name,
                        start_line=fn_start,
                        end_line=end_line,
                        lines=max(1, end_line - fn_start + 1),
                    )
                )
                in_function = False
                fn_name = ""
                fn_start = 0
                depth = 0
            continue

        if not pending_lines:
            if (
                stripped
                and not stripped.startswith("#")
                and "(" in stripped
                and not stripped.startswith("//")
            ):
                pending_lines = [line]
                pending_start = line_number

                candidate = compact_signature("\n".join(pending_lines))
                if "{" in line:
                    candidate_before_brace = candidate.split("{", 1)[0].strip()
                    if is_probable_function_signature(candidate_before_brace):
                        fn_name = extract_function_name(candidate_before_brace)
                        fn_start = pending_start
                        in_function = True
                        depth = brace_delta(line)
                        if depth <= 0:
                            results.append(
                                FunctionStat(
                                    file=display_path(path),
                                    name=fn_name,
                                    start_line=fn_start,
                                    end_line=line_number,
                                    lines=max(1, line_number - fn_start + 1),
                                )
                            )
                            in_function = False
                            fn_name = ""
                            fn_start = 0
                            depth = 0
                        pending_lines = []
                        pending_start = 0
                        continue
                    pending_lines = []
                    pending_start = 0
            continue

        pending_lines.append(line)
        candidate = compact_signature("\n".join(pending_lines))

        if "{" in line:
            candidate_before_brace = candidate.split("{", 1)[0].strip()
            if is_probable_function_signature(candidate_before_brace):
                fn_name = extract_function_name(candidate_before_brace)
                fn_start = pending_start
                in_function = True
                depth = brace_delta(line)
                if depth <= 0:
                    results.append(
                        FunctionStat(
                            file=display_path(path),
                            name=fn_name,
                            start_line=fn_start,
                            end_line=line_number,
                            lines=max(1, line_number - fn_start + 1),
                        )
                    )
                    in_function = False
                    fn_name = ""
                    fn_start = 0
                    depth = 0
                pending_lines = []
                pending_start = 0
                continue

            pending_lines = []
            pending_start = 0
            continue

        if ";" in line:
            pending_lines = []
            pending_start = 0
            continue

        if len(pending_lines) > 20:
            pending_lines = []
            pending_start = 0

    return results


def build_report(plugin_root: Path, top_n: int) -> dict:
    functions: list[FunctionStat] = []
    for source_path in iter_source_files(plugin_root):
        functions.extend(parse_functions(source_path))

    ordered = sorted(functions, key=lambda item: item.lines, reverse=True)
    total_lines = sum(item.lines for item in ordered)
    average_lines = (total_lines / len(ordered)) if ordered else 0.0

    return {
        "plugin_root": display_path(plugin_root),
        "function_count": len(ordered),
        "total_function_lines": total_lines,
        "average_function_lines": average_lines,
        "top_functions": [asdict(item) for item in ordered[: max(0, top_n)]],
        "all_functions": [asdict(item) for item in ordered],
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
