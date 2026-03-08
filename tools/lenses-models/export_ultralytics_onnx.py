#!/usr/bin/env python3
"""Export Ultralytics YOLO models to ONNX and build a Lenses package manifest."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shutil
from pathlib import Path
from typing import Iterable, Optional


def add_bool_flag(parser: argparse.ArgumentParser, name: str, default: bool, help_text: str) -> None:
    group = parser.add_mutually_exclusive_group(required=False)
    group.add_argument(f"--{name}", dest=name, action="store_true", help=help_text)
    group.add_argument(f"--no-{name}", dest=name, action="store_false", help=f"Disable: {help_text}")
    parser.set_defaults(**{name: default})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export Ultralytics model to ONNX for Lenses")
    parser.add_argument("--model", required=True, help="Source model path or Ultralytics model id (e.g. yolo11n-seg.pt)")
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Output package directory (will contain model.onnx, metadata.json, class-map.json)",
    )
    parser.add_argument("--imgsz", type=int, default=640, help="Square export image size")
    parser.add_argument("--batch", type=int, default=1, help="Export batch size")
    parser.add_argument("--opset", type=int, default=None, help="ONNX opset version override")
    parser.add_argument("--device", default=None, help="Export device, e.g. cpu, mps, 0")
    add_bool_flag(parser, "dynamic", default=False, help_text="Enable dynamic input shape")
    add_bool_flag(parser, "simplify", default=True, help_text="Simplify ONNX graph")
    add_bool_flag(parser, "half", default=False, help_text="Enable FP16 export when supported")
    add_bool_flag(parser, "nms", default=False, help_text="Fuse NMS into model when supported")
    add_bool_flag(
        parser,
        "assume_static_outputs",
        default=False,
        help_text="Mark metadata outputs as static (required for static output I/O binding path)",
    )
    add_bool_flag(
        parser,
        "bundled",
        default=True,
        help_text="Mark package as bundled for first-party distribution metadata",
    )
    parser.add_argument("--id", default=None, help="Model package id (default: inferred from output directory name)")
    parser.add_argument("--name", default=None, help="Human-readable model name")
    parser.add_argument(
        "--size-tier",
        choices=["n", "s", "m", "l", "x", "auto"],
        default="auto",
        help="Model size tier for dropdown sorting",
    )
    parser.add_argument(
        "--license-note",
        default="Review and comply with upstream model and dataset licenses before commercial distribution.",
        help="Distribution license note included in metadata",
    )
    add_bool_flag(parser, "allow_non_segment", default=False, help_text="Allow non-segmentation models")
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_class_names(raw_names: object) -> list[str]:
    if isinstance(raw_names, dict):
        pairs: list[tuple[int, str]] = []
        for key, value in raw_names.items():
            if isinstance(key, str):
                key = int(key)
            pairs.append((int(key), str(value)))
        pairs.sort(key=lambda item: item[0])
        return [name for _, name in pairs]

    if isinstance(raw_names, (list, tuple)):
        return [str(name) for name in raw_names]

    raise RuntimeError(f"Unsupported class names structure: {type(raw_names)!r}")


def coerce_task(model: object) -> str:
    task = getattr(model, "task", None)
    if isinstance(task, str) and task:
        return task

    overrides = getattr(model, "overrides", None)
    if isinstance(overrides, dict):
        override_task = overrides.get("task")
        if isinstance(override_task, str) and override_task:
            return override_task

    return "unknown"


def infer_size_tier(value: Optional[str]) -> Optional[str]:
    if not value:
        return None
    lowered = value.lower()
    for tier in ("n", "s", "m", "l", "x"):
        token = f"yolo11{tier}-seg"
        if token in lowered:
            return tier
    return None


def infer_model_id(args: argparse.Namespace) -> str:
    if args.id:
        return args.id
    return args.output_dir.name


def infer_model_name(args: argparse.Namespace, tier: Optional[str]) -> str:
    if args.name:
        return args.name
    if tier:
        return f"YOLO11{tier} Segmentation (COCO80)"
    return infer_model_id(args)


def export_model(args: argparse.Namespace) -> tuple[Path, list[str], str]:
    try:
        from ultralytics import YOLO
    except Exception as exc:  # pragma: no cover - import error path
        raise RuntimeError(
            "Ultralytics is required. Install with `pip install ultralytics` or use your local dev env."
        ) from exc

    model = YOLO(args.model)
    task = coerce_task(model)
    if task != "segment" and not args.allow_non_segment:
        raise RuntimeError(
            f"Expected segmentation model task='segment', got task='{task}'. Use --allow-non-segment to override."
        )

    export_kwargs = {
        "format": "onnx",
        "imgsz": args.imgsz,
        "batch": args.batch,
        "dynamic": args.dynamic,
        "simplify": args.simplify,
        "half": args.half,
        "nms": args.nms,
    }
    if args.opset is not None:
        export_kwargs["opset"] = args.opset
    if args.device is not None:
        export_kwargs["device"] = args.device

    exported_path = Path(model.export(**export_kwargs)).expanduser().resolve()
    class_names = normalize_class_names(getattr(model.model, "names", getattr(model, "names", {})))
    return exported_path, class_names, task


def write_package(
    output_dir: Path,
    model_path: Path,
    class_names: Iterable[str],
    task: str,
    args: argparse.Namespace,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    packaged_model = output_dir / "model.onnx"
    shutil.copy2(model_path, packaged_model)
    package_id = infer_model_id(args)
    inferred_tier = infer_size_tier(args.model) or infer_size_tier(package_id)
    size_tier = inferred_tier if args.size_tier == "auto" else args.size_tier
    model_name = infer_model_name(args, size_tier)

    class_names = list(class_names)
    class_map = {
        "version": 1,
        "dataset": "custom",
        "classes": [{"id": i, "name": name} for i, name in enumerate(class_names)],
    }
    class_map_path = output_dir / "class-map.json"
    class_map_path.write_text(json.dumps(class_map, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

    metadata = {
        "version": 1,
        "lenses_schema": "lenses-model-v1",
        "exported_at_utc": dt.datetime.now(tz=dt.timezone.utc).isoformat(),
        "source_model": args.model,
        "task": task,
        "model": {
            "file": packaged_model.name,
            "sha256": sha256_file(packaged_model),
            "imgsz": [args.imgsz, args.imgsz],
            "batch": args.batch,
            "dynamic": bool(args.dynamic),
            "opset": args.opset,
            "runtime": "onnxruntime",
        },
        "class_map_file": class_map_path.name,
        "class_count": len(class_names),
        "ultralytics_export": {
            "format": "onnx",
            "simplify": bool(args.simplify),
            "half": bool(args.half),
            "nms": bool(args.nms),
            "device": args.device,
        },
        "id": package_id,
        "name": model_name,
        "size_tier": size_tier or "",
        "distribution": {
            "bundled": bool(args.bundled),
            "license_note": args.license_note,
        },
    }
    metadata["model"]["static_input"] = not bool(args.dynamic)
    metadata["model"]["static_output"] = bool(args.assume_static_outputs)
    metadata["model"]["supports_iobinding_static_outputs"] = bool(args.assume_static_outputs)
    (output_dir / "metadata.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    exported_path, class_names, task = export_model(args)
    write_package(args.output_dir, exported_path, class_names, task, args)
    print(f"Model package created: {args.output_dir}")
    print(f"Exported ONNX source: {exported_path}")
    print(f"Classes: {len(class_names)} | Task: {task}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
