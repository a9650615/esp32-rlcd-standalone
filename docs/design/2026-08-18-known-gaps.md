# Known gaps, 2026-08-18

Things found while getting the charging icon onto the panel that are real but
were deliberately not fixed at the time. Recorded because the context that
found them is gone; each one is actionable on its own.

## The palette bug is still latent in the shared helper

`bind_i1_canvas()` (`components/ui/ui_theme.cpp`) never applies a transparent
background to the canvas object itself — it leaves `apply_surface()`'s default
of opaque white.

Both current callers happen to be fine: the battery overlay wants opaque white,
and the tray indicators sit on white anyway. But this is exactly the shape of
the bug that cost a whole session — an opaque object background silently
masking what a "transparent" palette entry was supposed to reveal. The next
caller that draws over something which is not white will hit it, and it will
present as "the bitmap is blank", which is the least informative symptom
possible.

Fixing it means honouring the background opacity the caller asks for, not
blindly setting transparent: the battery overlay depends on the current
behaviour.

## scripts/svg-to-bitmap.py

Four limitations, none of which announce themselves:

- **`--rotate`'s sign does not control mirroring.** `90` and `-90` produced
  byte-identical output for both glyphs tried, because both happen to have
  near-180° rotational symmetry. The regeneration comment in `ui_theme.hpp`
  implies more directional control than the flag gives for an arbitrary glyph.
- **Dilation can damage a shape.** `--min-stroke` grows in every direction, so
  it detaches a solid glyph's tapered tips into blobs — which is why the
  shipped bolt is generated with `--min-stroke 1`. It can also merge two
  distinct nearby strokes. The self-test uses a plain rounded rect, which has
  neither a taper to detach nor a neighbour to merge with, so it catches
  neither.
- **Point-in-path is even-odd, not nonzero winding.** A glyph relying on
  same-direction overlapping subpaths will rasterise with holes where it should
  be solid, silently.
- **The viewBox is ignored**; the fit is to the path's ink bounding box. Fine
  when the ink is the whole design, wrong if a glyph relies on declared padding
  to align against others on a shared grid.

## Silent no-ops instead of build failures

`g_charging_bolt_bitmap` and `g_tray_indicator_storage` are sized by a
hardcoded cap. An icon exceeding it is rejected at runtime by the bounds
checks rather than failing to compile. This matches the file's existing
fixed-backing-store convention, but everywhere else in this codebase geometry
overruns are caught by `static_assert`, and this is not.

## Still on the old canvas construction

`render_dither_card.cpp`'s `swatch_canvas()` writes pixels from byte zero,
the same defect that was fixed in `tray_indicator_icon()` and the battery
overlay. It is debug-only, so it was left alone; whatever it renders is
whatever survives the palette being overwritten.

## AirPlay: what to check first, and what will probably break

Nothing in `modules/airplay` has ever run. When the RSA key is supplied, check
in this order, because each step is observable before the next becomes
meaningful:

1. It builds and flashes with the real key — mechanically identical to the
   throwaway-key path.
2. `raop_init()` comes up and the device appears in a sender's AirPlay picker.
   That is mDNS working, observable before any client connects.
3. A real sender completes RTSP SETUP: the tray indicator lights and
   `RAOP_EVENT_CONNECTED` fires. `CONNECTED`/`DISCONNECTED` are the only two
   events wired to anything, so this is the highest-value checkpoint.
4. Sound actually reaches the speaker.
5. Only then tune the buffer depth (384 frames, about 3 s) against real Wi-Fi
   jitter, and watch PSRAM (about 1.3 MB, allocated at RTSP SETUP) under load
   from the other modules.

The prediction, worth writing down so it can be proved wrong: the first thing
to break will not be the ALAC decode but the RSA session-key decrypt. It is
the one path that has never executed even once — a real sender rejects the
challenge response against a throwaway key, so nothing downstream of it has
ever been reached.
