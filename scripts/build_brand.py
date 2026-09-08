#!/usr/bin/env python3
"""Regenerate every PunPun brand asset from one geometry definition.

The mark is two interlocked geometric ``p`` glyphs -- a literal reading of the
``.pp`` file extension. The second glyph is separated from the first by an SVG
mask (not a painted-over shape), so the gap is genuinely transparent and the
mark sits correctly on any background.

Everything here is offline and deterministic: same input, same bytes out.

    python3 scripts/build_brand.py --repo-root .

Requires only Pillow (for the raster targets). Pass --svg-only to skip rasters.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# Palette -- taken from the badge colours already used across the project so
# the new marks match the existing README/site look instead of replacing it.
# --------------------------------------------------------------------------
LIME = "#b9ff4a"
CYAN = "#66e3ff"
INK = "#11151e"
PAPER = "#f6f7fa"
TILE_TOP = "#1a2130"
TILE_BOTTOM = "#0d1119"
TILE_EDGE = "#2b3648"

# Slightly darkened glyph colours for small file icons, which are viewed
# against light editor backgrounds where the bright pair vibrates.
LIME_SMALL = "#8fd82e"
CYAN_SMALL = "#29b6dd"

# --------------------------------------------------------------------------
# Geometry, in a 512x512 design space.
# --------------------------------------------------------------------------
TILE = 512
TILE_RADIUS = 112
STROKE = 40
BOWL_R = 60

# A lowercase ``p``: a stem with a descender, and a bowl hung off its top-left.
# Two of them, set side by side with an optical gap. The pair is deliberately
# NOT interlocked -- overlapping them eats the first bowl and the mark starts
# reading as "fp" at small sizes.
GLYPHS = (
    # stem_x, stem_top, stem_bottom, bowl_cx, bowl_cy
    (94, 138, 374, 154, 198),
    (298, 138, 374, 358, 198),
)

# Tight ink bounds of the pair, used to place it in tile-less icons.
GLYPH_BOX = (
    GLYPHS[0][0] - STROKE / 2,
    GLYPHS[0][1] - STROKE / 2,
    GLYPHS[-1][3] + BOWL_R + STROKE / 2,
    GLYPHS[-1][2] + STROKE / 2,
)

def glyph_paths(g) -> str:
    stem_x, top, bottom, cx, cy = g
    return (f'      <path d="M{stem_x} {top} V{bottom}"/>\n'
            f'      <circle cx="{cx}" cy="{cy}" r="{BOWL_R}"/>\n')


def mark_body(_unused: str = "", lime: str = LIME, cyan: str = CYAN) -> str:
    """The glyph pair. No masking: the glyphs are spaced, not overlapped."""
    return f"""  <g fill="none" stroke-linecap="round" stroke-linejoin="round" stroke-width="{STROKE}">
    <g stroke="{lime}">
{glyph_paths(GLYPHS[0])}    </g>
    <g stroke="{cyan}">
{glyph_paths(GLYPHS[1])}    </g>
  </g>
"""


TILE_DEFS = f"""  <defs>
    <linearGradient id="ppTile" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{TILE_TOP}"/>
      <stop offset="1" stop-color="{TILE_BOTTOM}"/>
    </linearGradient>
  </defs>
"""

TILE_SHAPE = f"""  <rect x="0" y="0" width="{TILE}" height="{TILE}" rx="{TILE_RADIUS}" fill="url(#ppTile)"/>
  <rect x="4" y="4" width="{TILE - 8}" height="{TILE - 8}" rx="{TILE_RADIUS - 4}" fill="none" stroke="{TILE_EDGE}" stroke-width="8"/>
