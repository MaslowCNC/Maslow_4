# v1.1_schematic.py <board dir>: the Unified Control Board 36V v1.1 schematic changes, applied to v1.0 (tag
# hw/ucb-36v/v1.0).  Kept as the record; it asserts on v1.0's schematic, so it does not run twice.
#  - FB1 (600R ferrite, 3V3 -> 3V3A) becomes R178, 4.7 ohm 0603: with C164 it is an RC filter (~1.5 kHz).
#  - C164 22 uF 0805 on 3V3A.
#  - C165 10 pF C0G 0402 from U9_SYNC (G474 PB4, TIM3_CH1) to U9's RT/CLK pin (TPS54360B sync, per the
#    datasheet's recommended AC coupling; R30 stays as the free-running RT).
#  - title block rev 1.1.
import re, sys, uuid
D = sys.argv[1].rstrip("/") + "/"
ROOT = "/dfafd8b3-c553-40fe-89ed-f7586d7cf6a1"
SPINDLE = ROOT + "/176e5b9f-9a9c-4b6b-b18a-17424bf72869"
U = lambda: str(uuid.uuid4())

def one(s, old, new):
    assert s.count(old) == 1, ("expected once", old[:80], s.count(old))
    return s.replace(old, new)

def symbol_c(ref, x, y, rot, value, fp, mpn, lcsc, path, refpos, valpos):
    return '''	(symbol
		(lib_id "Device:C")
		(at %s %s %d)
		(unit 1)
		(body_style 1)
		(exclude_from_sim no)
		(in_bom yes)
		(on_board yes)
		(in_pos_files yes)
		(dnp no)
		(uuid "%s")
		(property "Reference" "%s"
			(at %s %s 0)
			(show_name no)
			(do_not_autoplace no)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(property "Value" "%s"
			(at %s %s 0)
			(show_name no)
			(do_not_autoplace no)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
''' % (x, y, rot, U(), ref, refpos[0], refpos[1], value, valpos[0], valpos[1]) + "".join('''		(property "%s" "%s"
			(at %s %s 0)
			(hide yes)
			(show_name no)
			(do_not_autoplace no)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
''' % (k, v, x, y) for k, v in (("Footprint", fp), ("Datasheet", ""), ("Description", ""), ("MPN", mpn),
                                 ("Supplier Part", lcsc), ("Supplier", "LCSC"))) + '''		(pin "1"
			(uuid "%s")
		)
		(pin "2"
			(uuid "%s")
		)
		(instances
			(project "Unified Control Board"
				(path "%s"
					(reference "%s")
					(unit 1)
				)
			)
		)
	)
''' % (U(), U(), path, ref)

def glabel(name, x, y, ang, justify, shape="input"):
    return '''	(global_label "%s"
		(shape %s)
		(at %s %s %d)
		(effects
			(font
				(size 1.27 1.27)
			)
			(justify %s)
		)
		(uuid "%s")
		(property "Intersheetrefs" "${INTERSHEET_REFS}"
			(at %s %s 0)
			(hide yes)
			(show_name no)
			(do_not_autoplace no)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
	)
''' % (name, shape, x, y, ang, justify, U(), x, y)

# ---------------------------------------------------------------- Spindle Drive sheet
p = D + "Spindle Drive.kicad_sch"; s = open(p, encoding="utf-8").read()
# FB1 -> R178 (same symbol uuid and pin uuids, so the PCB footprint stays linked)
m = re.search(r'\n\t\(symbol\n\t\t\(lib_id "Device:FerriteBead_Small"\)\n\t\t\(at 40.64 101.6 90\).*?\n\t\)\n', s, re.S)
assert m and '"Reference" "FB1"' in m.group(0)
blk = m.group(0)
blk = one(blk, '(lib_id "Device:FerriteBead_Small")', '(lib_id "Device:R")')
blk = one(blk, '(property "Reference" "FB1"', '(property "Reference" "R178"')
blk = one(blk, '(property "Value" "600R@100MHz"', '(property "Value" "4.7"')
blk = one(blk, '(property "Footprint" "Inductor_SMD:L_0603_1608Metric"', '(property "Footprint" "Resistor_SMD:R_0603_1608Metric"')
blk = one(blk, '(property "MPN" "GZ1608D601TF"', '(property "MPN" "AC0603FR-074R7L"')
blk = one(blk, '(property "Supplier Part" "C1002"', '(property "Supplier Part" "C144651"')
blk = one(blk, '(reference "FB1")', '(reference "R178")')
s = s.replace(m.group(0), blk)
# its labels move out to the resistor's pin ends (Device:R pins are 1.27 mm further out than the bead's)
def move_label(s, name, x0, y0, x1, y1):
    pat = re.compile(r'\t\(global_label "%s"\n\t\t\(shape \w+\)\n\t\t\(at %s %s \d+\).*?\n\t\)\n' % (re.escape(name), re.escape(x0), re.escape(y0)), re.S)
    mm = pat.search(s); assert mm, (name, x0, y0)
    b = mm.group(0).replace("(at %s %s " % (x0, y0), "(at %s %s " % (x1, y1))
    return s.replace(mm.group(0), b)
s = move_label(s, "3V3", "38.1", "101.6", "36.83", "101.6")
s = move_label(s, "3V3A", "43.18", "101.6", "44.45", "101.6")
# C164 22 uF on 3V3A, beside C145..C147
add = symbol_c("C164", 81.28, 101.6, 0, "22uF", "Capacitor_SMD:C_0805_2012Metric", "CL21A226MAQNNNE", "C45783", SPINDLE,
               (83.82, 95.25), (83.82, 107.95))
add += glabel("3V3A", 81.28, 97.79, 90, "left") + glabel("GND", 81.28, 105.41, 270, "left")
# PB4 (pin 57): no-connect -> U9_SYNC
nc = re.search(r'\t\(no_connect\n\t\t\(at 119.38 165.1\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n', s)
assert nc, "PB4 no_connect"
s = s.replace(nc.group(0), glabel("U9_SYNC", 119.38, 165.1, 0, "left", "output"))
i = s.rindex("\n\t(sheet_instances") if "\n\t(sheet_instances" in s else s.rindex("\n\t(embedded_fonts")
s = s[:i + 1] + add + s[i + 1:]
open(p, "w", encoding="utf-8").write(s)

# ---------------------------------------------------------------- root sheet
p = D + "Unified Control Board.kicad_sch"; r = open(p, encoding="utf-8").read()
ls = s[s.index("(lib_symbols"):]
i = ls.index('\t\t(symbol "Device:C"\n')
j = ls.index('\n\t\t(symbol "', i + 10)
devc = ls[i:j + 1]
assert '(symbol "Device:C"' not in r
k = r.index("\t(lib_symbols\n") + len("\t(lib_symbols\n")
r = r[:k] + devc + r[k:]
add = symbol_c("C165", -754.38, 340.36, 90, "10pF", "Capacitor_SMD:C_0402_1005Metric", "CL05C100JB5NNNC", "C32949", ROOT,
               (-754.38, 336.55), (-754.38, 344.17))
add += glabel("RT", -758.19, 340.36, 180, "right") + glabel("U9_SYNC", -750.57, 340.36, 0, "left", "input")
i = r.rindex("\n\t(sheet_instances") if "\n\t(sheet_instances" in r else r.rindex("\n\t(embedded_fonts")
r = r[:i + 1] + add + r[i + 1:]
r = one(r, '\t\t(rev "1.0")\n', '\t\t(rev "1.1")\n')
open(p, "w", encoding="utf-8").write(r)
print("schematic edited")
