# Direct Blink architecture

Status: normative starting architecture

## Mission

Run ordinary TypeScript compiled ahead of time to native code directly inside a Chromium renderer and expose the real Blink DOM/Web API surface without routing Native TypeScript calls through V8 or JavaScript.

The renderer is not an Electron renderer with JavaScript removed. It is a native Blink host with a Native TypeScript runtime as a first-class binding runtime.

## System shape

```text
ordinary TypeScript + lib.dom.d.ts
              |
              v
      ScriptC / Native IR
              |
         C or LLVM
              |
              v
       generated native C
              |
       typed extern "C"
              |
              v
 generated Blink C++ capsule
              |
              v
             Blink
              |
              v
 Chromium renderer/compositor/GPU
```

Browser-process services remain across Chromium's process boundary:

```text
trusted browser process
        |
        | typed asynchronous capabilities / Mojo transport
        v
sandboxed renderer process
  Blink + NtsWebRealm + ScriptC runtime + compiled application
```

Synchronous DOM operations never cross that process boundary.

## Source API ownership

TypeScript's standard DOM libraries are the public source contract. Application source uses `document`, `Window`, `Element`, `EventTarget`, `fetch`, and related standard declarations directly.

No Native-TypeScript-specific DOM declaration package is introduced merely to make the target work.

`lib.dom.d.ts` does not define the native Blink ABI. It describes the TypeScript-visible shape, while Blink's WebIDL database from the exact pinned Chromium revision carries implementation-facing semantics that the TypeScript declarations intentionally erase.

The binding pipeline therefore joins two inputs:

```text
lib.dom.d.ts ----------------------- source API identity/types
       \
        +--> reached-member join --> Native Web Binding Manifest
       /
Blink WebIDL database ------------- exact Blink/WebIDL semantics
```

The manifest is closed, immutable build input. It contains only reached operations and all semantic facts required to lower them.

## WebIDL role

The Chromium target consumes Chromium's own generated WebIDL database rather than treating WebIDL as a user-facing language.

The binding generator extracts, as applicable:

- exact interface and member identity;
- overload and optional-argument structure;
- WebIDL scalar conversion policy;
- `DOMString`, `USVString`, `ByteString`, enum, dictionary, union and sequence semantics;
- nullable/optional distinctions;
- interface inheritance and exposure sets;
- implementation method/name overrides;
- execution-context requirements;
- exception behavior;
- callback/event shapes;
- promise/async result behavior;
- custom-element reactions and other reached extended attributes;
- runtime/feature gating.

Unsupported reached semantics fail the build precisely. There is no dynamic fallback.

## Binding-neutral Blink realm

Native TypeScript must not fabricate a V8 `ScriptState` or enter V8 wrappers to satisfy Blink APIs. Where current Blink implementation seams assume V8 state, the Chromium integration introduces a binding-neutral realm contract beneath those assumptions.

Conceptually:

```text
                   BlinkBindingRealm
                    /            \
            V8 binding realm    NtsWebRealm
```

`NtsWebRealm` binds exactly one Native TypeScript runtime instance to exactly one Blink execution realm.

It owns or references:

- the Blink `ExecutionContext`;
- the owning renderer sequence/task runner;
- the ScriptC runtime instance;
- the DOM/native handle registry;
- callback registrations;
- native promise resolver integration;
- exception conversion state;
- realm identity and lifecycle state.

The invariant is:

> Native TypeScript calls Blink directly. V8 is neither the value carrier nor the callback/promise/exception carrier for this realm.

## Runtime ownership

One `NtsWebRealm` has one ScriptC owner executor: the Chromium/Blink renderer sequence that owns its execution context.

Only that sequence may:

- enter compiled TypeScript;
- access ScriptC heap objects;
- resolve or mutate DOM handles;
- call thread-affine Blink APIs;
- deliver DOM event callbacks;
- settle ScriptC promises from Blink completions;
- release realm-owned Oilpan roots;
- destroy the realm.

Wrong-sequence access is a checked failure, never an implicit scheduler hop for synchronous DOM APIs.

## Realm lifetime

A window/document runtime is tied to Blink execution-context lifetime, not renderer-process lifetime.

```text
ExecutionContext created
        |
        v
NtsWebRealm created
        |
        v
compiled application entry
        |
        v
DOM/events/promises/tasks
        |
 navigation / detach / context destruction
        |
        v
stop admissions
cancel subscriptions
settle/reject permitted outstanding async work
invalidate every handle generation
release Oilpan roots on owner sequence
stop ScriptC runtime
        |
        v
NtsWebRealm destroyed
```

Workers use the same model with their own execution contexts and runtime instances.

## Native object model

Blink pointers never appear in generated C.

Generated code observes opaque handles:

```c
typedef struct {
  uint32_t slot;
  uint32_t generation;
} NtsWebHandle;
```

A handle is scoped to one realm and has a static WebIDL/native type identity in the binding contract.

The Blink-side registry maps handle entries to GC-aware Blink references and maintains reverse identity interning so repeated acquisition of the same Blink object yields one logical native identity.

The registry must preserve:

- realm affinity;
- generation checking;
- type checking/upcasts according to WebIDL inheritance;
- deterministic invalidation on context destruction;
- alias identity;
- exact retain/release accounting for ScriptC-visible native references.

Blink/Oilpan owns object memory. The realm registry owns only the strong Oilpan edges required by live Native TypeScript handles.

## C ABI