"""

# Wordmark: stroked geometry rather than <text>, so it renders identically on
# every machine with no font embedded and no font installed.
# Wordmark letterforms, as centreline geometry. Origins are spaced so the ink
# gap between letters is ~18u against a 22u stroke; the first cut used a 4u gap
# and the word closed up into a blur below about 80px wide.
WORDMARK_ORIGINS = (11, 109, 193, 289, 387, 471)   # P u n P u n
WORDMARK_INK_W = 526
WORDMARK_INK_H = 122        # -11 .. 111 (cap top to baseline, plus stroke)


def _wordmark_letter(kind: str, x: int) -> str:
    if kind == "P":
        return (f'    <path d="M{x} 0 V100"/>\n'
                f'    <path d="M{x} 0 H{x + 30} A28 28 0 0 1 {x + 30} 56 H{x}"/>\n')
    if kind == "u":
        return (f'    <path d="M{x} 28 V78 A22 22 0 0 0 {x + 44} 78 V28"/>\n'
                f'    <path d="M{x + 44} 28 V100"/>\n')
    return f'    <path d="M{x} 100 V50 A22 22 0 0 1 {x + 44} 50 V100"/>\n'


WORDMARK_PATHS = "".join(
    _wordmark_letter(k, x) for k, x in zip("PunPun", WORDMARK_ORIGINS))
WORDMARK_STROKE = 22


def svg_mark() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {TILE} {TILE}" width="{TILE}" height="{TILE}" role="img" aria-label="PunPun">
  <title>PunPun</title>
{TILE_DEFS}{TILE_SHAPE}{mark_body("ppGap")}</svg>
"""


def svg_mark_flat() -> str:
    """Tile-less mark for favicons, terminal art and dark UI chrome."""
    x0, y0, x1, y1 = GLYPH_BOX
    pad = 16
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0 - pad} {y0 - pad} {x1 - x0 + 2 * pad} {y1 - y0 + 2 * pad}" role="img" aria-label="PunPun">
  <title>PunPun</title>
{mark_body("ppGapFlat")}</svg>
"""


def svg_wordmark() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="-16 -16 558 154" width="558" height="154" role="img" aria-label="PunPun">
  <title>PunPun</title>
  <g fill="none" stroke="currentColor" stroke-width="{WORDMARK_STROKE}" stroke-linecap="round" stroke-linejoin="round">
{WORDMARK_PATHS}  </g>
</svg>
"""


def svg_lockup(mode: str) -> str:
    """mode: 'auto' | 'light' | 'dark'."""
    if mode == "auto":
        style = f"""  <style>
    .wm {{ stroke: {INK}; }}
    @media (prefers-color-scheme: dark) {{ .wm {{ stroke: {PAPER}; }} }}
  </style>
"""
        wm_attr = 'class="wm"'
    else:
        style = ""
        wm_attr = f'stroke="{PAPER if mode == "dark" else INK}"'

    scale = 0.3515625      # 512 -> 180
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 220" width="660" height="220" role="img" aria-label="PunPun programming language">
  <title>PunPun programming language</title>
{style}{TILE_DEFS}  <g transform="translate(0 20) scale({scale})">
{TILE_SHAPE}{mark_body("ppGapLockup")}  </g>
  <g {wm_attr} transform="translate(229 70.6) scale(0.787)" fill="none" stroke-width="{WORDMARK_STROKE}" stroke-linecap="round" stroke-linejoin="round">
{WORDMARK_PATHS}  </g>
</svg>
"""


def svg_file_icon(theme: str) -> str:
    lime, cyan = (LIME_SMALL, CYAN_SMALL) if theme == "light" else (LIME, CYAN)
    x0, y0, x1, y1 = GLYPH_BOX
    pad = 24
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0 - pad} {y0 - pad} {x1 - x0 + 2 * pad} {y1 - y0 + 2 * pad}" width="32" height="32" role="img" aria-label="PunPun source file">
  <title>.pp source file</title>
{mark_body(f"ppGapFile{theme.capitalize()}", lime, cyan)}</svg>
"""


# --------------------------------------------------------------------------
# Raster rendering. Pillow only -- the geometry above is redrawn with
# primitives rather than rasterising the SVG, so no native SVG library
# is needed and the build works on a bare machine.
# --------------------------------------------------------------------------
SS = 8  # supersample factor


def _hex(c: str):
    c = c.lstrip("#")
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))


def _draw_glyph(layer, g, colour, stroke):
    from PIL import ImageDraw
    d = ImageDraw.Draw(layer)
    stem_x, top, bottom, cx, cy = (v * SS for v in g)
    w = stroke * SS
    r = w / 2
    # stem, drawn as a capsule so the caps are round like the SVG
    d.rectangle([stem_x - r, top, stem_x + r, bottom], fill=colour)
    d.ellipse([stem_x - r, top - r, stem_x + r, top + r], fill=colour)
    d.ellipse([stem_x - r, bottom - r, stem_x + r, bottom + r], fill=colour)
    # bowl: Pillow strokes inward from the bbox, so inflate by half a stroke
    outer = BOWL_R * SS + r
    d.ellipse([cx - outer, cy - outer, cx + outer, cy + outer],
              outline=colour, width=int(w))


