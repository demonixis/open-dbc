#!/usr/bin/env python3
"""Generate OpenDBC branding images from resources/editor.png.

Outputs go to resources/branding/: transparent logos for screenshots, social
network headers and Full HD wallpapers. The pyramid interior is taken from the
application icon so every image matches it; outlines, glows, backgrounds and
text are redrawn at the target resolution.

Requires Pillow and NumPy. Fonts are the macOS system Avenir Next and Menlo.
Usage: python3 scripts/generate_branding.py
"""

import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICON = ROOT / "resources" / "editor.png"
OUT = ROOT / "resources" / "branding"

AVENIR = "/System/Library/Fonts/Avenir Next.ttc"
AVENIR_HEAVY, AVENIR_DEMI, AVENIR_MEDIUM = 8, 2, 5
MENLO = "/System/Library/Fonts/Menlo.ttc"

# Palette sampled from the icon.
CYAN_GLOW = (40, 200, 230)
CYAN_EDGE = (120, 230, 240)
CYAN_CORE = (232, 255, 255)
GOLD = (245, 190, 60)
ICE = (230, 250, 252)
NAVY = (10, 24, 34)

# Centre line of the icon's triangle outline, in icon pixels.
APEX, LEFT, RIGHT = (507, 161), (153, 745), (861, 745)
TRI_HEIGHT = LEFT[1] - APEX[1]
PAD = 90  # Glow margin around the triangle, in icon pixels.
SUPERSAMPLE = 4

# DarkBASIC-style lines for the gold code texture.
CODE = [
    "sync on : sync rate 60", "make object cube 1, 10", "position object 1, 0, 0, 50",
    "do", "  yrotate object 1, object angle y(1) + 1", "  sync", "loop",
    "make matrix 1, 1000, 1000, 32, 32", "randomize matrix 1, 50",
    "set camera range 1, 5000", "point camera 500, 0, 500", "load image \"logo.png\", 1",
    "sprite 1, x, y, 1", "for i = 1 to 100", "  make object sphere i, 5", "next i",
    "if spacekey() then end", "print \"Hello, OpenDBC\"", "color backdrop rgb(0, 0, 0)",
    "autocam off", "hide mouse", "set display mode 1920, 1080, 32", "gosub _init",
    "_init:", "return", "dim scores(10)", "x# = x# + cos(a#) * 2.0", "repeat : until escapekey()",
    "make light 1", "set object texture 1, 2, 1", "play sound 1", "inc frame",
]


def tri_points(scale, origin=(0.0, 0.0)):
    """Triangle centre line mapped into a canvas whose glow margin starts at origin."""
    ox, oy = origin
    return [((x - LEFT[0] + PAD) * scale + ox, (y - APEX[1] + PAD) * scale + oy)
            for x, y in (APEX, LEFT, RIGHT)]


def inset(points, distance):
    """Shrink a triangle uniformly towards its incentre."""
    (ax, ay), (bx, by), (cx, cy) = points
    a = np.hypot(bx - cx, by - cy)
    b = np.hypot(ax - cx, ay - cy)
    c = np.hypot(ax - bx, ay - by)
    p = a + b + c
    ix, iy = (a * ax + b * bx + c * cx) / p, (a * ay + b * by + c * cy) / p
    area = abs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) / 2
    k = (2 * area / p - distance) / (2 * area / p)
    return [(ix + (x - ix) * k, iy + (y - iy) * k) for x, y in points]


def smooth_mask(size, draw_fn):
    """Antialiased L mask drawn at SUPERSAMPLE x resolution."""
    big = Image.new("L", (size[0] * SUPERSAMPLE, size[1] * SUPERSAMPLE), 0)
    draw_fn(ImageDraw.Draw(big), SUPERSAMPLE)
    return big.resize(size, Image.LANCZOS)


def scaled(points, k):
    return [(x * k, y * k) for x, y in points]


def outline_mask(size, points, width):
    def draw(d, k):
        pts = scaled(points, k)
        d.line(pts + [pts[0], pts[1]], fill=255, width=max(1, round(width * k)), joint="curve")
    return smooth_mask(size, draw)


