import os
from pathlib import Path
from PIL import Image

def clamp_to_byte(value):
    return max(0, min(255, int(value)))

def apply_cross_sharpen_7(image):
    grayscale = image.convert("L")
    width, height = grayscale.size
    src = list(grayscale.getdata())
    dst = list(src)  # Start with a copy (handles borders)

    for y in range(1, height - 1):
        for x in range(1, width - 1):
            idx = y * width + x
            val = (
                7 * src[idx]
                - src[idx - 1]
                - src[idx + 1]
                - src[idx - width]
                - src[idx + width]
            )
            dst[idx] = clamp_to_byte(val)

    processed_image = Image.new("L", (width, height))
    processed_image.putdata(dst)
    return processed_image

def main():
    input_dir = Path("tests/example_frames")
    output_dir = Path("tests/example_frames_kernel_cross_7")
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Applying kernel_cross_7 convolution...")
    for frame_path in sorted(input_dir.glob("*.jpg")):
        print(f"Processing {frame_path.name}...")
        with Image.open(frame_path) as img:
            processed = apply_cross_sharpen_7(img)
            processed.save(output_dir / frame_path.name)
    
    print(f"Done. Processed images saved to {output_dir}")

if __name__ == "__main__":
    main()
