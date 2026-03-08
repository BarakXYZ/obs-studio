#!/usr/bin/env python3
"""Validate a Lenses ONNX model package produced by tools/lenses-models."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


REQUIRED_METADATA_KEYS = {
    "version",
    "lenses_schema",
    "model",
    "class_map_file",
    "class_count",
}


class ValidationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate Lenses ONNX model package")
    parser.add_argument("--package-dir", type=Path, required=True, help="Path to model package directory")
    parser.add_argument("--strict-onnx", action="store_true", help="Fail if ONNX python package is unavailable")
    return parser.parse_args()


def read_json(path: Path) -> dict:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise ValidationError(f"Invalid JSON in {path}: {exc}") from exc

    if not isinstance(payload, dict):
        raise ValidationError(f"Expected JSON object in {path}")
    return payload


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_package(package_dir: Path, strict_onnx: bool) -> None:
    if not package_dir.exists() or not package_dir.is_dir():
        raise ValidationError(f"Package dir does not exist: {package_dir}")

    metadata_path = package_dir / "metadata.json"
    if not metadata_path.exists():
        raise ValidationError(f"Missing metadata.json in {package_dir}")

    metadata = read_json(metadata_path)
    missing_keys = REQUIRED_METADATA_KEYS.difference(metadata.keys())
    if missing_keys:
        raise ValidationError(f"metadata.json missing required keys: {sorted(missing_keys)}")

    model_info = metadata.get("model")
    if not isinstance(model_info, dict):
        raise ValidationError("metadata.model must be an object")

    model_file = model_info.get("file")
    if not isinstance(model_file, str) or not model_file:
        raise ValidationError("metadata.model.file must be a non-empty string")

    model_path = package_dir / model_file
    if not model_path.exists():
        raise ValidationError(f"Model file does not exist: {model_path}")

    declared_hash = model_info.get("sha256")
    if declared_hash:
        if not isinstance(declared_hash, str) or len(declared_hash) != 64:
            raise ValidationError("metadata.model.sha256 must be a valid 64-char hex digest")
        actual_hash = sha256_file(model_path)
        if declared_hash.lower() != actual_hash.lower():
            raise ValidationError("metadata.model.sha256 mismatch with model file")

    class_map_file = metadata.get("class_map_file")
    if not isinstance(class_map_file, str) or not class_map_file:
        raise ValidationError("metadata.class_map_file must be a non-empty string")

    class_map_path = package_dir / class_map_file
    if not class_map_path.exists():
        raise ValidationError(f"Class map file does not exist: {class_map_path}")

    class_map = read_json(class_map_path)
    classes = class_map.get("classes")
    if not isinstance(classes, list):
        raise ValidationError("class-map classes must be a list")

    expected_count = metadata.get("class_count")
    if not isinstance(expected_count, int) or expected_count < 1:
        raise ValidationError("metadata.class_count must be a positive integer")
    if expected_count != len(classes):
        raise ValidationError(
            f"class_count mismatch: metadata={expected_count} class-map={len(classes)}"
        )

    for expected_id, item in enumerate(classes):
        if not isinstance(item, dict):
            raise ValidationError(f"Class entry {expected_id} is not an object")
        if item.get("id") != expected_id:
            raise ValidationError(f"Class id mismatch at index {expected_id}")
        name = item.get("name")
        if not isinstance(name, str) or not name.strip():
            raise ValidationError(f"Class name missing/invalid at index {expected_id}")

    try:
        import onnx  # type: ignore
    except Exception:
        if strict_onnx:
            raise ValidationError("ONNX python package not available (required by --strict-onnx)")
        print("warning: skipping ONNX structural validation (onnx package not installed)")
        return

    try:
        model = onnx.load(str(model_path))
        onnx.checker.check_model(model)
    except Exception as exc:
        raise ValidationError(f"ONNX validation failed: {exc}") from exc


def main() -> int:
    args = parse_args()
    validate_package(args.package_dir.expanduser().resolve(), strict_onnx=args.strict_onnx)
    print(f"Model package is valid: {args.package_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
