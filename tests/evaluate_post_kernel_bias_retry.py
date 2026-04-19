from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from evaluate_kernel_triples import Variation, apply_cross_kernel
from sweep_quirc_filters import build_decoder, collect_frame_paths, decode_with_quirc


def clamp_to_byte(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def apply_bias(raw: bytes, delta: int) -> bytes:
    output = bytearray(len(raw))
    for index, value in enumerate(raw):
        output[index] = clamp_to_byte(value + delta)
    return bytes(output)


@dataclass(frozen=True)
class FrameOutcome:
    name: str
    recognized: bool
    candidate_only: bool
    clipped_black: int
    clipped_white: int


@dataclass(frozen=True)
class RetryScore:
    delta: int
    outcomes: tuple[FrameOutcome, ...]

    @property
    def valid(self) -> int:
        return sum(1 for item in self.outcomes if item.recognized)

    @property
    def candidate_only(self) -> int:
        return sum(1 for item in self.outcomes if item.candidate_only)

    def recognized_names(self) -> tuple[str, ...]:
        return tuple(item.name for item in self.outcomes if item.recognized)


def clipped_counts(raw: bytes) -> tuple[int, int]:
    black = 0
    white = 0
    for value in raw:
        if value == 0:
            black += 1
        elif value == 255:
            white += 1
    return black, white


def evaluate_retry(
    decoder: Path,
    images: list[tuple[str, Image.Image]],
    default_raw_by_name: dict[str, bytes],
    delta: int,
) -> RetryScore:
    outcomes: list[FrameOutcome] = []
    for name, image in images:
        biased = apply_bias(default_raw_by_name[name], delta)
        black, white = clipped_counts(biased)
        result = decode_with_quirc(decoder, Image.frombytes("L", image.size, biased))
        outcomes.append(
            FrameOutcome(
                name=name,
                recognized=result.recognized,
                candidate_only=(not result.recognized and result.count > 0),
                clipped_black=black,
                clipped_white=white,
            )
        )
    return RetryScore(delta=delta, outcomes=tuple(outcomes))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", nargs="*")
    parser.add_argument("--default-cw", type=int, default=8)
    parser.add_argument("--default-div", type=int, default=1)
    parser.add_argument("--default-off", type=int, default=-32)
    parser.add_argument("--deltas", type=int, nargs="+", default=[-48, -32, -16, 16, 32, 48, 64])
    parser.add_argument("--top-clipped", type=int, default=6)
    args = parser.parse_args()

    decoder = build_decoder()
    frame_paths = collect_frame_paths(args.frames)
    images: list[tuple[str, Image.Image]] = []
    for path in frame_paths:
        with Image.open(path) as image:
            images.append((Path(path).name, image.convert("L").copy()))

    default_variation = Variation(args.default_cw, args.default_div, args.default_off)
    default_raw_by_name: dict[str, bytes] = {}
    default_outcomes: list[FrameOutcome] = []
    for name, image in images:
        processed = apply_cross_kernel(
            image.tobytes(),
            image.width,
            image.height,
            default_variation.cw,
            default_variation.div,
            default_variation.off,
        )
        default_raw_by_name[name] = processed
        black, white = clipped_counts(processed)
        result = decode_with_quirc(decoder, Image.frombytes("L", image.size, processed))
        default_outcomes.append(
            FrameOutcome(
                name=name,
                recognized=result.recognized,
                candidate_only=(not result.recognized and result.count > 0),
                clipped_black=black,
                clipped_white=white,
            )
        )

    default_valid = {item.name for item in default_outcomes if item.recognized}
    default_candidate = {item.name for item in default_outcomes if item.candidate_only}
    default_no_candidate = {item.name for item in default_outcomes if not item.recognized and not item.candidate_only}

    retry_scores = [evaluate_retry(decoder, images, default_raw_by_name, delta) for delta in args.deltas]
    retry_scores.sort(key=lambda score: (score.valid, score.candidate_only, -abs(score.delta)), reverse=True)

    print(f"Frames: {len(images)}")
    print(
        f"Default {default_variation.name}: "
        f"valid={len(default_valid)}/{len(images)} candidate_only={len(default_candidate)}/{len(images)} "
        f"no_candidate={len(default_no_candidate)}/{len(images)}"
    )
    print()
    print("Best post-bias retries:")
    for score in retry_scores[: min(6, len(retry_scores))]:
        added = sorted(name for name in score.recognized_names() if name not in default_valid)
        print(
            f"  delta={score.delta:+d}: valid={score.valid}/{len(images)} "
            f"candidate_only={score.candidate_only}/{len(images)} adds={len(added)} "
            f"({', '.join(added) or '-'})"
        )

    best_retry = retry_scores[0]
    best_retry_valid = {item.name for item in best_retry.outcomes if item.recognized}
    best_retry_candidate = {item.name for item in best_retry.outcomes if item.candidate_only}

    direct_variation = Variation(
        args.default_cw,
        args.default_div,
        args.default_off + best_retry.delta,
    )
    direct_valid: set[str] = set()
    direct_candidate: set[str] = set()
    identical_to_direct = 0
    for name, image in images:
        direct_raw = apply_cross_kernel(
            image.tobytes(),
            image.width,
            image.height,
            direct_variation.cw,
            direct_variation.div,
            direct_variation.off,
        )
        if direct_raw == apply_bias(default_raw_by_name[name], best_retry.delta):
            identical_to_direct += 1
        result = decode_with_quirc(decoder, Image.frombytes("L", image.size, direct_raw))
        if result.recognized:
            direct_valid.add(name)
        elif result.count > 0:
            direct_candidate.add(name)

    print()
    print(
        f"Best retry vs direct recompute ({direct_variation.name}): "
        f"post_bias_valid={len(best_retry_valid)}/{len(images)} "
        f"direct_valid={len(direct_valid)}/{len(images)} "
        f"identical_frames={identical_to_direct}/{len(images)}"
    )
    print(
        f"  post-bias only successes: {', '.join(sorted(best_retry_valid - direct_valid)) or '-'}"
    )
    print(
        f"  direct-only successes: {', '.join(sorted(direct_valid - best_retry_valid)) or '-'}"
    )
    print(
        f"  post-bias candidate_only={len(best_retry_candidate)}/{len(images)} "
        f"direct candidate_only={len(direct_candidate)}/{len(images)}"
    )

    print()
    print("Most clipped default outputs:")
    clipped_rows = sorted(
        default_outcomes,
        key=lambda item: item.clipped_black + item.clipped_white,
        reverse=True,
    )
    total_pixels = images[0][1].width * images[0][1].height if images else 1
    for outcome in clipped_rows[: args.top_clipped]:
        clipped_total = outcome.clipped_black + outcome.clipped_white
        clipped_percent = 100.0 * clipped_total / total_pixels
        print(
            f"  {outcome.name}: clipped={clipped_total} ({clipped_percent:.1f}%) "
            f"black={outcome.clipped_black} white={outcome.clipped_white} "
            f"recognized={int(outcome.recognized)} candidate_only={int(outcome.candidate_only)}"
        )


if __name__ == "__main__":
    main()
