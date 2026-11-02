"""Builds trace.ico (PNG frames at 16..256 px) from src/assets/icons/app_icon.png.

Run from the repository root after the icon changes:  python src/platform/windows/make_ico.py
Needs Pillow. The .ico is committed, so a normal build does not need Python.
"""
import io
import struct
from pathlib import Path

from PIL import Image

SIZES = (16, 24, 32, 48, 64, 128, 256)
root = Path(__file__).resolve().parents[3]
source = Image.open(root / "src/assets/icons/app_icon.png").convert("RGBA")

frames = []
for size in SIZES:
    frame = source if source.size == (size, size) else source.resize((size, size), Image.LANCZOS)
    buffer = io.BytesIO()
    frame.save(buffer, format="PNG", optimize=True)
    frames.append((size, buffer.getvalue()))

header = struct.pack("<HHH", 0, 1, len(frames))
offset = len(header) + 16 * len(frames)
directory = b""
data = b""
for size, png in frames:
    dim = 0 if size >= 256 else size  # 0 means 256 in an icon directory
    directory += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset + len(data))
    data += png
(root / "src/platform/windows/trace.ico").write_bytes(header + directory + data)
print("wrote trace.ico:", len(header + directory + data), "bytes")
