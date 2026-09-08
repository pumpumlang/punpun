#!/usr/bin/env python3
"""Regenerate every PunPun brand asset from one PP geometry definition.

The mark is a pair of deliberately separated geometric lowercase ``p`` glyphs,
a literal reading of the ``.pp`` source extension.  The two glyphs do not
interlock: keeping an optical gap preserves the PP silhouette at favicon and
file-icon sizes.

All outputs are generated offline from this file.  Run it from any directory:

    python3 scripts/build_brand.py --repo-root .

Pillow is required for raster/ICO output.  ``--svg-only`` updates only vectors.
"""
from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path

LIME = "#b9ff4a"
CYAN = "#66e3ff"
INK = "#11151e"
PAPER = "#f6f7fa"
TILE_TOP = "#1a2130"
TILE_BOTTOM = "#0d1119"
TILE_EDGE = "#2b3648"
LIME_SMALL = "#8fd82e"
CYAN_SMALL = "#29b6dd"

TILE = 512
TILE_RADIUS = 112
STROKE = 40
BOWL_R = 60
GLYPHS = (
    (94, 138, 374, 154, 198),
    (298, 138, 374, 358, 198),
)
GLYPH_BOX = (
    GLYPHS[0][0] - STROKE / 2,
    GLYPHS[0][1] - STROKE / 2,
    GLYPHS[-1][3] + BOWL_R + STROKE / 2,
    GLYPHS[-1][2] + STROKE / 2,
)
WORDMARK_ORIGINS = (11, 109, 193, 289, 387, 471)
WORDMARK_STROKE = 22
HICOLOR_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
MIME_ALIAS_SIZES = (16, 32, 64, 128, 256, 512)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
SS = 8


