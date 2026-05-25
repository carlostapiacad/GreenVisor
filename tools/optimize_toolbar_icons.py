#!/usr/bin/env python3
"""Normalize toolbar PNG icons for runtime use.

This is a development-only asset utility. It removes edge-connected white
background, crops transparent padding, and resizes the largest dimension.
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - command line environment check
    raise SystemExit(
        "Pillow is required. Install it in the dev environment with: python -m pip install Pillow"
    ) from exc


def is_background_pixel(pixel: tuple[int, int, int, int], bg_rgb: tuple[int, int, int]) -> bool:
    r, g, b, a = pixel
    if a < 12:
        return True

    dr = abs(r - bg_rgb[0])
    dg = abs(g - bg_rgb[1])
    db = abs(b - bg_rgb[2])
    channel_range = max(r, g, b) - min(r, g, b)
    average = (r + g + b) / 3

    bright = r >= 238 and g >= 238 and b >= 238
    near_corner = (dr + dg + db) <= 95
    low_saturation_light = channel_range <= 24 and average >= 220
    return bright or near_corner or low_saturation_light


def remove_edge_background(image: Image.Image) -> Image.Image:
    image = image.convert("RGBA")
    width, height = image.size
    pixels = image.load()

    corners = [
        pixels[0, 0],
        pixels[width - 1, 0],
        pixels[0, height - 1],
        pixels[width - 1, height - 1],
    ]
    bg_rgb = tuple(round(sum(pixel[index] for pixel in corners) / len(corners)) for index in range(3))

    visited = bytearray(width * height)
    queue: deque[tuple[int, int]] = deque()

    def enqueue_if_background(x: int, y: int) -> None:
        index = y * width + x
        if visited[index]:
            return
        if is_background_pixel(pixels[x, y], bg_rgb):
            visited[index] = 1
            queue.append((x, y))

    for x in range(width):
        enqueue_if_background(x, 0)
        enqueue_if_background(x, height - 1)
    for y in range(height):
        enqueue_if_background(0, y)
        enqueue_if_background(width - 1, y)

    while queue:
        x, y = queue.popleft()
        r, g, b, _ = pixels[x, y]
        pixels[x, y] = (r, g, b, 0)
        for next_x, next_y in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= next_x < width and 0 <= next_y < height:
                enqueue_if_background(next_x, next_y)

    return image


def crop_transparent_padding(image: Image.Image) -> Image.Image:
    bbox = image.getchannel("A").getbbox()
    if bbox is None:
        return image
    return image.crop(bbox)


def resize_to_max_dimension(image: Image.Image, max_size: int) -> Image.Image:
    width, height = image.size
    largest = max(width, height)
    if largest <= max_size:
        return image

    scale = max_size / largest
    new_size = (max(1, round(width * scale)), max(1, round(height * scale)))
    return image.resize(new_size, Image.Resampling.LANCZOS)


def optimize_icon(path: Path, max_size: int, dry_run: bool) -> str:
    original_bytes = path.stat().st_size
    with Image.open(path) as source:
        original_size = source.size
        image = remove_edge_background(source)
        image = crop_transparent_padding(image)
        image = resize_to_max_dimension(image, max_size)

    if not dry_run:
        image.save(path, optimize=True)

    action = "would optimize" if dry_run else "optimized"
    new_bytes = path.stat().st_size if not dry_run else original_bytes
    return (
        f"{action}: {path} "
        f"{original_size[0]}x{original_size[1]} -> {image.size[0]}x{image.size[1]}, "
        f"{original_bytes} -> {new_bytes} bytes"
    )


def iter_pngs(paths: Iterable[Path]) -> Iterable[Path]:
    for path in paths:
        if path.is_dir():
            yield from sorted(path.glob("*.png"))
        elif path.suffix.lower() == ".png":
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description="Optimize toolbar PNG icons for the application runtime.")
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        default=[Path("assets/Icons/ToolBar")],
        help="PNG files or folders to process. Defaults to assets/Icons/ToolBar.",
    )
    parser.add_argument("--max-size", type=int, default=128, help="Maximum width or height in pixels.")
    parser.add_argument("--dry-run", action="store_true", help="Print changes without writing files.")
    args = parser.parse_args()

    pngs = list(iter_pngs(args.paths))
    if not pngs:
        print("No PNG files found.")
        return 0

    for png in pngs:
        print(optimize_icon(png, args.max_size, args.dry_run))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
