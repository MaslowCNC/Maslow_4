#!/usr/bin/env python3
"""Check the G474 firmware's pin map (src/board.h) against the board's KiCad netlist.

Exports the netlist of "Unified Control Board/Unified Control Board.kicad_sch" with kicad-cli and
checks every MCU pin the firmware uses, plus the parts around it whose values the firmware
assumes (shunts, sense resistors, VBUS divider, NTCs, BoardVersion divider, XY<->G474 wiring).

    python3 spindle-g474/tools/check_pins.py [--kicad-cli PATH]

Exit status 0 = everything matches.
"""
import argparse, os, shutil, subprocess, sys, tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
SCH = os.path.join(HERE, "..", "..", "Unified Control Board", "Unified Control Board.kicad_sch")

# U20 (STM32G474RBT3, LQFP64) pin -> (pin name, net) the firmware expects.
MCU = {
    "8": ("PC0", "INHA1"), "9": ("PC1", "INHB1"), "10": ("PC2", "INHC1"),
    "45": ("PA11", "INLA1"), "46": ("PA12", "INLB1"), "37": ("PB15", "INLC1"),
    "38": ("PC6", "INHA2"), "39": ("PC7", "INHB2"), "40": ("PC8", "INHC2"),
    "52": ("PC10", "INLA2"), "53": ("PC11", "INLB2"), "54": ("PC12", "INLC2"),
    "13": ("PA1", "ISA1"), "21": ("PA7", "ISB1"), "24": ("PB0", "ISC1"),
    "33": ("PB11", "ISA2"), "36": ("PB14", "ISB2"), "35": ("PB13", "ISC2"),
    "12": ("PA0", "VBUS_SENSE"), "22": ("PC4", "TEMP1"), "23": ("PC5", "TEMP2"),
    "14": ("PA2", "BoardCOM1"), "17": ("PA3", "BoardCOM2"),
    "19": ("PA5", "BEAMBREAK"), "60": ("PB7", "DETECTORLED"), "51": ("PA15", "SPINDLE_LED"),
    "59": ("PB6", "VAC_CONTROL"), "49": ("PA13", "SWDIO"), "50": ("PA14", "SWCLK"),
    "7": ("PG10", "SPINDLE_NRST"), "61": ("PB8", "SPINDLE_BOOT0"),
    "28": ("VREF+", "3V3A"), "29": ("VDDA", "3V3A"),
}

# (net, ref, pin) connections the firmware relies on.
LINKS = [
    # gate driver inputs: U21 = motor 1, U22 = motor 2 (DRV8300: INLA/B/C = 1/2/3, INHA/B/C = 22/23/24)
    ("INLA1", "U21", "1"), ("INLB1", "U21", "2"), ("INLC1", "U21", "3"),
    ("INHA1", "U21", "22"), ("INHB1", "U21", "23"), ("INHC1", "U21", "24"),
    ("INLA2", "U22", "1"), ("INLB2", "U22", "2"), ("INLC2", "U22", "3"),
    ("INHA2", "U22", "22"), ("INHB2", "U22", "23"), ("INHC2", "U22", "24"),
    # XY ESP32-S3 (U8): IO15 (pin 8) = BoardCOM2 -> G474 RX, IO16 (pin 9) = BoardCOM1 <- G474 TX
    ("BoardCOM2", "U8", "8"), ("BoardCOM1", "U8", "9"),
    # XY control of the G474's reset and boot pins through 1k: IO1 (pin 39), IO2 (pin 38)
    ("SPINDLE_RST_X", "U8", "39"), ("SPINDLE_BOOT_X", "U8", "38"),
    ("BoardVersion", "U8", "10"),
    # each sense input reads the shunt under the low-side FET of the phase the firmware assumes:
    # IS<x><m> <- 1k <- S<x><m> = source of the low-side FET whose drain (pin 5) is PH<x><m>
    ("SA1", "R116", "1"), ("SA1", "Q11", "1"), ("PHA1", "Q11", "5"),
    ("SB1", "R117", "1"), ("SB1", "Q13", "1"), ("PHB1", "Q13", "5"),
    ("SC1", "R118", "1"), ("SC1", "Q15", "1"), ("PHC1", "Q15", "5"),
    ("SA2", "R146", "1"), ("SA2", "Q21", "1"), ("PHA2", "Q21", "5"),
    ("SB2", "R147", "1"), ("SB2", "Q23", "1"), ("PHB2", "Q23", "5"),
    ("SC2", "R148", "1"), ("SC2", "Q25", "1"), ("PHC2", "Q25", "5"),
    ("ISB1", "R117", "2"), ("ISC1", "R118", "2"), ("ISB2", "R147", "2"), ("ISC2", "R148", "2"),
]

# ref -> value the firmware's constants assume
VALUES = {
    "R113": "5m", "R114": "5m", "R115": "5m",   # shunts (SHUNT_OHMS)
    "R143": "5m", "R144": "5m", "R145": "5m",
    "R171": "100k", "R172": "6.8k",           # VBUS divider (board.h VBUS_DIVIDER)
    "R173": "10k", "R174": "10k",             # NTC pull-ups
    "RT1": "10k NTC", "RT2": "10k NTC",       # NCP18XH103F03RB, B 3380
    "R116": "1k", "R119": "31.6k",            # sense bias network (SENSE_DIVIDER), motor 1 phase A
    "R146": "1k", "R149": "31.6k",
    "R26": "10kΩ", "R4": "2.2kΩ",             # BoardVersion 10k / 2.2k -> ~0.60 V = Unified36V band
    "R160": "4.7k", "R163": "4.7k",           # high-side input pull-downs
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kicad-cli", default=shutil.which("kicad-cli") or
                    "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        net = os.path.join(tmp, "ucb.xml")
        subprocess.run([args.kicad_cli, "sch", "export", "netlist", "--format", "kicadxml", "-o", net, SCH],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        root = ET.parse(net).getroot()

    vals = {c.get("ref"): c.findtext("value") for c in root.iter("comp")}
    pinnet, pinfn = {}, {}
    for n in root.iter("net"):
        for node in n.iter("node"):
            pinnet[(node.get("ref"), node.get("pin"))] = n.get("name")
            pinfn[(node.get("ref"), node.get("pin"))] = node.get("pinfunction") or ""

    bad = 0
    mcu = [r for r, v in vals.items() if v and "G474" in v]
    if mcu != ["U20"]:
        print(f"FAIL  expected one STM32G474 as U20, found {mcu}")
        bad += 1
    for pin, (name, want) in MCU.items():
        got, fn = pinnet.get(("U20", pin)), pinfn.get(("U20", pin), "")
        ok = got == want and fn.split("_")[0] == name
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  U20.{pin:<3} {name:<6} {want:<15} netlist: {fn} {got}")
    for want, ref, pin in LINKS:
        got = pinnet.get((ref, pin))
        ok = got == want
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  {ref}.{pin:<4} {want:<15} netlist: {got}")
    for ref, want in VALUES.items():
        ok = vals.get(ref) == want
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  {ref:<6} = {want:<10} netlist: {vals.get(ref)}")
    print(f"\n{bad} mismatch(es)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
