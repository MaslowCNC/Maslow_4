"""Generates the UI modes version of the workflow map: a static page with a hand-laid inline SVG."""
import sys
from html import escape

W, H = 1400, 3275
NODE_W, NODE_H = 210, 44

nodes = {}
def node(id, x, y, lines, kind="step", w=NODE_W, h=None, even=False):
    if isinstance(lines, str):
        lines = [lines]
    nodes[id] = dict(x=x, y=y, w=w, h=h or (NODE_H if len(lines) == 1 else 58), lines=lines, kind=kind, even=even)

C1, C2, C3, C4, C5 = 150, 440, 730, 1010, 1270
MODES = ("setup", "ready", "job", "error")

# 1. Connect
node("power", C3, 50, "Power on the machine", "start")
node("c_a1", C2, 135, "Machine joins your WiFi")
node("c_a2", C2, 205, "Open maslow.local")
node("c_b1", C4, 135, "Join the Maslow WiFi hotspot", w=230)
node("c_b2", C4, 205, "Go to the machine’s IP address", w=230)
node("c_b3", C4, 278, ["Name the device, then set up WiFi", "(WiFi dialog is a placeholder)", "Connecting: WiFi setup"], w=260, h=74)

# 2. Setup: confirm the work area first, then run only the steps the saved data doesn't cover
node("assess", C3, 378, ["Assess machine state", "(UI shows Connecting)"], "start")
node("s_setup", C1, 468, "Setup: Confirm the work area", "setup", w=250)
node("s_error", C5, 468, "Error: Not responding", "error", w=210)
node("reboot", C5, 552, ["Reboot from the UI", "(new: [ESP444]RESTART)"], "open", w=190)
node("q_on", C5, 642, "Back online?", "mdecision", w=180)
node("powercycle", C5, 727, "Power cycle", w=180)
node("back_a", C5, 807, "Back to Assess machine state", "open", w=200, h=34)

QX, AX = C1, 420   # x of the setup checks, and of the step that fixes each "no"
CONFIRM = ["Belts fully retracted?", "(check each arm by eye)"]
node("d_keep", QX, 558, ["Keep the saved", "work area?"], "decision", w=190, even=True)
node("ws_new", QX, 660, ["Set a new work area", "(size, spoil board thickness)", "clears the anchor points"], w=210, h=74)
node("q_t", AX, 558, ["Still tensioned?", "(page reload only: always", "no after a reboot)"], "mdecision", w=230, h=74, even=True)
node("ws_note", C4, 650, ["About a new work area", "Does a smaller work area need a retract", "first? A bigger one may not, since the", "belt lengths are still known."], "comment", w=280, h=98)
node("boot_note", C4, 780, ["After a reboot", "The encoders restore the belt lengths,", "but the machine comes back as Belts", "extended or Belts retracted, never", "tensioned. The user applies tension again."], "comment", w=280, h=118)
node("q2", QX, 780, "Belt lengths known?", "mdecision")
node("a2", AX, 780, ["Retract the belts", "Setup: Retracting belts"], w=190)
node("c2", AX, 872, CONFIRM, "decision", w=230)
node("qx", QX, 980, "Belts extended?", "mdecision")
node("e2", AX, 980, ["Next: Extend belts", "Setup: Belts retracted"], w=200)
node("e2b", AX, 1070, ["Extending belts: pull them", "out and attach to the anchors", "Setup: Extending belts"], w=230, h=74)
node("e_stop", 640, 1070, ["Extending stopped", "lengths known: extend", "again or retract"], "setup", w=160, h=74)
node("a4", QX, 1185, ["Apply tension", "(checks the saved anchors)", "Setup: Applying tension"], w=210, h=74)
node("tension_note", 580, 1187, ["Always returns a result now", "Current firmware passes or fails every", "time: a failure goes back to Belts", "extended. Older firmware (before", "20493b1b) can stick at Taking slack."], "comment", w=270, h=118)
node("q4", QX, 1295, "Position confirmed?", "mdecision")
node("a3", 400, 1295, ["Find anchors", "(also tensions the belts)", "Setup: Finding anchor points"], w=200, h=74)
node("e_anchor", 620, 1295, ["Error: can’t find anchors", "check the belts, try again"], "error", w=190)
node("s_conf", 400, 1405, "Setup: Checking for a tool", "setup", w=230)
node("jog_note", 1270, 1290, ["Jogging unlocks here", "Anchors and position are confirmed,", "so jogging works from here on,", "except while something moves."], "comment", w=250, h=98)

