#!/usr/bin/env python3
"""Generate canonical COCO-80 class map JSON for Lenses model packaging."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

COCO80_CLASSES = [
    "person",
    "bicycle",
    "car",
    "motorcycle",
    "airplane",
    "bus",
    "train",
    "truck",
    "boat",
    "traffic light",
    "fire hydrant",
    "stop sign",
    "parking meter",
    "bench",
    "bird",
    "cat",
    "dog",
    "horse",
    "sheep",
    "cow",
    "elephant",
    "bear",
    "zebra",
    "giraffe",
    "backpack",
    "umbrella",
    "handbag",
    "tie",
    "suitcase",
    "frisbee",
    "skis",
    "snowboard",
    "sports ball",
    "kite",
    "baseball bat",
    "baseball glove",
    "skateboard",
    "surfboard",
    "tennis racket",
    "bottle",
    "wine glass",
    "cup",
    "fork",
    "knife",
    "spoon",
    "bowl",
    "banana",
    "apple",
    "sandwich",
    "orange",
    "broccoli",
    "carrot",
    "hot dog",
    "pizza",
    "donut",
    "cake",
    "chair",
    "couch",
    "potted plant",
    "bed",
    "dining table",
    "toilet",
    "tv",
    "laptop",
    "mouse",
    "remote",
    "keyboard",
    "cell phone",
    "microwave",
    "oven",
    "toaster",
    "sink",
    "refrigerator",
    "book",
    "clock",
    "vase",
    "scissors",
    "teddy bear",
    "hair drier",
    "toothbrush",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate Lenses COCO-80 class-map JSON")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("tools/lenses-models/class-maps/coco80.json"),
        help="Output JSON path",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if len(COCO80_CLASSES) != 80:
        raise RuntimeError("COCO-80 class list must contain exactly 80 classes")
    if len(set(COCO80_CLASSES)) != 80:
        raise RuntimeError("COCO-80 class list contains duplicate names")

    payload = {
        "version": 1,
        "dataset": "coco80",
        "classes": [{"id": class_id, "name": name} for class_id, name in enumerate(COCO80_CLASSES)],
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(f"Wrote {args.output} ({len(COCO80_CLASSES)} classes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
