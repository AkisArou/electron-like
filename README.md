# electron-like

A research host for running statically compiled Native TypeScript directly against Blink, without routing Web APIs through V8 or JavaScript.

The intended product shape is not Electron-compatible internally. It keeps Chromium's browser/renderer security model, but the renderer owns a Native TypeScript runtime and calls Blink through a generated native ABI.

```text
TypeScript application
        |
        | typechecked with TypeScript lib.dom.d.ts
        v
ScriptC / Native TypeScript
        |
        | C or LLVM lowering
        v
generated C
        |
        | typed extern "C" ABI
        v
generated Blink C++ capsule
        |
        | direct calls
        v
Blink DOM / Web APIs
        |
        v
Chromium rendering / compositor / GPU
```

## Architectural invariants

- V8 is not part of the Native TypeScript Web API call path.
- Application code never evaluates JavaScript source.
- DOM calls are local to the Chromium renderer process and remain synchronous where the Web API is synchronous.
- The native application/runtime executes on the Blink renderer sequence that owns its execution context.
- `lib.dom.d.ts` is the TypeScript source contract; the pinned Blink WebIDL database supplies exact Chromium binding semantics.
- Generated application C sees only opaque, generation-checked handles. Blink C++ pointers never cross the C ABI.
- Blink/Oilpan owns Blink objects; the Native TypeScript realm owns the strong edges required by reachable native handles.
- Events enter compiled callbacks directly. They are not serialized through JavaScript or generic IPC.
- Web promises settle ScriptC promises directly through a binding-neutral async seam.
- DOM/Web exceptions become ScriptC exceptions directly rather than V8 exception objects.
- Navigation or execution-context destruction invalidates the complete realm and all handles created by it.
- Browser-process authority is exposed only through typed asynchronous capabilities; DOM objects and raw pointers never cross the process boundary.
- The public native ABI is statically generated from reached operations. There is no `invoke(name, args)` or string-dispatch fallback.

## Binding inputs

Two descriptions deliberately have different jobs:

- TypeScript's `lib.dom.d.ts` defines what ordinary TypeScript source can say and how it is type-checked.
- Blink's WebIDL database, from the exact pinned Chromium revision, defines the implementation-facing contract: WebIDL scalar/string conversions, nullability, dictionaries and unions, exposure/runtime conditions, exception behavior, implementation names, execution-context requirements, reactions, promises, callbacks, and related extended attributes.

The WebIDL binding generator joins reached `lib.dom.d.ts` members to the corresponding pinned Blink IDL definitions and emits a closed native binding manifest plus the C/C++ capsule surface. Application developers do not consume a replacement DOM declaration library.

## Current implementation

The repository is pinned to Chromium `96324a4012fe62f48b9463a67486eeb645bc5c78`.

Implemented in the standalone native contract:

- typed C Web API ABI in `include/nts_web.h`;
- explicit owned lifetime for Web/DOM exception strings;
- generation-checked DOM handle slots with retain/release, stale-handle rejection, subtype validation and realm invalidation;
- generation-checked event-subscription resources with deterministic cancellation;
- a plain-C interactive counter shaped like future ScriptC output, including a retained callback token and a captured DOM handle;
- a standalone counter conformance test that simulates clicks, verifies `Count: 1` / `Count: 2`, and verifies subscription/handle teardown;
- CI build/tests, exact Chromium patch checking, and a bridge scan that rejects V8/JavaScript bridge tokens.

Implemented in the Blink integration source:

- a Chromium patch that introduces a binding-neutral `WebExceptionState` and keeps the existing V8-facing `Document::CreateElementForBinding` as a sibling adapter;
- a direct C capsule for `nts_web_document_create_element()` that resolves a checked `Document` handle, calls Blink's own create-element algorithm, translates the neutral exception, and interns the real returned `Element`;
- direct `Document::body()` projection with WebIDL null represented by the zero native handle;
- direct `Node::setTextContent()` for the ordinary non-script element shape used by the fixture; script-element text is refused until its Trusted Types binding path is neutralized rather than bypassed;
- a proven `appendChild` subset for a detached `Element` appended to an `Element` parent; other shapes refuse instead of silently using an unchecked DOM mutation path;
- `NtsWebRealm`, bound to one renderer sequence and one Blink execution context, with deterministic invalidation through `ExecutionContextLifecycleObserver`;
- `BlinkNodeRegistry`, which roots real Blink nodes with Oilpan `Persistent<Node>` while the C side sees only opaque handles;
- identity interning without a long-lived untraced raw-pointer reverse map;
- `BlinkNativeEventListener` plus a realm-owned subscription registry, using Blink's native `EventTarget::addEventListener` / `removeEventListener` path and exact listener identity;
- a Chromium patch that prevents native listeners from touching the V8 isolated-world activity logger merely to register a listener;
- a Blink-owned counter host that converts a public `WebLocalFrame*` to its internal `Document` and launches the exact same plain-C counter source;
- an opt-in `content_shell` observer that starts the counter on the main frame at `DOMContentLoaded` and tears it down across navigation/frame destruction.

The acceptance page is [`examples/counter/index.html`](examples/counter/index.html) and contains no script.

## Build and run

Chromium patches are checked against the exact pinned source in CI. A clean Chromium checkout at the pinned revision can be overlaid and built with:

```sh
python3 scripts/build_chromium_counter.py /path/to/chromium/src
```

The overlay installs as `//third_party/blink/renderer/native_typescript`. Important GN targets are:

```text
//third_party/blink/renderer/native_typescript:nts_blink_bridge
//third_party/blink/renderer/native_typescript:nts_counter_example
//third_party/blink/renderer/native_typescript:nts_counter_host
```

Run the built `content_shell` with:

```text
--native-typescript-counter file:///absolute/path/to/electron-like/examples/counter/index.html
```

See [`docs/running.md`](docs/running.md) for the exact workflow.

The interactive call chain is now represented as:

```text
plain C counter
  -> nts_web_document / document_body / create_element
  -> checked realm and DOM handles
  -> Blink Document / Node calls
  -> Oilpan-rooted elements
  -> native EventTarget listener registration
  -> real browser click
  -> opaque C callback token
  -> captured button handle
  -> nts_web_node_set_text_content
  -> Blink layout / paint
```

No V8 object, V8 function, JavaScript source evaluation, property lookup, or generic command bridge participates in that Native TypeScript/Web API path. Stock Chromium still contains and initializes V8 for its ordinary JavaScript realm; this experiment does not route Native TypeScript through it.

## Repository role

This repository owns the Chromium experiment and eventually the Chromium target runtime/capsules. Generic TypeScript semantics, ownership analysis, callback semantics, promise/microtask machinery, native handles, and Native IR belong in `native-typescript` / the ScriptC fork when the experiment proves that a reusable primitive is required.

The standalone CMake build validates the public C contract and target-neutral runtime pieces. The Blink adapter is compiled inside the pinned Chromium GN/Ninja build, where Blink implementation types are available.

See [`docs/architecture.md`](docs/architecture.md) for the normative design, [`docs/chromium-seams.md`](docs/chromium-seams.md) for pinned Chromium implementation evidence, [`docs/records/`](docs/records/) for dated findings, and [`include/nts_web.h`](include/nts_web.h) for the C ABI contract.
