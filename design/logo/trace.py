# Traces the black shapes of a logo image into one SVG path (the "d"
# attribute), scaled to a 128x128 icon. Writes OUT and OUT-outer (the head's
# silhouette alone, for drafts/traced-solid.svg).
#
#   python3 -m venv venv && venv/bin/pip install potracer pillow numpy
#   venv/bin/python trace.py sahi-logo.jpeg path.txt
#
# The crop below is where the head sits in sahi-logo.jpeg.
import sys
import numpy as np
import potrace
from PIL import Image, ImageFilter

src, out = sys.argv[1], sys.argv[2]
img = Image.open(src).convert("L").crop((405, 82, 1005, 682))
img = img.resize((1200, 1200), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.2))
bits = np.array(img) >= 128  # potracer: False (dark) is ink
bmp = potrace.Bitmap(bits)
path = bmp.trace(turdsize=20, alphamax=0.6, opticurve=True, opttolerance=0.2)

s = 128 / 1200
def p(pt):
    return f"{pt.x * s:.2f} {pt.y * s:.2f}"

d = []
for curve in path:
    d.append(f"M{p(curve.start_point)}")
    for seg in curve.segments:
        if seg.is_corner:
            d.append(f"L{p(seg.c)}L{p(seg.end_point)}")
        else:
            d.append(f"C{p(seg.c1)} {p(seg.c2)} {p(seg.end_point)}")
    d.append("Z")
with open(out, "w") as f:
    f.write("".join(d))
print(len(path), "curves")

# The outermost curve alone: the head's silhouette, to fill underneath.
outer = max(path, key=lambda c: len(c.segments))
d = [f"M{p(outer.start_point)}"]
for seg in outer.segments:
    if seg.is_corner:
        d.append(f"L{p(seg.c)}L{p(seg.end_point)}")
    else:
        d.append(f"C{p(seg.c1)} {p(seg.c2)} {p(seg.end_point)}")
d.append("Z")
with open(out.replace(".txt", "-outer.txt"), "w") as f:
    f.write("".join(d))
