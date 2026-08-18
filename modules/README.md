# Modules

`components/` is the firmware: the display, clock, sensors, battery, portal,
and OTA. Remove any one of those and the board stops being this board.

`modules/` holds optional capabilities instead - things the board can do
without, compiled out entirely when they are not wanted. Audio (a codec/tone
output for alarms and notifications) is the first one. AirPlay is planned as
a second, once its RAOP handshake dependency (Apple's extracted AirPort
Express RSA private key) has a build-time story that keeps the key itself out
of this public repository - see the table below.

## What qualifies as a module

If disabling it leaves the display, clock, sensors, portal, and OTA behaving
identically - same boot sequence, same panel output, same update path - it is
a module. If the firmware would be a materially different thing without it,
it is a core component and belongs in `components/`.

## The rules

Every module in this directory follows all five of these. They are what makes
"optional" actually mean optional, rather than a Kconfig option that quietly
still costs flash and RAM whether or not anyone turns it on.

1. **Own `Kconfig`**, named `CONFIG_<NAME>_ENABLE`. Each module states its own
   default and explains why that default was chosen - `y` is fine for a
   feature that is cheap and harmless when idle (audio's amplifier defaults
   off and never runs unless asked), `n` is right for anything with a real
   cost, risk, or missing dependency (this is why the planned `airplay`
   module below will default `n`: the RSA key it needs is not in this repo).

2. **Off means nothing in the binary**, not a runtime `if`. Sources compile
   out of `SRCS` entirely; any managed component-registry dependency the
   module pulls in is gated the same way its own sources are, so a build with
   the option off links none of it. This is a testable claim, not a
   description - see the acceptance test below.

3. **Public header in `include/`**, with inline no-op stubs for every function
   when the option is off. A caller in `main/app_main.cpp` or elsewhere in
   `components/` never writes an `#ifdef` of its own - it calls the function,
   and gets either the real behavior or a harmless no-op, decided once, in
   the module's own header.

4. **Dependencies point one way: module -> core.** A module may include
   `board_rlcd` or any other core header it needs. No core component may
   ever include a module header. If a module needs to put something on the
   panel or in the UI, that is a registration/callback API core exposes to
   it - never an include running the other direction. If you find yourself
   wanting `ui` or `app_core` to know about a module, that is the signal to
   stop and design that API rather than reach for an include.

5. **Every module names its core touch points explicitly**, in its own
   `modules/<name>/README.md`. Not "wired into app_main" - the exact call,
   the exact file, the exact line's worth of context. For `audio`, three:
   one `audio_init()` call in `main/app_main.cpp`, the `POST /beep`/
   `POST /beep-sweep` routes in `components/wifi_provision/portal.cpp`, and
   `audio_init()`'s own `app_core::register_tray_indicator()` call plus
   `write_tone_step()`'s `app_core::set_tray_indicator_active()` calls, both
   in `audio.cpp` (see below). A reviewer should be able to grep for exactly
   those things and see the entire footprint a module has on core.

   That last pair is also the answer to "how does a module get something
   onto the panel without an include running module -> core -> ui": it
   doesn't reach for the UI at all, and it needs no handler indirection back
   through a component that depends on it either. `app_core::tray_registry`
   (`components/app_core/include/tray_registry.hpp`) is a fixed-capacity
   registry - `kMaxTrayIndicators` slots, no dynamic allocation - that any
   module may call directly, because `app_core` has no dependents of its
   own to make that circular. A module registers once, at init, with its
   own icon as **data**: a 1-bit bitmap plus width and height
   (`app_core::TrayIndicatorBitmap`), not code - core has no per-module
   drawing switch, it renders whatever bitmap it was handed onto an LVGL
   canvas. The call returns a `TrayIndicatorHandle`, and the module flips
   its own indicator on and off through that handle from wherever its own
   activity actually starts and stops - `audio` does this around GPIO46 in
   `write_tone_step()`. `app_core` and `ui` never learn a module exists,
   let alone what it is called; they just draw whatever bitmap each
   registered-and-active slot holds. When a second module (a future
   AirPlay module, say) wants a tray icon, it registers its own bitmap and
   gets its own handle - core does not change by a single line, and there
   is no enum anywhere for it to add itself to. `modules/audio/README.md`
   covers exactly what `audio` registers and when.

## The acceptance test

Building with every module's `_ENABLE` off must return the binary to the core
baseline: **1,541,040 bytes** (`0x1783b0`), measured 2026-08-18.

That is not the 1,525,840 bytes core measured before any module existed, nor
the 1,528,048 bytes recorded on 2026-08-17 after the first module (audio) and
the tray indicator registry landed - it is 12,992 bytes larger than that
2026-08-17 figure, and that growth is legitimate, not a leak, for reasons
that are both worth naming rather than folding silently into a bigger
number:

- **The debug-only `/beep`/`/beep-sweep` route glue in
  `components/wifi_provision/portal.cpp`** exists in every debug build
  regardless of `CONFIG_AUDIO_ENABLE` - it is core's side of the
  registration/callback boundary the module contract requires (rule 4), and
  it costs a few hundred bytes of query parsing and log strings whether or
  not the module underneath it is compiled in.