def _glyph_pair(size_px=None, tile=False):
    """Render the glyph pair at design scale onto an RGBA canvas."""
    from PIL import Image
    n = TILE * SS
    layer = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    _draw_glyph(layer, GLYPHS[0], _hex(LIME) + (255,), STROKE)
    _draw_glyph(layer, GLYPHS[1], _hex(CYAN) + (255,), STROKE)
    return layer


def render_mark_png(size: int):
    """Tiled app icon at `size` px."""
    from PIL import Image, ImageDraw
    n = TILE * SS

    grad = Image.new("RGBA", (1, n))
    t0, t1 = _hex(TILE_TOP), _hex(TILE_BOTTOM)
    for y in range(n):
        f = y / (n - 1)
        grad.putpixel((0, y), tuple(round(a + (b - a) * f) for a, b in zip(t0, t1)) + (255,))
    grad = grad.resize((n, n))

    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, n - 1, n - 1],
                                           radius=TILE_RADIUS * SS, fill=255)
    tile = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    tile.paste(grad, (0, 0), mask)

    ImageDraw.Draw(tile).rounded_rectangle(
        [4 * SS, 4 * SS, n - 1 - 4 * SS, n - 1 - 4 * SS],
        radius=(TILE_RADIUS - 4) * SS, outline=_hex(TILE_EDGE) + (255,), width=8 * SS)

    tile.alpha_composite(_glyph_pair())
    from PIL import Image as _I
    return tile.resize((size, size), _I.LANCZOS)


