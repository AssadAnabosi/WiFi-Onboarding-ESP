#!/usr/bin/env python3
"""Build the embeddable web interface for the ESP-IDF firmware.

Reads the editable sources from ``web-interface/``, minifies the HTML/CSS,
gzips every asset, and writes the results to ``main/www/`` as ``*.gz`` files
that ``main/CMakeLists.txt`` embeds into the application image via EMBED_FILES.

Run this once before ``idf.py build`` (and again whenever the web interface
changes):

    python utils/build_web.py

JavaScript is gzipped but intentionally left un-minified to avoid the JS
minifier mangling the source; gzip already removes most of the redundancy.
"""

import gzip
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "web-interface"
OUT_DIR = ROOT / "main" / "www"

# Asset types we ship to the device.
EMBED_SUFFIXES = {".html", ".css", ".js", ".ico"}
# Suffixes that are minified before gzipping.
MINIFY_SUFFIXES = {".html", ".css"}


def ensure_minifier():
    """Import css_html_js_minify, installing it on first use if necessary."""
    try:
        from css_html_js_minify import (  # noqa: F401
            process_single_css_file,
            process_single_html_file,
        )
    except ImportError:
        print("Installing required library 'css-html-js-minify'...")
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", "css-html-js-minify"]
        )
    from css_html_js_minify import (
        process_single_css_file,
        process_single_html_file,
    )

    return process_single_css_file, process_single_html_file


def main():
    if not SRC_DIR.is_dir():
        sys.exit(f"Source directory not found: {SRC_DIR}")

    minify_css, minify_html = ensure_minifier()

    # Start from a clean output directory so removed assets don't linger.
    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    count = 0
    total_in = 0
    total_out = 0

    for src in sorted(SRC_DIR.rglob("*")):
        if not src.is_file() or src.suffix.lower() not in EMBED_SUFFIXES:
            continue

        rel = src.relative_to(SRC_DIR)
        # Stage a working copy we can minify in place without touching the source.
        staged = OUT_DIR / rel
        staged.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, staged)

        if src.suffix.lower() == ".css":
            minify_css(str(staged), overwrite=True)
        elif src.suffix.lower() == ".html":
            minify_html(str(staged), overwrite=True)

        # css_html_js_minify may emit a ".min.<ext>" sibling instead of
        # overwriting; normalise back onto the staged path.
        min_sibling = staged.with_suffix(".min" + staged.suffix)
        if min_sibling.exists():
            shutil.move(str(min_sibling), str(staged))

        gz_path = staged.with_name(staged.name + ".gz")
        with open(staged, "rb") as f_in, gzip.open(gz_path, "wb", compresslevel=9) as f_out:
            shutil.copyfileobj(f_in, f_out)

        in_size = staged.stat().st_size
        out_size = gz_path.stat().st_size
        total_in += in_size
        total_out += out_size
        count += 1
        staged.unlink()  # keep only the embedded .gz
        print(f"  {rel}  {in_size} -> {out_size} B (gz)")

    print(
        f"\nEmbedded {count} asset(s): {total_in} B -> {total_out} B gzipped "
        f"into {OUT_DIR.relative_to(ROOT)}"
    )
    print("Now run: idf.py build flash monitor")


if __name__ == "__main__":
    main()
