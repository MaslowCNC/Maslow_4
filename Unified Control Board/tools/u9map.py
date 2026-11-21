# u9map.py <new.kicad_pcb> <old.kicad_pcb> <out.png>: top-down map of the lower board (x -38..18, y 38..78 mm): the
# buck's parts where they were (red outlines) and where they are (orange), the sense nets (cyan), the sense RC block
# (blue), the G474 (grey), U9_SYNC (green) and the board edge.  Run with KiCad 10's Python.
import sys, re, math, pcbnew, numpy as np, zlib, struct
TO = pcbnew.ToMM
newf, oldf, out = sys.argv[1:4]
X0, Y0, X1, Y1, PX = -38.0, 38.0, 18.0, 78.0, 14
W, H = int((X1 - X0) * PX), int((Y1 - Y0) * PX)
img = np.full((H, W, 3), 255, np.uint8)
def P(x, y): return int((x - X0) * PX), int((y - Y0) * PX)
def put(x, y, col, w):
    if 0 <= x < W and 0 <= y < H:
        img[max(0, y - w):min(H, y + w + 1), max(0, x - w):min(W, x + w + 1)] = col
def line(a, b, col, w=2):
    (xa, ya), (xb, yb) = P(*a), P(*b); n = max(abs(xb - xa), abs(yb - ya), 1)
    for k in range(n + 1): put(xa + (xb - xa) * k // n, ya + (yb - ya) * k // n, col, w)
def box(f, col, fill=True):
    bb = f.GetBoundingBox(False)
    (a, b_), (c, d) = P(TO(bb.GetLeft()), TO(bb.GetTop())), P(TO(bb.GetRight()), TO(bb.GetBottom()))
    a, c = sorted((max(0, min(W, a)), max(0, min(W, c)))); b_, d = sorted((max(0, min(H, b_)), max(0, min(H, d))))
    if fill: img[b_:d, a:c] = col
    else:
        img[b_:b_ + 2, a:c] = col; img[d - 2:d, a:c] = col; img[b_:d, a:a + 2] = col; img[b_:d, c - 2:c] = col
new, old = pcbnew.LoadBoard(newf), pcbnew.LoadBoard(oldf)
BUCK = ["U9", "L1", "D5", "C31", "C32", "R27", "R29", "R30", "C165", "R31", "C29", "C30", "C33", "C23"]
RC = ["R116", "R117", "R118", "R146", "R147", "R148", "R119", "R120", "R121", "R149", "R150", "R151",
      "C106", "C107", "C108", "C126", "C127", "C128"]
for f in new.GetFootprints():
    if TO(f.GetBoundingBox(False).GetWidth()) < 15 and f.GetReference() not in BUCK: box(f, (228, 228, 228))
for r in RC: box(new.FindFootprintByReference(r), (150, 195, 255))
box(new.FindFootprintByReference("U20"), (120, 120, 120))
for t in new.GetTracks():
    if t.GetClass() != "PCB_VIA" and re.fullmatch(r"IS[ABC][12]|S[ABC][12]", t.GetNetname() or ""):
        line((TO(t.GetStart().x), TO(t.GetStart().y)), (TO(t.GetEnd().x), TO(t.GetEnd().y)), (0, 190, 215), 1)
for r in BUCK: box(new.FindFootprintByReference(r), (255, 165, 50))
for r in BUCK: box(old.FindFootprintByReference(r), (220, 30, 30), fill=False)
for t in new.GetTracks():
    if t.GetNetname() == "U9_SYNC":
        if t.GetClass() == "PCB_VIA": put(*P(TO(t.GetPosition().x), TO(t.GetPosition().y)), (0, 150, 40), 4)
        else: line((TO(t.GetStart().x), TO(t.GetStart().y)), (TO(t.GetEnd().x), TO(t.GetEnd().y)), (0, 160, 40), 2)
poly = pcbnew.SHAPE_POLY_SET(); new.GetBoardPolygonOutlines(poly, True)
o = poly.Outline(0); pts = [(TO(o.CPoint(i).x), TO(o.CPoint(i).y)) for i in range(o.PointCount())]
for a, c in zip(pts, pts[1:] + pts[:1]): line(a, c, (90, 90, 90), 1)
def png(path, arr):
    h, w, _ = arr.shape; raw = b"".join(b"\x00" + arr[y].tobytes() for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
png(out, img); print("written", W, H)
