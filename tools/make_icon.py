"""Generate a minimal multi-size ICO for ArcTray (no PIL dependency)."""
import struct, os

SIZES = [16, 20, 24, 32, 48, 64]

def render(size):
    # 32bpp BGRA, bottom-up rows
    px = [[(0, 0, 0, 0) for _ in range(size)] for _ in range(size)]
    c = (size - 1) / 2.0
    r_out = size * 0.46
    r_in = size * 0.30
    for y in range(size):
        for x in range(size):
            dx, dy = x - c, y - c
            d = (dx * dx + dy * dy) ** 0.5
            if d <= r_out:
                if d >= r_in:
                    # ring: green->cyan gradient by angle
                    t = (dy / max(r_out, 1) + 1) / 2
                    g = 200
                    b = int(120 + 120 * t)
                    r = int(30 + 40 * (1 - t))
                    px[y][x] = (b, g, r, 255)
                else:
                    # inner: dark plate
                    px[y][x] = (24, 18, 14, 255)
    # three "fins" (heatsink hint) inside the plate
    fin_w = max(1, size // 12)
    for i, off in enumerate((-0.18, 0.0, 0.18)):
        x0 = int(c + off * size - fin_w / 2)
        y0 = int(size * 0.32)
        y1 = int(size * 0.68)
        for y in range(y0, y1):
            for x in range(x0, x0 + fin_w):
                if 0 <= x < size and 0 <= y < size:
                    px[y][x] = (200, 220, 240, 255)
    return px

def bmp_bytes(px, size):
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = []
    for y in range(size - 1, -1, -1):  # bottom-up
        row = b"".join(struct.pack("<BBBB", *px[y][x]) for x in range(size))
        rows.append(row)
    and_mask = b"\x00" * (((size + 31) // 32) * 4 * size)
    return header + b"".join(rows) + and_mask

def main():
    images = []
    for s in SIZES:
        data = bmp_bytes(render(s), s)
        images.append((s, data))
    out = bytearray()
    out += struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in images:
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "arc.ico")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)
    print("wrote", os.path.abspath(path), len(out), "bytes")

if __name__ == "__main__":
    main()
