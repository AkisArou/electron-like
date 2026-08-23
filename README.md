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

## Repository role

This repository owns the Chromium experiment and eventually the Chromium target runtime/capsules. Generic TypeScript semantics, ownership analysis, callback semantics, promise/microtask machinery, native handles, and Native IR belong in `native-typescript` / the ScriptC fork when the experiment proves that a reusable primitive is required.

The initial tree intentionally starts with the stable C contract and architecture before introducing a Chromium checkout. Code under the standalone build must not imply that stock Blink already exposes a stable C API; the Blink implementation is a C++ target adapter compiled inside a pinned Chromium build.

See [`docs/architecture.md`](docs/architecture.md) for the normative design and [`include/nts_web.h`](include/nts_web.h) for the first C ABI contract.
