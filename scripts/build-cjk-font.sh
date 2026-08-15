#!/usr/bin/env bash
# Regenerates the Traditional Chinese glyph subset used by the interface.
#
# The generated .c files are committed, so building the firmware needs neither
# node nor a network. Run this only after adding or changing Chinese text in
# components/ui/ui_strings.cpp, then commit the result.
#
# Why a subset: a full CJK face is several megabytes and this device has a 3 MiB
# application partition. Only the characters the interface actually uses are
# compiled in, which at the time of writing is under a hundred.
#
# Why Noto Sans TC: it is SIL Open Font License 1.1, so the generated subset can
# be redistributed with this GPL project. A system font (macOS PingFang, STHeiti)
# would render fine and could not be shipped.
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
strings_file="$project_dir/components/ui/ui_strings.cpp"
out_dir="$project_dir/components/ui/fonts"
cache_dir="${TMPDIR:-/tmp}/rlcd-font-cache"
font_file="$cache_dir/NotoSansTC-Regular.otf"
font_url="https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/TC/NotoSansTC-Regular.otf"

# Sizes that render Chinese. The 48 px face is the clock hero, which shows
# digits and a colon only, so it is deliberately absent - adding it would cost
# the most memory per glyph of any size for text that cannot occur.
sizes=(14 20 28)

for tool in node npx curl python3; do
  command -v "$tool" >/dev/null || { printf 'error: %s is required\n' "$tool" >&2; exit 1; }
done

mkdir -p "$cache_dir" "$out_dir"
if [[ ! -f "$font_file" ]]; then
  printf 'fetching Noto Sans TC (SIL OFL 1.1)\n' >&2
  curl -fsSL -o "$font_file" "$font_url"
fi

# The glyph set is derived from the source rather than maintained by hand, so a
# new string cannot silently render as blank boxes: rerun this and the glyph is
# there, or forget to and the test suite's own ASCII check still passes while
# the panel shows nothing - which is exactly why this file is generated.
symbols="$(python3 - "$strings_file" <<'PY'
import sys, pathlib
text = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
glyphs = sorted({c for c in text if ord(c) > 0x2E7F})
sys.stdout.write("".join(glyphs))
PY
)"

if [[ -z "$symbols" ]]; then
  printf 'error: no non-ASCII glyphs found in %s\n' "$strings_file" >&2
  exit 1
fi
printf 'subsetting %s glyphs\n' "$(python3 -c 'import sys;print(len(sys.argv[1]))' "$symbols")" >&2

for size in "${sizes[@]}"; do
  out="$out_dir/rlcd_cjk_${size}.c"
  npx --yes lv_font_conv@1.5.3 \
    --font "$font_file" \
    --symbols "$symbols" \
    --size "$size" \
    --bpp 1 \
    --format lvgl \
    --no-compress \
    --lv-include lvgl.h \
    --force-fast-kern-format \
    -o "$out"
  printf 'wrote %s (%s bytes)\n' "$out" "$(wc -c < "$out" | tr -d ' ')" >&2
done

printf '\nRegenerated. Commit components/ui/fonts/ along with the string change.\n' >&2
