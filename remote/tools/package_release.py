#!/usr/bin/env python3
"""Package the remote firmware for release.

Needs the three boards built first (pio run -e cyd -e cyd4 -e es3c40p, in remote/), which
leaves a merged firmware.factory.bin (bootloader + partitions + app, written at 0x0) per board.

Writes to --dist-dir:
  maslow-remote-<version>-windows.zip   Full-Install.bat + esptool.exe
  maslow-remote-<version>-macos.zip     Full-Install.command / .sh + esptool (arm64, x86_64)
  maslow-remote-<version>-linux.zip     Full-Install.sh + esptool (x86_64, aarch64)
Each zip carries firmware/<env>.bin for every board, so nothing has to be installed.

With --web-dir, also writes <env>.bin and manifest-<env>.json there for the browser
installer (docs/remote-installer/, ESP Web Tools).
"""

from __future__ import annotations

import argparse
import json
import tarfile
import urllib.request
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ESPTOOL_VERSION = "v4.10.0"
ESPTOOL_URL = "https://github.com/espressif/esptool/releases/download/{v}/esptool-{v}-{asset}"

REMOTE_DIR = Path(__file__).resolve().parent.parent
INSTALL_DIR = REMOTE_DIR / "install"

BOARDS = {
    "cyd": {"chip": "ESP32", "name": '2.8" Cheap Yellow Display (ESP32-2432S028R)'},
    "cyd4": {"chip": "ESP32", "name": '4.0" ESP32-32E / E32R40T'},
    "es3c40p": {"chip": "ESP32-S3", "name": '4.0" ESP32-S3 (Hosyond / lcdwiki ES3C40P)'},
}

# platform -> (scripts, [(esptool asset, tools/ subfolder)])
PLATFORMS = {
    "windows": (["Full-Install.bat"], [("windows-amd64.zip", "windows")]),
    "macos": (
        ["Full-Install.command", "Full-Install.sh"],
        [("macos-arm64.tar.gz", "macos-arm64"), ("macos-amd64.tar.gz", "macos-amd64")],
    ),
    "linux": (
        ["Full-Install.sh"],
        [("linux-amd64.tar.gz", "linux-amd64"), ("linux-aarch64.tar.gz", "linux-aarch64")],
    ),
}

ESPTOOL_README = """esptool {v} from Espressif: https://github.com/espressif/esptool
Downloaded from {url}
Licensed under GPL-2.0 (LICENSE in this folder); source code at the link above.
"""


def fetch(asset: str, cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / f"esptool-{ESPTOOL_VERSION}-{asset}"
    if not path.exists():
        url = ESPTOOL_URL.format(v=ESPTOOL_VERSION, asset=asset)
        print(f"Downloading {url}", flush=True)
        with urllib.request.urlopen(url) as response:
            path.write_bytes(response.read())
    return path


def esptool_files(archive: Path) -> dict[str, bytes]:
    """The esptool program and its license from a release archive, by file name."""
    wanted = {"esptool", "esptool.exe", "LICENSE"}
    out: dict[str, bytes] = {}
    if archive.name.endswith(".zip"):
        with ZipFile(archive) as z:
            for name in z.namelist():
                if Path(name).name in wanted:
                    out[Path(name).name] = z.read(name)
    else:
        with tarfile.open(archive) as t:
            for member in t.getmembers():
                if member.isfile() and Path(member.name).name in wanted:
                    out[Path(member.name).name] = t.extractfile(member).read()
    if not ({"esptool", "esptool.exe"} & out.keys()):
        raise SystemExit(f"No esptool program in {archive}")
    return out


def add(z: ZipFile, name: str, data: bytes, executable: bool = False) -> None:
    info = ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
    info.external_attr = (0o100755 if executable else 0o100644) << 16
    info.compress_type = ZIP_DEFLATED
    z.writestr(info, data)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--version", required=True, help="release version, e.g. 1.27.0")
    p.add_argument("--build-dir", type=Path, default=REMOTE_DIR / ".pio" / "build")
    p.add_argument("--dist-dir", type=Path, required=True)
    p.add_argument("--cache-dir", type=Path, default=REMOTE_DIR / ".release-cache")
    p.add_argument("--web-dir", type=Path, help="also write the browser installer's files here")
    args = p.parse_args()

    images = {}
    for env in BOARDS:
        image = args.build_dir / env / "firmware.factory.bin"
        if not image.is_file():
            raise SystemExit(f"Missing {image}: build it with pio run -e {env}")
        images[env] = image.read_bytes()

    args.dist_dir.mkdir(parents=True, exist_ok=True)
    for platform, (scripts, tools) in PLATFORMS.items():
        root = f"maslow-remote-{args.version}-{platform}"
        out = args.dist_dir / f"{root}.zip"
        with ZipFile(out, "w") as z:
            howto = (INSTALL_DIR / "HOWTO-INSTALL.txt").read_text()
            if platform == "windows":
                howto = howto.replace("\n", "\r\n")
            add(z, f"{root}/HOWTO-INSTALL.txt", howto.encode())
            for script in scripts:
                text = (INSTALL_DIR / script).read_text()
                if script.endswith(".bat"):
                    text = text.replace("\r\n", "\n").replace("\n", "\r\n")
                add(z, f"{root}/{script}", text.encode(), executable=not script.endswith(".bat"))
            for env, data in images.items():
                add(z, f"{root}/firmware/{env}.bin", data)
            for asset, folder in tools:
                url = ESPTOOL_URL.format(v=ESPTOOL_VERSION, asset=asset)
                for name, data in esptool_files(fetch(asset, args.cache_dir)).items():
                    add(z, f"{root}/tools/{folder}/{name}", data, executable=name == "esptool")
                add(z, f"{root}/tools/{folder}/README-ESPTOOL.txt", ESPTOOL_README.format(v=ESPTOOL_VERSION, url=url).encode())
        print(f"Wrote {out} ({out.stat().st_size // 1024} KB)", flush=True)

    if args.web_dir:
        args.web_dir.mkdir(parents=True, exist_ok=True)
        for env, board in BOARDS.items():
            (args.web_dir / f"{env}.bin").write_bytes(images[env])
            manifest = {
                "name": f"Maslow remote: {board['name']}",
                "version": args.version,
                "new_install_prompt_erase": True,
                "builds": [{"chipFamily": board["chip"], "parts": [{"path": f"{env}.bin", "offset": 0}]}],
            }
            (args.web_dir / f"manifest-{env}.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"Wrote the browser installer files to {args.web_dir}", flush=True)


if __name__ == "__main__":
    main()