node("d_has", C3, 1405, ["Tool detected?", "(spindle rises through the beam)"], "mdecision", w=240)
node("n_tool", C5, 1405, ["No tool loaded:", "measure one?"], "decision", w=190, even=True)
node("d_z", C3, 1490, ["Have you set Z", "for this tool?"], "decision", w=210, even=True)
node("remove", C4, 1490, ["Press Remove tool, take the bit out", "Setup: Removing the tool"], w=250)
node("measure", C4, 1575, ["Measure the stick-out, enter it", "Setup: Measure the tool"], w=240)
node("load", C4, 1665, ["Load the bit back in", "(the beam detects it)", "Setup: Load the tool"], w=220, h=74)
node("z_note", 380, 1595, ["About Z", "Z is set from the bit’s stick-out,", "measured from the spindle socket face", "to the tip. Needs firmware support."], "comment", w=270, h=98)
node("setz", C3, 1665, ["Z set from the measurement", "Setup: Setting Z"], w=230)
node("s_secure", C3, 1750, "Setup: Secure the work piece", "setup", w=250)
node("secure", C3, 1835, ["Is your work piece", "set up and secure?"], "decision", w=210, even=True)
node("secure_s", C3, 1925, ["Enter stock thickness, secure the work piece", "Tip: belts too tight? Release tension"], w=300)
node("release", C4, 1925, ["Release tension", "Setup: Releasing tension"])
node("secure2", C4, 2005, ["Secure your work piece", "Setup: Belts extended"])
node("apply", C4, 2085, ["Apply tension", "Setup: Applying tension"])
node("s_file", C3, 2170, "Setup: Choose a job file", "setup", w=230)
node("choose", C3, 2255, "Choose or upload a file", w=230)

# 3. Ready to cut: every setup step is done or skipped
node("s_ready2", C3, 2360, "Ready to cut", "ready", w=180)
node("home", C3, 2445, ["Set home XY", "(a good spot on the work piece)"], w=230)
node("trace", C3, 2530, ["Trace the boundary (optional)", "Ready to cut: Moving"], w=250)
node("final", C3, 2615, "Final check")
node("checklist", 1140, 2615, ["Does the file look right?", "Right material thickness?",
                               "Anchor points secure?", "Spoil board in place?"], "note", w=270, h=104)
node("start", C3, 2700, "Run job: press START", "start")

# 4. Job, and how it ends
node("s_cut", C3, 2795, "Job: Cutting", "job", w=180)
node("success", 170, 2915, "Job: Success", "job", w=170)
node("fault", 450, 2915, "Job: Spindle fault: recovering", "job", w=250)
node("stop", C4, 2915, ["Stop pressed", "Belts stop, spindle off, Z lifts,", "progress saved (needs firmware)"], w=240, h=74)
node("estop", C5, 2915, ["Error: Emergency stop", "emergency button or a", "machine error, not the user"], "error", w=220, h=74)

node("park", 170, 3015, ["Ready to cut: Parking", "(automatic)"], "ready", w=190)
node("s_ready3", 170, 3110, "Ready to cut", "ready", w=170)
node("off", 170, 3205, "Power off", w=150)