- **The tray indicator registry** (`app_core::tray_registry` - the fixed
  four-slot registration API, its diagnostic state-change logging in
  `components/ui/render_shared.cpp`, and the generic LVGL-canvas rendering
  in `ui_theme.cpp`) lives entirely in core by design - core draws whatever
  bitmap a registered-and-active slot holds without knowing which module,
  if any, owns it. This is core growing to host a real, module-agnostic
  feature, not module code leaking across the boundary; with
  `CONFIG_AUDIO_ENABLE=n` the registry still exists but nothing ever calls
  `register_tray_indicator()`, so it costs a few hundred bytes of dead
  capacity and draws nothing.
- **A second module's worth of the same registration glue, for AirPlay**
  (`main/app_main.cpp` calling `airplay::airplay_init()` unconditionally,
  plus the internal-RAM diagnostics and log-transport-ordering fixes that
  went with it) - the same rule-4 boundary as the first bullet, just paid
  twice now that there are two modules to register and log around
  regardless of whether either is compiled in.
- **The `i1_canvas_*` helpers in `ui_theme.cpp`** (`bind_i1_canvas`,
  `i1_canvas_stride`, `i1_canvas_pixel_offset`) that the charging-icon work
  factored out so the tray indicator, the battery-charging composite, and
  the `/dither-card` debug screen all size and address their 1bpp LVGL
  canvases the same correct way instead of three copies of a
  `(width + 7) / 8` formula that drifted from LVGL's own stride and palette
  layout on real hardware. This is core UI infrastructure, not
  module-specific.
- **The "show what changed" release-notes feature** (`components/market`'s
  `market.cpp`/`market_parse.cpp` and the new `market_schedule.cpp`,
  `components/ota`'s `ota_release.cpp`/`ota_pull.cpp`, and the
  `wifi_provision` routes and UI screens that surface it) - a real core
  feature for fetching and displaying release notes from a trusted source,
  plus the fix that kept its ASCII gate from rejecting valid punctuation.
  None of it is reachable through, or gated by, either module's `_ENABLE`.

A meaningful difference from 1,541,040 bytes beyond what a legitimate core
change like the ones above accounts for means something leaked into core - a
stray include, a symbol referenced outside the module's own files, a Kconfig
default that doesn't actually gate what it claims to. That is a bug to find
and fix, not a difference to explain away in a commit message.

One caveat on how much precision to read into that, found while re-measuring
this: rebuilding the exact commit that recorded 1,528,048 does not reproduce
it. It builds to 1,531,104 - 3,056 bytes larger - with the same toolchain,
the same ESP-IDF, and `idf_component.yml` pinning every dependency to an
exact version, so registry drift, the app version string, and the compiler
were all ruled out and the cause is still unknown. Two orders of magnitude
below the growth this section is meant to catch, so it does not undermine
the test, but it does mean a difference of a few thousand bytes is not by
itself evidence of anything. Treat this figure as reproducible to about
3 KB until someone finds the reason it is not exact.

Note also which configuration produces it: **every** module off, which today
means `CONFIG_AUDIO_ENABLE=n` as well as `CONFIG_AIRPLAY_ENABLE=n`. Measuring
with audio left on overstates core by roughly 59 KB - the audio module's own
footprint, counted as if it were core. That mistake has already been made
once and it looked exactly like a 72 KB leak.

To re-measure the baseline after core legitimately grows (a new core
component, a bigger font, and so on): set every `<NAME>_ENABLE` off, run
`./scripts/idf.sh build`, and read the binary size line at the end of the
build. Update the number above and the date in the same change that grew
core, so the baseline never drifts out of sync with what core actually is.

## Adding a new module

1. `mkdir modules/<name>` with `<name>.cpp`, `include/<name>.hpp`,
   `CMakeLists.txt`, `Kconfig.projbuild`, `idf_component.yml` if it needs a
   managed dependency, and its own `README.md`.
2. `Kconfig.projbuild`: one `config <NAME>_ENABLE` block, default stated and
   justified.
3. `CMakeLists.txt`: build the `SRCS` list conditionally on
   `CONFIG_<NAME>_ENABLE` (empty when off); keep `PRIV_REQUIRES` unconditional
   - see the comment in `modules/audio/CMakeLists.txt` for why: ESP-IDF
   resolves a component's REQUIRES graph in an early pass that does not
   reliably see per-component Kconfig choices, so conditioning REQUIRES
   itself can silently drop them from the build graph even though SRCS
   correctly follows the Kconfig value. Gating only SRCS still removes every
   line of the module's code from the final link, because nothing left
   references the ungated component's symbols and the linker never pulls
   those archive members in.
4. `include/<name>.hpp`: real declarations under `#ifdef CONFIG_<NAME>_ENABLE`,
   inline no-op stubs in the `#else` branch. Every function needs a stub.
5. Register `<name>` as an unconditional `PRIV_REQUIRES` wherever it is
   called from core (it always exists as a component; whether it does
   anything is the Kconfig's decision, not the caller's).
6. Build both ways (`_ENABLE=y` and `=n`) and confirm the `=n` size matches
   the current core baseline above.
7. Write `modules/<name>/README.md` and add the module to the table below.

## Current modules

| Module | Kconfig symbol | Default | Purpose |
| --- | --- | --- | --- |
| `audio` | `CONFIG_AUDIO_ENABLE` | `y` | ES8311 codec + speaker: short tones for alarms/notifications, triggered locally or over `POST /beep` |
| `airplay` | `CONFIG_AIRPLAY_ENABLE` | `n` | AirPlay 1 (RAOP) receiver feeding `audio`'s streaming sink and its own tray indicator; needs a builder-supplied RSA key at `modules/airplay/secrets/raop_private_key.pem` (gitignored, never shipped) - see `modules/airplay/README.md` |
