# UI asset provenance

## Charging-bolt icon (tray battery overlay)

- Repository: <https://github.com/twbs/icons> (Bootstrap Icons)
- Pinned commit: `6945b7006285d444cc17ff2e22c7691719229526` (2026-08-18)
- Licence: MIT (repository root `LICENSE`) - permissive-into-copyleft, so
  MIT source may be included in this repository's own GPL-3.0 work; same
  direction and same precedent as the ALAC decoder vendored under
  `modules/airplay/UPSTREAM.md`.
- Glyph: **`lightning-charge-fill`**, at `icons/lightning-charge-fill.svg`
  - a single filled path (not an outline), which is why it survives
    reduction to a ~22x10px 1-bit pixel map far better than the earlier
    Material Symbols attempt below.
- **Vendored**: `components/ui/assets/lightning-charge-fill.svg`, unlike
  the Material Symbols glyph this replaces. At 431 bytes, vendoring costs
  nothing and is what makes the regeneration command below actually
  runnable by whoever reads it, rather than a command that only works if
  they also go re-fetch the source first.
- What ships as firmware: a derived artefact, not the SVG - a 22x10 1-bit
  pixel map, rasterised from the vendored file by `scripts/svg-to-bitmap.py`
  with `--rotate 90` (the upstream glyph is vertical; the tray needs it
  horizontal - rotation happens on the vector geometry before
  rasterisation, not on the bitmap afterwards, so it does not re-introduce
  jaggedness) `--width 22 --height 10 --threshold 0.35 --min-stroke 1`
  (the tool's stated default is `--min-stroke 2`; this glyph is a solid
  fill and the dilation that default applies detaches its own tapered tips
  into separate blobs rather than protecting a taper - see the comment
  above `kChargingBoltRows` for how that was confirmed), and checked in as
  the literal `kChargingBoltRows` array in `components/ui/ui_theme.hpp`,
  next to the exact regeneration command.

### Superseded: Material Symbols `bolt`

An earlier version of this icon rasterised Material Symbols Outlined's
standalone `bolt` glyph (Apache-2.0, `google/material-design-icons`) instead.
That glyph is no longer used anywhere in this repository - the operator
supplied `lightning-charge-fill` above instead, a solid filled shape that
holds up better at this size than Material's thinner outlined stroke did -
so there is nothing left here for that provenance entry to describe. Not
recorded further; if it is ever needed again, `git log -p` on this file
finds the entry this replaced.

## System tray icons

- Repository: <https://github.com/phosphor-icons/core>
- Pinned commit: `2b75f3ad12b420c9504ef05df8d2564a28f8500e`
- Licence: MIT, repository root `LICENSE`.
- Copyright: Copyright (c) 2023 Phosphor Icons.
- Upstream to vendored paths:
  - `assets/bold/speaker-high-bold.svg` → `components/ui/assets/speaker-high-bold.svg`
  - `assets/bold/airplay-bold.svg` → `components/ui/assets/airplay-bold.svg`
  - `assets/bold/wifi-high-bold.svg` → `components/ui/assets/wifi-high-bold.svg`
  - `assets/bold/wifi-slash-bold.svg` → `components/ui/assets/wifi-slash-bold.svg`
- All four are single-path, filled `256×256` SVGs.
- Raster parameters: `--width 20 --height 20 --fit viewbox
  --threshold 0.35 --min-stroke 1`.
- The checked-in firmware assets are tight-packed, row-major, MSB-first I1
  arrays. The SVG files are provenance and regeneration inputs only.
- Generated arrays live in `modules/audio/audio.cpp`,
  `modules/airplay/airplay.cpp`, and `components/ui/ui_theme.cpp`.