node("rehome", 450, 3015, ["Job ends, Z lifts", "and re-homes by itself"], w=210)
node("r_ok", 450, 3110, ["Setup: Checking for a tool", "Message: Job stopped, spindle fault"], "setup", w=270)
node("r_fail", 740, 3015, ["Error: recovery failed", "Spindle recovery didn’t finish"], "error", w=210)
node("r_pc", 740, 3095, ["Clear with $X, re-home Z", "(to be tested)"], w=190)
node("r_back", 740, 3175, "Back to Assess machine state", "open", w=200, h=34)

node("r_stop", C4, 3015, ["Ready to cut: Job stopped", "stopped at line 1,240"], "ready", w=230)
node("again", C4, 3110, "Continue, run from the start, or choose a file", "open", w=300, h=34)

node("check_bit", C5, 3015, "Check the bit", w=170)
node("pc2", C5, 3095, "Power cycle", w=170)
node("assess_e", C5, 3175, "Back to Assess machine state", "open", w=200, h=34)

def port(id, side):
    n = nodes[id]
    return {"t": (n["x"], n["y"] - n["h"] / 2), "b": (n["x"], n["y"] + n["h"] / 2),
            "l": (n["x"] - n["w"] / 2, n["y"]), "r": (n["x"] + n["w"] / 2, n["y"])}[side]

edges = []
def edge(a, b, fs="b", ts="t", via=None, mid=None, label=None, lpos=None, dashed=False, faint=False):
    p0 = a if isinstance(a, tuple) else port(a, fs)
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
edge("assess", "s_setup", mid=428)
edge("assess", "s_error", mid=428)
edge("s_error", "reboot")
edge("reboot", "q_on")
edge("q_on", "powercycle", label="no", lpos=(C5, 685))
edge("powercycle", "back_a", dashed=True)
edge("q_on", "back_a", "r", "r", via=[(1382, 642), (1382, 807)], label="yes", lpos=(1382, 725), dashed=True)

# setup checks: "yes" runs down the spine, each "no" fixes it and rejoins the spine
edge("s_setup", "d_keep")
edge("d_keep", "ws_new", label="no", lpos=(QX, 602))
edge("d_keep", "q_t", "r", "l", label="yes", lpos=(QX + 125, 558))
edge("q_t", "d_has", "r", "t", via=[(C3, 558)], label="yes: skip to the tool check", lpos=(C3, 830))
edge("q_t", (QX, 714), "b", via=[(AX, 714)], label="no", lpos=(AX, 660))
edge("ws_new", "q2")
edge("q2", "a2", "r", "l", label="no", lpos=(QX + 125, 780))
edge("a2", "c2")
edge("a2", (190, 758), "t", via=[(AX, 738), (190, 738)], dashed=True, label="stopped: lengths lost", lpos=(305, 738))
edge("c2", "a2", "r", "r", via=[(575, 872), (575, 780)], label="no: add slack", lpos=(575, 826))
edge("c2", (QX, 927), via=[(AX, 927)], label="yes", lpos=(AX, 914))
edge("q2", "qx", label="yes", lpos=(QX, 845))
edge("qx", "e2", "r", "l", label="no", lpos=(QX + 125, 980))
edge("e2", "e2b", label="press", lpos=(AX + 40, 1022))
edge("e2b", (QX, 1130), via=[(AX, 1130)], label="Done extending", lpos=(300, 1130))
edge("e2b", "e_stop", "r", "l", label="Stop", lpos=(545, 1052))
edge("e_stop", (480, 1107), "b", via=[(640, 1118), (480, 1118)], dashed=True, label="extend again", lpos=(585, 1118))
edge("qx", "a4", label="yes", lpos=(QX, 1013))
edge("a4", "q4")
edge("a4", "a3", "r", "t", dashed=True, label="no saved anchors", lpos=(318, 1185))
edge("q4", "a3", "r", "l", label="no", lpos=(QX + 125, 1295))
edge("a3", "e_anchor", "r", "l", label="fails", lpos=(512, 1260))
edge("e_anchor", (470, 1332), "b", via=[(620, 1347), (470, 1347)], dashed=True, label="try again", lpos=(560, 1347))
edge("a3", (QX, 1357), via=[(400, 1357)])
edge("q4", "s_conf", "b", "l", via=[(QX, 1405)], label="yes", lpos=(QX, 1383))
edge("s_conf", "d_has", "r", "l")

