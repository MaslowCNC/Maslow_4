# detours.py <board dir> <log> [--local] [reroute.py options] [net ...]: try tools/reroute.py on every detour
# tools/netreview.py reports (with --local, the detours inside routes from local_detours()), worst
# first, net by net.  Every re-route reroute.py accepts is then checked by a full DRC: if any error, unconnected item
# or dangling track/via appears that was not there before, the board file goes back to how it was.  After a kept
# re-route the net is analysed again and its next detour tried (up to four per net).  Skips nets whose routing is
# deliberate (sense nets, U9_SYNC, the USB data pair, 3V3A) and nets with zones.  Uses top, In2 and bottom (never
# In1, the VCC plane).  Run with KiCad 10's Python.
import sys, os, re, json, shutil, subprocess, tempfile
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import netreview as nr

D = sys.argv[1].rstrip("/") + "/"
LOG = sys.argv[2]
LOCAL = "--local" in sys.argv   # detours inside a route (netreview.local_detours) instead of pad to pad
EXTRA = [a for a in sys.argv[3:] if (a.startswith("--") or a.replace(".", "").isdigit()) and a != "--local"]   # passed on to reroute.py
ONLY = set(a for a in sys.argv[3:] if a not in EXTRA)
PY = sys.executable
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
SKIP = re.compile(r"IS[ABC][12]|S[ABC][12]|U9_SYNC|USBD[+-]|3V3A")
PCB = D + "Unified Control Board.kicad_pcb"


def drc_bad():
    """the DRC items that would make a change unacceptable: errors, unconnected items, dangling tracks and vias"""
    out = tempfile.mktemp(suffix=".json")
    subprocess.run([KICAD_CLI, "pcb", "drc", "--severity-all", "--format", "json", "-o", out, PCB],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    d = json.load(open(out))
    os.unlink(out)
    bad = set()
    for sec in ("violations", "unconnected_items"):
        for v in d.get(sec, []):
            if v["severity"] == "error" or v["type"] in ("track_dangling", "via_dangling"):
                bad.add((v["type"], tuple(sorted((i["description"], round(i["pos"]["x"], 2), round(i["pos"]["y"], 2)) for i in v["items"]))))
    return bad


def analyse(net):
    b = pcbnew.LoadBoard(PCB)
    its = nr.load(b)[net]
    if LOCAL:
        return {"detours": nr.local_detours(net, its)}
    return nr.analyse(net, its)


base = drc_bad()
print("baseline: %d blocking DRC items" % len(base), flush=True)
b = pcbnew.LoadBoard(PCB)
ib = nr.load(b)
nets = sorted(n for n, its in ib.items() if n and any(i.kind in ("track", "via") for i in its))
todo = []
for net in nets:
    if (ONLY and net not in ONLY) or SKIP.fullmatch(net):
        continue
    res = nr.analyse(net, ib[net])
    found = nr.local_detours(net, ib[net]) if LOCAL else res["detours"]
    if res["zones"] or not found:
        continue
    todo.append((max(d["path"] - d["straight"] for d in found), net))
todo.sort(reverse=True)
log = open(LOG, "a")
backup = PCB + ".detour-backup"
for excess, net in todo:
    tried = set()
    for attempt in range(4):
        res = analyse(net)
        cand = [d for d in res["detours"] if (d["a"], d["b"]) not in tried]
        if not cand:
            break
        d = cand[0]
        tried.add((d["a"], d["b"]))
        shutil.copyfile(PCB, backup)
        snap = backup + ".islands.json"
        subprocess.run([PY, os.path.join(HERE, "repair.py"), D, "--snapshot", snap], capture_output=True, text=True)
        out = subprocess.run([PY, os.path.join(HERE, "reroute.py"), D, net, d["a"], d["b"], "--inner"] + EXTRA,
                             capture_output=True, text=True).stdout
        lines = [l.strip() for l in out.splitlines() if "chain" in l or "re-routed" in l or "kept" in l or " not " in l or "nothing" in l]
        verdict = "kept original"
        if "re-routed" in out:
            rep = subprocess.run([PY, os.path.join(HERE, "repair.py"), D, net, "--before", snap], capture_output=True, text=True).stdout
            lines += [l.strip() for l in rep.splitlines() if "island" in l or ("dead" in l and " 0 dead" not in l)]
            now = drc_bad()
            new = now - base
            if new:
                shutil.copyfile(backup, PCB)
                verdict = "REJECTED by DRC: " + "; ".join(sorted({t for t, _ in new}))
            else:
                base = now
                verdict = "RE-ROUTED"
        msg = " | ".join(lines)
        print("%-16s %s-%s %.0f/%.0f: %s  => %s" % (net, d["a"], d["b"], d["path"], d["straight"], msg, verdict), flush=True)
        log.write("%s\t%s\t%s\t%.1f\t%.1f\t%s\t%s\n" % (net, d["a"], d["b"], d["path"], d["straight"], msg, verdict))
        log.flush()
        if verdict != "RE-ROUTED":
            continue
for f in (backup, backup + ".islands.json"):
    if os.path.exists(f):
        os.unlink(f)
