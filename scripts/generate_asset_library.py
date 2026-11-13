"""Generate the CC0 texture pack embedded in the Studio's texture library.

Every image is synthesised from fixed seeds, so a run reproduces the same pack. The
script needs numpy and a JPEG writer, which Blender's bundled Python provides:

    /Applications/Blender.app/Contents/MacOS/Blender -b --python scripts/generate_asset_library.py

It writes resources/asset_library/textures/<category>_<1..3>.jpg (tileable surfaces)
and <name>_cube_{px,nx,py,ny,pz,nz}.jpg (environment cube faces).
"""

from pathlib import Path

import bpy
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "resources" / "asset_library" / "textures"
N = 512
QUALITY = 88
TAU = 2.0 * np.pi

# Tile coordinates in [0, 1); row 0 is the top of the image.
V, U = np.meshgrid((np.arange(N) + 0.5) / N, (np.arange(N) + 0.5) / N, indexing="ij")


# --- Helpers ---------------------------------------------------------------------------

def lerp(a, b, t):
    return a + (b - a) * t


def smoothstep(lo, hi, x):
    t = np.clip((x - lo) / (hi - lo), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def rgb(t):
    return t[..., None]


def color(code):
    return np.array([int(code[i:i + 2], 16) for i in (1, 3, 5)]) / 255.0


def ramp(t, *codes):
    """Map t in [0, 1] through evenly spaced "#rrggbb" stops."""
    stops = np.array([color(code) for code in codes])
    positions = np.linspace(0.0, 1.0, len(codes))
    return np.stack([np.interp(t, positions, stops[:, c]) for c in range(3)], axis=-1)


def perlin(u, v, fx, fy, seed):
    """Gradient noise in about [-1, 1] that wraps after fx by fy lattice cells per tile."""
    angle = np.random.default_rng(seed).uniform(0.0, TAU, (fy, fx))
    gx, gy = np.cos(angle), np.sin(angle)
    x, y = u * fx, v * fy
    xi, yi = np.floor(x).astype(int), np.floor(y).astype(int)
    xf, yf = x - xi, y - yi

    def corner(ox, oy):
        ix, iy = (xi + ox) % fx, (yi + oy) % fy
        return gx[iy, ix] * (xf - ox) + gy[iy, ix] * (yf - oy)

    sx, sy = xf * xf * xf * (xf * (xf * 6 - 15) + 10), yf * yf * yf * (yf * (yf * 6 - 15) + 10)
    return 1.5 * lerp(lerp(corner(0, 0), corner(1, 0), sx), lerp(corner(0, 1), corner(1, 1), sx), sy)


def fbm(seed, f, octaves=5, stretch=(1, 1), u=U, v=V, gain=0.5):
    """Tileable fractal noise in [0, 1]; stretch multiplies the frequency per axis."""
    total, amplitude, weight = 0.0, 1.0, 0.0
    for octave in range(octaves):
        scale = f << octave
        total = total + amplitude * perlin(u, v, scale * stretch[0], scale * stretch[1], seed + octave)
        weight += amplitude
        amplitude *= gain
    return np.clip(0.5 + 0.8 * total / weight, 0.0, 1.0)


def ridged(seed, f, octaves=5, stretch=(1, 1), u=U, v=V):
    return 1.0 - np.abs(2.0 * fbm(seed, f, octaves, stretch, u, v) - 1.0)


def warp(seed, f, amount, octaves=3):
    """Tile coordinates displaced by noise, to bend whatever is sampled through them."""
    return (U + amount * (fbm(seed, f, octaves) - 0.5), V + amount * (fbm(seed + 50, f, octaves) - 0.5))


def worley(seed, nx, ny=None, u=U, v=V, jitter=0.9):
    """Tileable cellular noise: F1, F2 (in cells), a random value per cell, offset to its point."""
    ny = ny or nx
    rng = np.random.default_rng(seed)
    px = 0.5 + jitter * (rng.random((ny, nx)) - 0.5)
    py = 0.5 + jitter * (rng.random((ny, nx)) - 0.5)
    value = rng.random((ny, nx))
    x, y = u * nx, v * ny
    xi, yi = np.floor(x).astype(int), np.floor(y).astype(int)
    f1 = np.full(x.shape, 9.0)
    f2 = np.full(x.shape, 9.0)
    cell, dx1, dy1 = np.zeros(x.shape), np.zeros(x.shape), np.zeros(x.shape)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx, cy = (xi + ox) % nx, (yi + oy) % ny
            dx, dy = xi + ox + px[cy, cx] - x, yi + oy + py[cy, cx] - y
            d = np.hypot(dx, dy)
            nearer = d < f1
            f2 = np.where(nearer, f1, np.minimum(f2, d))
            f1 = np.where(nearer, d, f1)
            cell, dx1, dy1 = np.where(nearer, value[cy, cx], cell), np.where(nearer, dx, dx1), np.where(nearer, dy, dy1)
    return f1, f2, cell, dx1, dy1


def shade(h, strength=1.0):
    """Light factor for a tileable height field lit from the top left; 1 on flat ground."""
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 30.0 * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 30.0 * strength
    light = np.array([-0.5, -0.6, 0.62])
    light /= np.linalg.norm(light)
    lit = (-dx * light[0] - dy * light[1] + light[2]) / np.sqrt(dx * dx + dy * dy + 1.0)
    return rgb(np.clip(1.0 + 0.9 * (lit - light[2]), 0.25, 1.7))


def save(name, pixels):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    image = bpy.data.images.new(name, N, N, alpha=False)
    rgba = np.ones((N, N, 4), dtype=np.float32)
    # Blender stores images bottom-up.
    rgba[..., :3] = np.clip(pixels, 0.0, 1.0)[::-1]
    image.pixels.foreach_set(rgba.ravel())
    image.file_format = "JPEG"
    image.save(filepath=str(OUTPUT / f"{name}.jpg"), quality=QUALITY)
    bpy.data.images.remove(image)


# --- Tileable surfaces: each returns (colour, height) for variant k in 0..2 ----------------

def grass(k, s):
    palette = [("#16300f", "#2f6a1c", "#5d9a2c", "#9cc24a"), ("#3a3414", "#6f6a26", "#a59a45", "#d6c878"),
               ("#0f2616", "#1f4f27", "#3f7d38", "#7aa850")][k]
    big = fbm(s, 3, 4)
    u, v = warp(s + 1, 6, 0.03)
    blades = fbm(s + 2, 24, 3, (4, 1), u, v)
    tufts = 1.0 - np.clip(worley(s + 3, 20, u=u, v=v)[0], 0.0, 1.0)
    h = 0.45 * blades + 0.3 * tufts + 0.25 * fbm(s + 4, 12, 4)
    c = ramp(np.clip(0.55 * h + 0.45 * big + 0.1 * (fbm(s + 5, 96, 2) - 0.5), 0.0, 1.0), *palette)
    if k == 1:
        # Bare earth shows through the dry grass.
        c = lerp(c, ramp(fbm(s + 6, 32, 3), "#4a3420", "#7a5a38"), rgb(smoothstep(0.58, 0.75, fbm(s + 7, 4, 4))) * 0.85)
    if k == 2:
        f1, _, cell, _, _ = worley(s + 8, 28)
        petals = rgb(smoothstep(0.16, 0.08, f1) * (cell > 0.8))
        c = lerp(c, lerp(color("#f4f0e0"), color("#f2d24a"), rgb(cell > 0.9)), petals)
        h = h + 0.3 * petals[..., 0]
    return c, h


def rock(k, s):
    if k == 0:
        f1, f2, _, _, _ = worley(s, 5, u=warp(s + 1, 4, 0.12)[0], v=warp(s + 1, 4, 0.12)[1])
        h = 0.55 * ridged(s + 2, 4, 6) + 0.3 * fbm(s + 3, 2, 3) + 0.25 * smoothstep(0.0, 0.12, f2 - f1)
        c = ramp(np.clip(0.7 * h + 0.3 * fbm(s + 4, 8, 4), 0.0, 1.0), "#2e2c2a", "#5d5a56", "#8a8680", "#b9b4aa")
        c = lerp(c, color("#5a6238"), rgb(smoothstep(0.6, 0.8, fbm(s + 5, 5, 4))) * 0.45)
    elif k == 1:
        # Sandstone strata: noise stretched along u and bent a little.
        u, v = warp(s, 3, 0.1)
        strata = fbm(s + 1, 2, 5, (1, 6), u, v)
        h = 0.6 * strata + 0.4 * ridged(s + 2, 6, 5, (1, 3), u, v)
        c = ramp(np.clip(0.75 * strata + 0.25 * fbm(s + 3, 16, 3), 0.0, 1.0), "#5a3a26", "#8f5f3c", "#b98a58", "#d9b98a", "#a06a44")
    else:
        u, v = warp(s, 5, 0.06)
        f1, f2, cell, _, _ = worley(s + 1, 6, u=u, v=v)
        h = 0.7 * smoothstep(0.0, 0.25, f2 - f1) + 0.15 * cell + 0.25 * fbm(s + 2, 16, 4)
        c = ramp(np.clip(0.35 + 0.4 * cell + 0.5 * (fbm(s + 3, 12, 4) - 0.5), 0.0, 1.0), "#1c1e24", "#383c46", "#565a64", "#747882")
        c = c * rgb(0.45 + 0.55 * smoothstep(0.0, 0.1, f2 - f1))
    return c, h


def snow(k, s):
    if k == 0:
        t = 0.8 * fbm(s, 3, 4) + 0.2 * fbm(s + 1, 16, 3)
        h = 0.4 * t
        c = ramp(t, "#b9c8e0", "#dfe8f4", "#f6f9fd", "#ffffff")
    elif k == 1:
        # Wind-packed drifts.
        u, v = warp(s, 3, 0.15)
        t = 0.7 * fbm(s + 1, 2, 3, (1, 4), u, v) + 0.3 * ridged(s + 2, 3, 3, (1, 3), u, v)
        h = 0.35 * t
        c = ramp(t, "#9fb4d6", "#cfdcee", "#eef3fa", "#ffffff")
    else:
        u, v = warp(s, 4, 0.1)
        f1, f2, _, _, _ = worley(s + 1, 4, u=u, v=v)
        crack = smoothstep(0.1, 0.0, f2 - f1)
        cover = smoothstep(0.4, 0.62, fbm(s + 2, 4, 5))
        h = 0.5 * cover + 0.3 * fbm(s + 3, 8, 4) - 0.3 * crack * (1.0 - cover)
        ice = ramp(fbm(s + 4, 6, 4), "#3f7ea8", "#6fb0d0", "#a8d8ea") + rgb(crack) * 0.35
        c = lerp(ice, ramp(fbm(s + 5, 16, 3), "#d4e2f0", "#ffffff"), rgb(cover))
    sparkle = np.random.default_rng(s + 9).random((N, N)) > 0.992
    return c + rgb(sparkle * 0.12), h


def sand(k, s):
    grain = np.random.default_rng(s + 9).random((N, N)) - 0.5
    if k == 0:
        t = 0.7 * fbm(s, 4, 5) + 0.3 * fbm(s + 1, 32, 3)
        h = 0.5 * t
        c = ramp(np.clip(t + 0.12 * grain, 0.0, 1.0), "#b89a68", "#d8be8a", "#ecd9aa", "#f6e9c6")
    elif k == 1:
        # Wind ripples: a whole number of waves so the tile wraps.
        phase = 9.0 * V + 2.2 * fbm(s, 2, 4) + 0.5 * fbm(s + 1, 8, 3)
        h = 0.6 * (0.5 + 0.5 * np.sin(TAU * phase)) ** 1.5 + 0.4 * fbm(s + 2, 3, 4)
        c = ramp(np.clip(0.8 * h + 0.1 + 0.1 * grain, 0.0, 1.0), "#9a6a34", "#c8924c", "#e4b46c", "#f2d090")
        h = 0.6 * h
    else:
        f1, _, cell, _, _ = worley(s, 16)
        pebble = smoothstep(0.34, 0.2, f1) * (cell > 0.55)
        h = 0.4 * fbm(s + 1, 5, 5) + 0.2 * fbm(s + 2, 40, 2) + 0.6 * pebble * (1.0 - f1)
        c = ramp(np.clip(0.3 + 0.5 * fbm(s + 3, 3, 5) + 0.15 * grain, 0.0, 1.0), "#4e4030", "#7a6648", "#a08a64", "#b8a47c")
        c = lerp(c, ramp((cell - 0.55) / 0.45, "#3c3a38", "#8a8278", "#c8c0b0", "#6a5040"), rgb(pebble))
    return c, h


def water(k, s):
    u, v = warp(s, 3, 0.15)
    waves = 0.65 * fbm(s + 1, 3, 3, (1, 2), u, v) + 0.35 * ridged(s + 2, 4, 3, (1, 2), u, v)
    f1, f2, _, _, _ = worley(s + 3, 7, u=warp(s + 4, 5, 0.2)[0], v=warp(s + 4, 5, 0.2)[1])
    caustic = smoothstep(0.22, 0.0, f2 - f1)
    if k == 0:
        h = 0.5 * waves
        c = ramp(waves, "#06204a", "#0e3f7c", "#1f66a8", "#5aa2d6") + rgb(smoothstep(0.7, 0.9, waves)) * 0.25
    elif k == 1:
        # Caustics are light on the bed, not relief.
        h = 0.4 * waves
        c = ramp(waves, "#0a7a8a", "#18a6a8", "#48c8c0", "#8fe4d6") + rgb(caustic) * color("#d8fff0") * 0.45
    else:
        h = 0.4 * waves
        c = ramp(waves, "#1a2a14", "#2e4420", "#4a6230", "#6e8448")
        scum = smoothstep(0.55, 0.7, fbm(s + 6, 5, 5)) * (0.6 + 0.4 * fbm(s + 7, 64, 2))
        c = lerp(c, ramp(fbm(s + 8, 48, 2), "#6a8a2c", "#a8c450"), rgb(scum) * 0.8)
        h = h + 0.15 * scum
    return c, h


def lava(k, s):
    u, v = warp(s, 4, 0.12)
    f1, f2, cell, _, _ = worley(s + 1, [5, 4, 7][k], u=u, v=v)
    crack = smoothstep([0.3, 0.5, 0.12][k], 0.0, f2 - f1)
    flow = fbm(s + 2, 3, 5, u=warp(s + 3, 3, 0.4)[0], v=warp(s + 3, 3, 0.4)[1])
    if k == 0:
        heat = np.clip(0.85 * crack + 0.25 * flow - 0.1, 0.0, 1.0)
    elif k == 1:
        # Mostly molten, with darker rafts of crust.
        heat = np.clip(0.15 + 0.6 * flow + 0.3 * crack - 0.45 * smoothstep(0.5, 0.8, cell), 0.0, 1.0)
    else:
        embers = smoothstep(0.72, 0.9, fbm(s + 4, 10, 4))
        heat = np.clip(0.7 * crack * smoothstep(0.3, 0.6, flow) + 0.5 * embers, 0.0, 1.0)
    crust = fbm(s + 5, 12, 5)
    h = (1.0 - heat) * (0.5 + 0.5 * crust)
    c = ramp(heat, "#120c0a", "#3a0c04", "#a81c04", "#f05a08", "#ffb020", "#fff0a0")
    c = c + rgb((1.0 - smoothstep(0.0, 0.3, heat)) * crust) * color("#3a302c") * 0.8
    # Glow is emitted, so only the crust takes the relief shading.
    return c, h, rgb(smoothstep(0.0, 0.35, heat))


def bark(k, s):
    u, v = warp(s, 4, 0.05)
    if k == 0:
        f1, f2, _, _, _ = worley(s + 1, 12, 3, u=u, v=v)
        h = 0.5 * ridged(s + 2, 3, 5, (6, 1), u, v) + 0.35 * smoothstep(0.0, 0.35, f2 - f1) + 0.15 * fbm(s + 3, 32, 3)
        c = ramp(np.clip(h + 0.2 * (fbm(s + 4, 5, 4) - 0.5), 0.0, 1.0), "#1a120c", "#3a281a", "#5c422c", "#84664a", "#a08868")
    elif k == 1:
        # Birch: pale paper with dark horizontal lenticels and a few scars.
        f1, _, cell, dx, dy = worley(s + 1, 5, 18)
        mark = smoothstep(1.0, 0.5, np.hypot(dx / (0.2 + 0.3 * cell), dy / 0.16)) * (cell > 0.35)
        scar = smoothstep(0.66, 0.8, fbm(s + 2, 3, 4, (1, 2)))
        streaks = fbm(s + 3, 3, 3, (1, 10), u, v)
        h = 0.2 * streaks + 0.15 * fbm(s + 4, 6, 4) - 0.25 * mark - 0.2 * scar
        c = ramp(np.clip(0.75 * streaks + 0.25 * fbm(s + 5, 4, 4), 0.0, 1.0), "#a89e8c", "#cfc8b8", "#e8e2d4", "#f6f2e8")
        c = lerp(c, ramp(fbm(s + 6, 24, 3), "#141210", "#3a3028"), rgb(np.clip(mark + scar, 0.0, 1.0)) * 0.92)
    else:
        # Pine: reddish plates split by deep furrows.
        f1, f2, cell, _, _ = worley(s + 1, 7, 3, u=u, v=v)
        plate = smoothstep(0.0, 0.3, f2 - f1)
        layers = fbm(s + 2, 4, 5, (5, 1), u, v)
        h = 0.6 * plate * (0.6 + 0.4 * cell) + 0.4 * layers
        c = ramp(np.clip(0.5 * layers + 0.5 * cell, 0.0, 1.0), "#4a2416", "#7a4024", "#a45c30", "#c48048", "#8a5a40")
        c = c * rgb(0.3 + 0.7 * plate)
    return c, h


def foliage(k, s):
    count, half_length, half_width, layers = [(7, 0.5, 0.26, 4), (7, 0.48, 0.3, 4), (14, 0.62, 0.12, 7)][k]
    palette = [("#1d4a16", "#3a7a22", "#6aa83a"), ("#8a2a10", "#c8641a", "#e8b030", "#b0901c"),
               ("#143a24", "#2a6a3c", "#58965a")][k]
    c = np.broadcast_to(color(palette[0]) * 0.35, (N, N, 3)).copy()
    h = np.zeros((N, N))
    for layer in range(layers):
        # Jitter stays low so a leaf is rarely cut by its neighbour's cell.
        _, _, cell, dx, dy = worley(s + layer * 7, count, jitter=0.7)
        angle = TAU * ((cell * 7.31) % 1.0)
        along = (dx * np.cos(angle) + dy * np.sin(angle)) / half_length
        across = (-dx * np.sin(angle) + dy * np.cos(angle)) / half_width
        # Pointed at both ends: the width shrinks along the leaf.
        d = np.hypot(along, across / np.maximum(1.0 - along * along, 1e-3) ** 0.5)
        inside = smoothstep(1.0, 0.9, d) * (np.abs(along) < 1.0)
        tone = np.clip((cell * 3.17) % 1.0 * 0.8 + 0.25 * (1.0 - d) + 0.1 * (fbm(s + 40, 24, 2) - 0.5), 0.0, 1.0)
        leaf = ramp(tone, *palette) * (0.55 + 0.45 * (layer + 1) / layers)
        midrib = smoothstep(0.1, 0.0, np.abs(across)) * 0.18
        veins = (0.5 + 0.5 * np.sin(TAU * 5.0 * (np.abs(along) + 0.6 * np.abs(across)))) * 0.06
        leaf = leaf * rgb(1.0 + midrib + veins)
        relief = (layer + np.clip(1.0 - d, 0.0, 1.0) ** 0.5 * (1.0 - 0.5 * smoothstep(0.15, 0.0, np.abs(across)))) / layers
        c = lerp(c, leaf, rgb(inside))
        h = lerp(h, relief, inside)
    return c, h


def wood(k, s):
    if k == 2:
        # Raw timber: ring lines bent around a few knots.
        knots = worley(s, 2, 3, jitter=0.6)[0]
        phase = 8.0 * V + 2.0 * fbm(s + 1, 2, 3, (1, 2)) + 2.0 * smoothstep(0.5, 0.0, knots)
        ring = 0.5 + 0.5 * np.sin(TAU * phase)
        fibre = fbm(s + 2, 4, 4, (1, 24))
        h = 0.2 * ring + 0.3 * fibre
        c = ramp(np.clip(0.6 * ring + 0.4 * fibre, 0.0, 1.0), "#6a4424", "#96683a", "#b88a54", "#d2ac78")
        return c * rgb(1.0 - 0.6 * smoothstep(0.16, 0.02, knots)), h
    planks = [6, 8][k]
    rng = np.random.default_rng(s)
    row = np.floor(V * planks).astype(int)
    fy = V * planks - row
    shift, tint, joint = rng.random(planks)[row], rng.random(planks)[row], rng.random(planks)[row]
    # Each plank samples the grain at its own offset; the noise still wraps along u.
    u = U + shift
    grain = 0.6 * fbm(s + 1, 2, 5, (1, 16), u, V) + 0.4 * (0.5 + 0.5 * np.sin(TAU * (planks * 3 * V + 2.5 * fbm(s + 2, 2, 3, (1, 3), u, V))))
    gap = smoothstep(0.0, 0.05, np.minimum(fy, 1.0 - fy)) * smoothstep(0.0, 0.004, np.abs((U - joint + 0.5) % 1.0 - 0.5))
    h = gap * (0.55 + 0.25 * grain + 0.2 * tint)
    palette = [("#7a5430", "#a47a48", "#c8a06a", "#e0c090"), ("#22140c", "#3e2616", "#5c3a22", "#7a5230")][k]
    c = ramp(np.clip(0.7 * grain + 0.3 * tint, 0.0, 1.0), *palette) * rgb(0.25 + 0.75 * gap)
    if k == 1:
        # A nail head beside each butt joint; fy is rescaled to tile units to keep it round.
        nail = smoothstep(0.012, 0.006, np.hypot((U - joint + 0.5) % 1.0 - 0.53, (fy - 0.5) / planks))
        c = lerp(c, color("#8a8a90"), rgb(nail) * 0.9)
    return c, h


def tiles(rows, columns, seed):
    """Running-bond grid: row and column indices, position inside the cell, a value per cell."""
    row = np.floor(V * rows).astype(int)
    x = U * columns + 0.5 * (row % 2)
    column = np.floor(x).astype(int) % columns
    value = np.random.default_rng(seed).random((rows, columns))[row, column]
    return row, x - np.floor(x), V * rows - row, value


def brick(k, s):
    rows, columns = [(8, 4), (4, 2), (10, 4)][k]
    _, fx, fy, value = tiles(rows, columns, s)
    rough = fbm(s + 1, 24, 4)
    # Edge distance measured in tile units on both axes, so the mortar keeps one width.
    edge = np.minimum(np.minimum(fx, 1.0 - fx) / columns, np.minimum(fy, 1.0 - fy) / rows) + 0.004 * (rough - 0.5)
    face = smoothstep(0.004, 0.012, edge)
    h = face * (0.6 + 0.2 * value + 0.2 * rough) + (1.0 - face) * 0.25 * rough
    mortar = ramp(fbm(s + 2, 48, 3), "#6e6a62", "#a8a296")
    if k == 0:
        c = ramp(np.clip(0.6 * value + 0.4 * rough, 0.0, 1.0), "#5a2218", "#8a3a26", "#a84e32", "#c26a48")
    elif k == 1:
        # Dressed stone blocks.
        h = face * (0.5 + 0.3 * fbm(s + 3, 6, 5) + 0.2 * value) + (1.0 - face) * 0.2 * rough
        c = ramp(np.clip(0.4 * value + 0.6 * fbm(s + 4, 8, 5), 0.0, 1.0), "#55524c", "#7c7870", "#a09a8e", "#bcb6a8")
    else:
        # Whitewash worn back to the brick.
        c = ramp(np.clip(0.6 * value + 0.4 * rough, 0.0, 1.0), "#6a3a28", "#96583a", "#b07850")
        paint = smoothstep(0.35, 0.6, fbm(s + 5, 4, 5) + 0.25 * (value - 0.5))
        c = lerp(c, ramp(rough, "#cfc8b8", "#f2eee4"), rgb(paint) * 0.92)
        mortar = lerp(mortar, color("#d8d2c4"), rgb(paint) * 0.6)
    stain = rgb(0.8 + 0.2 * fbm(s + 6, 3, 4))
    return lerp(mortar, c, rgb(face)) * stain, h


def roof(k, s):
    rows, columns, overhang, rounded = [(6, 6, 0.45, True), (8, 5, 0.12, False), (8, 8, 0.2, False)][k]
    row, fx, fy, value = tiles(rows, columns, s)
    # The tile of the row above hangs over the top of this cell.
    above = (row - 1) % rows
    ax = U * columns + 0.5 * (above % 2)
    above_fx = ax - np.floor(ax)
    above_value = np.random.default_rng(s).random((rows, columns))[above, np.floor(ax).astype(int) % columns]
    reach = overhang * (np.sqrt(np.clip(1.0 - (2.0 * above_fx - 1.0) ** 2, 0.0, 1.0)) if rounded else 1.0)
    covered = fy < reach
    fx, value = np.where(covered, above_fx, fx), np.where(covered, above_value, value)
    along = np.where(covered, 1.0 + fy, fy) / (1.0 + overhang)
    side = smoothstep(0.0, 0.04, np.minimum(fx, 1.0 - fx))
    rough = fbm(s + 1, 20, 4)
    # Tiles rise toward their lower edge, where they sit on the next row.
    h = 0.75 * along + 0.1 * rough
    if k == 0:
        h = h + 0.35 * np.sin(np.pi * fx)
        c = ramp(np.clip(0.6 * value + 0.4 * rough, 0.0, 1.0), "#7a3418", "#a84c24", "#c86a38", "#dc8a54")
    elif k == 1:
        h = h * 0.6 + 0.1 * value
        c = ramp(np.clip(0.6 * value + 0.4 * fbm(s + 2, 6, 5), 0.0, 1.0), "#2a3038", "#434c58", "#5e6876", "#7c8694")
    else:
        # Wooden shingles with a vertical grain and moss in the damp places.
        grain = fbm(s + 2, 4, 4, (16, 1), U + value, V)
        h = h * 0.7 + 0.15 * grain
        c = ramp(np.clip(0.5 * value + 0.5 * grain, 0.0, 1.0), "#3c2e22", "#5e4a36", "#806a50", "#9c8868")
        c = lerp(c, ramp(rough, "#3a5a20", "#6a8a34"), rgb(smoothstep(0.5, 0.75, fbm(s + 3, 4, 5)) * smoothstep(0.6, 0.2, along)) * 0.8)
    h = h * (0.7 + 0.3 * side)
    return c * rgb((0.55 + 0.45 * side) * (0.75 + 0.25 * along)), h


SURFACES = [(grass, 1.0), (rock, 1.4), (snow, 0.9), (sand, 0.9), (water, 0.7), (lava, 1.2), (bark, 1.4),
            (foliage, 0.8), (wood, 0.8), (brick, 1.0), (roof, 0.8)]


# --- Environment cubes -----------------------------------------------------------------

def hash3(ix, iy, iz, seed):
    h = (ix * 374761393 + iy * 668265263 + iz * 1274126177 + seed * 974711) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)