edge("d_has", "d_z", label="yes", lpos=(C3, 1447))
edge("d_has", "n_tool", "r", "l", label="no", lpos=(1000, 1405))
edge("n_tool", "measure", "b", "r", via=[(C5, 1575)], label="yes", lpos=(C5, 1500))
edge("n_tool", "s_secure", "r", "r", via=[(1386, 1405), (1386, 1750)], label="no: continue without a tool", lpos=(1150, 1750))
edge("d_z", "remove", "r", "l", label="no", lpos=(858, 1490))
edge("remove", "measure")
edge("measure", "load")
edge("load", "setz", "l", "r")
edge("d_z", "s_secure", "l", "l", via=[(580, 1490), (580, 1750)], label="yes", lpos=(580, 1620))
edge("setz", "s_secure")
edge("s_secure", "secure")

edge("secure", "secure_s", label="no", lpos=(C3, 1882))
edge("secure", "s_file", "l", "l", via=[(540, 1835), (540, 2170)], label="yes", lpos=(540, 2000))
edge("secure_s", "release", "r", "l", dashed=True)
edge("secure_s", "s_file")
edge("release", "secure2"); edge("secure2", "apply")
edge("apply", "s_file", "b", "r", via=[(C4, 2170)])
edge("s_file", "choose"); edge("choose", "s_ready2")
edge("s_ready2", "home")
edge("home", "trace", dashed=True)
edge("home", "final", "l", "l", via=[(540, 2445), (540, 2615)], label="skip", lpos=(540, 2530))
edge("trace", "final")
edge("final", "checklist", "r", "l", dashed=True)
edge("final", "start"); edge("start", "s_cut")

for o in ("success", "fault", "estop"):
    edge("s_cut", o, mid=2860)
edge("s_cut", "stop", mid=2860, label="Stop", lpos=(C4, 2860))
edge("success", "park")
edge("park", "s_ready3", label="immediately", lpos=(170, 3062))
edge("s_ready3", "off", dashed=True, label="when you’re done", lpos=(170, 3157))
edge("fault", "rehome")
edge("rehome", "r_ok")
edge("rehome", "r_fail", "r", "l", label="fails", lpos=(595, 3015))
edge("r_fail", "r_pc")
edge("r_pc", "r_back", dashed=True)
edge("stop", "r_stop")
edge("r_stop", "again", dashed=True)
edge("estop", "check_bit")
edge("check_bit", "pc2")
edge("pc2", "assess_e", dashed=True)

bands = [(0, 318, "1", "Connect"), (318, 2305, "2", "Setup: confirm the work area, then only what’s missing"),
         (2305, 2745, "3", "Ready to cut: prepare and start"), (2745, H, "4", "Job, and how it ends")]

out = []
out.append(f'<svg viewBox="0 0 {W} {H}" role="img" aria-labelledby="map-title map-desc" class="map">')
out.append('<title id="map-title">Maslow workflow by UI mode</title>')
out.append('<desc id="map-desc">From powering on and connecting, through the Setup and Ready to cut modes, to the '
           'Job mode and the ways a job can end: success, Stop, a spindle fault or an emergency stop.</desc>')
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
        r = h / 2 if k in MODES + ("warn",) else 8
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
        cls = "label" + (" sub" if i > 0 and not n["even"] else "") + (" strong" if k in MODES + ("warn", "start") else "")
        if i > 0 and k == "step" and line.split(":")[0] in ("Setup", "Ready to cut"):
            cls += " mode-setup" if line.startswith("Setup") else " mode-ready"
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
