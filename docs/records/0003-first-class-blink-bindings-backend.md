# Record 0003 — first-class Blink bindings backend

Date: 2026-08-23  
Status: accepted target architecture

## Question

Should Native TypeScript continue growing a standalone WebIDL-to-C++ generator around the current handwritten bridge, or should it become a first-class backend of Chromium's existing Blink Web IDL compilation pipeline?

## Evidence from the pinned Chromium tree

The pinned Chromium revision already has two relevant layers:

1. `third_party/blink/renderer/bindings/scripts/web_idl/` compiles all Blink IDL into an immutable `web_idl.Database` serialized as `web_idl_database.pickle`.
2. `third_party/blink/renderer/bindings/scripts/bind_gen/` consumes that database and emits Blink-V8 C++ bindings.

The Web IDL compiler has already handled partial definitions, mixins, inheritance, overload grouping, extended-attribute propagation, runtime feature association and construction of normalized type objects. The V8 generator is built as a C++ `CodeNode` tree emitter, but its interface generator imports V8-specific conversion machinery from `blink_v8_bridge.py`.

The direct native counter also established that the Native TypeScript path needs two distinct categories of code:

- API-specific conversion/call capsules that can be generated;
- realm, Oilpan rooting, handle identity, callback-token, subscription and lifecycle machinery that is generic runtime infrastructure.

## Alternatives considered

### Generate from `lib.dom.d.ts`

Rejected as the implementation source.

`lib.dom.d.ts` is the correct TypeScript source contract, but it erases WebIDL distinctions and Blink implementation metadata. It cannot determine exact integer/string conversions, extended attributes, runtime exposure, implementation names, exception requirements or promise/callback mechanics.

It remains an input to ScriptC's source type checking and symbol resolution.

### Parse Blink IDL in `electron-like`

Rejected.

A second parser/compiler would need to reproduce Chromium's composition and normalization phases and would drift from the pinned browser implementation. It would also duplicate work that Chromium already exposes through `web_idl.Database`.

### Extend the existing V8 `bind_gen/interface.py` directly

Rejected as the primary code structure.

The existing generator is explicitly a Blink-V8 backend and imports V8 conversion/callback machinery throughout its interface generation. Adding conditionals for a second runtime would couple two fundamentally different value models and make both generators harder to reason about.

Target-neutral utilities may be reused or factored out, but the Native TypeScript backend should be a sibling package.

### Parse generated V8 C++ and replace V8 calls

Rejected.

Generated C++ is an output format, not a stable semantic model. Parsing it would be brittle, would retain accidental V8 structure and would make implementation mapping dependent on textual code-generation choices.

### Generate from Blink C++/Clang AST alone

Rejected as the semantic source.

The C++ AST can validate selected implementation signatures, but it does not encode the WebIDL source contract, conversion algorithms, exposure, dictionaries, unions, callback semantics or extended attributes. It is useful as compile-time validation after generation, not as a replacement for WebIDL.

### Handwrite wrappers for the supported subset

Rejected as the final architecture.

Handwritten vertical slices are valuable executable specifications and feasibility tests. They do not scale to the Web API surface and make Chromium revision drift a manual review problem for every member.

### Use a generic operation dispatcher

Rejected.

A runtime `invoke(name/id, dynamic values)` would discard static type information, weaken reachability and ownership analysis, add runtime dispatch, and recreate a dynamic bridge. Native TypeScript requires typed per-operation ABI entry points.

## Decision

Add `nts_bind_gen` as a sibling backend to Chromium's existing Web IDL compiler.

```text
Blink IDL
   |
web_idl compiler
   |
web_idl_database.pickle
   |
   +---- bind_gen ------> Blink-V8 bindings
   |
   +---- nts_bind_gen --> Native Web schema
                          typed C ABI
                          direct Blink capsules
                          coverage/refusal report
```

`nts_bind_gen` consumes the exact normalized database from the pinned Chromium build. It may reuse target-neutral `CodeNode`, naming and file-emission utilities, but does not import V8 value-conversion logic.

