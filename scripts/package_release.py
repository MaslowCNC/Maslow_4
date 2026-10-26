#!/usr/bin/env python3

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import shutil
import sys
import urllib.request
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ESPTOOL_VERSION = "v4.6"
PLATFORM_CONFIG = {
    "posix": {
        "script_extension": ".sh",
        "exe_extension": "",
        "include_esptool_binary": False,
    },
    "win64": {
        "script_extension": ".bat",
        "exe_extension": ".exe",
        "include_esptool_binary": True,
    },
}


def log(message: str) -> None:
    print(message, flush=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Package Maslow firmware release assets.")
    parser.add_argument("--manifest", required=True, help="Path to release-manifest.json")
    parser.add_argument("--source-dir", required=True, help="Directory containing staged build outputs")
    parser.add_argument("--dist-dir", required=True, help="Directory to write packaged release assets")
    parser.add_argument("--version", required=True, help="Version without the leading v (for example 1.21)")
    parser.add_argument("--tag", required=True, help="Git tag for this release (for example v1.21)")
    return parser.parse_args()


def ensure_clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def format_value(template: str, *, env: str, version: str, tag: str) -> str:
    return template.format(env=env, target=env, version=version, tag=tag)


def validate_expected_inputs(manifest: dict, source_dir: Path, version: str, tag: str) -> list[str]:
    missing: list[str] = []
    for target in manifest["targets"]:
        env = target["env"]
        log(f"Validating staged inputs for target '{env}'")
        optional_patterns = {
            format_value(pattern, env=env, version=version, tag=tag)
            for pattern in target.get("optional_files", [])
        }
        for pattern in target["expected_artifact_globs"] + target.get("optional_files", []):
            formatted = format_value(pattern, env=env, version=version, tag=tag)
            matches = sorted(glob.glob(str(source_dir / formatted)))
            is_optional = formatted in optional_patterns
            status = "OK" if matches else ("OPTIONAL" if is_optional else "MISSING")
            log(f"  [{status}] {formatted}")
            if matches:
                for match in matches:
                    log(f"         -> {Path(match).relative_to(source_dir)}")
            elif not is_optional:
                missing.append(formatted)
    return missing


def add_release_copy(records: list[dict], seen_names: set[str], source_path: Path, output_name: str, dist_dir: Path, target: str, required: bool, source_label: str) -> None:
    if output_name in seen_names:
        raise ValueError(f"Duplicate release asset name generated: {output_name}")
    destination = dist_dir / output_name
    shutil.copy2(source_path, destination)
    records.append(
        {
            "name": output_name,
            "path": str(destination),
            "target": target,
            "required": required,
            "source": source_label,
        }
    )
    seen_names.add(output_name)
    log(f"Packaged {output_name} from {source_label}")


def copy_script_to_zip(zip_file: ZipFile, source_path: Path, archive_name: str) -> None:
    info = ZipInfo.from_file(source_path, archive_name)
    info.external_attr = 0o100755 << 16
    with source_path.open("rb") as handle:
        zip_file.writestr(info, handle.read(), compress_type=ZIP_DEFLATED)


def download_esptool_archive(cache_dir: Path, platform: str) -> Path:
    if PLATFORM_CONFIG[platform]["include_esptool_binary"]:
        archive_name = f"esptool-{ESPTOOL_VERSION}-{platform}.zip"
        url = f"https://github.com/espressif/esptool/releases/download/{ESPTOOL_VERSION}/{archive_name}"
    else:
        archive_name = f"{ESPTOOL_VERSION}.zip"
        url = f"https://github.com/espressif/esptool/archive/refs/tags/{archive_name}"

    cache_dir.mkdir(parents=True, exist_ok=True)
    archive_path = cache_dir / archive_name
    if archive_path.exists():
        log(f"Reusing cached esptool archive {archive_name}")
        return archive_path

    log(f"Downloading {url}")
    with urllib.request.urlopen(url) as response, archive_path.open("wb") as output:
        shutil.copyfileobj(response, output)
    return archive_path


def build_host_package(
    *,
    platform_entry: dict,
    env: str,
    version: str,
    tag: str,
    manifest_path: Path,
    source_dir: Path,
    dist_dir: Path,
    records: list[dict],
    seen_names: set[str],
) -> None:
    platform = platform_entry["platform"]
    config = PLATFORM_CONFIG[platform]
    repo_root = manifest_path.parent
    firmware_root = repo_root / "firmware"
    install_root = firmware_root / "install_scripts"
    fluidterm_root = firmware_root / "fluidterm"
    env_root = source_dir / "targets" / env
    zip_root_name = f"fluidnc-maslow4-{version}-{platform}"
    archive_names = [
        format_value(platform_entry["canonical_output"], env=env, version=version, tag=tag),
        format_value(platform_entry["compatibility_output"], env=env, version=version, tag=tag),
    ]

    # Cache downloaded esptool archives between reruns to avoid repeated network fetches.
    esptool_archive = download_esptool_archive(repo_root / ".release-cache", platform)
    with ZipFile(esptool_archive, "r") as esptool_zip:
        for archive_name in archive_names:
            if archive_name in seen_names:
                raise ValueError(f"Duplicate release asset name generated: {archive_name}")
            zip_path = dist_dir / archive_name
            log(f"Creating host package {archive_name}")
            with ZipFile(zip_path, "w", compression=ZIP_DEFLATED) as zip_file:
                zip_file.write(install_root / platform / "HOWTO-INSTALL.txt", arcname=f"{zip_root_name}/HOWTO-INSTALL.txt")
                zip_file.write(source_dir / "common" / "boot_app0.bin", arcname=f"{zip_root_name}/common/boot_app0.bin")
                for fuse_name in ["SecurityFusesOK.bin", "SecurityFusesOK0.bin"]:
                    zip_file.write(install_root / "common" / fuse_name, arcname=f"{zip_root_name}/common/{fuse_name}")

                zip_file.write(env_root / "bootloader.bin", arcname=f"{zip_root_name}/{env}/bootloader.bin")
                zip_file.write(env_root / "firmware.bin", arcname=f"{zip_root_name}/{env}/firmware.bin")
                zip_file.write(env_root / "partitions.bin", arcname=f"{zip_root_name}/{env}/partitions.bin")
                zip_file.write(env_root / "littlefs.bin", arcname=f"{zip_root_name}/{env}/littlefs.bin")
                zip_file.write(source_dir / "webui" / "index.html.gz", arcname=f"{zip_root_name}/{env}/index.html.gz")

                install_script = install_root / platform / f"install-{env}{config['script_extension']}"
                copy_script_to_zip(zip_file, install_script, f"{zip_root_name}/{install_script.name}")
                for script_name in ["full-install", "install-fs", "fluidterm", "checksecurity", "erase", "tools"]:
                    source_script = install_root / platform / f"{script_name}{config['script_extension']}"
                    copy_script_to_zip(zip_file, source_script, f"{zip_root_name}/{source_script.name}")
                if platform == "posix":
                    copy_script_to_zip(zip_file, install_root / platform / "full-install.command", f"{zip_root_name}/full-install.command")

                for fluidterm_name in ["fluidterm.py", "README-FluidTerm.md"]:
                    zip_file.write(fluidterm_root / fluidterm_name, arcname=f"{zip_root_name}/common/{fluidterm_name}")
                if platform == "win64":
                    zip_file.write(fluidterm_root / "fluidterm.exe", arcname=f"{zip_root_name}/win64/fluidterm.exe")

                if config["include_esptool_binary"]:
                    readme_name = f"README-ESPTOOL-{ESPTOOL_VERSION}.txt"
                    zip_file.write(install_root / platform / "README-ESPTOOL.txt", arcname=f"{zip_root_name}/{platform}/{readme_name}")
                    binary_name = f"esptool{config['exe_extension']}"
                    archive_member = f"esptool-{ESPTOOL_VERSION}-{platform}/{binary_name}"
                    info = ZipInfo(f"{zip_root_name}/{platform}/{binary_name}")
                    info.external_attr = 0o100755 << 16
                    zip_file.writestr(info, esptool_zip.read(archive_member), compress_type=ZIP_DEFLATED)
                else:
                    readme_name = f"README-ESPTOOL-SOURCE-{ESPTOOL_VERSION}.txt"
                    zip_file.write(install_root / "common" / "README-ESPTOOL-SOURCE.txt", arcname=f"{zip_root_name}/common/{readme_name}")
                    zip_file.write(esptool_archive, arcname=f"{zip_root_name}/common/esptool-source.zip")

            records.append(
                {
                    "name": archive_name,
                    "path": str(zip_path),
                    "target": env,
                    "required": archive_name == format_value(platform_entry["compatibility_output"], env=env, version=version, tag=tag),
                    "source": f"generated host package for {platform}",
                }
            )
            seen_names.add(archive_name)


def validate_final_assets(manifest: dict, records: list[dict], version: str, tag: str) -> list[str]:
    produced = {record["name"] for record in records}
    expected_names: set[str] = set()
    missing: list[str] = []
    for template in manifest.get("required_release_assets", []):
        if "{env}" in template or "{target}" in template:
            for target in manifest["targets"]:
                expected_names.add(format_value(template, env=target["env"], version=version, tag=tag))
        else:
            expected_names.add(format_value(template, env=manifest["targets"][0]["env"], version=version, tag=tag))
    for expected in sorted(expected_names):
        if expected not in produced:
            missing.append(expected)
    return missing


def write_release_metadata(records: list[dict], dist_dir: Path) -> None:
    metadata_records = []
    for record in records:
        asset_path = Path(record["path"])
        metadata_records.append(
            {
                "name": record["name"],
                "target": record["target"],
                "size": asset_path.stat().st_size,
                "sha256": sha256_file(asset_path),
                "source": record["source"],
                "required": record["required"],
            }
        )

    metadata_records.sort(key=lambda item: item["name"])
    release_assets_path = dist_dir / "release-assets.json"
    with release_assets_path.open("w", encoding="utf-8") as handle:
        json.dump(metadata_records, handle, indent=2)
        handle.write("\n")

    checksum_entries = []
    for asset_path in sorted(dist_dir.iterdir()):
        if asset_path.is_file() and asset_path.name != "SHA256SUMS.txt":
            checksum_entries.append(f"{sha256_file(asset_path)}  {asset_path.name}")
    with (dist_dir / "SHA256SUMS.txt").open("w", encoding="utf-8") as handle:
        handle.write("\n".join(checksum_entries) + "\n")


def main() -> int:
    args = parse_args()
    manifest_path = Path(args.manifest).resolve()
    source_dir = Path(args.source_dir).resolve()
    dist_dir = Path(args.dist_dir).resolve()
    manifest = load_manifest(manifest_path)

    if not source_dir.exists():
        log(f"Source directory does not exist: {source_dir}")
        return 1

    missing_inputs = validate_expected_inputs(manifest, source_dir, args.version, args.tag)
    if missing_inputs:
        log("Required staged inputs are missing:")
        for item in missing_inputs:
            log(f"  - {item}")
        return 1

    ensure_clean_dir(dist_dir)
    records: list[dict] = []
    seen_names: set[str] = set()

    for target in manifest["targets"]:
        env = target["env"]
        env_root = source_dir / "targets" / env
        canonical_assets = target["canonical_assets"]
        alias_assets = target["release_aliases"]

        firmware_source = env_root / "firmware.bin"
        firmware_names = [format_value(canonical_assets["firmware"], env=env, version=args.version, tag=args.tag)] + alias_assets.get("firmware", [])
        for name in firmware_names:
            add_release_copy(records, seen_names, firmware_source, name, dist_dir, env, True, f"targets/{env}/firmware.bin")

        webui_source = source_dir / "webui" / "index.html.gz"
        webui_names = [format_value(canonical_assets["webui"], env=env, version=args.version, tag=args.tag)] + alias_assets.get("webui", [])
        for name in webui_names:
            add_release_copy(records, seen_names, webui_source, name, dist_dir, env, True, "webui/index.html.gz")

        config_source = source_dir / "config" / "maslow.yaml"
        if config_source.exists():
            config_names = [format_value(canonical_assets["config"], env=env, version=args.version, tag=args.tag)] + alias_assets.get("config", [])
            for name in config_names:
                add_release_copy(records, seen_names, config_source, name, dist_dir, env, False, "config/maslow.yaml")
        else:
            log("Optional config/maslow.yaml not found; skipping config assets")

        for platform_entry in manifest.get("host_packages", []):
            build_host_package(
                platform_entry=platform_entry,
                env=env,
                version=args.version,
                tag=args.tag,
                manifest_path=manifest_path,
                source_dir=source_dir,
                dist_dir=dist_dir,
                records=records,
                seen_names=seen_names,
            )

    missing_outputs = validate_final_assets(manifest, records, args.version, args.tag)
    if missing_outputs:
        log("Required release assets were not produced:")
        for item in missing_outputs:
            log(f"  - {item}")
        return 1

    write_release_metadata(records, dist_dir)
    log("Final release directory contents:")
    for asset_path in sorted(dist_dir.iterdir()):
        if asset_path.is_file():
            log(f"  - {asset_path.name} ({asset_path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
