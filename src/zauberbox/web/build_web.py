import os
import gzip
import minify_html

def build():
    # Project root is 4 levels up from this script (src/zauberbox/web/build_web.py)
    package_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(package_dir, "..", "..", ".."))
    
    web_src_dir = os.path.join(project_root, "web")
    dist_dir = os.path.join(project_root, "dist")

    index_path = os.path.join(web_src_dir, "index.html")
    pico_path = os.path.join(web_src_dir, "pico.min.css")
    style_path = os.path.join(web_src_dir, "style.css")
    app_path = os.path.join(web_src_dir, "app.js")
    qr_path = os.path.join(web_src_dir, "qrcode.min.js")
    
    output_path = os.path.join(dist_dir, "index.html")
    output_gz_path = output_path + ".gz"

    os.makedirs(dist_dir, exist_ok=True)

    with open(index_path, "r") as f:
        content = f.read()

    # Inline Pico CSS
    with open(pico_path, "r") as f:
        pico_css = f.read()
    content = content.replace('<link rel="stylesheet" href="pico.min.css">', f'<style>{pico_css}</style>')

    # Inline local CSS
    with open(style_path, "r") as f:
        css = f.read()
    content = content.replace('<link rel="stylesheet" href="style.css">', f'<style>{css}</style>')

    # Inline QR library
    with open(qr_path, "r") as f:
        qr_js = f.read()
    content = content.replace('<script src="qrcode.min.js"></script>', f'<script>{qr_js}</script>')

    # Inline JS
    with open(app_path, "r") as f:
        js = f.read()
    content = content.replace('<script src="app.js"></script>', f'<script>{js}</script>')

    # Minify
    minified = minify_html.minify(
        content,
        minify_css=True,
        minify_js=True,
        keep_comments=False,
        remove_bangs=True,
        remove_processing_instructions=True,
        allow_removing_spaces_between_attributes=True,
        keep_closing_tags=False,
        keep_html_and_head_opening_tags=False
    )

    with open(output_path, "w") as f:
        f.write(minified)

    # Gzip
    with gzip.open(output_gz_path, "wb") as f:
        f.write(minified.encode("utf-8"))

    print(f"Build complete: {output_path} ({len(minified)} bytes)")
    print(f"Compressed: {output_gz_path} ({os.path.getsize(output_gz_path)} bytes)")

def main():
    build()

if __name__ == "__main__":
    main()
