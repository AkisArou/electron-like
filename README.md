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

A future WebIDL binding generator joins the reached `lib.dom.d.ts` members to the corresponding pinned Blink IDL definitions and emits a closed native binding manifest plus the C/C++ capsule surface. Application developers do not consume a replacement DOM declaration library.

## Current implementation

The repository is now pinned to Chromium `96324a4012fe62f48b9463a67486eeb645bc5c78`.

Implemented in the standalone native contract:

- typed C Web API ABI in `include/nts_web.h`;
- explicit owned lifetime for Web/DOM exception strings;
- generation-checked handle slots with retain/release, stale-handle rejection, subtype validation and realm invalidation;
- a plain-C counter fixture shaped like future ScriptC output, including unwind cleanup;
- CI build plus native handle-table tests.

Implemented as Blink-side C++ bridge code:

- `BlinkNodeRegistry`, which roots real Blink nodes with Oilpan `Persistent<Node>` while the C side sees only opaque handles;
- identity interning without a long-lived untraced raw-pointer reverse map;
- a `NativeEventListener` adapter that receives real Blink `Event*` objects and dispatches by Native TypeScript callback token, without a V8 listener wrapper.

The first required Chromium refactor is also pinned precisely: `Document::CreateElementForBinding` currently depends on V8-oriented `ExceptionState`. We will split its failure carrier from the DOM operation rather than call V8, ignore exceptions, call `CreateRawElement`, or duplicate the DOM create-element algorithm. See [`docs/chromium-seams.md`](docs/chromium-seams.md).

## Repository role

This repository owns the Chromium experiment and eventually the Chromium target runtime/capsules. Generic TypeScript semantics, ownership analysis, callback semantics, promise/microtask machinery, native handles, and Native IR belong in `native-typescript` / the ScriptC fork when the experiment proves that a reusable primitive is required.

The standalone CMake build validates the public C contract and target-neutral runtime pieces. The Blink adapter is compiled inside the pinned Chromium GN/Ninja build, where Blink implementation types are available.

See [`docs/architecture.md`](docs/architecture.md) for the normative design, [`docs/chromium-seams.md`](docs/chromium-seams.md) for pinned Chromium implementation evidence, and [`include/nts_web.h`](include/nts_web.h) for the C ABI contract.
