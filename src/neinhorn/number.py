import os
import re
import argparse
import sys
import random

def rename_mp3s(strip_existing, dry_run, shuffle):
    # 1. Setup & Filtering
    path = "."
    # Filter for mp3 files and sort them to maintain order
    prefix_pattern = re.compile(r'^\d+[\s.\-_]*')

    # 2. Filter and sort
    files = sorted(
        [f for f in os.listdir(path) if f.lower().endswith('.mp3')],
        key=lambda f: prefix_pattern.sub('', f).lower()
    )

    if shuffle:
        random.shuffle(files)
    
    if not files:
        print("No MP3 files found in the current directory.")
        return

    # 2. Safety Check: If not stripping, check for existing leading numbers
    if not strip_existing:
        pattern = re.compile(r'^\d+\s')
        if any(pattern.match(f) for f in files):
            print("Error: Existing leading numbers detected. Use --strip to overwrite them.")
            sys.exit(1)

    # 3. Determine Padding (Auto-calculate 01, 001, etc.)
    num_files = len(files)
    padding = max(1, len(str(num_files)))

    # 4. Prepare the rename list
    rename_tasks = []
    for index, filename in enumerate(files, start=1):
        clean_name = filename
        if strip_existing:
            # Removes leading digits, dots, dashes, and spaces
            clean_name = re.sub(r'^[\d\s._-]+', '', filename)
        
        new_name = f"{index:0{padding}d} {clean_name}"
        rename_tasks.append((filename, new_name))

    # 5. Show Preview
    print(f"\n{'[DRY RUN] ' if dry_run else ''}Proposed Renaming:")
    for old, new in rename_tasks[:15]:
        print(f"  {old}  ->  {new}")
    if len(rename_tasks) > 15:
        print(f"  ... and {len(rename_tasks) - 15} more.")

    # 6. Final Confirmation
    if not dry_run:
        confirm = input(f"\nProceed with renaming {len(rename_tasks)} files? (y/N): ").lower()
        if confirm == 'y':
            for old, new in rename_tasks:
                os.rename(os.path.join(path, old), os.path.join(path, new))
            print("Done!")
        else:
            print("Cancelled.")
    else:
        print("\nDry run complete. No files were changed.")

def main():
    parser = argparse.ArgumentParser(description="Prepend MP3 files with sequential numbering.")
    
    # Flags
    parser.add_argument("-s", "--strip", action="store_true", 
                        help="Strip existing leading numbers/symbols before prepending.")
    parser.add_argument("-d", "--dry-run", action="store_true", 
                        help="Show what would happen without renaming any files.")
    parser.add_argument("-r", "--shuffle", action="store_true", 
                        help="Shuffle order.")
    
    args = parser.parse_args()
    
    rename_mp3s(strip_existing=args.strip, dry_run=args.dry_run, shuffle=args.shuffle)

if __name__ == "__main__":
    main()
