"""Procedural painted textures for the canyon kit v2 (docs/references/kit-v2).

Every map is painted with numpy from docs/palette.json roles. Every map tiles, so the kit uses
world-metre UVs and neighbouring pieces join with continuous texture. The art direction sets
the limits:
  * colour plus an optional normal map, no roughness maps;
  * flat-lit (no painted directional shadow);
  * broad shapes, no photographic grain;
  * baked contact darkening leans toward the shadow colour and never goes below
    minBakedBrightness.

Arrays are (rows, cols, 3) sRGB floats with row 0 at the BOTTOM (v = 0), which matches the
order of Blender's image.pixels.
"""
import json
import os

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))
PALETTE_JSON = os.path.join(ROOT, "docs", "palette.json")


def load_roles():
    with open(PALETTE_JSON, encoding="utf-8") as f:
        data = json.load(f)
    roles = {}
    for group in ("environment", "teams", "powers", "ui", "pickups"):
        for role, value in data.get(group, {}).items():
            if isinstance(value, dict):
                for variant, hexcolor in value.items():
                    roles[f"{role}.{variant}"] = hexcolor
            else:
                roles[role] = value
    return roles, data["rules"]


ROLES, RULES = load_roles()


def rgb(value):
    """Palette role or hex -> np.array of sRGB floats."""
    value = ROLES.get(value, value)
    if isinstance(value, str):
        value = value.lstrip("#")
        return np.array([int(value[k:k + 2], 16) / 255 for k in (0, 2, 4)])
    return np.asarray(value, dtype=float)


def mix(a, b, t):
    a, b = rgb(a), rgb(b)
    t = np.asarray(t, dtype=float)
    return a + (b - a) * t[..., None] if t.ndim else a + (b - a) * t


def _lin(c):
    c = np.asarray(c)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def luma(c):
    lin = _lin(c)
    return lin[..., 0] * 0.2126 + lin[..., 1] * 0.7152 + lin[..., 2] * 0.0722


SHADOW = rgb(RULES["shadowColor"])
MIN_BAKED = RULES["minBakedBrightness"]


def darken(colour, amount):
    """Contact darkening toward the cool shadow colour, clamped to minBakedBrightness."""
    colour = np.asarray(colour, dtype=float)
    amount = np.asarray(amount, dtype=float)
    if amount.ndim:
        amount = amount[..., None]
    out = colour + (SHADOW - colour) * amount
    floor = MIN_BAKED * luma(colour)
    # Back off toward the original until the luminance floor holds.
    for _ in range(12):
        low = luma(out) < floor
        if not np.any(low):
            break
        out = np.where(low[..., None], (out + colour) / 2, out)
    return out


def lighten(colour, amount):
    colour = np.asarray(colour, dtype=float)
    return colour + (1 - colour) * amount


# ---------------------------------------------------------------- noise and helpers
def periodic_noise(h, w, sigma_v, sigma_u, seed):
    """Smooth tileable noise in [-1, 1]. sigma_* are feature sizes in pixels along each axis."""
    rng = np.random.default_rng(seed)
    white = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    kernel = np.exp(-2 * (np.pi ** 2) * ((fy * sigma_v) ** 2 + (fx * sigma_u) ** 2))
    field = np.real(np.fft.ifft2(np.fft.fft2(white) * kernel))
    field -= field.mean()
    return field / (np.abs(field).max() + 1e-9)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def grid(h, w):
    v, u = np.meshgrid((np.arange(h) + 0.5) / h, (np.arange(w) + 0.5) / w, indexing="ij")
    return u, v


