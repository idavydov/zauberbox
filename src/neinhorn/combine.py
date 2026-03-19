import argparse
import os
import sys
import glob
from PIL import Image, ImageDraw
from neinhorn.gen_card import generate_card

def combine_chunk(cards, output_file):
    # Dimensions based on 300 DPI
    DPI = 300
    MM_TO_INCH = 25.4
    
    # Sheet size (152mm x 102mm)
    SHEET_W = int((152 / MM_TO_INCH) * DPI)   # ~1795 px
    SHEET_H = int((102 / MM_TO_INCH) * DPI)   # ~1205 px

    line_width = int((1 / MM_TO_INCH) * DPI) # ~11-12 px for 1mm
    
    # Calculate scaled card dimensions to fit exactly between lines
    # (2 cards + 1 gap = SHEET_W)
    # (3 cards + 2 gaps = SHEET_H)
    CARD_W = (SHEET_W - line_width) // 2
    CARD_H = (SHEET_H - 2 * line_width) // 3

    sheet = Image.new("RGB", (SHEET_W, SHEET_H), "white")

    for i, card_img in enumerate(cards):
        # Resize to scaled dimensions
        card_img = card_img.resize((CARD_W, CARD_H), Image.Resampling.LANCZOS)

        row = i // 2
        col = i % 2
        
        # QR Facing Center:
        # Col 0 (Left): [Cover | QR] -> Normal
        # Col 1 (Right): [QR | Cover] -> Rotate 180
        if col == 1:
            card_img = card_img.transpose(Image.ROTATE_180)
            
        # Offset includes the gap for the cut lines
        x_offset = col * (CARD_W + line_width)
        y_offset = row * (CARD_H + line_width)
        
        sheet.paste(card_img, (x_offset, y_offset))

    # The gaps on the white background automatically form the cut lines.
    sheet.save(output_file, "JPEG", quality=95)
    print(f"Created sheet: {output_file} (Cards scaled to {CARD_W}x{CARD_H} px to preserve cut lines)")

def get_cover_image(directory):
    extensions = ('*.png', '*.jpg', '*.jpeg', '*.webp')
    images = []
    for ext in extensions:
        images.extend(glob.glob(os.path.join(directory, ext)))
    # Exclude card.jpg if it exists
    images = [img for img in images if os.path.basename(img).lower() != 'card.jpg']
    if len(images) == 1:
        return images[0]
    return None

def main():
    parser = argparse.ArgumentParser(description="Combine mixtape cards into 152x102mm sheets in groups of 6.")
    parser.add_argument("--generate", action="store_true", help="Generate card.jpg if missing in subdirectories")
    parser.add_argument("--out-dir", default="sheets", help="Directory to save combined sheets (default: sheets)")
    
    args = parser.parse_args()
    
    # 1. Gather directories (excluding the output directory)
    current_dir = os.getcwd()
    subdirs = [d for d in os.listdir(current_dir) if os.path.isdir(d) and d != args.out_dir]
    subdirs.sort()
    
    if not subdirs:
        print("No subdirectories found.")
        return

    os.makedirs(args.out_dir, exist_ok=True)
    
    all_cards = []
    
    for dr in subdirs:
        if dr.startswith("ign_") or dr.startswith("."):
            continue
        card_path = os.path.join(dr, "card.jpg")
        
        if not os.path.exists(card_path):
            if args.generate:
                cover = get_cover_image(dr)
                if cover:
                    print(f"Generating card for {dr}...")
                    uri = f"file://{os.path.basename(os.path.abspath(dr))}"
                    try:
                        card_img = generate_card(cover, uri, card_path, tracks_dir=dr)
                        card_img.save(card_path, "JPEG", quality=95)
                        all_cards.append(card_img)
                    except Exception as e:
                        print(f"Error generating card for {dr}: {e}")
                else:
                    print(f"Skipping {dr}: No unique cover image found and --generate is on.")
            else:
                print(f"Skipping {dr}: No card.jpg found. Use --generate to create it.")
        else:
            try:
                all_cards.append(Image.open(card_path).convert("RGB"))
            except Exception as e:
                print(f"Error opening {card_path}: {e}")

    # 2. Group into chunks of 6
    for i in range(0, len(all_cards), 6):
        chunk = all_cards[i:i+6]
        sheet_num = (i // 6) + 1
        output_file = os.path.join(args.out_dir, f"sheet_{sheet_num}.jpg")
        combine_chunk(chunk, output_file)

if __name__ == "__main__":
    main()