def gain(mask, factor):
    return mask.point(lambda v: min(255, int(v * factor)))


def colored(mask, color):
    layer = Image.new("RGBA", mask.size, color + (0,))
    layer.putalpha(mask)
    return layer


def blurred(mask, radius, factor):
    return gain(mask.filter(ImageFilter.GaussianBlur(radius)), factor)


def pyramid(height):
    """Glowing pyramid on a transparent canvas; height is the triangle's height in pixels."""
    s = height / TRI_HEIGHT
    size = (round((RIGHT[0] - LEFT[0] + 2 * PAD) * s), round((TRI_HEIGHT + 2 * PAD) * s))
    pts = tri_points(s)
    out = Image.new("RGBA", size, (0, 0, 0, 0))

    icon = Image.open(ICON).convert("RGBA")
    icon = icon.resize((round(icon.width * s), round(icon.height * s)), Image.LANCZOS)
    interior = Image.new("RGBA", size, (0, 0, 0, 0))
    interior.paste(icon, (round(-(LEFT[0] - PAD) * s), round(-(APEX[1] - PAD) * s)))
    inner = inset(pts, 3 * s)
    fill = smooth_mask(size, lambda d, k: d.polygon(scaled(inner, k), fill=255))
    interior.putalpha(fill)

    stroke = outline_mask(size, pts, 13 * s)
    core = outline_mask(size, pts, 5 * s)
    out.alpha_composite(colored(blurred(stroke, 34 * s, 1.1), CYAN_GLOW))
    out.alpha_composite(interior)
    out.alpha_composite(colored(blurred(stroke, 9 * s, 1.6), CYAN_GLOW))
    out.alpha_composite(colored(stroke, CYAN_EDGE))
    out.alpha_composite(colored(core, CYAN_CORE))
    return out


def text_mask(text, font, tracking):
    """L mask of a text run with extra letter spacing (fraction of the font size)."""
    advance = [font.getlength(ch) + font.size * tracking for ch in text]
    width = round(sum(advance) - font.size * tracking)
    ascent, descent = font.getmetrics()
    mask = Image.new("L", (width + 2, ascent + descent), 0)
    d = ImageDraw.Draw(mask)
    x = 0.0
    for ch, adv in zip(text, advance):
        d.text((x, 0), ch, font=font, fill=255)
        x += adv
    return mask


def crop_alpha(img, margin):
    box = img.getchannel("A").point(lambda v: 255 if v > 2 else 0).getbbox()
    box = (max(0, box[0] - margin), max(0, box[1] - margin),
           min(img.width, box[2] + margin), min(img.height, box[3] + margin))
    return img.crop(box)


def wordmark(cap_height, on_dark=True, glow=True):
    """'Open' + 'DBC' wordmark; cap_height is the height of the capitals in pixels."""
    font = ImageFont.truetype(AVENIR, round(cap_height / 0.70), index=AVENIR_HEAVY)
    tracking = 0.02
    first, second = text_mask("Open", font, tracking), text_mask("DBC", font, tracking)
    gap = round(font.size * tracking)
    margin = round(cap_height * 0.5) if glow else 2
    w = first.width + gap + second.width + 2 * margin
    h = first.height + 2 * margin
    full = Image.new("L", (w, h), 0)
    full.paste(first, (margin, margin))
    full.paste(second, (margin + first.width + gap, margin))

    light = ICE if on_dark else NAVY
    gold = GOLD if on_dark else (196, 138, 18)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if glow:
        out.alpha_composite(colored(blurred(full, cap_height * 0.18, 0.55), CYAN_GLOW))
    split = margin + first.width + gap // 2
    left = full.copy()
    left.paste(0, (split, 0, w, h))
    right = full.copy()
    right.paste(0, (0, 0, split, h))
    out.alpha_composite(colored(left, light))
    out.alpha_composite(colored(right, gold))
    return crop_alpha(out, margin)


def plain_text(text, size, color, index=AVENIR_MEDIUM, tracking=0.0, path=AVENIR):
    font = ImageFont.truetype(path, size, index=index)
    return crop_alpha(colored(text_mask(text, font, tracking), color), 1)


