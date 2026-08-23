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

## Binding architecture

Two descriptions deliberately have different jobs:

- TypeScript's `lib.dom.d.ts` defines what ordinary TypeScript source can say and how it is type-checked.
- Blink's normalized WebIDL database, from the exact pinned Chromium revision, defines the implementation-facing contract: WebIDL conversions, inheritance, dictionaries and unions, exposure/runtime conditions, implementation names, exception behavior, callbacks, promises and extended attributes.

The final generator is not an independent raw-WebIDL compiler. Native TypeScript becomes a sibling backend to Chromium's existing Blink bindings pipeline:

```text
Blink IDL
   |
Chromium web_idl compiler
   |
web_idl_database.pickle
   |
   +---- Blink bind_gen --> V8 bindings
   |
   +---- nts_bind_gen ---> Native Web schema
                           typed C ABI
                           direct Blink C++ capsules
                           coverage/refusal report
```

The deterministic Native Web schema is the contract between Chromium and ScriptC. ScriptC maps resolved `lib.dom.d.ts` symbols to schema operations, performs reachability and emits an application operation-selection manifest. Only selected capsules and their transitive type/callback dependencies are linked.

API-specific files such as the current handwritten `nts_blink_dom.cc` are executable generator prototypes. Realm, Oilpan handle identity, callback ingress, subscription ownership, promise scheduling and lifecycle remain permanent handwritten runtime machinery.

See [`docs/bindings-generation.md`](docs/bindings-generation.md) for the complete generator/runtime split and [`docs/records/0003-first-class-blink-bindings-backend.md`](docs/records/0003-first-class-blink-bindings-backend.md) for the decision record.

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

See [`docs/running.md`](docs/running.md) for the exact counter workflow.

The interactive call chain is represented as:

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

## Development direction

The completed Chromium target will be integrated into [`AkisArou/native-typescript`](https://github.com/AkisArou/native-typescript), which is the product repository and source of truth.

That repository already contains the Native TypeScript ScriptC fork at `third_party/scriptc` and its parent integration package at `packages/scriptc`. The intended integrated ownership is:

```text
native-typescript/
├── packages/bindgen-webidl/     # Native Web schema + nts_bind_gen
├── packages/target-chromium/    # pin, runtime, host, patches, artifact graph
├── packages/scriptc/            # parent integration/orchestration
└── third_party/scriptc/         # compiler/runtime submodule changes
```

A full Chromium checkout is a disposable pinned build dependency. This repository is used only as migration input and executable feasibility evidence; it should not remain a separately developed product implementation after parity is reached.

See [`docs/development-workflow.md`](docs/development-workflow.md) for the corrected single-repository integration workflow.

## Repository role

This repository owns the feasibility spike and its evidence until migration. Durable generator, target, runtime, product-host and conformance work moves into `native-typescript`; reusable compiler/runtime changes live in its checked-in ScriptC submodule and shared packages.

The standalone CMake build validates the current C contract and target-neutral runtime specimens. The Blink adapter is compiled inside the pinned Chromium GN/Ninja build, where Blink implementation types are available.

Documentation:

- [`docs/architecture.md`](docs/architecture.md) — normative final architecture;
- [`docs/bindings-generation.md`](docs/bindings-generation.md) — WebIDL/schema/generator design;
- [`docs/development-workflow.md`](docs/development-workflow.md) — Native TypeScript integration workflow;
- [`docs/chromium-seams.md`](docs/chromium-seams.md) — pinned Chromium implementation evidence;
- [`docs/records/`](docs/records/) — dated decisions and findings;
- [`include/nts_web.h`](include/nts_web.h) — current experimental C ABI.
