#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
REVISION_FILE = ROOT / "chromium" / "revision.json"
PATCH_DIR = ROOT / "chromium" / "patches"
RAW_ROOT = "https://raw.githubusercontent.com/chromium/chromium"


def patch_input_paths(patch: pathlib.Path) -> list[str]:
    paths: list[str] = []
    for line in patch.read_text(encoding="utf-8").splitlines():
        if not line.startswith("--- "):
            continue
        value = line[4:].split("\t", 1)[0]
        if value == "/dev/null":
            continue
        if value.startswith("a/"):
            value = value[2:]
        if value not in paths:
            paths.append(value)
    return paths


def download(revision: str, relative: str, destination: pathlib.Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    url = f"{RAW_ROOT}/{revision}/{relative}"
    with urllib.request.urlopen(url) as response:
        destination.write_bytes(response.read())


def run(*args: str, cwd: pathlib.Path) -> None:
    subprocess.run(args, cwd=cwd, check=True)


def main() -> None:
    revision = json.loads(REVISION_FILE.read_text(encoding="utf-8"))["revision"]
    patches = [
        PATCH_DIR / name.strip()
        for name in (PATCH_DIR / "series").read_text(encoding="utf-8").splitlines()
        if name.strip() and not name.lstrip().startswith("#")
    ]

    with tempfile.TemporaryDirectory(prefix="nts-chromium-patch-") as temp:
        checkout = pathlib.Path(temp)
        needed: list[str] = []
        for patch in patches:
            for path in patch_input_paths(patch):
                if path not in needed:
                    needed.append(path)
        for path in needed:
            download(revision, path, checkout / path)

        run("git", "init", "-q", cwd=checkout)
        run("git", "config", "user.email", "patch-check@example.invalid", cwd=checkout)
        run("git", "config", "user.name", "patch-check", cwd=checkout)
        run("git", "add", ".", cwd=checkout)
        run("git", "commit", "-qm", "pinned chromium inputs", cwd=checkout)

        for patch in patches:
            run("git", "apply", "--check", str(patch), cwd=checkout)
            run("git", "apply", str(patch), cwd=checkout)
            print(f"verified {patch.name}")

    print(f"all Chromium patches apply to {revision}")


if __name__ == "__main__":
    main()
