#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(*args: str, cwd: pathlib.Path) -> None:
    print("+", " ".join(args))
    subprocess.run(args, cwd=cwd, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Apply the pinned direct-Blink overlay and build content_shell"
    )
    parser.add_argument("chromium_src", type=pathlib.Path)
    parser.add_argument(
        "--out",
        default="out/nts-counter",
        help="Chromium output directory relative to the Chromium checkout",
    )
    parser.add_argument(
        "--gn-args",
        default="is_debug=true symbol_level=1",
        help="GN args used when generating the output directory",
    )
    args = parser.parse_args()

    checkout = args.chromium_src.resolve()
    apply_script = ROOT / "scripts" / "apply_chromium.py"

    run(sys.executable, str(apply_script), str(checkout), cwd=ROOT)
    run("gn", "gen", args.out, f"--args={args.gn_args}", cwd=checkout)
    run("autoninja", "-C", args.out, "content_shell", cwd=checkout)

    page = (ROOT / "examples" / "counter" / "index.html").resolve().as_uri()
    print()
    print("Build complete.")
    print("Run the content_shell binary from the Chromium output directory with:")
    print(f"  --native-typescript-counter {page}")


if __name__ == "__main__":
    main()
