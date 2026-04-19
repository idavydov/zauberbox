from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from PIL import Image

from sweep_quirc_filters import build_decoder, collect_frame_paths, decode_with_quirc


TransformFn = Callable[[Image.Image], Image.Image]


@dataclass(frozen=True)
class Candidate:
    name: str
    family: str
    description: str
    estimated_cost: int
    transform: TransformFn


@dataclass(frozen=True)
class CandidateScore:
    candidate: Candidate
    valid: int
    candidate_only: int
    invalid: int
    recovered_frames: tuple[str, ...]


def clamp_to_byte(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def copy_borders(src: bytes, dst: bytearray, width: int, height: int, border_radius: int) -> None:
    if width <= 0 or height <= 0:
        return

    top_rows = min(border_radius, height)
    for y in range(top_rows):
        row_start = y * width
        dst[row_start:row_start + width] = src[row_start:row_start + width]

    if height > border_radius:
        for y in range(max(border_radius, height - border_radius), height):
            row_start = y * width
            dst[row_start:row_start + width] = src[row_start:row_start + width]

    left_columns = min(border_radius, width)
    right_start = max(left_columns, width - border_radius)
    for y in range(border_radius, max(border_radius, height - border_radius)):
        row_start = y * width
        for x in range(left_columns):
            dst[row_start + x] = src[row_start + x]
        for x in range(right_start, width):
            dst[row_start + x] = src[row_start + x]


def apply_linear_contrast(raw: bytes, amount_percent: int) -> bytes:
    output = bytearray(len(raw))
    for index, value in enumerate(raw):
        centered = value - 128
        output[index] = clamp_to_byte(128 + ((centered * amount_percent) + 50) // 100)
    return bytes(output)


def apply_kernel_3x3(raw: bytes, width: int, height: int, kernel: tuple[int, ...], divisor: int = 1) -> bytes:
    output = bytearray(width * height)
    copy_borders(raw, output, width, height, 1)
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


def apply_box_blur_3x3(raw: bytes, width: int, height: int) -> bytes:
    output = bytearray(width * height)
    copy_borders(raw, output, width, height, 1)
    for y in range(1, height - 1):
        for x in range(1, width - 1):
            sum_value = 0
            for dy in (-1, 0, 1):
                row_offset = (y + dy) * width
                for dx in (-1, 0, 1):
                    sum_value += raw[row_offset + x + dx]
            output[y * width + x] = (sum_value + 4) // 9
    return bytes(output)


def apply_separable_kernel(raw: bytes, width: int, height: int, kernel: tuple[int, ...], divisor: int) -> bytes:
    radius = len(kernel) // 2
    scratch = bytearray(width * height)
    output = bytearray(width * height)
    copy_borders(raw, scratch, width, height, radius)
    copy_borders(raw, output, width, height, radius)

    for y in range(radius, height - radius):
        row_start = y * width
        for x in range(radius, width - radius):
            acc = 0
            for index, weight in enumerate(kernel):
                src_x = x + index - radius
                acc += weight * raw[row_start + src_x]
            scratch[row_start + x] = (acc + (divisor // 2)) // divisor

    for y in range(radius, height - radius):
        for x in range(radius, width - radius):
            acc = 0
            for index, weight in enumerate(kernel):
                src_y = y + index - radius
                acc += weight * scratch[src_y * width + x]
            output[y * width + x] = (acc + (divisor // 2)) // divisor

    return bytes(output)


def apply_high_boost(raw: bytes, blurred: bytes, src_weight: int, blur_weight: int) -> bytes:
    output = bytearray(len(raw))
    for index, value in enumerate(raw):
        sharpened = ((src_weight * value) - (blur_weight * blurred[index]) + 50) // 100
        output[index] = clamp_to_byte(sharpened)
    return bytes(output)


def image_from_raw(raw: bytes, width: int, height: int) -> Image.Image:
    return Image.frombytes("L", (width, height), raw)


def resize_down_up(image: Image.Image, scale_num: int, scale_den: int, resample: int) -> Image.Image:
    grayscale = image.convert("L")
    width, height = grayscale.size
    resized_width = max(1, (width * scale_num + (scale_den // 2)) // scale_den)
    resized_height = max(1, (height * scale_num + (scale_den // 2)) // scale_den)
    reduced = grayscale.resize((resized_width, resized_height), resample=resample)
    return reduced.resize((width, height), resample=resample)


def transform_linear_contrast(image: Image.Image, amount_percent: int) -> Image.Image:
    grayscale = image.convert("L")
    width, height = grayscale.size
    return image_from_raw(apply_linear_contrast(grayscale.tobytes(), amount_percent), width, height)


def transform_kernel_3x3(image: Image.Image, kernel: tuple[int, ...]) -> Image.Image:
    grayscale = image.convert("L")
    width, height = grayscale.size
    return image_from_raw(apply_kernel_3x3(grayscale.tobytes(), width, height, kernel), width, height)


def transform_box_high_boost(image: Image.Image, src_weight: int, blur_weight: int) -> Image.Image:
    grayscale = image.convert("L")
    width, height = grayscale.size
    raw = grayscale.tobytes()
    blurred = apply_box_blur_3x3(raw, width, height)
    return image_from_raw(apply_high_boost(raw, blurred, src_weight, blur_weight), width, height)


def transform_separable_high_boost(
    image: Image.Image,
    kernel: tuple[int, ...],
    divisor: int,
    src_weight: int,
    blur_weight: int,
) -> Image.Image:
    grayscale = image.convert("L")
    width, height = grayscale.size
    raw = grayscale.tobytes()
    blurred = apply_separable_kernel(raw, width, height, kernel, divisor)
    return image_from_raw(apply_high_boost(raw, blurred, src_weight, blur_weight), width, height)


def transform_resize_only(image: Image.Image, scale_num: int, scale_den: int, resample: int) -> Image.Image:
    return resize_down_up(image, scale_num, scale_den, resample)


def transform_resize_then_high_boost(
    image: Image.Image,
    scale_num: int,
    scale_den: int,
    resample: int,
    kernel: tuple[int, ...],
    divisor: int,
    src_weight: int,
    blur_weight: int,
) -> Image.Image:
    resized = resize_down_up(image, scale_num, scale_den, resample)
    return transform_separable_high_boost(resized, kernel, divisor, src_weight, blur_weight)


def build_candidates() -> list[Candidate]:
    gauss5 = (1, 4, 6, 4, 1)
    gauss7 = (1, 6, 15, 20, 15, 6, 1)
    nearest = Image.Resampling.NEAREST
    bilinear = Image.Resampling.BILINEAR
    candidates: list[Candidate] = [
        Candidate(
            name="baseline",
            family="baseline",
            description="raw grayscale",
            estimated_cost=1,
            transform=lambda image: image.convert("L"),
        ),
    ]

    for amount in (115, 130, 145):
        candidates.append(
            Candidate(
                name=f"contrast_{amount}",
                family="contrast_linear",
                description=f"single-pass contrast {amount}%",
                estimated_cost=1,
                transform=lambda image, amount=amount: transform_linear_contrast(image, amount),
            )
        )

    kernel_variants = {
        "cross_5": (0, -1, 0, -1, 5, -1, 0, -1, 0),
        "full_9": (-1, -1, -1, -1, 9, -1, -1, -1, -1),
        "cross_7": (0, -1, 0, -1, 7, -1, 0, -1, 0),
        "full_13": (-1, -1, -1, -1, 13, -1, -1, -1, -1),
    }
    for name, kernel in kernel_variants.items():
        candidates.append(
            Candidate(
                name=f"kernel_{name}",
                family="kernel_3x3",
                description=f"single 3x3 sharpen kernel {name}",
                estimated_cost=9,
                transform=lambda image, kernel=kernel: transform_kernel_3x3(image, kernel),
            )
        )

    for src_weight, blur_weight in ((275, 175), (350, 250), (400, 300)):
        candidates.append(
            Candidate(
                name=f"box3_hb_{src_weight}_{blur_weight}",
                family="box3_high_boost",
                description=f"3x3 box blur plus high-boost {src_weight}-{blur_weight}",
                estimated_cost=10,
                transform=lambda image, src_weight=src_weight, blur_weight=blur_weight: transform_box_high_boost(
                    image,
                    src_weight,
                    blur_weight,
                ),
            )
        )

    for src_weight, blur_weight in ((350, 250), (400, 300), (450, 350)):
        candidates.append(
            Candidate(
                name=f"gauss5_hb_{src_weight}_{blur_weight}",
                family="gauss5_high_boost",
                description=f"5-tap separable blur plus high-boost {src_weight}-{blur_weight}",
                estimated_cost=11,
                transform=lambda image, src_weight=src_weight, blur_weight=blur_weight: transform_separable_high_boost(
                    image,
                    gauss5,
                    16,
                    src_weight,
                    blur_weight,
                ),
            )
        )
        candidates.append(
            Candidate(
                name=f"gauss7_hb_{src_weight}_{blur_weight}",
                family="gauss7_high_boost",
                description=f"7-tap separable blur plus high-boost {src_weight}-{blur_weight}",
                estimated_cost=15,
                transform=lambda image, src_weight=src_weight, blur_weight=blur_weight: transform_separable_high_boost(
                    image,
                    gauss7,
                    64,
                    src_weight,
                    blur_weight,
                ),
            )
        )

    for scale_num, scale_den in ((1, 2), (2, 3), (3, 4)):
        label = f"{scale_num}of{scale_den}"
        candidates.append(
            Candidate(
                name=f"resize_nearest_{label}",
                family="resize_nearest",
                description=f"down/up resize nearest at {scale_num}/{scale_den}",
                estimated_cost=3,
                transform=lambda image, scale_num=scale_num, scale_den=scale_den: transform_resize_only(
                    image,
                    scale_num,
                    scale_den,
                    nearest,
                ),
            )
        )
        candidates.append(
            Candidate(
                name=f"resize_bilinear_{label}",
                family="resize_bilinear",
                description=f"down/up resize bilinear at {scale_num}/{scale_den}",
                estimated_cost=6,
                transform=lambda image, scale_num=scale_num, scale_den=scale_den: transform_resize_only(
                    image,
                    scale_num,
                    scale_den,
                    bilinear,
                ),
            )
        )

    for scale_num, scale_den in ((1, 2), (2, 3), (3, 4)):
        label = f"{scale_num}of{scale_den}"
        for src_weight, blur_weight in ((400, 300), (450, 350)):
            candidates.append(
                Candidate(
                    name=f"resize_nearest_{label}_gauss5_hb_{src_weight}_{blur_weight}",
                    family="resize_nearest_gauss5_high_boost",
                    description=(
                        f"nearest resize {scale_num}/{scale_den}, then 5-tap blur plus "
                        f"high-boost {src_weight}-{blur_weight}"
                    ),
                    estimated_cost=14,
                    transform=lambda image,
                    scale_num=scale_num,
                    scale_den=scale_den,
                    src_weight=src_weight,
                    blur_weight=blur_weight: transform_resize_then_high_boost(
                        image,
                        scale_num,
                        scale_den,
                        nearest,
                        gauss5,
                        16,
                        src_weight,
                        blur_weight,
                    ),
                )
            )
            candidates.append(
                Candidate(
                    name=f"resize_bilinear_{label}_gauss5_hb_{src_weight}_{blur_weight}",
                    family="resize_bilinear_gauss5_high_boost",
                    description=(
                        f"bilinear resize {scale_num}/{scale_den}, then 5-tap blur plus "
                        f"high-boost {src_weight}-{blur_weight}"
                    ),
                    estimated_cost=17,
                    transform=lambda image,
                    scale_num=scale_num,
                    scale_den=scale_den,
                    src_weight=src_weight,
                    blur_weight=blur_weight: transform_resize_then_high_boost(
                        image,
                        scale_num,
                        scale_den,
                        bilinear,
                        gauss5,
                        16,
                        src_weight,
                        blur_weight,
                    ),
                )
            )

    return candidates


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bounded grid search of MCU-plausible single-path transforms using vendored quirc."
    )
    parser.add_argument(
        "frames",
        nargs="*",
        help="Optional image paths. Defaults to tests/example_frames/*.jpg.",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=10,
        help="How many overall top candidates to print.",
    )
    parser.add_argument(
        "--per-family",
        type=int,
        default=1,
        help="How many best candidates to print per family.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    decoder = build_decoder()
    frame_paths = collect_frame_paths(args.frames)
    if not frame_paths:
        print("No frames found.")
        return 1

    images: list[tuple[str, Image.Image]] = []
    for frame_path in frame_paths:
        with Image.open(frame_path) as image:
            images.append((Path(frame_path).name, image.convert("L").copy()))

    candidates = build_candidates()
    baseline = next(candidate for candidate in candidates if candidate.name == "baseline")
    baseline_recovered: set[str] = set()
    for frame_name, image in images:
        result = decode_with_quirc(decoder, baseline.transform(image))
        if result.recognized:
            baseline_recovered.add(frame_name)

    scores: list[CandidateScore] = []
    for candidate in candidates:
        valid = 0
        candidate_only = 0
        invalid = 0
        recovered_frames: list[str] = []
        for frame_name, image in images:
            result = decode_with_quirc(decoder, candidate.transform(image))
            if result.recognized:
                valid += 1
                if frame_name not in baseline_recovered:
                    recovered_frames.append(frame_name)
            elif result.count > 0:
                candidate_only += 1
                invalid += 1
        scores.append(
            CandidateScore(
                candidate=candidate,
                valid=valid,
                candidate_only=candidate_only,
                invalid=invalid,
                recovered_frames=tuple(recovered_frames),
            )
        )

    ranked = sorted(
        scores,
        key=lambda score: (score.valid, score.candidate_only, -score.candidate.estimated_cost, score.candidate.name),
        reverse=True,
    )

    best_by_family: dict[str, list[CandidateScore]] = {}
    for score in ranked:
        best_by_family.setdefault(score.candidate.family, [])
        if len(best_by_family[score.candidate.family]) < max(1, args.per_family):
            best_by_family[score.candidate.family].append(score)

    print(f"Decoder: {decoder}")
    print(f"Frames: {len(images)}")
    print(f"Candidates tried: {len(candidates)}")
    print(f"Baseline valid: {len(baseline_recovered)}/{len(images)}")
    print()
    print("Best per family:")
    family_order = [
        "baseline",
        "contrast_linear",
        "kernel_3x3",
        "box3_high_boost",
        "gauss5_high_boost",
        "gauss7_high_boost",
        "resize_nearest",
        "resize_bilinear",
        "resize_nearest_gauss5_high_boost",
        "resize_bilinear_gauss5_high_boost",
    ]
    for family in family_order:
        for score in best_by_family.get(family, []):
            recovered = ",".join(score.recovered_frames) if score.recovered_frames else "-"
            print(
                f"  {score.candidate.name}: valid={score.valid}/{len(images)} "
                f"candidate_only={score.candidate_only}/{len(images)} "
                f"cost~{score.candidate.estimated_cost} recovered={recovered}"
            )

    print()
    print(f"Top {min(args.top, len(ranked))} overall:")
    for index, score in enumerate(ranked[: args.top], start=1):
        recovered = ",".join(score.recovered_frames) if score.recovered_frames else "-"
        print(
            f"  {index:02d}. {score.candidate.name}: valid={score.valid}/{len(images)} "
            f"candidate_only={score.candidate_only}/{len(images)} "
            f"family={score.candidate.family} cost~{score.candidate.estimated_cost} "
            f"recovered={recovered}"
        )

    print()
    print("MCU mapping:")
    print("  contrast/kernel/high-boost families map to per-pixel ops plus convolution and vector add/sub/mul.")
    print("  resize families map to straightforward nearest or bilinear loops; no obvious local ESP-DSP image resize helper was found.")
    print("  separable blur families are the closest fit to ESP-DSP-style convolution primitives.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