The C ABI is generated and statically identified. It exists so both ScriptC's readable C backend and LLVM backend can target the same Chromium capsule semantics.

Representative calls:

```c
NtsWebHandleResult nts_web_document_create_element(
    NtsWebRealm *realm,
    NtsWebHandle document,
    NtsUtf8View local_name);

NtsWebVoidResult nts_web_node_append_child(
    NtsWebRealm *realm,
    NtsWebHandle parent,
    NtsWebHandle child);
```

There is no generic equivalent of:

```text
invoke(handle, "createElement", args)
get(handle, "body")
eval(source)
```

Member resolution occurs before code generation.

## Generated C++ capsules

The implementation of the C ABI is generated C++ compiled inside the pinned Chromium build.

Its allowed responsibility is exact target realization:

- resolve checked native handles;
- perform the WebIDL conversion already selected by the manifest;
- call the exact Blink implementation entry;
- materialize a returned Blink object as a checked native handle;
- translate the target's binding-neutral exception/result state into the native ABI.

The capsule does not decide whole-program lifetime, escape, scheduling policy, or language semantics that belong to ScriptC/Native IR.

## Values and conversions

The WebIDL binding family maps WebIDL concepts into a closed Native IR/native ABI algebra.

Examples:

```text
boolean                    -> bool
integer WebIDL types       -> declared checked/exact numeric conversion
float/double               -> declared number conversion
DOMString                  -> code-unit-preserving string projection
USVString                  -> Unicode-scalar projection
ByteString                 -> checked byte-string projection
interface T                -> handle<T>
T?                         -> nullable handle<T>
sequence<T>                -> native array/span projection
dictionary                 -> generated record + presence bits
enum                       -> closed enum/string projection
union                      -> generated tagged union
callback                   -> ScriptC callback token
Promise<T>                 -> ScriptC Promise<T>
ArrayBuffer/typed arrays   -> explicit native buffer/view forms
```

The compiler may remove checks only when its static analysis proves the declared conversion cannot fail.

## Events and callbacks

DOM events are native callback registrations, not JavaScript listener functions.

Conceptually:

```text
compiled closure
      |
ScriptC callback token
      |
Nts native EventListener adapter
      |
blink::EventTarget
```

On dispatch, Blink invokes the native listener on the realm owner sequence. Event/interface payloads are represented as native handles or transport-safe values according to their generated contract, and the callback enters compiled TypeScript directly.

A registration has explicit ownership and cancellation. Removing a listener tears down the Blink registration before releasing the callback token. Realm destruction cancels all remaining registrations before callback storage is reclaimed.

## Exceptions

Native TypeScript does not create a V8 exception object and translate it back.

Blink implementation entry points used by the Native TypeScript binding must expose a binding-neutral failure sink/result capable of representing the Web-observable distinction required by the reached API, including as applicable:

- `TypeError`;
- `RangeError`;
- `DOMException` name/code/message;
- operation-disabled/security failures;
- internal target failure mapped to an explicit runtime error.

The generated capsule translates that directly into ScriptC exception semantics.

No foreign C++ exception unwinds through generated C/ScriptC frames.

## Promises and asynchronous Web APIs

`Promise<T>` in WebIDL maps to ScriptC's promise model directly.

The implementation seam beneath a reached Blink async API must be binding-neutral: a native completion/resolver settles the ScriptC promise on the owning realm sequence without creating an intermediate V8 Promise.

The Chromium renderer scheduler remains the host scheduler. ScriptC does not own a competing browser event loop.

Task/microtask integration must preserve Web-observable ordering. Native TypeScript promise jobs participate in the realm's browser microtask checkpoint contract, while task sources such as timers, network completion, animation, and input remain Chromium/Blink task sources.

## Browser-process capabilities

Privileged operations remain outside the sandboxed renderer unless Chromium's architecture already implements them safely there.

A renderer reaches trusted host services through finite typed capabilities, transported over Chromium IPC/Mojo or an equivalent generated transport.

Cross-process operations are asynchronous. Raw pointers, Blink objects, ScriptC closures and arbitrary object graphs never cross the boundary.

## Build architecture

The final build consumes an exact Chromium revision.

Conceptually:

```text
Chromium source + generated WebIDL database
                 |
                 +-- Native Web binding generator
                 |      |
                 |      +-- manifest
                 |      +-- C header
                 |      +-- Blink C++ capsules
                 |
TypeScript + lib.dom.d.ts
                 |
          ScriptC compiler
                 |
           C/LLVM objects
                 |
                 +----------------+
                                  v
                       Chromium GN/Ninja graph
                                  |
                                  v
 browser process + sandboxed renderer product
```

Generated adapters are declared build artifacts with the exact Chromium revision, WebIDL database digest, source declaration digest and compiler contract version in their cache/provenance identity.

## Compatibility

DOM compatibility claims are tied to:

- an exact Chromium revision/version;
- the matching TypeScript DOM declaration revision used at compile time;
- a recorded binding coverage report;
- browser/Web Platform conformance tests for the supported reached surface.

Source-level resemblance is not sufficient evidence of compatibility.

## Non-goals

The architecture does not:

- emulate DOM calls in another process;
- expose Blink C++ pointers to application code;
- use V8 as a hidden bridge;
- use JavaScript glue to implement normal Web API access;
- maintain a string-based reflection registry for statically reached members;
- treat `lib.dom.d.ts` as sufficient evidence of Blink implementation semantics;
- fork TypeScript's DOM declarations merely to express native binding metadata.
