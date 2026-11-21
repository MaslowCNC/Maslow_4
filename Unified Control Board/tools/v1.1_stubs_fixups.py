# v1.1_stubs_fixups.py <board dir>: the two hand fixes that went with tools/v1.1_stubs.py (2026-11-21).  Each step
# checks the board first, so the script can run again safely.  Run with KiCad 10's Python:
#   /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
#       "Unified Control Board/tools/v1.1_stubs_fixups.py" "Unified Control Board"
#
# 1. A PHC2 via at (-3.25, 26.8) joined nothing but a 2.9 mm chain of tiny bottom-layer segments that wandered from
#    R139's link (at the via at (-0.85, 28.15)) west past R139's gate pad.  v1.1_stubs.py removed the via and the
#    first fragments one DRC pass at a time; the nine segments left are removed here in one go (traced by hand:
#    nothing else attaches to them).
# 2. U22 pin 12 (PHC2) reached the via at (-0.9, 34.35) only through a 0.3 mm diagonal that clipped the pad's corner
#    and ran 0.3 mm past it.  v1.1_stubs.py took that diagonal for a stub (it only looks for anchors: a track end, a
#    via or a pad centre inside the track, not a pad corner) and removed it, which DRC then reported as unconnected.
#    The link is now a straight 0.3 mm bottom-layer track from the via into the pad's centre, and the 0.05 mm
#    fragment that sat inside the pad goes.
import sys
import pcbnew

TO, MM = pcbnew.ToMM, pcbnew.FromMM
D = sys.argv[1].rstrip("/") + "/"
PCB = D + "Unified Control Board.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
keep = []


def at(p, q, tol=0.003):
    return abs(TO(p.x) - q[0]) < tol and abs(TO(p.y) - q[1]) < tol


def find_seg(net, layer, a, z):
    for t in b.GetTracks():
        if t.GetClass() == "PCB_TRACK" and t.GetNetname() == net and t.GetLayer() == layer:
            if (at(t.GetStart(), a) and at(t.GetEnd(), z)) or (at(t.GetStart(), z) and at(t.GetEnd(), a)):
                return t
    return None


# 1. the rest of the dead PHC2 chain
DEAD = [((-1.3, 27.0), (-2.95, 27.0)), ((-1.25, 27.05), (-1.3, 27.0)), ((-1.2, 27.05), (-1.25, 27.05)),
        ((-1.15, 27.1), (-1.2, 27.05)), ((-1.1, 27.1), (-1.15, 27.1)), ((-0.9, 27.3), (-1.1, 27.1)),
        ((-0.9, 27.85), (-0.9, 27.3)), ((-0.95, 27.9), (-0.9, 27.85)), ((-0.95, 28.0), (-0.95, 27.9))]
gone = 0
for a, z in DEAD:
    t = find_seg("PHC2", pcbnew.B_Cu, a, z)
    if t:
        b.Remove(t)
        keep.append(t)
        gone += 1
print("dead PHC2 chain: %d segments removed" % gone)

# 2. U22 pin 12 to the via at (-0.9, 34.35)
VIA, PAD = (-0.9, 34.35), (-0.25, 34.35)
u22_12 = b.FindFootprintByReference("U22").FindPadByNumber("12")
assert u22_12.GetNetname() == "PHC2" and u22_12.HitTest(pcbnew.VECTOR2I(MM(PAD[0]), MM(PAD[1])))
assert any(t.GetClass() == "PCB_VIA" and t.GetNetname() == "PHC2" and at(t.GetPosition(), VIA) for t in b.GetTracks())
t = find_seg("PHC2", pcbnew.B_Cu, VIA, PAD)
if not t:
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(pcbnew.VECTOR2I(MM(VIA[0]), MM(VIA[1])))
    t.SetEnd(pcbnew.VECTOR2I(MM(PAD[0]), MM(PAD[1])))
    t.SetLayer(pcbnew.B_Cu)
    t.SetNet(b.FindNet("PHC2"))
    b.Add(t)
    keep.append(t)
    print("U22.12: linked to the via at (-0.9, 34.35)")
t.SetWidth(MM(0.3))   # the .kicad_dru "Motor Kelvin and sense taps" minimum; 0.225 mm from pin 11 (GLA2)
frag = find_seg("PHC2", pcbnew.B_Cu, (-0.2, 34.4), (-0.25, 34.388))
if frag:
    b.Remove(frag)
    keep.append(frag)
    print("U22.12: fragment inside the pad removed")
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
