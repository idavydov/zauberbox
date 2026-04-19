from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import itertools
import time

from PIL import Image

from sweep_quirc_filters import build_decoder, collect_frame_paths, decode_with_quirc


def clamp_to_byte(value: int) -> int:
    return max(0, min(255, value))


def apply_cross_kernel(raw: bytes, width: int, height: int, center_weight: int, divisor: int = 1, offset: int = 0) -> bytes:
    output = bytearray(width * height)
    # Copy borders
    output[:width] = raw[:width]
    output[-width:] = raw[-width:]
    for y in range(1, height - 1):
        output[y * width] = raw[y * width]
        output[y * width + width - 1] = raw[y * width + width - 1]

    for y in range(1, height - 1):
        row = y * width
        prev_row = (y - 1) * width
        next_row = (y + 1) * width
        for x in range(1, width - 1):
            # cross kernel: (0, -1, 0, -1, CW, -1, 0, -1, 0)
            acc = (
                center_weight * raw[row + x]
                - raw[prev_row + x]
                - raw[next_row + x]
                - raw[row + x - 1]
                - raw[row + x + 1]
            )
            output[row + x] = clamp_to_byte(acc // divisor + offset)
    return bytes(output)


@dataclass(frozen=True)
class Variation:
    cw: int
    div: int
    off: int

    @property
    def name(self) -> str:
        return f"cw{self.cw}_d{self.div}_o{self.off}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", nargs="*")
    args = parser.parse_args()

    decoder = build_decoder()
    frame_paths = collect_frame_paths(args.frames)
    
    images = []
    for p in frame_paths:
        with Image.open(p) as img:
            images.append((Path(p).name, img.convert("L").copy()))

    num_frames = len(images)
    all_frames_mask = (1 << num_frames) - 1

    # 1. Baseline coverage
    baseline_mask = 0
    for i, (name, img) in enumerate(images):
        result = decode_with_quirc(decoder, img)
        if result.recognized:
            baseline_mask |= (1 << i)
    
    print(f"Frames: {num_frames}")
    print(f"Baseline coverage: {bin(baseline_mask).count('1')}/{num_frames}")

    # 2. Define and evaluate variations
    center_weights = [5, 6, 7, 8, 9, 10, 11, 12]
    divisors = [1, 2, 3, 4]
    offsets = [-64, -48, -32, -16, 0, 16, 32, 48, 64]

    variations = []
    for cw, div, off in itertools.product(center_weights, divisors, offsets):
        variations.append(Variation(cw, div, off))

    print(f"Evaluating {len(variations)} variations...")
    variation_data = []
    for var in variations:
        mask = 0
        for i, (name, img) in enumerate(images):
            processed_raw = apply_cross_kernel(img.tobytes(), img.width, img.height, var.cw, var.div, var.off)
            processed_img = Image.frombytes("L", img.size, processed_raw)
            result = decode_with_quirc(decoder, processed_img)
            if result.recognized:
                mask |= (1 << i)
        if mask: # Only keep variations that actually help
            variation_data.append((var, mask))

    print(f"Active variations (covering at least 1 frame): {len(variation_data)}")

    # 3. Best sequence of 3 transforms WITH baseline
    def find_best_k(base_mask, k, data):
        best_indices = []
        max_covered = bin(base_mask).count('1')
        
        # Combinatorial search
        start_time = time.time()
        for combo in itertools.combinations(range(len(data)), k):
            union = base_mask
            for idx in combo:
                union |= data[idx][1]
            
            count = bin(union).count('1')
            if count > max_covered:
                max_covered = count
                best_indices = combo
            if count == num_frames:
                break
        
        end_time = time.time()
        return best_indices, max_covered, end_time - start_time

    print("\n--- Strategy: Baseline + 3 Transforms ---")
    best_idx, count, duration = find_best_k(baseline_mask, 3, variation_data)
    print(f"Search took {duration:.2f}s. Total coverage: {count}/{num_frames}")
    print("  0. Baseline")
    current_mask = baseline_mask
    for i, idx in enumerate(best_idx, 1):
        var, mask = variation_data[idx]
        added_mask = mask & ~current_mask
        added_names = [images[j][0] for j in range(num_frames) if (added_mask >> j) & 1]
        print(f"  {i}. {var.name}: adds {bin(added_mask).count('1')} ({', '.join(added_names)})")
        current_mask |= mask

    # 4. Best sequence of 3 transforms WITHOUT baseline
    print("\n--- Strategy: 3 Transforms ONLY (Skip Baseline) ---")
    best_idx, count, duration = find_best_k(0, 3, variation_data)
    print(f"Search took {duration:.2f}s. Total coverage: {count}/{num_frames}")
    current_mask = 0
    for i, idx in enumerate(best_idx, 1):
        var, mask = variation_data[idx]
        added_mask = mask & ~current_mask
        added_names = [images[j][0] for j in range(num_frames) if (added_mask >> j) & 1]
        print(f"  {i}. {var.name}: adds {bin(added_mask).count('1')} ({', '.join(added_names)})")
        current_mask |= mask

    # 5. Constrained: Same CW/Div + 3 Offsets (with Baseline)
    print("\n--- Strategy: Baseline + 3 Offsets (Same CW/Div) ---")
    best_constrained_idx = []
    max_constrained_count = 0
    best_params = None
    
    for cw, div in itertools.product(center_weights, divisors):
        sub_data = [(v, m) for v, m in variation_data if v.cw == cw and v.div == div]
        if len(sub_data) < 3: continue
        
        indices, count, _ = find_best_k(baseline_mask, 3, sub_data)
        if count > max_constrained_count:
            max_constrained_count = count
            best_constrained_idx = [variation_data.index(sub_data[i]) for i in indices]
            best_params = (cw, div)
            
    print(f"Best Constrained (CW={best_params[0]}, Div={best_params[1]}) coverage: {max_constrained_count}/{num_frames}")
    current_mask = baseline_mask
    for i, idx in enumerate(best_constrained_idx, 1):
        var, mask = variation_data[idx]
        added_mask = mask & ~current_mask
        added_names = [images[j][0] for j in range(num_frames) if (added_mask >> j) & 1]
        print(f"  {i}. {var.name}: adds {bin(added_mask).count('1')} ({', '.join(added_names)})")
        current_mask |= mask

if __name__ == "__main__":
    main()
