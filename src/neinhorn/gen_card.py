import qrcode
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import argparse
import os
import sys
import glob
import random
from importlib import resources

def draw_text_with_outline(draw, position, text, font, text_color="white", outline_color="black"):
    """Draws text with a 1px outline for maximum visibility on images."""
    x, y = position
    # Draw outline (8 directions)
    for dx, dy in [(-1,-1), (0,-1), (1,-1), (-1,0), (1,0), (-1,1), (0,1), (1,1)]:
        draw.text((x + dx, y + dy), text, font=font, fill=outline_color)
    # Draw main text
    draw.text((x, y), text, font=font, fill=text_color)

def generate_card(cover_path, uri, output_file, tracks_dir=None, qr_width_ratio=0.33, font_size=18):
    """
    Generates a single mixtape card image.
    Layout:
    - Left: Cover image filling the space.
    - Right: QR Code section (width determined by qr_width_ratio).
    - Overlay: Tracklist drawn at the bottom-left of the cover.
    """
    # Dimensions based on 76mm x 34mm at 300 DPI
    DPI = 300
    MM_TO_INCH = 25.4
    WIDTH_PX = int((76 / MM_TO_INCH) * DPI)  # ~898 px
    HEIGHT_PX = int((34 / MM_TO_INCH) * DPI) # ~402 px
    
    QR_SECTION_WIDTH = int(WIDTH_PX * qr_width_ratio)
    COVER_WIDTH = WIDTH_PX - QR_SECTION_WIDTH
    
    # --- 1. Cover Image Processing ---
    if not os.path.exists(cover_path):
        print(f"Error: Cover image '{cover_path}' not found.")
        sys.exit(1)
        
    cover_img = Image.open(cover_path).convert("RGB")
    target_w, target_h = COVER_WIDTH, HEIGHT_PX
    
    img_w, img_h = cover_img.size
    img_aspect = img_w / img_h
    target_aspect = target_w / target_h
    
    # Crop and Resize cover to fill the left section
    if img_aspect > target_aspect:
        new_width = int(target_h * img_aspect)
        cover_rescaled = cover_img.resize((new_width, target_h), Image.Resampling.LANCZOS)
        left = (new_width - target_w) // 2
        cover_final = cover_rescaled.crop((left, 0, left + target_w, target_h))
    else:
        new_height = int(target_w / img_aspect)
        cover_rescaled = cover_img.resize((target_w, new_height), Image.Resampling.LANCZOS)
        top = (new_height - target_h) // 2
        cover_final = cover_rescaled.crop((0, top, target_w, top + target_h))
    
    # Create the base card
    card = Image.new("RGB", (WIDTH_PX, HEIGHT_PX), "white")
    
    # --- 2. QR Background Generation: Slowly changing Gaussian Noise ---
    # Get 8 most frequent colors from the cover
    try:
        quantized = cover_final.quantize(colors=8)
        palette = quantized.getpalette()
        # Each color is 3 values (R, G, B)
        colors = [tuple(palette[i:i+3]) for i in range(0, 24, 3)]
    except Exception:
        # Fallback to a simple palette if quantization fails
        colors = [(255, 255, 255), (200, 200, 200), (150, 150, 150)]

    # Create a small noise source (e.g., 5x5)
    noise_w, noise_h = 5, 5
    noise_src = Image.new("RGB", (noise_w, noise_h))
    pixels = noise_src.load()
    for y in range(noise_h):
        for x in range(noise_w):
            pixels[x, y] = random.choice(colors)

    # Upscale and blur to create "slowly changing" effect
    qr_bg = noise_src.resize((QR_SECTION_WIDTH, HEIGHT_PX), Image.Resampling.BILINEAR)
    qr_bg = qr_bg.filter(ImageFilter.GaussianBlur(radius=QR_SECTION_WIDTH / 4))
    
    card.paste(cover_final, (0, 0))
    card.paste(qr_bg, (COVER_WIDTH, 0))
    
    draw = ImageDraw.Draw(card)

    # --- 3. QR Code Generation and Robust Positioning ---
    qr = qrcode.QRCode(
        version=1,
        error_correction=qrcode.constants.ERROR_CORRECT_H,
        box_size=10, 
        border=1,
    )
    qr.add_data(uri)
    qr.make(fit=True)
    qr_img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
    
    # Padding within the QR section (e.g., 10% of the section width)
    padding = int(min(QR_SECTION_WIDTH, HEIGHT_PX) * 0.1)
    
    # Determine max possible size for the QR code while maintaining margins
    max_available_w = QR_SECTION_WIDTH - (2 * padding)
    max_available_h = HEIGHT_PX - (2 * padding)
    qr_side = min(max_available_w, max_available_h)
    
    qr_img = qr_img.resize((qr_side, qr_side), Image.Resampling.LANCZOS)
    
    # Calculate Center Position within the QR Section
    # Section starts at x = COVER_WIDTH
    qr_x = COVER_WIDTH + (QR_SECTION_WIDTH - qr_side) // 2
    qr_y = (HEIGHT_PX - qr_side) // 2
    
    card.paste(qr_img, (qr_x, qr_y))

    # --- 4. Tracklist Overlay (Bottom-left of cover) ---
    if tracks_dir:
        search_dir = tracks_dir if tracks_dir != True else "."
        if os.path.isdir(search_dir):
            try:
                files = [f[:-4] for f in os.listdir(search_dir) if f.lower().endswith('.mp3')]
                files.sort()
                
                # --- Try loading bundled font first ---
                font = None
                try:
                    font_path = resources.files('neinhorn.fonts').joinpath('NotoSans-Medium.ttf')
                    with resources.as_file(font_path) as path:
                        if path.exists():
                            font = ImageFont.truetype(str(path), font_size)
                            print(f"Using bundled font: {path}")
                except Exception as e:
                    print(f"Warning: Could not load bundled font: {e}")

                if not font:
                    font_paths = [
                        "/usr/share/fonts/truetype/noto/NotoSans-Medium.ttf",
                        "/home/idavydov/Music/Neinhorn/fonts/fonts/ttf/JetBrainsMono-Medium.ttf",
                        "/usr/share/fonts/opentype/inter/Inter-Bold.otf",
                        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                        "C:\\Windows\\Fonts\\arial.ttf",
                        "/Library/Fonts/Arial.ttf"
                    ]
                    for path in font_paths:
                        if os.path.exists(path):
                            font = ImageFont.truetype(path, font_size)
                            print("Font:", path)
                            break
                if not font:
                    font = ImageFont.load_default()
                
                line_height = font_size + 4
                margin_x = 15
                margin_y = 15
                
                # Calculate how many tracks can fit in the vertical space
                max_tracks = (HEIGHT_PX - (margin_y * 2)) // line_height
                visible_tracks = files[:max_tracks]
                
                # Draw tracks starting from the bottom up
                total_text_height = len(visible_tracks) * line_height
                start_y = HEIGHT_PX - total_text_height - margin_y
                
                for i, track in enumerate(visible_tracks):
                    pos_x = margin_x
                    pos_y = start_y + (i * line_height)
                    draw_text_with_outline(draw, (pos_x, pos_y), track, font)
            except Exception as e:
                print(f"Warning: Could not process tracks: {e}")
    
    card.save(output_file, "JPEG", quality=95)
    print(f"Generated {WIDTH_PX}x{HEIGHT_PX}px card: {output_file}")

