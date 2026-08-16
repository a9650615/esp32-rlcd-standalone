#!/usr/bin/env python3
"""The set of CJK characters the interface can draw, derived from its sources.

Shared by scripts/build-cjk-font.sh, which subsets the font to exactly these,
and scripts/check-cjk-font.py, which fails the build when the two disagree.
One definition rather than two: a generator and a checker that each decide for
themselves what counts will eventually disagree, and the symptom is a build
failing over a character the generator was never going to include.

Run directly to print the set; import for the function.
"""
import re
import sys
from pathlib import Path

# Above this needs a glyph from the CJK subset. Below it is Latin, punctuation
# and symbols that Montserrat already covers.
CJK_FLOOR = 0x2E7F


def _strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def glyphs_used(ui_dir: Path) -> set:
    """Characters inside string literals under `ui_dir`.

    Literals rather than whole files, so Chinese in a comment costs no flash.

    Scoped to the UI layer on purpose. Parsers elsewhere carry Chinese for
    matching, not drawing - market_parse compares TWSE field names like
    "發行量加權股價指數" and then displays "TAIEX" - and subsetting glyphs for
    text that never reaches the panel would spend flash on nothing.
    """
    used = set()
    sources = sorted(ui_dir.rglob("*.cpp")) + sorted(ui_dir.rglob("*.hpp"))
    for source in sources:
        if "fonts" in source.parts:
            continue
        text = _strip_comments(source.read_text(encoding="utf-8"))
        for literal in re.findall(r'"((?:[^"\\\n]|\\.)*)"', text):
            used |= {c for c in literal if ord(c) > CJK_FLOOR}
    return used


if __name__ == "__main__":
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("components/ui")
    sys.stdout.write("".join(sorted(glyphs_used(root))))
