from __future__ import annotations

from dataclasses import dataclass

from PIL import Image

from sweep_quirc_filters import (
    Variant,
    apply_variant,
    build_decoder,
    collect_frame_paths,
    decode_with_quirc,
)


@dataclass(frozen=True)
class Candidate:
    name: str
    description: str
    variant: Variant


CANDIDATES = [
    Candidate(
        name="baseline",
        description="raw grayscale",
        variant=Variant(autocontrast=False, denoise="none", contrast=1.0, sharpen="none"),
    ),
    Candidate(
        name="contrast130_unsharp250",
        description="contrast 1.30 + strong unsharp",
        variant=Variant(autocontrast=False, denoise="none", contrast=1.3, sharpen="unsharp2_250_2"),
    ),
    Candidate(
        name="autocontrast_unsharp250",
        description="autocontrast + strong unsharp",
        variant=Variant(autocontrast=True, denoise="none", contrast=1.0, sharpen="unsharp2_250_2"),
    ),
    Candidate(
        name="median3_unsharp250",
        description="median3 + strong unsharp",
        variant=Variant(autocontrast=False, denoise="median3", contrast=1.0, sharpen="unsharp2_250_2"),
    ),
    Candidate(
        name="median5_unsharp250",
        description="median5 + strong unsharp",
        variant=Variant(autocontrast=False, denoise="median5", contrast=1.0, sharpen="unsharp2_250_2"),
    ),
    Candidate(
        name="blur05_unsharp250",
        description="light blur + strong unsharp",
        variant=Variant(autocontrast=False, denoise="blur0.5", contrast=1.0, sharpen="unsharp2_250_2"),
    ),
    Candidate(
        name="autocontrast_contrast115_unsharp250",
        description="autocontrast + contrast 1.15 + strong unsharp",
        variant=Variant(autocontrast=True, denoise="none", contrast=1.15, sharpen="unsharp2_250_2"),
    ),
    Candidate(
        name="median3_contrast115_unsharp250",
        description="median3 + contrast 1.15 + strong unsharp",
        variant=Variant(autocontrast=False, denoise="median3", contrast=1.15, sharpen="unsharp2_250_2"),
    ),
    Candidate(
        name="blur05_contrast130_unsharp250",
        description="light blur + contrast 1.30 + strong unsharp",
        variant=Variant(autocontrast=False, denoise="blur0.5", contrast=1.3, sharpen="unsharp2_250_2"),
    ),
]


def main() -> int:
    decoder = build_decoder()
    frame_paths = collect_frame_paths([])
    if not frame_paths:
        print("No frames found.")
        return 1

    print(f"Decoder: {decoder}")
    print(f"Frames: {len(frame_paths)}")
    print()

    ranking: list[tuple[Candidate, int, int]] = []
    for candidate in CANDIDATES:
        valid = 0
        candidate_only = 0
        print(f"{candidate.name}: {candidate.description}")
        for frame_path in frame_paths:
            image = Image.open(frame_path).convert("L")
            result = decode_with_quirc(decoder, apply_variant(image, candidate.variant))
            if result.recognized:
                valid += 1
                print(f"  {frame_path.name}: valid {list(result.payloads)}")
            elif result.count > 0:
                candidate_only += 1
                print(f"  {frame_path.name}: invalid count={result.count} errors={list(result.invalid_errors)}")
            else:
                print(f"  {frame_path.name}: none")
        ranking.append((candidate, valid, candidate_only))
        print(f"  summary: valid={valid}/{len(frame_paths)} candidate_only={candidate_only}/{len(frame_paths)}")
        print()

    print("Ranking:")
    for candidate, valid, candidate_only in sorted(ranking, key=lambda item: (item[1], item[2]), reverse=True):
        print(f"  {candidate.name}: valid={valid}/{len(frame_paths)} candidate_only={candidate_only}/{len(frame_paths)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
