"""Generates workflow-map.html: a static page with a hand-laid inline SVG flow map."""
import sys
from html import escape

W, H = 1400, 2747
NODE_W, NODE_H = 210, 44

nodes = {}
def node(id, x, y, lines, kind="step", w=NODE_W, h=None, even=False):
    if isinstance(lines, str):
        lines = [lines]
    nodes[id] = dict(x=x, y=y, w=w, h=h or (NODE_H if len(lines) == 1 else 58), lines=lines, kind=kind, even=even)

C1, C2, C3, C4, C5 = 150, 440, 730, 1010, 1270

# 1. Connect
node("power", C3, 50, "Power on the machine", "start")
node("c_a1", C2, 135, "Machine joins your WiFi")
node("c_a2", C2, 205, "Open maslow.local")
node("c_b1", C4, 135, "Join the Maslow WiFi hotspot", w=230)
node("c_b2", C4, 205, "Go to the machine’s IP address", w=230)
node("c_b3", C4, 278, ["WiFi setup dialog", "(option to turn it off)"], w=230)

# 2. Check state and set up
D = 707  # how far bands 3 and 4 moved down to make room for the setup checks
T = 260  # how far the tool steps moved down
node("assess", C3, 378, "Assess machine state", "start")
node("s_setup", 150, 468, "Setup needed", "status", w=180)
node("s_ready", C3, 468, "Ready to cut", "status", w=180)
node("s_error", C5, 468, "Not responding", "error", w=180)
node("reboot", C5, 552, ["Reboot from the UI", "(new: [ESP444]RESTART)"], "open", w=190)
node("q_on", C5, 642, "Back online?", "mdecision", w=180)
node("powercycle", C5, 727, "Power cycle", w=180)
node("back_a", C5, 807, "Back to Assess machine state", "open", w=200, h=34)

QX, AX = 150, 390   # x of the setup checks, and of the step that fixes each "no"
CONFIRM = ["Belts fully retracted?", "(check each arm by eye)"]
node("q2", QX, 558, "Belt lengths known?", "mdecision")
node("a2", AX, 558, "Retract the belts", w=190)
node("c2", AX, 650, CONFIRM, "decision", w=230)
node("qx", QX, 760, "Belts extended?", "mdecision")
node("wq2", AX, 760, ["Have you set", "your workspace?"], "decision", w=190, even=True)
node("ws2", AX, 850, ["Set work area", "(table length and width)", "clears the anchor points"], w=200, h=74)
node("e2", AX, 940, ["Extend the belts", "(as far as the workspace allows)"], w=210)
node("q3", QX, 1050, "Anchor points known?", "mdecision")
node("a3", AX, 1050, ["Calibrate (finds anchors", "and tensions the belts)"], w=210)
node("e_anchor", 630, 1050, "Error: can’t find anchors", "error", w=190)
node("q4", QX, 1160, "Belts tensioned?", "mdecision")
node("a4", AX, 1160, ["Apply tension", "(after Release tension)"], w=190)

node("d_ws", C3, 558, "Has your workspace changed?", "decision", w=230)
node("retract", C4, 558, "Retract belts")
node("c_ws", C4, 650, CONFIRM, "decision", w=230)
node("back", C4, 742, "Back to Setup needed", "open", h=34)
node("q_bigger", C4, 852, ["Question about the retract", "Needed when the workspace shrinks.", "If it got bigger, can we skip the", "retract and just extend, since the", "belt lengths are already known?"], "comment", w=260, h=118)

node("d_has", C3, 1010 + T, "Tool loaded?", "decision")
node("warn", C4, 1010 + T, ["Warning: no tool loaded", "(the flow continues)"], "warn", w=230)
node("d_tool", C3, 1095 + T, "Tool change needed?", "decision", w=230)
node("remove", C4, 1095 + T, "Remove the old tool")
node("load", C4, 1180 + T, "Load the new tool")
node("d_z", C3, 1180 + T, "Have you set your Z?", "decision")
node("z_note", 380, 1200 + T, ["About Z", "There’s a default Z, but the user", "confirms it’s where they want it.", "This part of the flow may change soon."], "comment", w=270, h=98)
node("setz", C3, 1270 + T, "Set Z")
node("s_ready2", C3, 1360 + T, "Ready to cut", "status", w=180)

