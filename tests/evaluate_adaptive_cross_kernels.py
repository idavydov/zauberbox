from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from sweep_quirc_filters import build_decoder, collect_frame_paths, decode_with_quirc
from evaluate_kernel_triples import Variation, apply_cross_kernel


@dataclass(frozen=True)
class FrameEval:
    name: str
    recognized: bool
    candidate_only: bool


@dataclass(frozen=True)
class KernelScore:
    variation: Variation
    evaluations: tuple[FrameEval, ...]

    @property
    def valid(self) -> int:
        return sum(1 for item in self.evaluations if item.recognized)

    @property
    def candidate_only(self) -> int:
        return sum(1 for item in self.evaluations if item.candidate_only)

    @property
    def no_candidate(self) -> int:
        return len(self.evaluations) - self.valid - self.candidate_only

    def recognized_names(self) -> tuple[str, ...]:
        return tuple(item.name for item in self.evaluations if item.recognized)

    def candidate_names(self) -> tuple[str, ...]:
        return tuple(item.name for item in self.evaluations if item.candidate_only)


def evaluate_variation(
    decoder: Path,
    images: list[tuple[str, Image.Image]],
    variation: Variation,
) -> KernelScore:
    evaluations: list[FrameEval] = []
    for name, image in images:
        raw = apply_cross_kernel(image.tobytes(), image.width, image.height, variation.cw, variation.div, variation.off)
        result = decode_with_quirc(decoder, Image.frombytes("L", image.size, raw))
        evaluations.append(
            FrameEval(
                name=name,
                recognized=result.recognized,
                candidate_only=(not result.recognized and result.count > 0),
            )
        )
    return KernelScore(variation=variation, evaluations=tuple(evaluations))


def names_where(
    score: KernelScore,
    predicate,
) -> set[str]:
    return {item.name for item in score.evaluations if predicate(item)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", nargs="*")
    parser.add_argument("--top", type=int, default=8, help="How many single-kernel winners to print.")
    parser.add_argument("--center-weights", type=int, nargs="+", default=[5, 6, 7, 8, 9, 10, 11, 12])
    parser.add_argument("--divisors", type=int, nargs="+", default=[1, 2, 3, 4])
    parser.add_argument("--offsets", type=int, nargs="+", default=[-64, -48, -32, -16, 0, 16, 32, 48, 64])
    parser.add_argument("--default-cw", type=int, default=7)
    parser.add_argument("--default-div", type=int, default=1)
    parser.add_argument("--default-off", type=int, default=0)
    args = parser.parse_args()

    decoder = build_decoder()
    frame_paths = collect_frame_paths(args.frames)
    images: list[tuple[str, Image.Image]] = []
    for path in frame_paths:
        with Image.open(path) as image:
            images.append((Path(path).name, image.convert("L").copy()))

    variations = [
        Variation(cw, div, off)
        for cw in args.center_weights
        for div in args.divisors
        for off in args.offsets
    ]

    raw_valid = 0
    raw_candidate_only = 0
    for _, image in images:
        result = decode_with_quirc(decoder, image)
        if result.recognized:
            raw_valid += 1
        elif result.count > 0:
            raw_candidate_only += 1

    scores = [evaluate_variation(decoder, images, variation) for variation in variations]
    scores.sort(
        key=lambda score: (
            score.valid,
            score.candidate_only,
            -score.variation.div,
            score.variation.cw,
            -abs(score.variation.off),
        ),
        reverse=True,
    )

    default_variation = Variation(args.default_cw, args.default_div, args.default_off)
    default = next(score for score in scores if score.variation == default_variation)
    default_valid = names_where(default, lambda item: item.recognized)
    default_candidate_only = names_where(default, lambda item: item.candidate_only)
    default_fail = {item.name for item in default.evaluations if not item.recognized}
    default_no_candidate = default_fail - default_candidate_only

    def fallback_summary(pool: set[str]) -> tuple[KernelScore, tuple[str, ...]]:
        best_score: KernelScore | None = None
        best_added: tuple[str, ...] = ()
        best_count = -1
        for score in scores:
            if score.variation == default.variation:
                continue
            added = tuple(sorted(name for name in names_where(score, lambda item: item.recognized) if name in pool and name not in default_valid))
            if len(added) > best_count or (
                len(added) == best_count
                and best_score is not None
                and (score.valid, score.candidate_only) > (best_score.valid, best_score.candidate_only)
            ):
                best_score = score
                best_added = added
                best_count = len(added)
        assert best_score is not None
        return best_score, best_added

    best_any, best_any_added = fallback_summary(default_fail)
    best_after_candidate, best_after_candidate_added = fallback_summary(default_candidate_only)
    best_after_none, best_after_none_added = fallback_summary(default_no_candidate)

    print(f"Frames: {len(images)}")
    print(f"Raw baseline: valid={raw_valid}/{len(images)} candidate_only={raw_candidate_only}/{len(images)}")
    print(
        f"Default {default.variation.name}: "
        f"valid={default.valid}/{len(images)} candidate_only={default.candidate_only}/{len(images)} no_candidate={default.no_candidate}/{len(images)}"
    )
    print()
    print(f"Top {min(args.top, len(scores))} single kernels:")
    for score in scores[:args.top]:
        recognized = ",".join(score.recognized_names()[:8])
        candidate = ",".join(score.candidate_names()[:6])
        print(
            f"  {score.variation.name}: valid={score.valid}/{len(images)} "
            f"candidate_only={score.candidate_only}/{len(images)} no_candidate={score.no_candidate}/{len(images)} "
            f"valid_frames={recognized or '-'} candidate_frames={candidate or '-'}"
        )

    print()
    print("Best single fallback after default failure:")
    print(
        f"  {best_any.variation.name}: adds {len(best_any_added)} "
        f"({', '.join(best_any_added) or '-'})"
    )
    print("Best fallback when default found a candidate but decode failed:")
    print(
        f"  {best_after_candidate.variation.name}: adds {len(best_after_candidate_added)} "
        f"({', '.join(best_after_candidate_added) or '-'})"
    )
    print("Best fallback when default found no candidate:")
    print(
        f"  {best_after_none.variation.name}: adds {len(best_after_none_added)} "
        f"({', '.join(best_after_none_added) or '-'})"
    )


if __name__ == "__main__":
    main()