def main():
    parser = argparse.ArgumentParser(description="Generate a Mixtape Card with Track Overlay.")
    parser.add_argument("--cover", help="Path to the cover image")
    parser.add_argument("--uri", help="The URI (e.g., file://001). Defaults to current directory name.")
    parser.add_argument("--out", default="card.jpg", help="Output filename (default: card.jpg)")
    parser.add_argument("--tracks", default=".", help="Path to .mp3 files (default: '.')")
    parser.add_argument("--no-tracks", action="store_true", help="Disable track list overlay")
    parser.add_argument("--qwidth", type=float, default=0.33, help="QR section width ratio (default 0.33)")
    parser.add_argument("--tsize", type=int, default=18, help="Font size (default 18)")

    args = parser.parse_args()
    
    # --- Resolve Defaults ---
    cover = args.cover
    if not cover:
        extensions = ('*.png', '*.jpg', '*.jpeg', '*.webp')
        images = []
        for ext in extensions:
            images.extend(glob.glob(ext))
        
        # Ignore the output file itself if it matches the glob (e.g., card.jpg)
        images = [img for img in images if os.path.basename(img).lower() != args.out.lower()]
        
        if len(images) == 1:
            cover = images[0]
            print(f"No --cover provided, using: {cover}")
        else:
            parser.error(f"No --cover provided and couldn't find a unique image file (found {len(images)}) in current directory. (Ignored {args.out})")

    uri = args.uri or os.path.basename(os.getcwd())
    if not args.uri:
        print(f"No --uri provided, using current directory name: {uri}")

    tracks_dir = None if args.no_tracks else args.tracks
    generate_card(cover, uri, args.out, tracks_dir=tracks_dir, qr_width_ratio=args.qwidth, font_size=args.tsize)

if __name__ == "__main__":
    main()