# 3. Prepare and run
node("secure", C3, 1008 + D, "Secure your work piece")
node("d_tight", C3, 1093 + D, "Too tight?", "decision")
node("release", C4, 1093 + D, "Release tension")
node("secure2", C4, 1173 + D, "Secure your work piece")
node("apply", C4, 1253 + D, "Apply tension")
node("home", C3, 1253 + D, ["Set home XY", "(a good spot on the work piece)"], w=230)
node("trace", C3, 1343 + D, "Trace the boundary")
node("final", C3, 1428 + D, "Final check")
node("checklist", 1140, 1428 + D, ["Does the file look right?", "Right material thickness?",
                                   "Anchor points secure?", "Spoil board in place?"], "note", w=270, h=104)
node("start", C3, 1513 + D, "Run job: press START", "start")
node("s_cut", C3, 1598 + D, "Cutting", "status", w=180)

# 4. After the cut
node("success", C2 - 140, 1713 + D, "Success", "good", w=180)
node("estop", C3, 1713 + D, ["Emergency stop", "(by the user)"], "warn")
node("stopped", 1080, 1713 + D, ["Machine stopped", "(error)"], "error", w=230)
node("park", 300, 1810 + D, ["Park", "(happens automatically)"], w=190)
node("s_ready3", 300, 1903 + D, "Ready to cut", "status", w=170)
node("sleep", 160, 1993 + D, "Sleep (retracts?)", "open", w=160)
node("off", 440, 1993 + D, "Power off", w=160)
node("assess_e", C3, 1813 + D, "Assess machine state")
node("pc2", C3, 1893 + D, "Power cycle?", "open")
node("assess_s", 1080, 1813 + D, "Assess machine state")
node("log", 1080, 1893 + D, "Check the error log")

def port(id, side):
    n = nodes[id]
    return {"t": (n["x"], n["y"] - n["h"] / 2), "b": (n["x"], n["y"] + n["h"] / 2),
            "l": (n["x"] - n["w"] / 2, n["y"]), "r": (n["x"] + n["w"] / 2, n["y"])}[side]

edges = []
def edge(a, b, fs="b", ts="t", via=None, mid=None, label=None, lpos=None, dashed=False, faint=False):
    p0 = port(a, fs)
    p1 = b if isinstance(b, tuple) else port(b, ts)
    pts = [p0]
    if via:
        pts += via
    elif fs == "b" and ts == "t" and p0[0] != p1[0]:
        my = mid if mid is not None else (p0[1] + p1[1]) / 2
        pts += [(p0[0], my), (p1[0], my)]
    elif fs in "lr" and ts == "t":
        pts += [(p1[0], p0[1])]
    pts.append(p1)
    edges.append(dict(pts=pts, label=label, lpos=lpos, dashed=dashed, faint=faint))

edge("power", "c_a1", label="on your network", lpos=(585, 92))
edge("power", "c_b1", label="no network: use the hotspot", lpos=(875, 92))
edge("c_a1", "c_a2"); edge("c_b1", "c_b2"); edge("c_b2", "c_b3")
edge("c_a2", "assess", mid=334)
edge("c_b3", "assess", mid=334)
for st in ("s_setup", "s_ready", "s_error"):
    edge("assess", st, mid=428)
edge("s_error", "reboot")
edge("reboot", "q_on")
edge("q_on", "powercycle", label="no", lpos=(C5, 685))
edge("powercycle", "back_a", dashed=True)
edge("q_on", "back_a", "r", "r", via=[(1382, 642), (1382, 807)], label="yes", lpos=(1382, 725), dashed=True)