def noise3(x, y, z, seed):
    xi, yi, zi = (np.floor(a).astype(np.int64) for a in (x, y, z))
    xf, yf, zf = (smoothstep(0.0, 1.0, a - i) for a, i in ((x, xi), (y, yi), (z, zi)))
    c = [[[hash3(xi + i, yi + j, zi + k, seed) for k in (0, 1)] for j in (0, 1)] for i in (0, 1)]
    return lerp(lerp(lerp(c[0][0][0], c[1][0][0], xf), lerp(c[0][1][0], c[1][1][0], xf), yf),
                lerp(lerp(c[0][0][1], c[1][0][1], xf), lerp(c[0][1][1], c[1][1][1], xf), yf), zf)


def fbm3(x, y, z, seed, octaves=5):
    total, amplitude, weight = 0.0, 1.0, 0.0
    for octave in range(octaves):
        scale = 2.0 ** octave
        total = total + amplitude * noise3(x * scale, y * scale, z * scale, seed + octave)
        weight += amplitude
        amplitude *= 0.5
    return total / weight


SKIES = {
    "day": dict(seed=11, sky=("#bcd8f0", "#7fb4e8", "#3f84d4", "#1f5cb4"), sun=(0.45, 0.62, 0.64), sun_color="#fff4d8",
                glow=0.25, cloud=("#8c9cb4", "#ffffff"), cover=0.47, haze="#c8dcec", ground=("#5c7a3c", "#3c5628"),
                hills="#6f8ea4", stars=0.0),
    "sunset": dict(seed=23, sky=("#ffb060", "#e87a58", "#8a5a90", "#2c3470"), sun=(0.82, 0.07, 0.56), sun_color="#ffd890",
                   glow=0.9, cloud=("#5a3a5c", "#ff9a5a"), cover=0.44, haze="#f0a070", ground=("#3a2c2c", "#1e1a20"),
                   hills="#5a3c50", stars=0.0),
    "night": dict(seed=37, sky=("#1c2a4c", "#101c3c", "#080f26", "#04081a"), sun=(-0.5, 0.55, 0.67), sun_color="#e8eeff",
                  glow=0.12, cloud=("#0c1224", "#2c3858"), cover=0.56, haze="#20304e", ground=("#0c1418", "#06090c"),
                  hills="#0e1626", stars=1.0),
}


