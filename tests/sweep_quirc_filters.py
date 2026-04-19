from __future__ import annotations

import argparse
import binascii
import subprocess
from dataclasses import dataclass
from itertools import product
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageEnhance, ImageFilter, ImageOps


REPO_ROOT = Path(__file__).resolve().parent.parent
EXAMPLE_FRAMES_DIR = REPO_ROOT / "tests" / "example_frames"
QUIRC_SOURCES = [
    REPO_ROOT / "firmware" / "src" / "qr_reader" / "quirc" / "identify.c",
    REPO_ROOT / "firmware" / "src" / "qr_reader" / "quirc" / "decode.c",
    REPO_ROOT / "firmware" / "src" / "qr_reader" / "quirc" / "quirc.c",
    REPO_ROOT / "firmware" / "src" / "qr_reader" / "quirc" / "version_db.c",
    REPO_ROOT / "firmware" / "src" / "qr_reader" / "openmv" / "collections.c",
]
QUIRC_INCLUDE_DIR = REPO_ROOT / "firmware" / "src" / "qr_reader" / "quirc"
OPENMV_INCLUDE_DIR = REPO_ROOT / "firmware" / "src" / "qr_reader" / "openmv"
QUIRC_DRIVER_SOURCE = REPO_ROOT / "tests" / "quirc_decode.c"
QUIRC_DRIVER_BINARY = REPO_ROOT / "tests" / ".build" / "quirc_decode"
QUIRC_COMPAT_INCLUDE_DIR = REPO_ROOT / "tests" / "quirc_compat"


@dataclass(frozen=True)
class Variant:
    autocontrast: bool
    denoise: str
    contrast: float
    sharpen: str

    @property
    def name(self) -> str:
        return (
            f"autocontrast={'on' if self.autocontrast else 'off'}"
            f",denoise={self.denoise}"
            f",contrast={self.contrast:.2f}"
            f",sharpen={self.sharpen}"
        )


@dataclass(frozen=True)
class DecodeResult:
    count: int
    payloads: tuple[str, ...]
    invalid_errors: tuple[str, ...]

    @property
    def recognized(self) -> bool:
        return bool(self.payloads)


def build_decoder() -> Path:
    QUIRC_DRIVER_BINARY.parent.mkdir(parents=True, exist_ok=True)
    source_mtime = max(path.stat().st_mtime for path in [QUIRC_DRIVER_SOURCE, *QUIRC_SOURCES])
    if QUIRC_DRIVER_BINARY.exists() and QUIRC_DRIVER_BINARY.stat().st_mtime >= source_mtime:
        return QUIRC_DRIVER_BINARY

    command = [
        "cc",
        "-std=c99",
        "-O2",
        "-Wall",
        "-Wextra",
        "-DHOST_BUILD",
        "-I",
        str(QUIRC_COMPAT_INCLUDE_DIR),
        "-I",
        str(QUIRC_INCLUDE_DIR),
        "-I",
        str(OPENMV_INCLUDE_DIR),
        str(QUIRC_DRIVER_SOURCE),
        *(str(path) for path in QUIRC_SOURCES),
        "-lm",
        "-o",
        str(QUIRC_DRIVER_BINARY),
    ]
    subprocess.run(command, check=True, cwd=REPO_ROOT)
    return QUIRC_DRIVER_BINARY


def decode_payload_text(payload_hex: str) -> str:
    payload = binascii.unhexlify(payload_hex)
    try:
        return payload.decode("utf-8")
    except UnicodeDecodeError:
        return payload.decode("latin-1", errors="replace")


def decode_with_quirc(decoder: Path, image: Image.Image) -> DecodeResult:
    grayscale = image.convert("L")
    width, height = grayscale.size
    raw = grayscale.tobytes()
    completed = subprocess.run(
        [str(decoder), str(width), str(height)],
        input=raw,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
        cwd=REPO_ROOT,
    )

    count = 0
    payloads: list[str] = []
    invalid_errors: list[str] = []
    for line in completed.stdout.decode("utf-8").splitlines():
        parts = line.split("\t")
        if not parts:
            continue
        if parts[0] == "COUNT" and len(parts) == 2:
            count = int(parts[1])
        elif parts[0] == "VALID" and len(parts) == 4:
            payloads.append(decode_payload_text(parts[3]))
        elif parts[0] == "INVALID" and len(parts) == 2:
            invalid_errors.append(parts[1])

    return DecodeResult(count=count, payloads=tuple(payloads), invalid_errors=tuple(invalid_errors))