def solid_box(img):
    """Bounding box of the opaque shape, ignoring soft glow; used for layout."""
    return img.getchannel("A").point(lambda v: 255 if v > 96 else 0).getbbox()


def solid_size(img):
    box = solid_box(img)
    return box[2] - box[0], box[3] - box[1]


def arrange(entries):
    """Compose (image, x, y) entries, where (x, y) is the top left of each solid box."""
    offsets = []
    for img, x, y in entries:
        box = solid_box(img)
        offsets.append((round(x - box[0]), round(y - box[1])))
    x0 = min(ox for ox, _ in offsets)
    y0 = min(oy for _, oy in offsets)
    x1 = max(ox + img.width for (img, _, _), (ox, _) in zip(entries, offsets))
    y1 = max(oy + img.height for (img, _, _), (_, oy) in zip(entries, offsets))
    out = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    for (img, _, _), (ox, oy) in zip(entries, offsets):
        out.alpha_composite(img, (ox - x0, oy - y0))
    return out


def lockup(height, on_dark=True, stacked=False, glow=True):
    """Pyramid plus wordmark on a transparent canvas."""
    mark = pyramid(height)
    word = wordmark(height * (0.24 if stacked else 0.36), on_dark, glow)
    (mw, mh), (ww, wh) = solid_size(mark), solid_size(word)
    if stacked:
        gap = height * 0.14
        return arrange([(mark, (ww - mw) / 2 if ww > mw else 0, 0),
                        (word, (mw - ww) / 2 if mw > ww else 0, mh + gap)])
    gap = height * 0.14
    return arrange([(mark, 0, 0), (word, mw + gap, (mh - wh) / 2)])


# --- Backgrounds -------------------------------------------------------------

def radial(w, h, cx, cy, rx, ry):
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
    return np.exp(-d * 2.2)


def code_layer(w, h, font_px, alpha, rng, columns_alpha=None):
    """Gold DarkBASIC listing texture laid out in columns."""
    font = ImageFont.truetype(MENLO, font_px, index=0)
    line_h = round(font_px * 1.55)
    col_w = round(font.getlength("m") * 44)
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    x = -rng.randrange(0, col_w)
    while x < w:
        level = rng.uniform(0.35, 1.0) if columns_alpha is None else columns_alpha(x / w)
        y = -rng.randrange(0, line_h * 4)
        while y < h:
            if rng.random() < 0.82:
                d.text((x, y), rng.choice(CODE), font=font, fill=round(255 * level))
            y += line_h
        x += col_w
    return np.asarray(mask, np.float32) / 255 * alpha


def grid_layer(w, h, horizon, vx, spacing):
    """Perspective floor grid below the horizon, as an alpha array."""
    k = 2
    img = Image.new("L", (w * k, h * k), 0)
    d = ImageDraw.Draw(img)
    floor = h - horizon
    width = max(1, round(1.4 * k * h / 1080))
    for i in range(-60, 61):
        bx = vx + i * spacing
        # Extend each ray well below the frame so it keeps its slope.
        d.line([(vx * k, horizon * k), ((vx + (bx - vx) * 3) * k, (horizon + floor * 3) * k)],
               fill=255, width=width)
    z = 0.55
    while z < 60:
        y = horizon + floor / z
        if y < h + 2:
            d.line([(0, y * k), (w * k, y * k)], fill=255, width=width)
        z += 0.42
    grid = np.asarray(img.resize((w, h), Image.LANCZOS), np.float32) / 255
    ys = np.arange(h, dtype=np.float32)[:, None]
    fade = np.clip((ys - horizon) / (floor * 0.55), 0, 1) ** 1.6
    return grid * fade


