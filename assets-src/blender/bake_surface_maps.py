"""Derive the normal maps and cube faces of the DBPro demos.

Run from the repository root, after the demos' own bakes:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/bake_surface_maps.py

Normal maps treat a texture's brightness as height; the water is tiling filtered noise. Cubes are
procedural skies and rooms in the demos' colours, with one file per face for SET CUBE MAPPING ON,
which takes them in the order +X, -X, +Y, -Y, +Z, -Z. The outputs are recorded in
examples/ASSETS.tsv by scripts/audit_example_assets.py --refresh.
"""

import sys
from pathlib import Path

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DEMOS = ROOT / "examples/dbpro/demos"
CUBE_SIDE = 128
RANDOM = np.random.default_rng(5150)


def load_rgb(path: Path) -> np.ndarray:
    """A texture's sRGB-encoded channels, with row 0 at the top of the image."""
    image = bpy.data.images.load(str(path))
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    bpy.data.images.remove(image)
    return pixels.reshape(height, width, 4)[::-1, :, :3].copy()


def to_linear(encoded: np.ndarray) -> np.ndarray:
    return np.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)


def save_data(encoded: np.ndarray, path: Path) -> None:
    """Writes channels as these very bytes: the saver encodes what it takes as linear light."""
    save_linear(to_linear(np.clip(encoded, 0.0, 1.0)), path)


def save_linear(rgb: np.ndarray, path: Path) -> None:
    height, width, _ = rgb.shape
    pixels = np.ones((height, width, 4), dtype=np.float32)
    pixels[:, :, :3] = rgb[::-1]
    bt.save_jpeg(pixels, path)


def blur(height_map: np.ndarray, passes: int) -> np.ndarray:
    for _ in range(passes):
        height_map = (
            height_map * 4.0
            + (np.roll(height_map, 1, 0) + np.roll(height_map, -1, 0)
               + np.roll(height_map, 1, 1) + np.roll(height_map, -1, 1)) * 2.0
            + np.roll(np.roll(height_map, 1, 0), 1, 1) + np.roll(np.roll(height_map, 1, 0), -1, 1)
            + np.roll(np.roll(height_map, -1, 0), 1, 1) + np.roll(np.roll(height_map, -1, 0), -1, 1)
        ) / 16.0
    return height_map


def normals_of(height_map: np.ndarray, strength: float) -> np.ndarray:
    """Tangent-space normals, encoded: red follows U and green follows V, down the image."""
    slope_u = (np.roll(height_map, -1, 1) - np.roll(height_map, 1, 1)) * 0.5 * strength
    slope_v = (np.roll(height_map, -1, 0) - np.roll(height_map, 1, 0)) * 0.5 * strength
    normal = np.stack((-slope_u, -slope_v, np.ones_like(height_map)), axis=2)
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    return normal * 0.5 + 0.5


def brightness(rgb: np.ndarray) -> np.ndarray:
    return rgb @ np.array((0.299, 0.587, 0.114), dtype=np.float32)


def normal_from_texture(source: Path, target: Path, strength: float, smoothing: int = 1) -> None:
    save_data(normals_of(blur(brightness(load_rgb(source)), smoothing), strength), target)


def water_height(size: int) -> np.ndarray:
    """Noise filtered in frequency so that it tiles: long swells under small ripples."""
    frequency_u = np.fft.fftfreq(size)[:, None] * size
    frequency_v = np.fft.rfftfreq(size)[None, :] * size
    radius = np.maximum(np.hypot(frequency_u, frequency_v), 1.0)
    amplitude = radius ** -1.6 * (radius >= 3.0) * np.exp(-((radius / 70.0) ** 2))
    phase = RANDOM.uniform(0.0, 2.0 * np.pi, amplitude.shape)
    height_map = np.fft.irfft2(amplitude * np.exp(1j * phase), s=(size, size))
    return (height_map / np.abs(height_map).max()).astype(np.float32)


def cube_directions(side: int) -> list[np.ndarray]:
    """Each face's unit directions, with the faces laid out as the GPU samples them."""
    axis = (np.arange(side, dtype=np.float32) + 0.5) / side * 2.0 - 1.0
    u, v = np.meshgrid(axis, axis)
    one = np.ones_like(u)
    faces = ((one, -v, -u), (-one, -v, u), (u, one, v), (u, -one, -v), (u, -v, one), (-u, -v, -one))
    directions = []
    for x, y, z in faces:
        direction = np.stack((x, y, z), axis=2)
        directions.append(direction / np.linalg.norm(direction, axis=2, keepdims=True))
    return directions


def save_cube(shade, folder: Path, name: str) -> None:
    for face, direction in zip(("px", "nx", "py", "ny", "pz", "nz"), cube_directions(CUBE_SIDE)):
        save_linear(shade(direction), folder / f"{name}_{face}.jpg")


def gradient(height: np.ndarray, stops) -> np.ndarray:
    """Linear colours between (height, rgb) stops."""
    keys = [stop[0] for stop in stops]
    channels = [np.interp(height, keys, [stop[1][c] for stop in stops]) for c in range(3)]
    return np.stack(channels, axis=2).astype(np.float32)


def rgb(red: int, green: int, blue: int) -> tuple:
    return tuple(float(to_linear(np.array(value / 255.0))) for value in (red, green, blue))


