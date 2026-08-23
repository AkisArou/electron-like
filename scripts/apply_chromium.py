#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
REVISION_FILE = ROOT / "chromium" / "revision.json"
PATCH_DIR = ROOT / "chromium" / "patches"


def output(*args: str, cwd: pathlib.Path) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def run(*args: str, cwd: pathlib.Path) -> None:
    subprocess.run(args, cwd=cwd, check=True)


def copy_overlay(checkout: pathlib.Path) -> None:
    destination = checkout / "chromium" / "bridge"
    destination.mkdir(parents=True, exist_ok=True)

    for source in (ROOT / "chromium" / "bridge").iterdir():
        if source.is_file():
            shutil.copy2(source, destination / source.name)

    shutil.copy2(ROOT / "include" / "nts_web.h", destination / "nts_web.h")

    runtime_destination = destination / "runtime"
    runtime_destination.mkdir(parents=True, exist_ok=True)
    for name in ("nts_handle_table.c", "nts_handle_table.h", "nts_web_exception.c"):
        shutil.copy2(ROOT / "src" / "runtime" / name,
                     runtime_destination / name)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Apply electron-like's direct-Blink patch/overlay to Chromium"
    )
    parser.add_argument("chromium_src", type=pathlib.Path)
    args = parser.parse_args()

    checkout = args.chromium_src.resolve()
    if not (checkout / ".git").exists():
        raise SystemExit(f"not a Chromium git checkout: {checkout}")

    pin = json.loads(REVISION_FILE.read_text(encoding="utf-8"))["revision"]
    head = output("git", "rev-parse", "HEAD", cwd=checkout)
    if head != pin:
        raise SystemExit(f"Chromium revision mismatch: expected {pin}, got {head}")

    if output("git", "status", "--porcelain", cwd=checkout):
        raise SystemExit("Chromium checkout must be clean before applying the overlay")

    patch_names = [
        line.strip()
        for line in (PATCH_DIR / "series").read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]

    for name in patch_names:
        patch = PATCH_DIR / name
        run("git", "apply", "--check", str(patch), cwd=checkout)
        run("git", "apply", str(patch), cwd=checkout)
        print(f"applied {name}")

    copy_overlay(checkout)
    print(f"installed Native TypeScript Blink bridge at {checkout / 'chromium' / 'bridge'}")
    print("GN target: //chromium/bridge:nts_blink_bridge")


if __name__ == "__main__":
    main()
