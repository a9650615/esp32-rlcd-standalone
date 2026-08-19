#!/usr/bin/env python3
"""Rasterises an SVG glyph to a 1-bit pixel map at a target size.

Why this exists
----------------
Hand-placing pixels for the tray's charging-bolt icon (a ~22x10px overlay)
failed three times in a row, each time the same way: someone reasoned about
where pixels should go without being able to see the result. A rasteriser
does not guess - point it at a vector source and the same shape comes out
every time, which is the actual argument for this tool. It is not that a
rasteriser beats hand-placed pixels on fidelity at this size (it may not);
it is that it is reproducible.

The pipeline (this is what font hinting does, not a coincidence):
  1. Supersample: sample the path at N times the target resolution
     (--supersample, default 16).
  2. Area-average: each destination pixel gets the fraction of its NxN
     source block that fell inside the shape, not a hard yes/no.
  3. Threshold: fractions >= --threshold become set pixels. 0.5 sounds like
     the obvious choice and is wrong for glyphs with tapered tips - a taper
     is mostly < 50% coverage right up to the point, so a 50% threshold
     lops the point off. Default here is 0.35: keeps tapers, at the cost of
     fattening thick strokes slightly. Lower it further if tips still
     vanish; raise it if the shape looks bloated.
  4. Minimum stroke width (--min-stroke, default 2): after thresholding, a
     taper can win down to a one-pixel-wide dotted run, which reads as a
     hairline or a crack rather than a stroke. Any run thinner than this
     gets grown back out (see _enforce_min_stroke for the actual rule).
  5. Print the result as ASCII, always, before anything is transcribed
     into firmware source. If it does not look like the glyph here, it
     will not look like it on the panel either.

Usage
-----
  python3 scripts/svg-to-bitmap.py path/to/glyph.svg --width 22 --height 10
  python3 scripts/svg-to-bitmap.py path/to/glyph.svg --width 22 --height 10 \\
      --threshold 0.3 --min-stroke 2 --emit-rows

  --emit-rows also prints a components/ui/ui_theme.cpp-style
  `constexpr std::string_view kFooRows[] = {...}` block, using the same
  'X' = ink / '.' = background convention as the existing hand-drawn
  charging-bolt pixel map, ready to paste in (after checking it against a
  screenshot, not instead of that).

  python3 scripts/svg-to-bitmap.py --selftest

What this has actually been used for
-------------------------------------
The tray's charging-bolt overlay (22x10px, `battery_fill_rect()` in
ui_theme.hpp): Material Symbols Outlined "bolt" rasterised at
--threshold 0.35 --min-stroke 2, the defaults above, which is why they are
the defaults rather than something narrower this file's own history
outgrew. See UPSTREAM.md and the comment above kChargingBoltRows in
ui_theme.cpp for the exact commit and the regeneration command. That is the
only icon converted so far - see the evaluation below for why.

Evaluation: is the rest of the hand-drawn icon set worth converting?
----------------------------------------------------------------------
Run once, deliberately, before converting anything beyond the bolt, so this
does not get redone from scratch. Rasterised real Material Symbols glyphs
("bolt", "battery_charging_full", "sunny") at every hand-drawn icon's actual
target size and read the ASCII, rather than reasoning about it:

  - Tray scale (10-22px: Wi-Fi 22x20, battery-bolt 22x10, audio 16x12,
    AirPlay 16x12) - **probable losses, not converted.** Material's outlined
    weight is tuned thin, for backlit displays at 20dp+; that thinness is
    exactly what has already failed twice on this panel (see
    kChargingBoltRows's own history). `battery_charging_full` at 22x10
    degenerates to a rectangle with a hole - its bolt is a notch inside the
    same path that draws the battery outline, not a separable shape.
  - 28x28 (home-tile leading visual: weather/temperature/humidity) -
    **borderline, not converted.** "sunny" at 28x28 keeps a legible disc but
    its 8 thin rays fragment at the corners - the precise failure
    draw_sun's own comment says its 4-fat-ray design exists to avoid.
  - 38-48px (weather page: 38x30 forecast icons, 48x46 main icon) -
    **the identified future candidate, not converted in this pass.**
    "sunny" at 48x46 renders clean and recognisable - arguably closer to a
    literal sun than the deliberately abstracted hand-drawn version. This is
    the size range where vector downscaling is expected to win outright;
    revisit `draw_sun`/`draw_cloud`/`draw_rain`/`draw_snow` in ui_theme.cpp
    here first if this gets picked up again.

Supported path syntax: M/m L/l H/h V/v C/c S/s Q/q T/t A/a Z/z - the
common subset every Material Symbols glyph in this repo's UPSTREAM.md so far
has used. Anything else raises rather than silently mis-rendering.
"""
import argparse
import re
import sys
from pathlib import Path as FsPath

