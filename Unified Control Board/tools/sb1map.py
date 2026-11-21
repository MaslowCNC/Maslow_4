# sb1map.py <new.kicad_pcb> <old.kicad_pcb> <out.png>: top-down map of the U9 / current-sense corner (x -36..12,
# y 38..74 mm) with the old SB1 in red, the new SB1 in green, U9's buck parts in orange, the sense RC block in
# blue, the parts v1.1 added in purple and the unrouted U9_SYNC as a dashed line.  Run with KiCad 10's Python.
import sys, pcbnew, numpy as np, zlib, struct
TO = pcbnew.ToMM
newf, oldf, out = sys.argv[1:4]
X0, Y0, X1, Y1, PX = -36.0, 38.0, 12.0, 74.0, 14
W, H = int((X1 - X0) * PX), int((Y1 - Y0) * PX)
img = np.full((H, W, 3), 255, np.uint8)
def P(x, y): return int((x - X0) * PX), int((y - Y0) * PX)
def put(x, y, col, w):
    if 0 <= x < W and 0 <= y < H:
        img[max(0, y - w):min(H, y + w + 1), max(0, x - w):min(W, x + w + 1)] = col
def line(a, b, col, w=2):
    (xa, ya), (xb, yb) = P(*a), P(*b); n = max(abs(xb - xa), abs(yb - ya), 1)
    for k in range(n + 1): put(xa + (xb - xa) * k // n, ya + (yb - ya) * k // n, col, w)
def fillbox(x0, y0, x1, y1, col):
    (a, b), (c, d) = P(x0, y0), P(x1, y1)
    a, c = sorted((max(0, min(W, a)), max(0, min(W, c)))); b, d = sorted((max(0, min(H, b)), max(0, min(H, d))))
    img[b:d, a:c] = col
def fpbox(f, col):
    bb = f.GetBoundingBox(False); fillbox(TO(bb.GetLeft()), TO(bb.GetTop()), TO(bb.GetRight()), TO(bb.GetBottom()), col)
new, old = pcbnew.LoadBoard(newf), pcbnew.LoadBoard(oldf)
for f in new.GetFootprints():
    bb = f.GetBoundingBox(False)
    if TO(bb.GetWidth()) < 15: fpbox(f, (228, 228, 228))
for r in ("U9", "L1", "D5", "C31"): fpbox(new.FindFootprintByReference(r), (255, 165, 50))
for r in ("R116","R117","R118","R146","R147","R148","R119","R120","R121","R149","R150","R151","C106","C107","C108","C126","C127","C128"): fpbox(new.FindFootprintByReference(r), (150, 195, 255))
for r in ("R178", "C164", "C165"): fpbox(new.FindFootprintByReference(r), (160, 70, 210))
fpbox(new.FindFootprintByReference("U20"), (120, 120, 120))
for brd, col in ((old, (220, 30, 30)), (new, (20, 150, 40))):
    for t in brd.GetTracks():
        if t.GetNetname() == "SB1":
            if t.GetClass() == "PCB_VIA": put(*P(TO(t.GetPosition().x), TO(t.GetPosition().y)), col, 4)
            else: line((TO(t.GetStart().x), TO(t.GetStart().y)), (TO(t.GetEnd().x), TO(t.GetEnd().y)), col, 2)
u20 = new.FindFootprintByReference("U20"); p57 = u20.FindPadByNumber("57").GetPosition()
c2 = new.FindFootprintByReference("C165").FindPadByNumber("2").GetPosition()
a, b2 = (TO(p57.x), TO(p57.y)), (TO(c2.x), TO(c2.y))
for k in range(0, 60, 2): line((a[0] + (b2[0]-a[0])*k/60, a[1] + (b2[1]-a[1])*k/60), (a[0] + (b2[0]-a[0])*(k+1)/60, a[1] + (b2[1]-a[1])*(k+1)/60), (160, 70, 210), 1)
def png(path, arr):
    h, w, _ = arr.shape; raw = b"".join(b"\x00" + arr[y].tobytes() for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
png(out, img); print("written", W, H)
