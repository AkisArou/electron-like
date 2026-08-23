#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "chromium" / "bridge"

# These tokens are forbidden in the Native TypeScript -> Blink bridge itself.
# Blink core may still include V8 headers internally; the invariant is that the
# NTS binding path never constructs or routes through V8 state/wrappers/values.
FORBIDDEN = {
    "v8::": "V8 value/runtime use",
    "ScriptState": "V8 ScriptState bridge",
    "V8Document": "generated V8 DOM wrapper",
    "V8Element": "generated V8 DOM wrapper",
    "V8Event": "generated V8 DOM wrapper",
    "ExecuteScript": "script evaluation",
    "EvaluateScript": "script evaluation",
    "eval(": "script evaluation",
}


def main() -> None:
    failures: list[str] = []
    for path in sorted(BRIDGE.rglob("*")):
        if path.suffix not in {".cc", ".h", ".c"}:
            continue
        text = path.read_text(encoding="utf-8")
        for token, reason in FORBIDDEN.items():
            if token in text:
                failures.append(f"{path.relative_to(ROOT)}: {token!r}: {reason}")

    if failures:
        print("Native Blink bridge violated the V8-free call-path invariant:")
        for failure in failures:
            print(f"  {failure}")
        return_code = 1
    else:
        print("Native Blink bridge contains no forbidden V8/JavaScript bridge tokens")
        return_code = 0
    raise SystemExit(return_code)


if __name__ == "__main__":
    main()