import numpy as np

_TOKEN_RE = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?")


def _tokens(d: str):
    return _TOKEN_RE.findall(d)


def _floats(tokens, i, n):
    return [float(tokens[i + k]) for k in range(n)], i + n


def parse_svg_paths(svg_text: str):
    """Returns the list of `d` attribute strings from every <path> in the SVG."""
    return re.findall(r'<path[^>]*\bd="([^"]+)"', svg_text)


def parse_svg_viewbox(svg_text: str):
    """Returns the SVG's own `viewBox` as (min_x, min_y, width, height), or
    None if it has none. Used only by --fit viewbox (see rasterize()) - the
    default fit still ignores this entirely and fits to the path's ink
    bounding box, unchanged from before --fit existed."""
    m = re.search(r'viewBox="([^"]+)"', svg_text)
    if m is None:
        return None
    parts = [float(v) for v in m.group(1).replace(",", " ").split()]
    if len(parts) != 4:
        raise ValueError(f"expected 4 numbers in viewBox, got {m.group(1)!r}")
    return tuple(parts)


def _quad_to_points(p0, c, p1, segments=16):
    return [
        (
            (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * c[0] + t**2 * p1[0],
            (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * c[1] + t**2 * p1[1],
        )
        for t in (k / segments for k in range(1, segments + 1))
    ]


def _cubic_to_points(p0, c1, c2, p1, segments=16):
    pts = []
    for k in range(1, segments + 1):
        t = k / segments
        mt = 1 - t
        x = mt**3 * p0[0] + 3 * mt**2 * t * c1[0] + 3 * mt * t**2 * c2[0] + t**3 * p1[0]
        y = mt**3 * p0[1] + 3 * mt**2 * t * c1[1] + 3 * mt * t**2 * c2[1] + t**3 * p1[1]
        pts.append((x, y))
    return pts


def _arc_to_points(p0, rx, ry, x_rot_deg, large_arc, sweep, p1, segments=24):
    """SVG elliptical-arc endpoint parametrisation (spec F.6), flattened."""
    if rx == 0 or ry == 0:
        return [p1]
    phi = np.radians(x_rot_deg)
    cos_phi, sin_phi = np.cos(phi), np.sin(phi)
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1p = cos_phi * dx + sin_phi * dy
    y1p = -sin_phi * dx + cos_phi * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p**2 / rx**2 + y1p**2 / ry**2
    if lam > 1:
        s = lam**0.5
        rx, ry = rx * s, ry * s
    num = rx**2 * ry**2 - rx**2 * y1p**2 - ry**2 * x1p**2
    den = rx**2 * y1p**2 + ry**2 * x1p**2
    co = (max(num, 0) / den) ** 0.5 if den else 0.0
    if large_arc == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cos_phi * cxp - sin_phi * cyp + (p0[0] + p1[0]) / 2
    cy = sin_phi * cxp + cos_phi * cyp + (p0[1] + p1[1]) / 2

    def angle(ux, uy, vx, vy):
        sign = 1 if ux * vy - uy * vx >= 0 else -1
        dot = max(-1, min(1, (ux * vx + uy * vy) / (((ux**2 + uy**2) * (vx**2 + vy**2)) ** 0.5)))
        return sign * np.degrees(np.arccos(dot))

    theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dtheta > 0:
        dtheta -= 360
    elif sweep and dtheta < 0:
        dtheta += 360
    pts = []
    for k in range(1, segments + 1):
        t = np.radians(theta1 + dtheta * k / segments)
        pts.append(
            (
                cos_phi * rx * np.cos(t) - sin_phi * ry * np.sin(t) + cx,
                sin_phi * rx * np.cos(t) + cos_phi * ry * np.sin(t) + cy,
            )
        )
    return pts


def path_d_to_subpaths(d: str):
    """Flattens one SVG path `d` string into subpaths of (x, y) polyline points.

    Curves and arcs are flattened to line segments here rather than handed
    to matplotlib as curve codes, so the point-in-shape test downstream is
    always a plain polygon test - simpler to reason about, and what let the
    earlier hand-authored pixel maps avoid the self-intersecting-polygon
    artefacts a naive point-in-polygon fill can produce.
    """
    tokens = _tokens(d)
    subpaths = []
    cur = []
    x = y = 0.0
    start = (0.0, 0.0)
    last_cubic_ctrl = None
    last_quad_ctrl = None
    i = 0
    cmd = None
    while i < len(tokens):
        tok = tokens[i]
        if re.match(r"[A-Za-z]", tok):
            cmd = tok
            i += 1
        # else: repeated implicit command with the same letter, reuse cmd
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            (nx, ny), i = _floats(tokens, i, 2)
            if rel:
                nx, ny = x + nx, y + ny
            if cur:
                subpaths.append(cur)
            cur = [(nx, ny)]
            x, y = nx, ny
            start = (x, y)
        elif c == "L":
            (nx, ny), i = _floats(tokens, i, 2)
            if rel:
                nx, ny = x + nx, y + ny
            cur.append((nx, ny))
            x, y = nx, ny
        elif c == "H":
            ([nx], i) = _floats(tokens, i, 1)
            nx = x + nx if rel else nx
            cur.append((nx, y))
            x = nx
        elif c == "V":
            ([ny], i) = _floats(tokens, i, 1)
            ny = y + ny if rel else ny
            cur.append((x, ny))
            y = ny
        elif c == "C":
            (vals, i) = _floats(tokens, i, 6)
            x1, y1, x2, y2, nx, ny = vals
            if rel:
                x1, y1, x2, y2, nx, ny = x + x1, y + y1, x + x2, y + y2, x + nx, y + ny
            cur.extend(_cubic_to_points((x, y), (x1, y1), (x2, y2), (nx, ny)))
            last_cubic_ctrl = (x2, y2)
            x, y = nx, ny
        elif c == "S":
            (vals, i) = _floats(tokens, i, 4)
            x2, y2, nx, ny = vals
            if rel:
                x2, y2, nx, ny = x + x2, y + y2, x + nx, y + ny
            x1, y1 = (2 * x - last_cubic_ctrl[0], 2 * y - last_cubic_ctrl[1]) if last_cubic_ctrl else (x, y)
            cur.extend(_cubic_to_points((x, y), (x1, y1), (x2, y2), (nx, ny)))
            last_cubic_ctrl = (x2, y2)
            x, y = nx, ny
        elif c == "Q":
            (vals, i) = _floats(tokens, i, 4)
            x1, y1, nx, ny = vals
            if rel:
                x1, y1, nx, ny = x + x1, y + y1, x + nx, y + ny
            cur.extend(_quad_to_points((x, y), (x1, y1), (nx, ny)))
            last_quad_ctrl = (x1, y1)
            x, y = nx, ny
        elif c == "T":
            (vals, i) = _floats(tokens, i, 2)
            nx, ny = vals
            if rel:
                nx, ny = x + nx, y + ny
            x1, y1 = (2 * x - last_quad_ctrl[0], 2 * y - last_quad_ctrl[1]) if last_quad_ctrl else (x, y)
            cur.extend(_quad_to_points((x, y), (x1, y1), (nx, ny)))
            last_quad_ctrl = (x1, y1)
            x, y = nx, ny
        elif c == "A":
            (vals, i) = _floats(tokens, i, 3)
            rx, ry, xrot = vals
            large_arc = int(float(tokens[i]))
            sweep = int(float(tokens[i + 1]))
            i += 2
            (nx, ny), i = _floats(tokens, i, 2)
            if rel:
                nx, ny = x + nx, y + ny
            cur.extend(_arc_to_points((x, y), rx, ry, xrot, large_arc, sweep, (nx, ny)))
            x, y = nx, ny
        elif c == "Z":
            if cur:
                cur.append(start)
                subpaths.append(cur)
            cur = []
            x, y = start
        else:
            raise ValueError(f"unsupported path command '{cmd}'")
        if c not in ("C", "S"):
            last_cubic_ctrl = None
        if c not in ("Q", "T"):
            last_quad_ctrl = None
    if cur:
        subpaths.append(cur)
    return subpaths


def _winding_mask(subpaths, src_x: np.ndarray, src_y: np.ndarray) -> np.ndarray:
    """Nonzero-winding-rule containment test over a full sample grid.

    The standard signed-crossing-number ("winding number") algorithm - see
    e.g. Dan Sunday's wn_PnPoly - summed across *every* subpath's edges
    together, so a point is inside iff its total winding across the whole
    compound path is nonzero.

    This replaces an earlier version built on
    matplotlib.path.Path.contains_points(). That call's behaviour across
    multiple overlapping or nested subpaths is undocumented, and empirically
    does not implement either the even-odd or the nonzero-winding fill rule
    for a compound path with more than one subpath: a synthetic outer-CCW/
    inner-CW square (the standard nonzero-winding convention for punching a
    hole) still tests as filled at the hole's own centre - `p.contains_point`
    returns True where every real fill rule says False. For a *simple*
    (single, non-self-overlapping) path - every glyph this file has actually
    shipped, including the charging bolt - both rules and matplotlib's
    behaviour agree, so this change is not expected to move any existing
    output; see _selftest's nonzero-winding cases for the case where it
    would have mattered.

    `src_x`/`src_y` are 1-D coordinate arrays (already offset/scaled into the
    path's own coordinate space). Returns a (len(src_y), len(src_x)) bool
    grid, the same shape rasterize() previously built via
    mpl_path.contains_points(...).reshape(...).
    """
    gx, gy = np.meshgrid(src_x, src_y)
    winding = np.zeros(gx.shape, dtype=np.int32)
    for sp in subpaths:
        if len(sp) < 2:
            continue  # degenerate no-op subpath (Material glyphs emit these)
        ring = sp if sp[0] == sp[-1] else [*sp, sp[0]]
        xs = np.array([p[0] for p in ring])
        ys = np.array([p[1] for p in ring])
        for ex0, ey0, ex1, ey1 in zip(xs[:-1], ys[:-1], xs[1:], ys[1:]):
            if ey0 == ey1:
                continue  # horizontal edges never cross a horizontal ray
            is_left = (ex1 - ex0) * (gy - ey0) - (gx - ex0) * (ey1 - ey0)
            if ey0 < ey1:
                upward = (gy >= ey0) & (gy < ey1)
                winding += np.where(upward & (is_left > 0), 1, 0)
            else:
                downward = (gy >= ey1) & (gy < ey0)
                winding -= np.where(downward & (is_left < 0), 1, 0)
    return winding != 0


def _rotate_subpaths(subpaths, degrees: float):
    """Rotates every subpath point about the ink's own bbox centre.

    Applied to the vector geometry, before supersampling - rotating the
    final bitmap afterwards instead would just re-rasterise an
    already-rasterised (already-jagged) shape at an angle, restoring
    exactly the jaggedness supersampling exists to avoid. Rotating the path
    first means the one rasterisation pass sees the rotated geometry
    directly.

    This is a rotation, not a mirror: it turns a shape end over end, which
    only produces a left-right or top-bottom *mirror image* for a shape that
    already happens to have that symmetry - both glyphs this flag has been
    used on so far (see the module docstring) are close enough to
    180-degree-rotationally-symmetric that +90 and -90 came out
    byte-identical, which is a property of those two glyphs, not evidence
    that the sign controls mirroring in general. Use --flip-x/--flip-y (see
    _flip_subpaths below) for an actual reflection.
    """
    if not degrees:
        return subpaths
    pts = np.array([p for sp in subpaths if len(sp) >= 2 for p in sp])
    cx, cy = (pts.min(axis=0) + pts.max(axis=0)) / 2
    theta = np.radians(degrees)
    cos_t, sin_t = np.cos(theta), np.sin(theta)
    rotated = []
    for sp in subpaths:
        new_sp = []
        for x, y in sp:
            dx, dy = x - cx, y - cy
            new_sp.append((dx * cos_t - dy * sin_t + cx,
                          dx * sin_t + dy * cos_t + cy))
        rotated.append(new_sp)
    return rotated


def _flip_subpaths(subpaths, flip_x: bool, flip_y: bool):
    """Mirrors every subpath point about the ink's own bbox centre.

    A genuine reflection, unlike --rotate above: this is what actually
    produces a left-right (`flip_x`) or top-bottom (`flip_y`) mirror image
    for an arbitrary, asymmetric glyph, which rotation alone cannot.
    """
    if not flip_x and not flip_y:
        return subpaths
    pts = np.array([p for sp in subpaths if len(sp) >= 2 for p in sp])
    cx, cy = (pts.min(axis=0) + pts.max(axis=0)) / 2
    flipped = []
    for sp in subpaths:
        new_sp = [
            (2 * cx - x if flip_x else x, 2 * cy - y if flip_y else y)
            for x, y in sp
        ]
        flipped.append(new_sp)
    return flipped


def rasterize(svg_text: str, width: int, height: int, supersample: int = 16,
              margin: float = 0.0, rotate: float = 0.0, flip_x: bool = False,
              flip_y: bool = False, fit: str = "ink"):
    """Returns a (height, width) float array of per-pixel coverage in [0, 1].

    `fit` selects what rectangle gets scaled to fill width x height:
      - "ink" (default, unchanged from before this parameter existed): the
        path's own ink bounding box. Fine when the ink is the whole design;
        wrong if a glyph relies on declared padding to align against others
        on a shared grid.
      - "viewbox": the SVG's own declared `viewBox`, so that padding is
        honoured. Raises if the SVG has no viewBox.
    """
    ds = parse_svg_paths(svg_text)
    if not ds:
        raise ValueError("no <path d=...> found in SVG")
    subpaths = []
    for d in ds:
        subpaths.extend(path_d_to_subpaths(d))
    subpaths = _flip_subpaths(subpaths, flip_x, flip_y)
    subpaths = _rotate_subpaths(subpaths, rotate)

    if fit == "viewbox":
        viewbox = parse_svg_viewbox(svg_text)
        if viewbox is None:
            raise ValueError("--fit viewbox requested but the SVG has no viewBox")
        minx, miny, content_w, content_h = viewbox
        maxx, maxy = minx + content_w, miny + content_h
    elif fit == "ink":
        ink_pts = np.array([p for sp in subpaths if len(sp) >= 2 for p in sp])
        minx, miny = ink_pts.min(axis=0)
        maxx, maxy = ink_pts.max(axis=0)
        content_w, content_h = maxx - minx, maxy - miny
    else:
        raise ValueError(f"unknown fit {fit!r}, expected 'ink' or 'viewbox'")
    pad_x, pad_y = content_w * margin, content_h * margin
    minx, maxx = minx - pad_x, maxx + pad_x
    miny, maxy = miny - pad_y, maxy + pad_y
    content_w, content_h = maxx - minx, maxy - miny

    scale = min(width / content_w, height / content_h)
    drawn_w, drawn_h = content_w * scale, content_h * scale
    off_x, off_y = (width - drawn_w) / 2, (height - drawn_h) / 2

    n = supersample
    dest_x = (np.arange(width * n) + 0.5) / n  # [0, width), continuous target coords
    dest_y = (np.arange(height * n) + 0.5) / n
    src_x = minx + (dest_x - off_x) / scale
    src_y = miny + (dest_y - off_y) / scale
    inside = _winding_mask(subpaths, src_x, src_y)  # (height*n, width*n)

    coverage = inside.reshape(height, n, width, n).mean(axis=(1, 3))
    return coverage


def threshold(coverage: np.ndarray, level: float) -> np.ndarray:
    return coverage >= level


def _run_lengths_1d(row: np.ndarray) -> np.ndarray:
    """For each True element, the length of its contiguous run."""
    out = np.zeros(row.shape, dtype=int)
    i = 0
    n = len(row)
    while i < n:
        if not row[i]:
            i += 1
            continue
        j = i
        while j < n and row[j]:
            j += 1
        out[i:j] = j - i
        i = j
    return out


def _enforce_min_stroke(mask: np.ndarray, min_width: int, max_rounds: int = 4) -> np.ndarray:
    """Grows any run thinner than min_width, without touching runs already wide enough.

    This is the ponytail version of what font hinting calls stem darkening:
    a real implementation would grow along the local stroke direction only.
    Here, a pixel is "thin" if its run is short in *either* the horizontal or
    the vertical direction through it (catches thin horizontal bars via a
    short vertical run, and thin vertical bars via a short horizontal run).
    Only thin pixels get dilated (1-pixel isotropic growth), repeated for a
    bounded number of rounds - thick regions are left alone, so a shape that
    is already chunky everywhere comes back unchanged.

    ponytail: real font hinting also protects against a stem growing into a
    neighbour and merging two strokes into one. This one check below is that
    protection, in the cheapest form that still catches the failure rather
    than shipping it silently: dilation only ever adds pixels, so it can
    never split a shape (an isotropically-grown tapered tip can still visibly
    read as a detached blob dangling off a thin line even though it stays one
    connected region - that is a rendering-quality judgement call, not a
    topology fact, and no mechanical check here makes it; look at the ASCII
    output for that), but it *can* mechanically be proven to merge two
    previously-separate strokes into one - a drop in connected-component
    count - which is exactly what is checked for below, once per round,
    printed as a warning rather than left for someone to notice on the panel.
    """
    if min_width <= 1:
        return mask
    from scipy.ndimage import binary_dilation, label

    mask = mask.copy()
    for _ in range(max_rounds):
        h_runs = np.apply_along_axis(_run_lengths_1d, 1, mask)
        v_runs = np.apply_along_axis(_run_lengths_1d, 0, mask)
        thin = mask & ((h_runs > 0) & (h_runs < min_width) | (v_runs > 0) & (v_runs < min_width))
        if not thin.any():
            break
        _, components_before = label(mask)
        mask = mask | binary_dilation(thin)
        _, components_after = label(mask)
        if components_after < components_before:
            print(
                f"warning: --min-stroke {min_width} merged "
                f"{components_before - components_after} previously-separate "
                "stroke(s) into their neighbours while widening a thin run - "
                "inspect the ASCII output; lower --min-stroke if two strokes "
                "now touch that should not.",
                file=sys.stderr,
            )
    return mask


def to_ascii(mask: np.ndarray, ink="X", bg=".", double_rows=True) -> str:
    lines = ["".join(ink if v else bg for v in row) for row in mask]
    if double_rows:
        lines = [line for line in lines for _ in range(2)]
    return "\n".join(lines)


def to_cpp_rows(mask: np.ndarray, name: str, ink="X", bg=".") -> str:
    rows = ["".join(ink if v else bg for v in row) for row in mask]
    width = mask.shape[1]
    body = "\n".join(f'    "{r}",' for r in rows)
    return (
        f"constexpr std::string_view {name}[] = {{\n{body}\n}};\n"
        f"constexpr int {name}Width = {width};\n"
        f"constexpr int {name}Height = sizeof({name}) / sizeof({name}[0]);\n"
    )


def _selftest():
    # A 4x4 fully-inked square, sourced from a plain rectangle path.
    svg = '<svg viewBox="0 0 4 4"><path d="M0,0 L4,0 L4,4 L0,4 Z"/></svg>'
    cov = rasterize(svg, width=4, height=4, supersample=8)
    assert np.allclose(cov, 1.0), f"expected full coverage, got {cov}"

    # A thin 1px-tall horizontal bar across a 10x10 canvas: below-centre
    # rows must be empty, the bar row full, matching where the rect sits.
    svg2 = '<svg viewBox="0 0 10 10"><path d="M0,4 L10,4 L10,5 L0,5 Z"/></svg>'
    cov2 = rasterize(svg2, width=10, height=10, supersample=16)
    mask2 = threshold(cov2, 0.35)
    assert mask2[4].all() and not mask2[0].any() and not mask2[9].any()

    # min-stroke enforcement actually widens a 1px-thin run.
    thin = np.zeros((5, 5), dtype=bool)
    thin[2, :] = True
    grown = _enforce_min_stroke(thin, min_width=2)
    # every ink column must now be at least 2 rows tall somewhere in it
    assert all(grown[:, c].sum() >= 2 for c in range(5) if thin[:, c].any())

    # arc parsing: a full circle path (two semicircular arcs) covers its centre.
    svg3 = '<svg viewBox="-5 -5 10 10"><path d="M-5,0 A5,5 0 1 1 5,0 A5,5 0 1 1 -5,0 Z"/></svg>'
    cov3 = rasterize(svg3, width=10, height=10, supersample=16)
    assert cov3[5, 5] > 0.5, "circle centre should be inked"

    # A path made only of small-radius corner arcs (rx=ry=1, large-arc=0,
    # sweep=1 - the same flag pattern the vendored lightning-charge-fill.svg
    # uses for its own rounded corners) must rasterise to one solid,
    # connected region even squeezed down to a small, non-square target -
    # not fragments. A real bug here (in the arc math, or in something
    # downstream of it) produced exactly that once: a shape that looked
    # fine at a generous size but split into disconnected blobs at the
    # actual ~22x10 tray-icon target, which this catches without needing a
    # 22x10-specific fixture.
    from scipy.ndimage import label
    svg5 = ('<svg viewBox="0 0 20 10"><path d="M1,0 L19,0 A1,1 0 0 1 20,1 '
           'L20,9 A1,1 0 0 1 19,10 L1,10 A1,1 0 0 1 0,9 L0,1 '
           'A1,1 0 0 1 1,0 Z"/></svg>')
    cov5 = rasterize(svg5, width=22, height=10, supersample=16)
    mask5 = threshold(cov5, 0.35)
    _, num_components = label(mask5)
    assert num_components == 1, (
        f"expected one connected region from an all-arc rounded rect, got "
        f"{num_components}:\n{to_ascii(mask5, double_rows=False)}")
    assert mask5.sum() > 0.5 * mask5.size, (
        f"rounded rect should stay mostly filled, only {mask5.sum()} of "
        f"{mask5.size} pixels inked")

    # --rotate: a wide-and-short rectangle (occupying the left 80% of a
    # square viewBox, so rotation actually moves it) rasterised with
    # rotate=90 should come out tall-and-narrow, occupying the *top* 80% -
    # proof this rotates the geometry (and therefore the fit) rather than
    # being a no-op or a plain reflection.
    svg4 = '<svg viewBox="0 0 10 10"><path d="M0,4 L8,4 L8,6 L0,6 Z"/></svg>'
    cov4 = rasterize(svg4, width=10, height=10, supersample=8, rotate=90)
    row_ink = cov4.sum(axis=1) > 0.5
    col_ink = cov4.sum(axis=0) > 0.5
    assert row_ink.sum() > col_ink.sum(), (
        f"expected a tall-narrow result after rotating a wide-short "
        f"rectangle 90 degrees, got row_ink={row_ink}, col_ink={col_ink}")

    # Nonzero winding: a genuine hole (an inner subpath wound *opposite* the
    # outer one - the standard authoring convention for a hole) must render
    # as a hole, and a nested subpath wound the *same* direction (no hole
    # intended, just two overlapping solid regions) must not - proving both
    # directions of the rule are actually applied, not an accidental union
    # of independent subpaths regardless of winding (see _winding_mask's own
    # comment for the matplotlib behaviour this replaced, which passed
    # neither case).
    hole_svg = ('<svg viewBox="0 0 10 10"><path d="M0,0 L10,0 L10,10 L0,10 Z '
               'M3,3 L3,7 L7,7 L7,3 Z"/></svg>')
    mask_hole = threshold(rasterize(hole_svg, width=10, height=10, supersample=16), 0.5)
    assert not mask_hole[5, 5], "opposite-winding inner subpath should punch a hole"
    assert mask_hole[1, 1], "outside the hole, the outer square should stay solid"

    union_svg = ('<svg viewBox="0 0 10 10"><path d="M0,0 L10,0 L10,10 L0,10 Z '
                'M3,3 L7,3 L7,7 L3,7 Z"/></svg>')
    mask_union = threshold(rasterize(union_svg, width=10, height=10, supersample=16), 0.5)
    assert mask_union[5, 5], "same-winding nested subpath must not punch a false hole"

    # Dilation merging two separate strokes must announce itself rather than
    # silently changing the glyph's topology: two thin horizontal bars one
    # row apart have nowhere to grow but into each other once --min-stroke 3
    # demands each be at least 3px tall.
    import contextlib
    import io

    bars_close = np.zeros((5, 6), dtype=bool)
    bars_close[1, :] = True
    bars_close[3, :] = True
    stderr = io.StringIO()
    with contextlib.redirect_stderr(stderr):
        merged = _enforce_min_stroke(bars_close, min_width=3)
    assert "merged" in stderr.getvalue(), (
        "expected a merge warning when dilation joins two close strokes, got "
        f"mask:\n{to_ascii(merged, double_rows=False)}")
    _, merged_components = label(merged)
    assert merged_components == 1, "the two bars should actually be one region now"

    # The same idea, far enough apart that --min-stroke 2 cannot reach from
    # one bar to the other: must not warn.
    bars_far = np.zeros((9, 6), dtype=bool)
    bars_far[1, :] = True
    bars_far[7, :] = True
    stderr_far = io.StringIO()
    with contextlib.redirect_stderr(stderr_far):
        _enforce_min_stroke(bars_far, min_width=2)
    assert stderr_far.getvalue() == "", (
        f"unexpected merge warning for bars that should stay separate: "
        f"{stderr_far.getvalue()!r}")

    # --fit viewbox: an SVG whose viewBox declares padding well beyond its
    # ink (a small rect centred in a 10x10 declared box) should, under
    # --fit viewbox, keep that padding rather than being stretched to fill
    # the frame - the default ('ink') fit is unchanged, still stretching to
    # the ink bounding box exactly as before this option existed.
    padded_svg = '<svg viewBox="0 0 10 10"><path d="M4,4 L6,4 L6,6 L4,6 Z"/></svg>'
    mask_ink_fit = threshold(
        rasterize(padded_svg, width=10, height=10, supersample=16, fit="ink"), 0.5)
    mask_viewbox_fit = threshold(
        rasterize(padded_svg, width=10, height=10, supersample=16, fit="viewbox"), 0.5)
    assert mask_ink_fit.all(), "fit=ink should stretch the ink to fill the frame"
    assert not mask_viewbox_fit[0, 0] and not mask_viewbox_fit[9, 9], (
        "fit=viewbox should keep the declared padding instead of stretching "
        "the ink into the corners")
    assert mask_viewbox_fit[5, 5], "fit=viewbox should still ink the rect's own area"

    try:
        rasterize('<svg><path d="M0,0 L1,0 L1,1 L0,1 Z"/></svg>',
                 width=4, height=4, fit="viewbox")
        raise AssertionError("expected --fit viewbox to raise without a viewBox")
    except ValueError:
        pass

    # --flip-x/--flip-y: an L-shape (a tall stroke on the left, a foot along
    # the bottom) is asymmetric about its own centre in both axes, so a
    # correct reflection - and only a reflection, not the rotation --rotate
    # provides - moves the stroke/foot to the opposite side.
    l_svg = ('<svg viewBox="0 0 10 10"><path d="M0,0 L2,0 L2,8 L10,8 L10,10 '
            'L0,10 Z"/></svg>')
    mask_plain = threshold(rasterize(l_svg, width=10, height=10, supersample=16), 0.5)
    mask_flip_x = threshold(
        rasterize(l_svg, width=10, height=10, supersample=16, flip_x=True), 0.5)
    mask_flip_y = threshold(
        rasterize(l_svg, width=10, height=10, supersample=16, flip_y=True), 0.5)
    # Row 1 sits above the foot, so only the left-hand stroke shows there.
    assert mask_plain[1, 1] and not mask_plain[1, 8]
    assert mask_flip_x[1, 8] and not mask_flip_x[1, 1], "flip-x should mirror left-right"
    # Column 8 sits right of the stroke, so only the bottom foot shows there.
    assert mask_plain[8, 8] and not mask_plain[1, 8]
    assert mask_flip_y[1, 8] and not mask_flip_y[8, 8], "flip-y should mirror top-bottom"

    print("selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("svg", nargs="?", help="path to an SVG file")
    ap.add_argument("--width", type=int)
    ap.add_argument("--height", type=int)
    ap.add_argument("--supersample", type=int, default=16)
    ap.add_argument("--threshold", type=float, default=0.35,
                     help="coverage fraction to set a pixel (default 0.35; "
                          "0.5 drops tapered tips)")
    ap.add_argument("--min-stroke", type=int, default=2,
                     help="minimum run width in pixels; thinner runs are "
                          "dilated back out (default 2)")
    ap.add_argument("--margin", type=float, default=0.0,
                     help="fraction of content size to pad on each side "
                          "before fitting to width/height")
    ap.add_argument("--rotate", type=float, default=0.0,
                     help="degrees to rotate the geometry before "
                          "rasterising (about the ink's own bbox centre) - "
                          "not a post-hoc bitmap rotation, see "
                          "_rotate_subpaths()'s own comment for why. This "
                          "rotates; it does not mirror. +90/-90 can print "
                          "identically for a glyph with rotational symmetry "
                          "(as both glyphs this tool has shipped happen to "
                          "have) - that is not evidence sign controls "
                          "mirroring in general. Use --flip-x/--flip-y for "
                          "an actual reflection")
    ap.add_argument("--flip-x", action="store_true",
                     help="mirror the geometry left-right before rasterising")
    ap.add_argument("--flip-y", action="store_true",
                     help="mirror the geometry top-bottom before rasterising")
    ap.add_argument("--fit", choices=["ink", "viewbox"], default="ink",
                     help="what rectangle fills width x height: the path's "
                          "own ink bounding box (default, unchanged "
                          "behaviour) or the SVG's declared viewBox (honours "
                          "the source's own padding/alignment)")
    ap.add_argument("--emit-rows", metavar="NAME",
                     help="also print a kNAMERows-style C++ literal block")
    ap.add_argument("--no-double", action="store_true",
                     help="print ASCII one line per row (default doubles "
                          "each row so it reads roughly square in a terminal)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        _selftest()
        return

    if not args.svg or not args.width or not args.height:
        ap.error("svg, --width and --height are required unless --selftest")

    svg_text = FsPath(args.svg).read_text()
    coverage = rasterize(svg_text, args.width, args.height, args.supersample,
                         args.margin, args.rotate, args.flip_x, args.flip_y,
                         args.fit)
    mask = threshold(coverage, args.threshold)
    mask = _enforce_min_stroke(mask, args.min_stroke)

    print(f"# {args.width}x{args.height}, supersample={args.supersample}, "
          f"threshold={args.threshold}, min_stroke={args.min_stroke}, "
          f"rotate={args.rotate}")
    print(to_ascii(mask, double_rows=not args.no_double))
    if args.emit_rows:
        print()
        print(to_cpp_rows(mask, args.emit_rows))


if __name__ == "__main__":
    sys.exit(main())