def _hex(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def _glyph_paths(glyph) -> str:
    stem_x, top, bottom, cx, cy = glyph
    return (
        f'      <path d="M{stem_x} {top} V{bottom}"/>\n'
        f'      <circle cx="{cx}" cy="{cy}" r="{BOWL_R}"/>\n'
    )


def mark_body(lime: str = LIME, cyan: str = CYAN) -> str:
    return f'''  <g fill="none" stroke-linecap="round" stroke-linejoin="round" stroke-width="{STROKE}">
    <g stroke="{lime}">
{_glyph_paths(GLYPHS[0])}    </g>
    <g stroke="{cyan}">
{_glyph_paths(GLYPHS[1])}    </g>
  </g>
'''


TILE_DEFS = f'''  <defs>
    <linearGradient id="ppTile" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{TILE_TOP}"/>
      <stop offset="1" stop-color="{TILE_BOTTOM}"/>
    </linearGradient>
  </defs>
'''
TILE_SHAPE = f'''  <rect x="0" y="0" width="{TILE}" height="{TILE}" rx="{TILE_RADIUS}" fill="url(#ppTile)"/>
  <rect x="4" y="4" width="{TILE - 8}" height="{TILE - 8}" rx="{TILE_RADIUS - 4}" fill="none" stroke="{TILE_EDGE}" stroke-width="8"/>
'''


def _wordmark_letter(kind: str, x: int) -> str:
    if kind == "P":
        return (
            f'    <path d="M{x} 0 V100"/>\n'
            f'    <path d="M{x} 0 H{x + 30} A28 28 0 0 1 {x + 30} 56 H{x}"/>\n'
        )
    if kind == "u":
        return (
            f'    <path d="M{x} 28 V78 A22 22 0 0 0 {x + 44} 78 V28"/>\n'
            f'    <path d="M{x + 44} 28 V100"/>\n'
        )
    return f'    <path d="M{x} 100 V50 A22 22 0 0 1 {x + 44} 50 V100"/>\n'


WORDMARK_PATHS = "".join(
    _wordmark_letter(kind, x) for kind, x in zip("PunPun", WORDMARK_ORIGINS)
)


def svg_mark() -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {TILE} {TILE}" width="{TILE}" height="{TILE}" role="img" aria-label="PunPun">
  <title>PunPun</title>
{TILE_DEFS}{TILE_SHAPE}{mark_body()}</svg>
'''


def svg_mark_flat() -> str:
    x0, y0, x1, y1 = GLYPH_BOX
    pad = 16
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0 - pad} {y0 - pad} {x1 - x0 + 2 * pad} {y1 - y0 + 2 * pad}" role="img" aria-label="PunPun">
  <title>PunPun</title>
{mark_body()}</svg>
'''


def svg_wordmark() -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="-16 -16 558 154" width="558" height="154" role="img" aria-label="PunPun">
  <title>PunPun</title>
  <g fill="none" stroke="currentColor" stroke-width="{WORDMARK_STROKE}" stroke-linecap="round" stroke-linejoin="round">
{WORDMARK_PATHS}  </g>
</svg>
'''


def svg_lockup(mode: str) -> str:
    if mode == "auto":
        style = f'''  <style>
    .wm {{ stroke: {INK}; }}
    @media (prefers-color-scheme: dark) {{ .wm {{ stroke: {PAPER}; }} }}
  </style>
'''
        wm_attr = 'class="wm"'
    else:
        style = ""
        wm_attr = f'stroke="{PAPER if mode == "dark" else INK}"'
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 220" width="660" height="220" role="img" aria-label="PunPun programming language">
  <title>PunPun programming language</title>
{style}{TILE_DEFS}  <g transform="translate(0 20) scale(0.3515625)">
{TILE_SHAPE}{mark_body()}  </g>
  <g {wm_attr} transform="translate(229 70.6) scale(0.787)" fill="none" stroke-width="{WORDMARK_STROKE}" stroke-linecap="round" stroke-linejoin="round">
{WORDMARK_PATHS}  </g>
</svg>
'''


def svg_file_icon(theme: str) -> str:
    lime, cyan = (LIME_SMALL, CYAN_SMALL) if theme == "light" else (LIME, CYAN)
    x0, y0, x1, y1 = GLYPH_BOX
    pad = 24
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0 - pad} {y0 - pad} {x1 - x0 + 2 * pad} {y1 - y0 + 2 * pad}" width="32" height="32" role="img" aria-label="PunPun source file">
  <title>.pp source file</title>
{mark_body(lime, cyan)}</svg>
'''


def _draw_glyph(layer, glyph, colour, stroke=STROKE):
    from PIL import ImageDraw
    draw = ImageDraw.Draw(layer)
    stem_x, top, bottom, cx, cy = (v * SS for v in glyph)
    width = stroke * SS
    radius = width / 2
    draw.rectangle([stem_x - radius, top, stem_x + radius, bottom], fill=colour)
    draw.ellipse([stem_x - radius, top - radius, stem_x + radius, top + radius], fill=colour)
    draw.ellipse([stem_x - radius, bottom - radius, stem_x + radius, bottom + radius], fill=colour)
    outer = BOWL_R * SS + radius
    draw.ellipse([cx - outer, cy - outer, cx + outer, cy + outer], outline=colour, width=int(width))


def _glyph_pair(lime=LIME, cyan=CYAN):
    from PIL import Image
    layer = Image.new("RGBA", (TILE * SS, TILE * SS), (0, 0, 0, 0))
    _draw_glyph(layer, GLYPHS[0], _hex(lime) + (255,))
    _draw_glyph(layer, GLYPHS[1], _hex(cyan) + (255,))
    return layer


def render_mark_png(size: int):
    from PIL import Image, ImageDraw
    n = TILE * SS
    gradient = Image.new("RGBA", (1, n))
    top, bottom = _hex(TILE_TOP), _hex(TILE_BOTTOM)
    for y in range(n):
        f = y / (n - 1)
        gradient.putpixel((0, y), tuple(round(a + (b - a) * f) for a, b in zip(top, bottom)) + (255,))
    gradient = gradient.resize((n, n))
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, n - 1, n - 1], radius=TILE_RADIUS * SS, fill=255)
    tile = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    tile.paste(gradient, (0, 0), mask)
    ImageDraw.Draw(tile).rounded_rectangle(
        [4 * SS, 4 * SS, n - 1 - 4 * SS, n - 1 - 4 * SS],
        radius=(TILE_RADIUS - 4) * SS,
        outline=_hex(TILE_EDGE) + (255,),
        width=8 * SS,
    )
    tile.alpha_composite(_glyph_pair())
    return tile.resize((size, size), Image.Resampling.LANCZOS)


def render_file_png(size: int, theme: str = "dark"):
    from PIL import Image
    lime, cyan = (LIME_SMALL, CYAN_SMALL) if theme == "light" else (LIME, CYAN)
    pair = _glyph_pair(lime, cyan)
    x0, y0, x1, y1 = (round(v * SS) for v in GLYPH_BOX)
    pad = round(10 * SS)
    box = pair.crop((x0 - pad, y0 - pad, x1 + pad, y1 + pad))
    side = max(box.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.alpha_composite(box, ((side - box.width) // 2, (side - box.height) // 2))
    return square.resize((size, size), Image.Resampling.LANCZOS)


def _wm_line(draw, x0, y0, x1, y1, colour, width):
    draw.line((x0, y0, x1, y1), fill=colour, width=max(1, int(round(width))))
    r = width / 2
    for x, y in ((x0, y0), (x1, y1)):
        draw.ellipse((x - r, y - r, x + r, y + r), fill=colour)


def render_wordmark(layer, colour, scale, ox, oy):
    from PIL import ImageDraw
    draw = ImageDraw.Draw(layer)
    width = WORDMARK_STROKE * scale
    X = lambda v: ox + v * scale
    Y = lambda v: oy + v * scale
    for kind, x in zip("PunPun", WORDMARK_ORIGINS):
        if kind == "P":
            _wm_line(draw, X(x), Y(0), X(x), Y(100), colour, width)
            _wm_line(draw, X(x), Y(0), X(x + 30), Y(0), colour, width)
            _wm_line(draw, X(x), Y(56), X(x + 30), Y(56), colour, width)
            bbox = [X(x + 2), Y(0), X(x + 58), Y(56)]
            draw.arc(bbox, -90, 90, fill=colour, width=max(1, int(round(width))))
        elif kind == "u":
            _wm_line(draw, X(x), Y(28), X(x), Y(78), colour, width)
            _wm_line(draw, X(x + 44), Y(28), X(x + 44), Y(100), colour, width)
            bbox = [X(x), Y(56), X(x + 44), Y(100)]
            draw.arc(bbox, 0, 180, fill=colour, width=max(1, int(round(width))))
        else:
            _wm_line(draw, X(x), Y(50), X(x), Y(100), colour, width)
            _wm_line(draw, X(x + 44), Y(50), X(x + 44), Y(100), colour, width)
            bbox = [X(x), Y(28), X(x + 44), Y(72)]
            draw.arc(bbox, 180, 360, fill=colour, width=max(1, int(round(width))))


def render_lockup_png(width: int, mode: str = "dark"):
    from PIL import Image
    vb_w, vb_h = 660, 220
    canvas = Image.new("RGBA", (vb_w * SS, vb_h * SS), (0, 0, 0, 0))
    tile = render_mark_png(180 * SS)
    canvas.alpha_composite(tile, (0, 20 * SS))
    colour = _hex(PAPER if mode == "dark" else INK) + (255,)
    render_wordmark(canvas, colour, 0.787 * SS, 228.7 * SS, 70.6 * SS)
    return canvas.resize((width, round(width * vb_h / vb_w)), Image.Resampling.LANCZOS)


def render_social_png():
    from PIL import Image
    card = Image.new("RGBA", (1280, 640), _hex(TILE_BOTTOM) + (255,))
    lockup = render_lockup_png(880, "dark")
    card.alpha_composite(lockup, ((1280 - lockup.width) // 2, (640 - lockup.height) // 2))
    return card


def _write_text(path: Path, text: str, written: list[Path], root: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    written.append(path.relative_to(root))


def _save_png(image, path: Path, written: list[Path], root: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "PNG", optimize=True)
    written.append(path.relative_to(root))


def _save_ico(images: list, path: Path, sizes: tuple[int, ...], written: list[Path], root: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    largest = images[-1]
    largest.save(path, "ICO", sizes=[(size, size) for size in sizes])
    written.append(path.relative_to(root))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--svg-only", action="store_true", help="skip raster/ICO targets")
    args = parser.parse_args()
    root = args.repo_root.resolve()
    assets = root / "assets"
    vscode = root / "editors" / "vscode"
    written: list[Path] = []

    vectors = {
        assets / "punpun-logo.svg": svg_lockup("auto"),
        assets / "punpun-logo-light.svg": svg_lockup("light"),
        assets / "punpun-logo-dark.svg": svg_lockup("dark"),
        assets / "punpun-mark.svg": svg_mark(),
        assets / "punpun-mark-flat.svg": svg_mark_flat(),
        assets / "punpun-wordmark.svg": svg_wordmark(),
        vscode / "fileicons" / "pp-file-dark.svg": svg_file_icon("dark"),
        vscode / "fileicons" / "pp-file-light.svg": svg_file_icon("light"),
        vscode / "assets" / "punpun-mark.svg": svg_mark(),
    }
    for path, text in vectors.items():
        _write_text(path, text, written, root)

    if args.svg_only:
        print("\n".join(map(str, written)))
        return 0

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("build_brand: Pillow is required for raster output (pip install pillow), or pass --svg-only", file=sys.stderr)
        return 2

    _save_png(render_mark_png(128), vscode / "icon.png", written, root)
    _save_png(render_mark_png(128), vscode / "assets" / "punpun-icon-128.png", written, root)
    _save_png(render_file_png(32, "light"), vscode / "assets" / "punpun-file-light.png", written, root)
    _save_png(render_file_png(32, "dark"), vscode / "assets" / "punpun-file-dark.png", written, root)

    for size in (256, 512, 1024):
        _save_png(render_mark_png(size), assets / f"punpun-mark-{size}.png", written, root)
    for size in MIME_ALIAS_SIZES:
        _save_png(render_file_png(size), assets / f"punpun-icon-{size}.png", written, root)
    for size in HICOLOR_SIZES:
        _save_png(render_file_png(size), assets / "hicolor" / f"{size}x{size}" / "text-x-punpun.png", written, root)

    _save_png(render_file_png(16), assets / "favicon-16.png", written, root)
    _save_png(render_file_png(32), assets / "favicon-32.png", written, root)
    _save_png(render_lockup_png(1200, "dark"), assets / "punpun-logo-dark.png", written, root)
    _save_png(render_lockup_png(1200, "light"), assets / "punpun-logo-light.png", written, root)
    _save_png(render_social_png(), assets / "punpun-social-preview.png", written, root)

    # Application and file-association ICOs intentionally use different marks:
    # tiled for PunPun itself, transparent PP pair for source files.
    render_mark_png(256).save(assets / "punpun.ico", "ICO", sizes=[(s, s) for s in ICO_SIZES])
    written.append((assets / "punpun.ico").relative_to(root))
    render_file_png(256).save(assets / "punpun-source.ico", "ICO", sizes=[(s, s) for s in ICO_SIZES])
    written.append((assets / "punpun-source.ico").relative_to(root))
    render_file_png(64).save(assets / "favicon.ico", "ICO", sizes=[(16, 16), (32, 32), (48, 48)])
    written.append((assets / "favicon.ico").relative_to(root))

    print("\n".join(map(str, written)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