def sun_glow(direction: np.ndarray, sun, color, tightness: float, strength: float) -> np.ndarray:
    towards = np.clip(direction @ np.array(sun, dtype=np.float32), 0.0, 1.0)
    return (towards ** tightness)[:, :, None] * np.array(color, dtype=np.float32) * strength


def clouds(direction: np.ndarray, seed: float) -> np.ndarray:
    """Streaks above the horizon, flattened as distant cloud layers are."""
    y = np.maximum(direction[:, :, 1], 0.02)
    x, z = direction[:, :, 0] / y, direction[:, :, 2] / y
    value = (np.sin(x * 1.3 + seed) * np.sin(z * 0.9 - seed) + np.sin(x * 3.1 - z * 2.3 + seed) * 0.5
             + np.sin(x * 7.0 + z * 5.0) * 0.2)
    return (np.clip(value, 0.0, 1.0) * np.clip(direction[:, :, 1] * 6.0, 0.0, 1.0)
            * np.clip((1.0 - direction[:, :, 1]) * 1.5, 0.0, 1.0))[:, :, None]


def dusk_sky(direction: np.ndarray) -> np.ndarray:
    sun = (0.798, 0.225, -0.559)
    sky = gradient(direction[:, :, 1],
                   ((-1.0, rgb(40, 30, 40)), (-0.02, rgb(150, 100, 95)), (0.0, rgb(245, 165, 110)),
                    (0.12, rgb(205, 135, 125)), (0.4, rgb(95, 95, 150)), (1.0, rgb(40, 50, 105))))
    sky = sky * (1.0 - clouds(direction, 1.7) * 0.35) + clouds(direction, 1.7) * np.array(rgb(255, 170, 130)) * 0.35
    return sky + sun_glow(direction, sun, rgb(255, 180, 110), 24.0, 0.8) + sun_glow(direction, sun, rgb(255, 235, 200),
                                                                                  900.0, 6.0)


def garden_sky(direction: np.ndarray) -> np.ndarray:
    sun = (-0.42, 0.52, -0.74)
    sky = gradient(direction[:, :, 1],
                   ((-1.0, rgb(150, 130, 175)), (-0.1, rgb(225, 205, 225)), (0.05, rgb(230, 215, 225)),
                    (0.35, rgb(130, 165, 225)), (1.0, rgb(60, 95, 180))))
    sky = sky + clouds(direction, 0.4) * np.array(rgb(255, 245, 240)) * 0.5
    return sky + sun_glow(direction, sun, rgb(255, 225, 170), 40.0, 0.7) + sun_glow(direction, sun, rgb(255, 250, 225),
                                                                                  1200.0, 8.0)


def reactor_room(direction: np.ndarray) -> np.ndarray:
    """A dim hall: panelled walls, a ceiling of lamp rows and the core's glow from one side."""
    room = gradient(direction[:, :, 1],
                    ((-1.0, rgb(22, 26, 32)), (-0.25, rgb(38, 44, 52)), (0.3, rgb(52, 58, 68)),
                     (1.0, rgb(30, 34, 42))))
    # Panels and lamps follow the room's walls, so they stay straight across a cube's faces.
    largest = np.abs(direction).max(axis=2, keepdims=True)
    on_box = direction / largest
    panels = (np.abs(((on_box[:, :, 0] + on_box[:, :, 2]) * 3.0) % 1.0 - 0.5) < 0.03)[:, :, None]
    room = room * (1.0 - panels * 0.5)
    ceiling = on_box[:, :, 1] > 0.999
    rows = np.abs((on_box[:, :, 0] * 2.5) % 1.0 - 0.5) < 0.09
    lamps = (ceiling & rows)[:, :, None]
    room = room * (1.0 - lamps) + lamps * np.array(rgb(215, 235, 255)) * 2.5
    strips = ((np.abs(on_box[:, :, 1] - 0.55) < 0.02) & ~ceiling)[:, :, None]
    room = room + strips * np.array(rgb(255, 170, 90)) * 0.9
    return room + sun_glow(direction, (0.6, 0.05, 0.8), rgb(90, 200, 255), 10.0, 0.5)


def main() -> None:
    bt.setup_bake_scene()

    rally = DEMOS / "dusk_rally/textures"
    save_data(normals_of(water_height(512), 13.0), rally / "water_normal.jpg")
    normal_from_texture(rally / "asphalt.jpg", rally / "asphalt_normal.jpg", 2.4)
    save_cube(dusk_sky, rally, "sky_cube")

    gardens = DEMOS / "sky_gardens/textures"
    normal_from_texture(gardens / "wood.jpg", gardens / "wood_normal.jpg", 7.0)
    save_cube(garden_sky, gardens, "sky_cube")

    reactor = DEMOS / "reactor_breach/textures"
    normal_from_texture(reactor / "wall.jpg", reactor / "wall_normal.jpg", 7.0)
    normal_from_texture(reactor / "floor.jpg", reactor / "floor_normal.jpg", 6.0)
    normal_from_texture(reactor / "ceiling.jpg", reactor / "ceiling_normal.jpg", 7.0)
    normal_from_texture(reactor / "machine.jpg", reactor / "machine_normal.jpg", 6.0)
    save_cube(reactor_room, reactor, "room_cube")


main()