def clamp_to_byte(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def copy_borders(src: bytes, dst: bytearray, width: int, height: int) -> None:
    if width <= 0 or height <= 0:
        return

    first_row = src[:width]
    dst[:width] = first_row
    if height > 1:
        last_row_start = (height - 1) * width
        dst[last_row_start:last_row_start + width] = src[last_row_start:last_row_start + width]

    if width == 1:
        for y in range(1, height - 1):
            row_start = y * width
            dst[row_start] = src[row_start]
        return

    for y in range(1, height - 1):
        row_start = y * width
        dst[row_start] = src[row_start]
        dst[row_start + width - 1] = src[row_start + width - 1]


def apply_box_blur_3x3(src: bytes, width: int, height: int) -> bytearray:
    dst = bytearray(width * height)
    copy_borders(src, dst, width, height)
    for y in range(1, height - 1):
        for x in range(1, width - 1):
            sum_value = 0
            for dy in (-1, 0, 1):
                row_offset = (y + dy) * width
                for dx in (-1, 0, 1):
                    sum_value += src[row_offset + x + dx]
            dst[y * width + x] = (sum_value + 4) // 9
    return dst


def apply_strong_unsharp(src: bytes, blurred: bytes) -> bytearray:
    dst = bytearray(len(src))
    for index, src_value in enumerate(src):
        sharpened = ((350 * src_value) - (250 * blurred[index]) + 50) // 100
        dst[index] = clamp_to_byte(sharpened)
    return dst


def apply_firmware_default(image: Image.Image) -> Image.Image:
    grayscale = image.convert("L")
    width, height = grayscale.size
    raw = grayscale.tobytes()
    scratch = apply_box_blur_3x3(raw, width, height)
    blurred = apply_box_blur_3x3(scratch, width, height)
    sharpened = apply_strong_unsharp(raw, blurred)
    return Image.frombytes("L", (width, height), bytes(sharpened))


def apply_denoise(image: Image.Image, mode: str) -> Image.Image:
    if mode == "none":
        return image
    if mode == "median3":
        return image.filter(ImageFilter.MedianFilter(size=3))
    if mode == "median5":
        return image.filter(ImageFilter.MedianFilter(size=5))
    if mode == "blur0.5":
        return image.filter(ImageFilter.GaussianBlur(radius=0.5))
    raise ValueError(f"unsupported denoise mode: {mode}")


def apply_sharpen(image: Image.Image, mode: str) -> Image.Image:
    if mode == "none":
        return image
    if mode == "unsharp1_125_2":
        return image.filter(ImageFilter.UnsharpMask(radius=1, percent=125, threshold=2))
    if mode == "unsharp2_175_2":
        return image.filter(ImageFilter.UnsharpMask(radius=2, percent=175, threshold=2))
    if mode == "unsharp2_250_2":
        return image.filter(ImageFilter.UnsharpMask(radius=2, percent=250, threshold=2))
    raise ValueError(f"unsupported sharpen mode: {mode}")


def apply_variant(image: Image.Image, variant: Variant) -> Image.Image:
    processed = image.convert("L")
    if variant.autocontrast:
        processed = ImageOps.autocontrast(processed)
    processed = apply_denoise(processed, variant.denoise)
    if variant.contrast != 1.0:
        processed = ImageEnhance.Contrast(processed).enhance(variant.contrast)
    processed = apply_sharpen(processed, variant.sharpen)
    return processed


def iter_variants() -> Iterable[Variant]:
    for autocontrast, denoise, contrast, sharpen in product(
        [False, True],
        ["none", "median3", "median5", "blur0.5"],
        [1.0, 1.15, 1.3],
        ["none", "unsharp1_125_2", "unsharp2_175_2", "unsharp2_250_2"],
    ):
        yield Variant(
            autocontrast=autocontrast,
            denoise=denoise,
            contrast=contrast,
            sharpen=sharpen,
        )


def collect_frame_paths(frame_paths: list[str]) -> list[Path]:
    if frame_paths:
        return [Path(path).resolve() for path in frame_paths]
    return sorted(EXAMPLE_FRAMES_DIR.glob("*.jpg"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Try filter variants on captured QR frames using the vendored quirc decoder."
    )
    parser.add_argument(
        "frames",
        nargs="*",
        help="Optional image paths. Defaults to tests/example_frames/*.jpg.",
    )
    parser.add_argument(
        "--max-successes",
        type=int,
        default=5,
        help="Maximum number of successful variants to print per frame.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    frame_paths = collect_frame_paths(args.frames)
    if not frame_paths:
        print("No frames found.")
        return 1

    decoder = build_decoder()
    variants = list(iter_variants())

    print(f"Decoder: {decoder}")
    print(f"Frames: {len(frame_paths)}")
    print(f"Variants: {len(variants)}")
    print()

    recognized_without_filters = 0
    recognized_with_filters = 0

    for frame_path in frame_paths:
        image = Image.open(frame_path).convert("L")
        baseline = decode_with_quirc(decoder, image)
        firmware_default = decode_with_quirc(decoder, apply_firmware_default(image))

        print(f"{frame_path.name}: size={image.width}x{image.height}")
        if baseline.recognized:
            recognized_without_filters += 1
            print(f"  baseline: recognized payloads={list(baseline.payloads)}")
        else:
            print(
                "  baseline:"
                f" no valid decode, candidates={baseline.count}, invalid={list(baseline.invalid_errors)}"
            )

        if firmware_default.recognized:
            print(f"  firmware_default: recognized payloads={list(firmware_default.payloads)}")
        else:
            print(
                "  firmware_default:"
                f" no valid decode, candidates={firmware_default.count},"
                f" invalid={list(firmware_default.invalid_errors)}"
            )

        successes: list[tuple[Variant, DecodeResult]] = []
        for variant in variants:
            result = decode_with_quirc(decoder, apply_variant(image, variant))
            if result.recognized:
                successes.append((variant, result))

        if successes:
            recognized_with_filters += 1
            print(f"  successful variants: {len(successes)}")
            for variant, result in successes[: args.max_successes]:
                print(f"    {variant.name} -> {list(result.payloads)}")
            if len(successes) > args.max_successes:
                remaining = len(successes) - args.max_successes
                print(f"    ... {remaining} more")
        else:
            print("  successful variants: none")
        print()

    print("Summary:")
    print(f"  baseline recognized: {recognized_without_filters}/{len(frame_paths)}")
    print(f"  recognized after filtering: {recognized_with_filters}/{len(frame_paths)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