def background(w, h, focus, horizon=None, code_alpha=0.07, grid=True, seed=1, code_px=None):
    """Dark navy scene: haze around focus, optional floor grid and code texture."""
    rng = random.Random(seed)
    fx, fy = focus
    ys = np.linspace(0, 1, h, dtype=np.float32)[:, None]
    top, bottom = np.array([4, 10, 17], np.float32), np.array([11, 24, 33], np.float32)
    img = np.broadcast_to(top + (bottom - top) * ys[..., None], (h, w, 3)).copy()

    scale = h / 1080
    haze = radial(w, h, fx, fy, w * 0.42, h * 0.55)
    img += haze[..., None] * np.array([14, 58, 72], np.float32)
    warm = radial(w, h, fx, fy, w * 0.16, h * 0.22)
    img += warm[..., None] * np.array([34, 24, 4], np.float32)

    if code_alpha > 0:
        code = code_layer(w, h, code_px or max(10, round(15 * scale)), code_alpha, rng)
        # Keep the texture away from the focus so it does not compete with the logo.
        code *= 1 - 0.75 * radial(w, h, fx, fy, w * 0.30, h * 0.45)
        if horizon is not None:
            code *= np.clip((horizon - ys[:, 0] * h) / (h * 0.08), 0, 1)[:, None]
        img += code[..., None] * (np.array(GOLD, np.float32) - img)

    if horizon is not None:
        if grid:
            g = grid_layer(w, h, horizon, fx, w / 11)
            glow = np.asarray(Image.fromarray((g * 255).astype(np.uint8))
                              .filter(ImageFilter.GaussianBlur(4 * scale)), np.float32) / 255
            g = np.clip(g * 0.55 + glow * 0.9, 0, 1)
            img += g[..., None] * (np.array(CYAN_GLOW, np.float32) - img) * 0.85
        line = np.exp(-((np.arange(h, dtype=np.float32) - horizon) / (3 * scale)) ** 2)[:, None]
        spread = np.exp(-((np.arange(w, dtype=np.float32) - fx) / (w * 0.38)) ** 2)[None, :]
        img += (line * spread)[..., None] * np.array([60, 190, 210], np.float32)
        bloom = np.exp(-((np.arange(h, dtype=np.float32) - horizon) / (40 * scale)) ** 2)[:, None]
        img += (bloom * spread)[..., None] * np.array([6, 34, 44], np.float32)

    vignette = radial(w, h, w / 2, h / 2, w * 0.95, h * 1.05)
    img *= (0.55 + 0.45 * vignette)[..., None]
    # Ordered dither against banding; unlike noise it keeps the PNGs compressible.
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32)
    img += np.tile(bayer / 16 - 0.47, (h // 4 + 1, w // 4 + 1))[:h, :w, None]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")


def place(canvas, layer, cx, cy):
    """Centre the solid box of layer on (cx, cy)."""
    box = solid_box(layer)
    canvas.alpha_composite(layer, (round(cx - (box[0] + box[2]) / 2),
                                   round(cy - (box[1] + box[3]) / 2)))


def reflect(canvas, mark, cx, base_y, strength=0.22):
    """Faded mirror image of a pyramid whose triangle base sits on base_y."""
    flipped = mark.transpose(Image.FLIP_TOP_BOTTOM)
    alpha = np.asarray(flipped.getchannel("A"), np.float32)
    fade = np.linspace(1, 0, flipped.height, dtype=np.float32)[:, None] ** 2.2
    flipped.putalpha(Image.fromarray((alpha * fade * strength).astype(np.uint8)))
    flipped = flipped.filter(ImageFilter.GaussianBlur(2))
    pad = PAD / (TRI_HEIGHT + 2 * PAD) * mark.height
    canvas.alpha_composite(flipped, (round(cx - flipped.width / 2), round(base_y - pad)))


def pyramid_on_floor(canvas, height, cx, base_y):
    mark = pyramid(height)
    pad = PAD / (TRI_HEIGHT + 2 * PAD) * mark.height
    reflect(canvas, mark, cx, base_y)
    canvas.alpha_composite(mark, (round(cx - mark.width / 2), round(base_y + pad - mark.height)))


# --- Outputs -----------------------------------------------------------------

TAGLINE = "Run DarkBASIC Classic & Pro programs on modern systems"
TECH = "C++20  ·  SDL3 GPU  ·  Vulkan  ·  Metal  ·  Studio + CLI"


def text_block(height, tech=True):
    """Wordmark, tagline and optional tech line, left aligned; height is the wordmark caps."""
    parts = [wordmark(height)]
    parts.append(plain_text(TAGLINE, round(height * 0.34), (176, 210, 220)))
    if tech:
        parts.append(plain_text(TECH, round(height * 0.24), GOLD, index=0, path=MENLO))
    gaps = [height * 0.42, height * 0.30]
    entries, y = [], 0
    for i, p in enumerate(parts):
        entries.append((p, 0, y))
        y += solid_size(p)[1] + (gaps[i] if i < len(gaps) else 0)
    return arrange(entries)


def header(w, h, safe, pyramid_h, cap_h, tech=True, seed=3):
    """Social banner: pyramid and text block centred in the safe box (x, y, w, h)."""
    sx, sy, sw, sh = safe
    mark = pyramid(pyramid_h)
    block = text_block(cap_h, tech)
    (mw, mh), (bw, _) = solid_size(mark), solid_size(block)
    gap = pyramid_h * 0.22
    left = sx + (sw - (mw + gap + bw)) / 2
    cy = sy + sh / 2
    mark_cx = left + mw / 2
    canvas = background(w, h, (mark_cx, cy), horizon=round(cy + mh / 2 - pyramid_h * 0.03),
                        code_alpha=0.08, seed=seed)
    place(canvas, mark, mark_cx, cy)
    place(canvas, block, left + mw + gap + bw / 2, cy)
    return canvas


def wallpaper_horizon():
    w, h = 1920, 1080
    horizon = 700
    canvas = background(w, h, (w / 2, horizon - 200), horizon=horizon, code_alpha=0.06, seed=7)
    pyramid_on_floor(canvas, 470, w / 2, horizon)
    place(canvas, wordmark(44), w / 2, 960)
    return canvas


def wallpaper_minimal():
    w, h = 1920, 1080
    canvas = background(w, h, (w / 2, h / 2 - 40), code_alpha=0, grid=False, seed=11)
    place(canvas, crop_alpha(pyramid(380), 40), w / 2, h / 2 - 70)
    place(canvas, wordmark(40), w / 2, h / 2 + 240)
    return canvas


def wallpaper_code():
    w, h = 1920, 1080
    canvas = background(w, h, (w * 0.5, h * 0.48), code_alpha=0.26, grid=False, seed=5, code_px=17)
    place(canvas, crop_alpha(pyramid(560), 40), w * 0.5, h * 0.48)
    return canvas


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    outputs = {
        "logo_mark.png": crop_alpha(pyramid(1024), 60),
        "logo_horizontal.png": crop_alpha(lockup(512), 8),
        "logo_horizontal_light_bg.png": crop_alpha(lockup(512, on_dark=False, glow=False), 8),
        "logo_stacked.png": crop_alpha(lockup(720, stacked=True), 8),
        "logo_stacked_light_bg.png": crop_alpha(lockup(720, on_dark=False, stacked=True, glow=False), 8),
        # X / Twitter: the avatar covers the lower left, so content sits right of centre.
        "header_x_1500x500.png": header(1500, 500, (330, 40, 1130, 420), 300, 70),
        # LinkedIn: the avatar covers the lower left on desktop.
        "header_linkedin_1584x396.png": header(1584, 396, (420, 30, 1104, 336), 250, 58, tech=False),
        "header_facebook_1640x624.png": header(1640, 624, (240, 60, 1300, 450), 340, 74),
        # YouTube: only the central 1546x423 is visible on every device.
        "header_youtube_2560x1440.png": header(2560, 1440, (507, 508, 1546, 423), 380, 84),
        "social_preview_github_1280x640.png": header(1280, 640, (60, 80, 1160, 480), 330, 64),
        "wallpaper_horizon_1920x1080.png": wallpaper_horizon(),
        "wallpaper_minimal_1920x1080.png": wallpaper_minimal(),
        "wallpaper_code_1920x1080.png": wallpaper_code(),
    }
    for name, img in outputs.items():
        if not name.startswith("logo_"):
            img = img.convert("RGB")
        img.save(OUT / name, optimize=True)
        print(f"{name}: {img.width}x{img.height}")


if __name__ == "__main__":
    main()
