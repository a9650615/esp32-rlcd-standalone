#!/usr/bin/env python3
"""Fails the build when the interface uses a character the font subset lacks.

The Chinese glyph set is derived from components/ui/ui_strings.cpp by
scripts/build-cjk-font.sh, which means adding a string and forgetting to
regenerate leaves those characters with no glyph. LVGL draws a missing glyph as
an empty box, silently - the build succeeds, the flash succeeds, and the panel
shows tofu. That happened once already, for 32 characters.

So the coverage is checked on every build rather than remembered.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cjk_glyphs import glyphs_used  # noqa: E402

def font_codepoints(font_file: Path) -> set:
    """Codepoints an lv_font_conv SPARSE_TINY cmap covers.

    unicode_list holds offsets from range_start, not absolute codepoints -
    reading them as absolute makes every character look missing.
    """
    text = font_file.read_text(encoding="utf-8")
    start = re.search(r"\.range_start = (\d+)", text)
    listing = re.search(r"unicode_list_0\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if start is None or listing is None:
        raise SystemExit(f"{font_file.name}: unrecognised font format")
    offsets = (int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", listing.group(1)))
    return {int(start.group(1)) + o for o in offsets}


def main() -> int:
    project = Path(__file__).resolve().parent.parent
    ui_dir = project / "components/ui"
    fonts = sorted((project / "components/ui/fonts").glob("rlcd_cjk_*.c"))
    if not fonts:
        print("error: no generated CJK fonts; run ./scripts/build-cjk-font.sh",
              file=sys.stderr)
        return 1

    wanted = glyphs_used(ui_dir)
    failed = False
    for font in fonts:
        missing = sorted(c for c in wanted if ord(c) not in font_codepoints(font))
        if missing:
            failed = True
            print(f"error: {font.name} is missing {len(missing)} glyph(s) the "
                  f"interface uses: {''.join(missing)}", file=sys.stderr)
    if failed:
        print("\nRun ./scripts/build-cjk-font.sh and commit components/ui/fonts/.",
              file=sys.stderr)
        return 1

    print(f"cjk font coverage: {len(wanted)} characters, all present in "
          f"{len(fonts)} sizes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
