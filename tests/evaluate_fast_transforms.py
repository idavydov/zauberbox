from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageOps

from sweep_quirc_filters import build_decoder, collect_frame_paths, decode_with_quirc


@dataclass(frozen=True)
class TransformResult:
    recognized_frames: int
    candidate_frames: int
    invalid_frames: int


@dataclass(frozen=True)
class Transform:
    name: str
    description: str


TRANSFORMS = [
    Transform("baseline", "raw grayscale"),
    Transform("autocontrast", "single transform, full-range stretch"),
    Transform("contrast_115", "single transform, linear contrast 1.15x"),
    Transform("contrast_130", "single transform, linear contrast 1.30x"),
    Transform("kernel_cross_5", "single 3x3 sharpen kernel [0,-1,0;-1,5,-1;0,-1,0]"),
    Transform("kernel_full_9", "single 3x3 sharpen kernel [-1,-1,-1;-1,9,-1;-1,-1,-1]"),
    Transform("unsharp_box_175", "one box blur + high-boost amount 1.75"),
    Transform("unsharp_box_250", "one box blur + high-boost amount 2.50"),
]


def clamp_to_byte(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def copy_borders(src: bytes, dst: bytearray, width: int, height: int) -> None:
    if width <= 0 or height <= 0:
        return

    dst[:width] = src[:width]
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


def apply_linear_contrast(raw: bytes, amount_percent: int) -> bytes:
    output = bytearray(len(raw))
    for index, value in enumerate(raw):
        centered = value - 128
        output[index] = clamp_to_byte(128 + ((centered * amount_percent) + 50) // 100)
    return bytes(output)


def apply_box_blur_3x3(raw: bytes, width: int, height: int) -> bytes:
    output = bytearray(width * height)
    copy_borders(raw, output, width, height)
    for y in range(1, height - 1):
        for x in range(1, width - 1):
            sum_value = 0
            for dy in (-1, 0, 1):
                row_offset = (y + dy) * width
                for dx in (-1, 0, 1):
                    sum_value += raw[row_offset + x + dx]
            output[y * width + x] = (sum_value + 4) // 9
    return bytes(output)


def apply_high_boost(raw: bytes, blurred: bytes, src_weight: int, blur_weight: int) -> bytes:
    output = bytearray(len(raw))
    for index, value in enumerate(raw):
        sharpened = ((src_weight * value) - (blur_weight * blurred[index]) + 50) // 100
        output[index] = clamp_to_byte(sharpened)
    return bytes(output)


def apply_kernel(raw: bytes, width: int, height: int, kernel: tuple[int, ...], divisor: int = 1) -> bytes:
    output = bytearray(width * height)
    copy_borders(raw, output, width, height)
    for y in range(1, height - 1):
        for x in range(1, width - 1):
            acc = 0
            kernel_index = 0
            for dy in (-1, 0, 1):
                row_offset = (y + dy) * width
                for dx in (-1, 0, 1):
                    acc += kernel[kernel_index] * raw[row_offset + x + dx]
                    kernel_index += 1
            output[y * width + x] = clamp_to_byte(acc // divisor)
    return bytes(output)


def transform_image(image: Image.Image, transform_name: str) -> Image.Image:
    grayscale = image.convert("L")
    width, height = grayscale.size
    raw = grayscale.tobytes()

    if transform_name == "baseline":
        return grayscale
    if transform_name == "autocontrast":
        return ImageOps.autocontrast(grayscale)
    if transform_name == "contrast_115":
        return Image.frombytes("L", (width, height), apply_linear_contrast(raw, 115))
    if transform_name == "contrast_130":
        return Image.frombytes("L", (width, height), apply_linear_contrast(raw, 130))
    if transform_name == "kernel_cross_5":
        kernel = (0, -1, 0, -1, 5, -1, 0, -1, 0)
        return Image.frombytes("L", (width, height), apply_kernel(raw, width, height, kernel))
    if transform_name == "kernel_full_9":
        kernel = (-1, -1, -1, -1, 9, -1, -1, -1, -1)
        return Image.frombytes("L", (width, height), apply_kernel(raw, width, height, kernel))
    if transform_name == "unsharp_box_175":
        blurred = apply_box_blur_3x3(raw, width, height)
        return Image.frombytes("L", (width, height), apply_high_boost(raw, blurred, 275, 175))
    if transform_name == "unsharp_box_250":
        blurred = apply_box_blur_3x3(raw, width, height)
        return Image.frombytes("L", (width, height), apply_high_boost(raw, blurred, 350, 250))
    raise ValueError(f"unsupported transform: {transform_name}")


def main() -> int:
    decoder = build_decoder()
    frame_paths = collect_frame_paths([])
    if not frame_paths:
        print("No frames found.")
        return 1

    print(f"Decoder: {decoder}")
    print(f"Frames: {len(frame_paths)}")
    print()

    scorecard: list[tuple[Transform, TransformResult]] = []
    for transform in TRANSFORMS:
        recognized_frames = 0
        candidate_frames = 0
        invalid_frames = 0

        print(f"{transform.name}: {transform.description}")
        for frame_path in frame_paths:
            image = Image.open(frame_path).convert("L")
            result = decode_with_quirc(decoder, transform_image(image, transform.name))
            status = "none"
            if result.recognized:
                recognized_frames += 1
                status = f"valid {list(result.payloads)}"
            elif result.count > 0:
                candidate_frames += 1
                invalid_frames += 1
                status = f"invalid count={result.count} errors={list(result.invalid_errors)}"
            print(f"  {Path(frame_path).name}: {status}")

        transform_result = TransformResult(
            recognized_frames=recognized_frames,
            candidate_frames=candidate_frames,
            invalid_frames=invalid_frames,
        )
        scorecard.append((transform, transform_result))
        print(
            f"  summary: valid={recognized_frames}/{len(frame_paths)}"
            f" candidate_only={candidate_frames}/{len(frame_paths)}"
        )
        print()

    print("Ranking:")
    for transform, result in sorted(
        scorecard,
        key=lambda item: (item[1].recognized_frames, item[1].candidate_frames),
        reverse=True,
    ):
        print(
            f"  {transform.name}: valid={result.recognized_frames}/{len(frame_paths)}"
            f" candidate_only={result.candidate_frames}/{len(frame_paths)}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
