"""JLCPCB order package from the board, in the formats of the previous order (jlcpcb/ at 500dc69c):
BOM  "Comment","Designator","Footprint","LCSC Part #","Quantity" one line per LCSC part number (JLCPCB warns when two lines match one part), from the
     footprints' own fields (synced from the schematic), skipping parts excluded from the BOM or DNP;
CPL  Designator,Mid X,Mid Y,Layer,Rotation = KiCad's position export (both sides, mm);
Gerbers with the board's plot settings + PTH/NPTH Excellon drill, zipped.
usage: gen_jlcpcb.py board.kicad_pcb out_dir   (run with KiCad's python; needs kicad-cli)"""
import sys, os, csv, glob, shutil, zipfile, subprocess, collections, pcbnew
board, out = sys.argv[1], sys.argv[2]
KC = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
os.makedirs(out, exist_ok=True); gdir = os.path.join(out, 'gerbers')
shutil.rmtree(gdir, ignore_errors=True); os.makedirs(gdir)
b = pcbnew.LoadBoard(board)
groups = collections.OrderedDict(); missing = []
for f in sorted(b.GetFootprints(), key=lambda f: (f.GetReference().rstrip('0123456789'), int(''.join(c for c in f.GetReference() if c.isdigit()) or 0))):
    if f.IsExcludedFromBOM() or (hasattr(f, 'IsDNP') and f.IsDNP()): continue
    ref = f.GetReference()
    lc = f.GetFieldText('Supplier Part') if f.HasField('Supplier Part') else (f.GetFieldText('LCSC') if f.HasField('LCSC') else '')
    if not lc: missing.append(ref)
    fp = f.GetFPID().GetLibItemName().wx_str() if hasattr(f.GetFPID().GetLibItemName(), 'wx_str') else str(f.GetFPID().GetLibItemName())
    key = lc or ('NO-LCSC', f.GetValue(), fp)
    groups.setdefault(key, []).append((ref, f.GetValue(), fp))
with open(os.path.join(out, 'UnifiedControlBoard_BOM_JLCPCB.csv'), 'w', newline='') as fh:
    w = csv.writer(fh, quoting=csv.QUOTE_ALL); w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #', 'Quantity'])
    for key, parts in groups.items():
        lc = key if isinstance(key, str) else ''
        # comment: the most specific value text in the group (longest, e.g. '100nF/50V' over '100nF'); footprint: the most common name
        val = max((v for _, v, _ in parts), key=lambda v: (len(v), v))
        fp = collections.Counter(p for _, _, p in parts).most_common(1)[0][0]
        w.writerow([val, ','.join(r for r, _, _ in parts), fp, lc, str(len(parts))])
pos = os.path.join(out, 'pos.csv')
subprocess.run([KC, 'pcb', 'export', 'pos', '--format', 'csv', '--units', 'mm', '--side', 'both', '-o', pos, board], check=True, capture_output=True)
rows = list(csv.DictReader(open(pos))); os.remove(pos)
with open(os.path.join(out, 'UnifiedControlBoard_CPL_JLCPCB.csv'), 'w', newline='') as fh:
    w = csv.writer(fh); w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    for r in rows: w.writerow([r['Ref'], f"{float(r['PosX']):.4f}mm", f"{float(r['PosY']):.4f}mm", r['Side'].capitalize(), f"{float(r['Rot']) % 360:.2f}"])
LAYERS = 'F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts'
subprocess.run([KC, 'pcb', 'export', 'gerbers', '--layers', LAYERS, '--subtract-soldermask', '--check-zones', '-o', gdir + '/', board], check=True, capture_output=True)
subprocess.run([KC, 'pcb', 'export', 'drill', '--format', 'excellon', '--excellon-separate-th', '--excellon-units', 'mm', '-o', gdir + '/', board], check=True, capture_output=True)
zg = os.path.join(out, 'UnifiedControlBoard_Gerbers.zip')
with zipfile.ZipFile(zg, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(glob.glob(gdir + '/*')): z.write(p, os.path.basename(p))
with zipfile.ZipFile(os.path.join(out, 'UnifiedControlBoard_JLCPCB.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    for n in ('UnifiedControlBoard_Gerbers.zip', 'UnifiedControlBoard_BOM_JLCPCB.csv', 'UnifiedControlBoard_CPL_JLCPCB.csv'): z.write(os.path.join(out, n), n)
top = sum(1 for r in rows if r['Side'] == 'top'); bot = len(rows) - top
print(f'BOM: {len(groups)} lines, {sum(len(v) for v in groups.values())} parts, without LCSC: {missing}')
print(f'CPL: {len(rows)} parts ({top} top, {bot} bottom); gerbers: {len(os.listdir(gdir))} files')
