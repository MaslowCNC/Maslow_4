#!/usr/bin/env python3
"""Build the remote firmware and refresh the files users download, in remote/downloads/.

Run by hand when the remote changes, then commit remote/downloads/:

  cd remote
  python3 tools/make_downloads.py

It runs `pio run` (skip with --no-build), which leaves a merged firmware.factory.bin
(bootloader + partitions + app, written at 0x0), and writes:
  maslow-remote-windows.zip   Full-Install.bat + esptool.exe
  maslow-remote-macos.zip     Full-Install.command / .sh + esptool (arm64, x86_64)
  maslow-remote-linux.zip     Full-Install.sh + esptool (x86_64, aarch64)
  maslow-remote.bin           the image, for the browser installer
  manifest.json               ESP Web Tools manifest for the browser installer
                              (docs/remote-installer/index.html loads it from GitHub)
  VERSION.txt                 what was built
Each zip carries firmware/maslow-remote.bin and esptool, so users install nothing.
"""

from __future__ import annotations

import argparse
import datetime
import json
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ESPTOOL_VERSION = "v4.10.0"
ESPTOOL_URL = "https://github.com/espressif/esptool/releases/download/{v}/esptool-{v}-{asset}"

REMOTE_DIR = Path(__file__).resolve().parent.parent
INSTALL_DIR = REMOTE_DIR / "install"

ENV = "es3c40p"  # the only board: 4.0" ESP32-S3 Hosyond / lcdwiki ES3C40P
IMAGE = "maslow-remote.bin"

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


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=REMOTE_DIR, capture_output=True, text=True).stdout.strip()


def default_version() -> str:
    """<date>-<commit>, with -modified when remote/ has uncommitted changes."""
    commit = git("rev-parse", "--short", "HEAD") or "unknown"
    dirty = git("status", "--porcelain", "--", "src", "platformio.ini", "install")
    return f"{datetime.date.today():%Y-%m-%d}-{commit}" + ("-modified" if dirty else "")


def build() -> None:
    # PlatformIO's own install first: a stray older pio on PATH can use other tool versions.
    penv = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    pio = str(penv) if penv.exists() else shutil.which("pio") or "pio"
    print(f"Building: {pio} run", flush=True)
    subprocess.run([pio, "run"], cwd=REMOTE_DIR, check=True)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--version", help="version label (default: <date>-<commit>)")
    p.add_argument("--no-build", action="store_true", help="package the existing build")
    p.add_argument("--build-dir", type=Path, default=REMOTE_DIR / ".pio" / "build")
    p.add_argument("--out-dir", type=Path, default=REMOTE_DIR / "downloads")
    p.add_argument("--cache-dir", type=Path, default=REMOTE_DIR / ".release-cache")
    args = p.parse_args()
    version = args.version or default_version()

    if not args.no_build:
        build()

    factory = args.build_dir / ENV / "firmware.factory.bin"
    if not factory.is_file():
        raise SystemExit(f"Missing {factory}: build it with pio run (in remote/)")
    image = factory.read_bytes()

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    version_text = f"Maslow remote {version}\nBoard: 4.0\" ESP32-S3 Hosyond / lcdwiki ES3C40P\nesptool {ESPTOOL_VERSION}\n"
    for platform, (scripts, tools) in PLATFORMS.items():
        root = f"maslow-remote-{platform}"
        out = out_dir / f"{root}.zip"
        with ZipFile(out, "w") as z:
            howto = (INSTALL_DIR / "HOWTO-INSTALL.txt").read_text()
            if platform == "windows":
                howto = howto.replace("\n", "\r\n")
            add(z, f"{root}/HOWTO-INSTALL.txt", howto.encode())
            add(z, f"{root}/VERSION.txt", version_text.encode())
            for script in scripts:
                text = (INSTALL_DIR / script).read_text()
                if script.endswith(".bat"):
                    text = text.replace("\r\n", "\n").replace("\n", "\r\n")
                add(z, f"{root}/{script}", text.encode(), executable=not script.endswith(".bat"))
            add(z, f"{root}/firmware/{IMAGE}", image)
            for asset, folder in tools:
                url = ESPTOOL_URL.format(v=ESPTOOL_VERSION, asset=asset)
                for name, data in esptool_files(fetch(asset, args.cache_dir)).items():
                    add(z, f"{root}/tools/{folder}/{name}", data, executable=name == "esptool")
                add(z, f"{root}/tools/{folder}/README-ESPTOOL.txt", ESPTOOL_README.format(v=ESPTOOL_VERSION, url=url).encode())
        print(f"Wrote {out} ({out.stat().st_size // 1024} KB)", flush=True)

    (out_dir / IMAGE).write_bytes(image)
    manifest = {
        "name": "Maslow remote",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": IMAGE, "offset": 0}]}],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (out_dir / "VERSION.txt").write_text(version_text)
    print(f"Wrote {IMAGE}, manifest.json and VERSION.txt ({version}) to {out_dir}", flush=True)
    print("Commit remote/downloads/ to publish them.", flush=True)


if __name__ == "__main__":
    main()