# setup checks: "yes" runs down the spine, each "no" fixes it and rejoins the spine
edge("s_setup", "q2")
edge("q2", "a2", "r", "l", label="no", lpos=(QX + 125, 558))
edge("a2", "c2")
edge("c2", "a2", "r", "r", via=[(545, 650), (545, 558)], label="rare: again", lpos=(545, 604), faint=True)
edge("c2", (QX, 705), via=[(AX, 705)], label="yes", lpos=(AX, 692))
edge("q2", "qx", label="yes", lpos=(QX, 622))
edge("qx", "wq2", "r", "l", label="no", lpos=(QX + 125, 760))
edge("wq2", "ws2", label="no", lpos=(AX, 801))
edge("ws2", "e2")
edge("wq2", (AX, 895), "r", via=[(545, 760), (545, 895)], label="yes", lpos=(545, 828))
edge("e2", (QX, 990), via=[(AX, 990)])
edge("qx", "q3", label="yes", lpos=(QX, 805))
edge("q3", "a3", "r", "l", label="no", lpos=(QX + 125, 1050))
edge("a3", "e_anchor", "r", "l", label="fails", lpos=(515, 1050))
edge("a3", (QX, 1105), via=[(AX, 1105)])
edge("q3", "q4", label="yes", lpos=(QX, 1089))
edge("q4", "a4", "r", "l", label="no", lpos=(QX + 125, 1160))
edge("a4", (QX, 1218), via=[(AX, 1218)])
edge("q4", "d_has", "b", "l", via=[(QX, 1010 + T)], label="yes", lpos=(QX, 1198))

edge("s_ready", "d_ws")
edge("d_ws", "retract", "r", "l", label="yes", lpos=(875, 558))
edge("retract", "c_ws")
edge("c_ws", "retract", "r", "r", via=[(1160, 650), (1160, 558)], label="rare: again", lpos=(1160, 604), faint=True)
edge("c_ws", "back", label="yes", lpos=(C4, 701), dashed=True)
edge("d_ws", "d_has", label="no", lpos=(C3, 880))
edge("d_has", "d_tool", label="yes", lpos=(C3, 1046 + T))
edge("d_has", "warn", "r", "l", label="no", lpos=(875, 1010 + T))
edge("warn", (C3, 1062 + T), via=[(C4, 1062 + T)])
edge("d_tool", "remove", "r", "l", label="yes", lpos=(875, 1095 + T))
edge("remove", "load")
edge("load", "setz", "b", "r", via=[(C4, 1270 + T)])
edge("d_tool", "d_z", label="no", lpos=(C3, 1138 + T))
edge("d_z", "setz", label="no", lpos=(C3, 1225 + T))
edge("d_z", "s_ready2", "l", "l", via=[(580, 1180 + T), (580, 1360 + T)], label="yes", lpos=(580, 1270 + T))
edge("setz", "s_ready2")

edge("s_ready2", "secure")
edge("secure", "d_tight")
edge("d_tight", "release", "r", "l", label="yes", lpos=(875, 1093 + D))
edge("d_tight", "home", label="no", lpos=(730, 1172 + D))
edge("release", "secure2"); edge("secure2", "apply")
edge("apply", "home", "l", "r")
edge("home", "trace"); edge("trace", "final")
edge("final", "checklist", "r", "l", dashed=True)
edge("final", "start"); edge("start", "s_cut")

for o in ("success", "estop", "stopped"):
    edge("s_cut", o, mid=1660 + D)
edge("success", "park")
edge("park", "s_ready3", label="immediately", lpos=(300, 1857 + D))
for o in ("sleep", "off"):
    edge("s_ready3", o, mid=1950 + D, dashed=True)
edges[-1]["label"] = "when you’re done"; edges[-1]["lpos"] = (300, 1950 + D)
edge("estop", "assess_e"); edge("assess_e", "pc2")
edge("stopped", "assess_s"); edge("assess_s", "log")