def normal_from_height(height, strength, tile_u=True, tile_v=True):
    """Tangent-space normal map (OpenGL / glTF convention, +v up) from a height field in pixels."""
    pad_u = "wrap" if tile_u else "edge"
    pad_v = "wrap" if tile_v else "edge"
    hp = np.pad(height, ((1, 1), (0, 0)), mode=pad_v)
    hp = np.pad(hp, ((0, 0), (1, 1)), mode=pad_u)
    du = (hp[1:-1, 2:] - hp[1:-1, :-2]) * 0.5
    dv = (hp[2:, 1:-1] - hp[:-2, 1:-1]) * 0.5
    n = np.stack([-du * strength, -dv * strength, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


# ---------------------------------------------------------------- clay cover / cliffs
# The side texture covers 1 m along the wall (u) by 1 m of height (v = 1 at the wall top).
# Lower pieces (0.5 m walls, 0.6 m plateaus) show the upper part, so the cap always lines up.
CLAY_CAP = 0.86     # top of the highest stratum; the coral cap sits above it
CLAY_SEAMS = (0.86, 0.6, 0.32)


def _wave(u, seed, amplitude):
    """Tileable gentle wave (integer frequencies only) for strata boundaries."""
    rng = np.random.default_rng(seed)
    out = np.zeros_like(u)
    for k, a in ((1, 1.0), (2, 0.55), (3, 0.35), (5, 0.18)):
        out += a * np.sin(2 * np.pi * (k * u + rng.uniform()))
    return out / 2.08 * amplitude


def clay_side(size=512, cap_role="wall_top.base"):
    h = w = size
    u, v = grid(h, w)
    cap = rgb(cap_role)
    upper = mix("wall_side.base", cap_role, 0.3)
    middle = rgb("wall_side.base")
    lower = mix("wall_side.base", "wall_side.alt", 0.9)
    seam_colour = darken(rgb("wall_side.alt"), 0.35)
    # Wavy boundaries, each its own phase.
    bounds = [s + _wave(u, 11 + i, 0.026 if i else 0.012) for i, s in enumerate(CLAY_SEAMS)]
    colour = np.empty((h, w, 3))
    height = np.zeros((h, w))
    layers = [(bounds[0], np.full_like(v, 2.0), cap), (bounds[1], bounds[0], upper),
              (bounds[2], bounds[1], middle), (np.full_like(v, -1.0), bounds[2], lower)]
    for lo, hi, base in layers:
        inside = (v >= lo) & (v < hi)
        t = np.clip((v - lo) / np.maximum(hi - lo, 1e-3), 0, 1)
        if base is cap:
            shade = 1.0 + 0.0 * t
            col = base[None, None, :] * np.ones_like(v)[..., None]
            # Light rim on the cap's top edge (the rounded lip in the references).
            col = np.where((v > 0.965)[..., None], lighten(col, 0.10), col)
        else:
            # Each stratum is a slightly rounded ledge: lit along its upper part, quietly
            # darker just above the seam below it.
            shade = 0.9 + 0.13 * smoothstep(0.0, 0.85, t)
            col = base[None, None, :] * shade[..., None]
        colour = np.where(inside[..., None], col, colour)
        bulge = np.sin(np.pi * t) * 3.0 if base is not cap else np.zeros_like(t)
        height = np.where(inside, bulge, height)
    # Soft dark seams between strata (shallow, close in value: no outline around every band).
    for i, b in enumerate(bounds):
        d = np.abs(v - b)
        width = 0.018 if i else 0.012
        k = 1 - smoothstep(width * 0.3, width, d)
        colour = colour + (seam_colour - colour) * (0.5 * k)[..., None]
        height -= k * 4.0
    # Broad mottling within the bands plus a few horizontal streaks.
    blot = periodic_noise(h, w, 28, 70, 5)
    streak = periodic_noise(h, w, 12, 120, 6)
    colour *= (1 + 0.05 * blot + 0.012 * streak)[..., None]
    # Baked contact darkening in the lowest 15 cm of a full-height wall.
    contact = 1 - smoothstep(0.0, 0.15, v)
    colour = darken(colour, 0.35 * contact)
    height += 1.2 * streak
    return np.clip(colour, 0, 1), normal_from_height(height, 0.9, tile_v=False)


def clay_top(size=512, role="wall_top.base", alt="wall_top.alt"):
    """Cap surface of cover and cliffs, tiling every metre: quiet patches of the alt tone."""
    h = w = size
    base = rgb(role)
    patches = periodic_noise(h, w, 55, 55, 21)
    fine = periodic_noise(h, w, 10, 10, 22)
    colour = base + (mix(role, alt, 1.0) - base) * smoothstep(0.1, 0.7, patches)[..., None] * 0.8
    colour = colour * (1 + 0.035 * fine)[..., None]
    colour = np.where((patches < -0.55)[..., None], lighten(colour, 0.05), colour)
    height = patches * 2 + fine * 1.5
    return np.clip(colour, 0, 1), normal_from_height(height, 0.6)


# ---------------------------------------------------------------- sand
def sand(size=512, metres=2.0, speckles=True):
    """Peach sand tiling every `metres`: broad, quiet tonal variation and sparse small accents."""
    h = w = size
    base = rgb("floor_sand.base")
    large = periodic_noise(h, w, 90 / metres, 90 / metres, 31)
    medium = periodic_noise(h, w, 30 / metres, 30 / metres, 32)
    light = lighten(base, 0.07)
    shade = mix("floor_sand.base", "floor_sand.shade", 0.45)
    colour = np.broadcast_to(base, (h, w, 3)).copy()
    colour += (light - base) * smoothstep(0.15, 0.8, large)[..., None]
    colour += (shade - base) * smoothstep(0.2, 0.9, -large)[..., None] * 0.5
    colour *= (1 + 0.025 * medium)[..., None]
    if speckles:
        rng = np.random.default_rng(33)
        u, v = grid(h, w)
        speck = mix("floor_sand.base", "floor_sand.edge", 0.6)
        for _ in range(int(14 * metres * metres)):
            cu, cv, r = rng.uniform(), rng.uniform(), rng.uniform(0.004, 0.009) / metres
            du = np.minimum(np.abs(u - cu), 1 - np.abs(u - cu))
            dv = np.minimum(np.abs(v - cv), 1 - np.abs(v - cv))
            k = 1 - smoothstep(r * 0.6, r, np.hypot(du, dv * 1.3))
            colour += (speck - colour) * (0.6 * k)[..., None]
    return np.clip(colour, 0, 1)


# ---------------------------------------------------------------- wood
def wood(size=512):
    """Plank wood, 1 m along the grain (u) by 1 m across (v): long soft grain streaks and a few
    knots. Plank divisions are modelled geometry, so the texture carries only the grain."""
    h = w = size
    base = rgb("crate_wood.base")
    hi = rgb("crate_wood.highlight")
    band = rgb("crate_wood.band")
    grain = periodic_noise(h, w, 3.5, 120, 41)
    broad = periodic_noise(h, w, 30, 160, 42)
    colour = base + (hi - base) * (0.35 + 0.35 * broad)[..., None]
    lines = smoothstep(0.35, 0.75, grain)
    colour += (band - colour) * (0.28 * lines)[..., None]
    height = -lines * 2 + broad
    return np.clip(colour, 0, 1), normal_from_height(height, 0.8)


def wood_end(size=256):
    """End grain for post tops, covering 0.25 m x 0.25 m centred on the post: soft rings."""
    h = w = size
    u, v = grid(h, w)
    r = np.hypot(u - 0.5, v - 0.5)
    wobble = periodic_noise(h, w, 12, 12, 43) * 0.01
    rings = 0.5 + 0.5 * np.sin((r + wobble) * 2 * np.pi * 9)
    base = mix("crate_wood.highlight", "rope.highlight", 0.25)
    ring = mix("crate_wood.base", "crate_wood.highlight", 0.3)
    colour = base + (ring - base) * (0.55 * smoothstep(0.55, 0.9, rings))[..., None]
    colour = np.where((r > 0.46)[..., None], mix("crate_wood.base", "crate_wood.band", 0.15), colour)
    return np.clip(colour, 0, 1)


def rope(w=64, h=256):
    """Twisted rope: u runs along 0.25 m of rope, v around it."""
    u, v = grid(h, w)
    twist = 0.5 + 0.5 * np.sin(2 * np.pi * (u * 6 + v * 2))
    base, hi = rgb("rope.base"), rgb("rope.highlight")
    colour = base + (hi - base) * smoothstep(0.3, 0.9, twist)[..., None]
    colour = darken(colour, 0.25 * (1 - smoothstep(0.05, 0.35, twist)))
    return np.clip(colour, 0, 1)


# ---------------------------------------------------------------- foliage
def grass_blade(w=64, h=256):
    """One golden concealment blade: v runs root (0) to tip (1), u across the blade.
    Orange root and edges, yellow body and tip, a soft lighter centre crease."""
    u, v = grid(h, w)
    root, base, high = rgb("concealment_grass.root"), rgb("concealment_grass.base"), rgb("concealment_grass.highlight")
    mid = root + (base - root) * 0.55
    colour = root + (mid - root) * smoothstep(0.2, 0.55, v)[..., None]
    colour += (base - colour) * smoothstep(0.55, 0.9, v)[..., None]
    colour += (high - colour) * smoothstep(0.88, 1.0, v)[..., None]
    across = np.abs(u - 0.5) * 2
    colour += (root - colour) * (0.5 * smoothstep(0.45, 1.0, across) * (1 - 0.6 * v))[..., None]
    crease = 1 - smoothstep(0.0, 0.18, across)
    colour += (high - colour) * (0.25 * crease * smoothstep(0.2, 0.6, v))[..., None]
    colour = darken(colour, 0.38 * (1 - smoothstep(0.0, 0.22, v)))
    return np.clip(colour, 0, 1)


def leaf(size=256):
    """Broad green leaf for outskirts plants: u across, v base (0) to tip (1); a light midrib,
    soft side veins and slightly darker edges."""
    u, v = grid(size, size)
    base, high, shade = rgb("bush.base"), rgb("bush.highlight"), rgb("bush.shade")
    across = np.abs(u - 0.5) * 2
    colour = base + (high - base) * (0.55 * (1 - across) * smoothstep(0.1, 0.9, v))[..., None]
    colour += (shade - colour) * (0.6 * smoothstep(0.65, 1.0, across))[..., None]
    midrib = 1 - smoothstep(0.015, 0.05, np.abs(u - 0.5))
    colour += (lighten(high, 0.15) - colour) * (0.7 * midrib)[..., None]
    veins = 0.5 + 0.5 * np.cos(2 * np.pi * (v * 7 - across * 1.6))
    colour += (high - colour) * (0.18 * smoothstep(0.85, 1.0, veins) * (across < 0.85))[..., None]
    colour = darken(colour, 0.3 * (1 - smoothstep(0.0, 0.2, v)))
    return np.clip(colour, 0, 1)


def cactus(w=64, h=256):
    """Cactus body, v base (0) to top (1): shaded base, bright crown."""
    u, v = grid(h, w)
    shade, base, high = rgb("cactus.shade"), rgb("cactus.base"), rgb("cactus.highlight")
    colour = shade + (base - shade) * smoothstep(0.0, 0.35, v)[..., None]
    colour += (high - colour) * (0.55 * smoothstep(0.55, 1.0, v))[..., None]
    stripes = 0.5 + 0.5 * np.cos(2 * np.pi * u * 2)
    colour *= (1 + 0.04 * stripes)[..., None]
    colour = darken(colour, 0.3 * (1 - smoothstep(0.0, 0.15, v)))
    return np.clip(colour, 0, 1)


def water(w=256, h=4):
    """Shore gradient sampled by u: 0 at the bank (light shore band) to 1 (open water)."""
    u, _ = grid(h, w)
    shore, shallow, deep = rgb("water.shore"), rgb("water.shallow"), rgb("water.deep")
    body = shallow + (deep - shallow) * (0.35 * smoothstep(0.55, 1.0, u))[..., None]
    k = smoothstep(0.32, 0.5, u)
    colour = shore + (body - shore) * k[..., None]
    return np.clip(colour, 0, 1)


# ---------------------------------------------------------------- writing
TEXTURES = {
    # name: (painter, has_normal)
    "clay_side": (clay_side, True),
    "clay_top": (clay_top, True),
    "sand": (sand, False),
    "wood": (wood, True),
    "wood_end": (wood_end, False),
    "rope": (rope, False),
    "grass_blade": (grass_blade, False),
    "leaf": (leaf, False),
    "cactus": (cactus, False),
    "water": (water, False),
}


def paint(name):
    painter, has_normal = TEXTURES[name]
    out = painter()
    if has_normal:
        return out
    return out, None


def save_png(array, path):
    """Write an (h, w, 3) float array (row 0 = bottom) as an 8-bit sRGB PNG without bpy."""
    import struct
    import zlib

    data = (np.clip(array, 0, 1) * 255 + 0.5).astype(np.uint8)[::-1]
    h, w = data.shape[:2]
    raw = b"".join(b"\x00" + data[y].tobytes() for y in range(h))

    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    return path


def write_all(directory):
    written = {}
    for name in TEXTURES:
        colour, normal = paint(name)
        written[name] = save_png(colour, os.path.join(directory, f"{name}.png"))
        if normal is not None:
            written[name + "_n"] = save_png(normal, os.path.join(directory, f"{name}_n.png"))
    return written


if __name__ == "__main__":
    import sys
    for name, path in write_all(sys.argv[1] if len(sys.argv) > 1 else "build/kit-v2-textures").items():
        print(name, path)