def render_file_png(size: int):
    """Tile-less glyph pair on transparency, for file-manager icon sets."""
    from PIL import Image
    pair = _glyph_pair()
    x0, y0, x1, y1 = (round(v * SS) for v in GLYPH_BOX)
    pad = round(10 * SS)
    box = pair.crop((x0 - pad, y0 - pad, x1 + pad, y1 + pad))
    side = max(box.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.alpha_composite(box, ((side - box.width) // 2, (side - box.height) // 2))
    return sq.resize((size, size), Image.LANCZOS)



def _wm_line(d, x0, y0, x1, y1, colour, w):
    r = w / 2
    d.rectangle([min(x0, x1) - (0 if x0 != x1 else r), min(y0, y1) - (0 if y0 != y1 else r),
                 max(x0, x1) + (0 if x0 != x1 else r), max(y0, y1) + (0 if y0 != y1 else r)],
                fill=colour)
    for px, py in ((x0, y0), (x1, y1)):
        d.ellipse([px - r, py - r, px + r, py + r], fill=colour)


def _wm_arc(d, cx, cy, r, start, end, colour, w, bleed=4):
    # Overrun the arc a few degrees at each end so it fuses with the stems
    # instead of leaving a hairline notch at the join.
    outer = r + w / 2
    d.arc([cx - outer, cy - outer, cx + outer, cy + outer],
          start - bleed, end + bleed, fill=colour, width=int(round(w)))


def render_wordmark(layer, colour, k, ox, oy):
    """Draw the wordmark onto `layer`; k scales the 22u-stroke design space."""
    from PIL import ImageDraw
    d = ImageDraw.Draw(layer)
    w = WORDMARK_STROKE * k
    X = lambda v: ox + v * k
    Y = lambda v: oy + v * k

    for kind, x in zip("PunPun", WORDMARK_ORIGINS):
        if kind == "P":
            _wm_line(d, X(x), Y(0), X(x), Y(100), colour, w)
            _wm_line(d, X(x), Y(0), X(x + 30), Y(0), colour, w)
            _wm_line(d, X(x), Y(56), X(x + 30), Y(56), colour, w)
            _wm_arc(d, X(x + 30), Y(28), 28 * k, -90, 90, colour, w)
        elif kind == "u":
            _wm_line(d, X(x), Y(28), X(x), Y(78), colour, w)
            _wm_line(d, X(x + 44), Y(28), X(x + 44), Y(100), colour, w)
            _wm_arc(d, X(x + 22), Y(78), 22 * k, 0, 180, colour, w)
        else:
            _wm_line(d, X(x), Y(50), X(x), Y(100), colour, w)
            _wm_line(d, X(x + 44), Y(50), X(x + 44), Y(100), colour, w)
            _wm_arc(d, X(x + 22), Y(50), 22 * k, 180, 360, colour, w)


def render_lockup_png(width: int, mode: str = "dark", bg=None):
    """Full mark+wordmark lockup. mode picks the wordmark colour."""
    from PIL import Image
    vb_w, vb_h = 660, 220
    n_w, n_h = vb_w * SS, vb_h * SS
    canvas = Image.new("RGBA", (n_w, n_h), bg or (0, 0, 0, 0))

    tile = render_mark_png(180 * SS)
    canvas.alpha_composite(tile, (0, 20 * SS))

    colour = _hex(PAPER if mode == "dark" else INK) + (255,)
    render_wordmark(canvas, colour, 0.787 * SS, 228.7 * SS, 70.6 * SS)

    h = round(width * vb_h / vb_w)
    return canvas.resize((width, h), Image.LANCZOS)


def render_social_png(mode: str = "dark"):
    """1280x640 GitHub social-preview card."""
    from PIL import Image
    bg = _hex(TILE_BOTTOM if mode == "dark" else PAPER) + (255,)
    card = Image.new("RGBA", (1280, 640), bg)
    lock = render_lockup_png(880, mode)
    card.alpha_composite(lock, ((1280 - lock.width) // 2, (640 - lock.height) // 2))
    return card


# --------------------------------------------------------------------------

HICOLOR_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)


def write(path: Path, text: str, log):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    log(path)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo-root", default=".", type=Path)
    ap.add_argument("--svg-only", action="store_true",
                    help="skip raster targets (no Pillow needed)")
    args = ap.parse_args()
    root: Path = args.repo_root.resolve()

    written = []
    def log(p):
        written.append(p.relative_to(root) if p.is_relative_to(root) else p)

    a = root / "assets"
    v = root / "editors" / "vscode"

    write(a / "punpun-logo.svg", svg_lockup("auto"), log)
    write(a / "punpun-logo-light.svg", svg_lockup("light"), log)
    write(a / "punpun-logo-dark.svg", svg_lockup("dark"), log)
    write(a / "punpun-mark.svg", svg_mark(), log)
    write(a / "punpun-mark-flat.svg", svg_mark_flat(), log)
    write(a / "punpun-wordmark.svg", svg_wordmark(), log)
    write(v / "fileicons" / "pp-file-dark.svg", svg_file_icon("dark"), log)
    write(v / "fileicons" / "pp-file-light.svg", svg_file_icon("light"), log)

    if args.svg_only:
        print("\n".join(str(p) for p in written))
        return 0

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("build_brand: Pillow is required for raster output "
              "(pip install pillow), or pass --svg-only", file=sys.stderr)
        return 2

    def save(img, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        img.save(path, "PNG", optimize=True)
        log(path)

    # VS Code marketplace icon: must be PNG, 128x128 is the documented size.
    save(render_mark_png(128), v / "icon.png")
    save(render_mark_png(256), a / "punpun-mark-256.png")
    save(render_mark_png(512), a / "punpun-mark-512.png")
    save(render_mark_png(1024), a / "punpun-mark-1024.png")
    save(render_file_png(32), a / "favicon-32.png")
    save(render_file_png(16), a / "favicon-16.png")
    save(render_lockup_png(1200, "dark"), a / "punpun-logo-dark.png")
    save(render_lockup_png(1200, "light"), a / "punpun-logo-light.png")
    save(render_social_png("dark"), a / "punpun-social-preview.png")

    for s in HICOLOR_SIZES:
        save(render_file_png(s), a / "hicolor" / f"{s}x{s}" / "text-x-punpun.png")

    ico = a / "punpun.ico"
    base = render_mark_png(256)
    base.save(ico, "ICO", sizes=[(s, s) for s in ICO_SIZES])
    log(ico)

    fav = a / "favicon.ico"
    render_file_png(64).save(fav, "ICO", sizes=[(16, 16), (32, 32), (48, 48)])
    log(fav)

    print("\n".join(str(p) for p in written))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