bands = [(0, 318, "1", "Connect"), (318, 1669, "2", "Check the machine and set up"),
         (1669, 1648 + D, "3", "Prepare the work piece and run the job"), (1648 + D, H, "4", "After the cut")]

out = []
out.append(f'<svg viewBox="0 0 {W} {H}" role="img" aria-labelledby="map-title map-desc" class="map">')
out.append('<title id="map-title">Maslow machine workflow</title>')
out.append('<desc id="map-desc">From powering on and connecting, through checking the machine state and '
           'setting up belts, tool and Z, to securing the work piece, running the job and what happens '
           'when the cut ends or stops.</desc>')
out.append('<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
           'orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" class="arrowhead"/></marker></defs>')
for i, (y0, y1, num, name) in enumerate(bands):
    out.append(f'<rect x="0" y="{y0}" width="{W}" height="{y1 - y0}" class="band {"band-alt" if i % 2 else ""}"/>')
    out.append(f'<text x="20" y="{y0 + 30}" class="band-num">{num}</text>')
    out.append(f'<text x="44" y="{y0 + 30}" class="band-label">{escape(name.upper())}</text>')

for e in edges:
    d = "M" + " L".join(f"{x:.0f},{y:.0f}" for x, y in e["pts"])
    cls = "edge" + (" dashed" if e["dashed"] else "") + (" faint" if e["faint"] else "")
    out.append(f'<path d="{d}" class="{cls}" marker-end="url(#arrow)"/>')

for n in nodes.values():
    x, y, w, h, k = n["x"], n["y"], n["w"], n["h"], n["kind"]
    l, t = x - w / 2, y - h / 2
    if k in ("decision", "mdecision"):
        pts = [(l, y), (l + 14, t), (l + w - 14, t), (l + w, y), (l + w - 14, t + h), (l + 14, t + h)]
        out.append('<polygon points="%s" class="shape %s"/>' % (" ".join(f"{a:.0f},{b:.0f}" for a, b in pts), k))
    else:
        r = h / 2 if k in ("status", "error", "good", "warn") else 8
        out.append(f'<rect x="{l:.0f}" y="{t:.0f}" width="{w}" height="{h}" rx="{r:.0f}" class="shape {k}"/>')
    lines = n["lines"]
    if k == "comment":
        for i, line in enumerate(lines):
            cls = "label comment-title" if i == 0 else "label note-text"
            out.append(f'<text x="{l + 14:.0f}" y="{t + 24 + i * 20:.0f}" class="{cls}">{escape(line)}</text>')
        continue
    if k == "note":
        for i, line in enumerate(lines):
            out.append(f'<text x="{l + 18:.0f}" y="{t + 26 + i * 21:.0f}" class="label note-text">'
                       f'<tspan class="tick">✓</tspan> {escape(line)}</text>')
        continue
    lh = 17
    y0 = y - (len(lines) - 1) * lh / 2 + 4.5
    for i, line in enumerate(lines):
        cls = "label" + (" sub" if i > 0 and not n["even"] else "") + (" strong" if k in ("status", "error", "good", "warn", "start") else "")
        out.append(f'<text x="{x:.0f}" y="{y0 + i * lh:.0f}" text-anchor="middle" class="{cls} label-{k}">{escape(line)}</text>')

for e in edges:
    if not e["label"]:
        continue
    lx, ly = e["lpos"]
    tw = len(e["label"]) * 6.6 + 12
    g = ' class="faint"' if e["faint"] else ""
    out.append(f'<g{g}><rect x="{lx - tw / 2:.0f}" y="{ly - 10:.0f}" width="{tw:.0f}" height="20" rx="10" class="edge-label-bg"/>')
    out.append(f'<text x="{lx:.0f}" y="{ly + 4:.0f}" text-anchor="middle" class="edge-label">{escape(e["label"])}</text></g>')
out.append("</svg>")
svg = "\n".join(out)

tpl = open(sys.argv[1]).read()
open(sys.argv[2], "w").write(tpl.replace("<!--SVG-->", svg))