A deterministic Native Web schema becomes the contract between Chromium and ScriptC. ScriptC consumes that schema to map resolved `lib.dom.d.ts` symbols to operation identities, perform reachability and emit typed native calls. ScriptC does not consume Chromium's Python pickle directly.

The final build uses two levels of selection:

1. a revision-wide schema covering every member understood by the backend;
2. an application operation-selection manifest emitted by ScriptC.

Only selected capsules and their transitive type/callback/dictionary dependencies are linked into the application renderer.

The typed C ABI remains the compiler/backend seam. It is not replaced by direct generated C++ application code because the C ABI keeps ScriptC independent of Chromium's C++ ABI, permits both C and LLVM lowering, is easy to inspect/test, and can be optimized through LTO.

## Horizontal Blink changes

The project will prefer a small set of binding-neutral horizontal seams over per-member patches.

The target seams are:

- a pluggable exception sink behind `ExceptionState` or an equivalent shared exception carrier;
- a binding-neutral realm/context abstraction beneath operations that currently require `ScriptState`;
- a binding-neutral promise/completion resolver beneath `ScriptPromise`-returning implementations;
- generic native callback gateways;
- a generic Oilpan-backed interface-object registry.

The current `Document.createElement` `WebExceptionState` overload is retained as proof of the exception cut until the central exception carrier exists. It is not the desired pattern for thousands of methods.

## Runtime/generated boundary

Generated:

- operation and attribute capsules;
- exact WebIDL conversions;
- dictionaries, unions, enums and callback marshalling;
- type IDs and inheritance tables;
- operation identities and selection metadata;
- coverage/refusal data.

Handwritten runtime:

- `NtsWebRealm` and execution-context lifecycle;
- owner-sequence checks;
- Oilpan/native handle identity and rooting;
- callback-token storage and ScriptC ingress;
- subscription cancellation and teardown;
- promise settlement gateway and scheduler integration;
- browser-process capability transport;
- product renderer host.

Experiment-only:

- counter-specific host and application startup glue;
- handwritten API wrappers after equivalent generated capsules pass the same tests.

## Consequences

### Positive

- exact reuse of Chromium's WebIDL normalization;
- no duplicate IDL compiler;
- precise source of truth for Blink revision semantics;
- a stable schema boundary between Python/Chromium and ScriptC;
- generated per-member code without a dynamic bridge;
- application-level reachability and dead-code elimination;
- explicit coverage/refusal reports;
- smaller, more maintainable Chromium patch surface;
- compile-time detection of Blink implementation drift.

### Costs

- the generator must run inside or beside a full Chromium checkout;
- the build becomes a coordinated multi-repository pipeline;
- ScriptC needs a Native Web schema importer and operation IR;
- broad promise/`ScriptState` coverage still requires real Blink architectural work;
- Chromium revision updates require schema and coverage review.

These costs are inherent to direct Blink integration. The chosen design localizes them rather than hiding them in handwritten wrappers.

## Falsifiers

This decision should be revisited if evidence shows one of the following:

- Chromium's normalized WebIDL database omits essential implementation information that cannot be recovered from revisioned declarative policy or target-neutral call planning;
- a sibling backend cannot be integrated into GN without maintaining a patch surface comparable to a separate browser fork;
- the typed C ABI prevents required Web semantics or creates irreducible overhead after LTO;
- a binding-neutral exception/realm/promise seam proves materially more invasive than a different direct-native architecture;
- ScriptC cannot map resolved TypeScript DOM symbols to schema operations with precise diagnostics.

A difficult implementation is not itself a falsifier. The falsifier must show that another architecture preserves the same direct, V8-free Web API semantics with lower total complexity.

## Result

The existing direct counter remains the executable architecture specimen. The next implementation work is not another handwritten DOM member. It is the smallest `nts_bind_gen` slice that emits schema and generated capsules for the already proven `Document`/`Node`/`EventTarget` counter surface, plus the ScriptC schema consumer that selects those operations.