def sky(direction, p):
    """Colour seen along unit directions: a function of direction only, so faces join."""
    x, y, z = direction
    seed = p["seed"]
    sun = np.array(p["sun"]) / np.linalg.norm(p["sun"])
    toward_sun = np.clip(x * sun[0] + y * sun[1] + z * sun[2], 0.0, 1.0)
    up = np.clip(y, 0.0, 1.0)
    c = ramp(up ** 0.45, *p["sky"])
    if p["stars"]:
        cells = [a * 70.0 for a in (x, y, z)]
        index = [np.floor(a).astype(np.int64) for a in cells]
        # Stars keep to the middle of their cell so no neighbour lookup is needed.
        offset = [a - i - (0.25 + 0.5 * hash3(*index, seed + 60 + n)) for n, (a, i) in enumerate(zip(cells, index))]
        brightness = hash3(*index, seed + 70)
        star = np.exp(-(offset[0] ** 2 + offset[1] ** 2 + offset[2] ** 2) / 0.012) * (brightness > 0.86) * (brightness - 0.8) * 5.0
        milky = smoothstep(0.45, 0.75, fbm3(x * 3.0, y * 3.0, z * 3.0, seed + 80)) * smoothstep(0.5, 0.0, np.abs(x * 0.5 + y * 0.6 - z * 0.62))
        c = c + rgb(star * smoothstep(0.0, 0.15, y)) * lerp(color("#ffe8c8"), color("#c8dcff"), rgb(hash3(*index, seed + 90)))
        c = c + rgb(milky * 0.12) * color("#8898c8")
    # Sun or moon: a wide glow, a tight halo and the disc.
    c = c + color(p["sun_color"]) * rgb(p["glow"] * toward_sun ** 6 + 0.5 * toward_sun ** 150 + smoothstep(0.9993, 0.9996, toward_sun))
    # Clouds lie on a plane overhead, which stretches them toward the horizon.
    px, pz = x / (up + 0.18) * 1.1, z / (up + 0.18) * 1.1
    density = fbm3(px + 0.6 * fbm3(px, pz, 3.0, seed + 1, 3), pz, 0.0, seed, 6)
    cloud = smoothstep(p["cover"], p["cover"] + 0.22, density) * smoothstep(0.0, 0.12, y)
    light = np.clip(0.25 + 1.6 * (density - fbm3(px + 0.25 * sun[0], pz + 0.25 * sun[2], 0.0, seed, 4)) + 0.6 * (1.0 - cloud), 0.0, 1.0)
    cloud_color = lerp(color(p["cloud"][0]), color(p["cloud"][1]), rgb(light)) + color(p["sun_color"]) * rgb(0.35 * toward_sun ** 4)
    c = lerp(c, cloud_color, rgb(cloud * 0.92))
    # Distant hills, then the ground fading into the haze at the horizon.
    azimuth = np.arctan2(z, x)
    ridge = 0.015 + 0.09 * fbm3(np.cos(azimuth) * 2.5, np.sin(azimuth) * 2.5, 0.0, seed + 20, 4)
    hills = lerp(color(p["haze"]), color(p["hills"]), 0.75)
    c = lerp(c, hills, rgb(smoothstep(ridge + 0.004, ridge - 0.004, y)))
    c = lerp(c, color(p["haze"]), rgb(smoothstep(0.12, 0.0, np.abs(y)) * 0.45))
    depth = smoothstep(0.0, -0.5, y)
    ground = lerp(color(p["ground"][0]), color(p["ground"][1]), rgb(fbm3(x / (0.2 - np.minimum(y, 0.0)) * 2.0, z / (0.2 - np.minimum(y, 0.0)) * 2.0, 0.0, seed + 30, 4)))
    ground = lerp(lerp(color(p["haze"]), hills, 0.6), ground, rgb(0.25 + 0.75 * depth))
    return lerp(c, ground, rgb(smoothstep(0.004, -0.004, y)))


def cube_faces():
    """Direction per pixel for each face, in the Direct3D cube map layout."""
    t, s = np.meshgrid(2.0 * (np.arange(N) + 0.5) / N - 1.0, 2.0 * (np.arange(N) + 0.5) / N - 1.0, indexing="ij")
    one = np.ones_like(s)
    faces = {"px": (one, -t, -s), "nx": (-one, -t, s), "py": (s, one, t), "ny": (s, -one, -t),
             "pz": (s, -t, one), "nz": (-s, -t, -one)}
    return {name: np.stack(d) / np.linalg.norm(np.stack(d), axis=0) for name, d in faces.items()}


def main():
    for index, (surface, relief) in enumerate(SURFACES):
        for k in range(3):
            c, h, *lit = surface(k, 1000 * (index + 1) + 100 * k)
            light = shade(h, relief)
            if lit:
                light = lerp(light, 1.0, lit[0])
            save(f"{surface.__name__}_{k + 1}", c * light)
    for name, parameters in SKIES.items():
        for face, direction in cube_faces().items():
            save(f"{name}_cube_{face}", sky(direction, parameters))


if __name__ == "__main__":
    main()
