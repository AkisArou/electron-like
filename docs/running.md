# Running the direct-Blink counter

The acceptance fixture is intentionally a plain-C application loaded into a patched, pinned Chromium `content_shell`. The HTML page contains no script; the renderer observer starts the C application after the main document fires `DOMContentLoaded`.

## Prerequisites

- Chromium's documented checkout/build prerequisites and `depot_tools` in `PATH`.
- A clean Chromium source checkout at exactly:

```text
96324a4012fe62f48b9463a67486eeb645bc5c78
```

The helper refuses another revision or a dirty checkout.

## Build

From this repository:

```sh
python3 scripts/build_chromium_counter.py /path/to/chromium/src
```

The helper:

1. verifies the exact Chromium revision;
2. applies the patch series in `chromium/patches/`;
3. overlays the bridge, portable C runtime, and the exact `examples/counter/app.c` source into `third_party/blink/renderer/native_typescript`;
4. runs `gn gen out/nts-counter`;
5. runs `autoninja -C out/nts-counter content_shell`.

Custom GN args can be passed with `--gn-args`.

## Run

Launch the built `content_shell` with the harness switch and the absolute `file://` URL of `examples/counter/index.html`:

```text
--native-typescript-counter file:///absolute/path/to/electron-like/examples/counter/index.html
```

On Linux the executable is normally:

```sh
/path/to/chromium/src/out/nts-counter/content_shell \
  --native-typescript-counter \
  file:///absolute/path/to/electron-like/examples/counter/index.html
```

The expected page is created by native code after the empty document loads:

```text
Native TypeScript
[ Count: 0 ]
```

Each real browser click enters `BlinkNativeEventListener`, dispatches the opaque callback token into `examples/counter/app.c`, increments native state, and calls `nts_web_node_set_text_content()` on the captured button handle. No application JavaScript is present or evaluated.

## What this fixture proves

For this exact surface the renderer call chain is:

```text
plain C application
  -> typed nts_web C ABI
  -> checked realm/handle/subscription tables
  -> Blink C++ capsules
  -> Document / Node / EventTarget
  -> layout / paint
```

The counter uses:

- `Document::body()` directly;
- the binding-neutral `Document::CreateElementForBinding()` patch;
- `Node::setTextContent()` for ordinary non-script elements;
- a proven no-failure `appendChild` subset: a detached `Element` appended to an `Element` parent;
- Blink's `NativeEventListener` and native `EventTarget::addEventListener` overload;
- explicit listener cancellation and Oilpan-root teardown on realm destruction.

The generic `appendChild` exception-bearing surface is deliberately not claimed yet. Shapes that are not statically inside the proven subset return `NTS_WEB_OPERATION_DISABLED` rather than using an unchecked Blink mutation API.
