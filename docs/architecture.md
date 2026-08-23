# Direct Blink architecture

Status: normative target architecture

## Mission

Run ordinary TypeScript compiled ahead of time to native code inside a Chromium renderer and expose the real Blink DOM/Web API surface without routing Native TypeScript calls through V8, JavaScript, a remote DOM proxy or a generic command bridge.

The product is not Electron with the JavaScript runtime removed. It is a Chromium content embedder in which Native TypeScript is a first-class Blink binding runtime.

## Core invariants

1. Native TypeScript Web API calls execute in the sandboxed renderer process on the sequence that owns the Blink execution context.
2. Synchronous Web APIs remain local synchronous calls; they do not cross browser/renderer IPC.
3. Generated application code never observes a Blink C++ pointer.
4. V8 values, V8 DOM wrappers, JavaScript functions and evaluated source are not carriers in the Native TypeScript binding path.
5. TypeScript source uses the standard DOM declaration libraries; no framework-specific DOM dialect is required.
6. Blink's normalized WebIDL database from the exact Chromium revision is the implementation-semantic source of truth.
7. Web API entry points are typed generated C symbols. There is no `invoke(name, values)` fallback.
8. Realm, handle, callback, promise and shutdown behavior are explicit runtime contracts.
9. Unsupported reached semantics fail the build or operation precisely; they are never silently approximated.
10. Browser-process authority is available only through finite typed asynchronous capabilities.

## Final system shape

```text
ordinary TypeScript + lib.dom.d.ts
                |
                v
       ScriptC frontend/checker
                |
     reachability + Native Web IR
                |
                | operation identities from Native Web schema
                v
          C or LLVM lowering
                |
                v
       generated native application
                |
          typed extern "C" ABI
                |
                v
 generated Native TypeScript Blink capsules
                |
     binding-neutral Blink implementation seams
                |
                v
       real Blink DOM / Web APIs
                |
                v
 Chromium layout / paint / compositor / GPU
```

The process boundary remains Chromium's:

```text
┌─────────────────────────────────────────────┐
│ trusted browser process                     │
│                                             │
│ windowing, navigation, permissions,         │
│ application lifecycle, privileged services  │
└───────────────────┬─────────────────────────┘
                    │ typed asynchronous Mojo/capability calls
┌───────────────────▼─────────────────────────┐
│ sandboxed renderer process                  │
│                                             │
│ Blink + NtsWebRealm + ScriptC runtime       │
│ + compiled application + generated capsules │
└─────────────────────────────────────────────┘
```

DOM handles and raw pointers never cross that line.

## Semantic authorities

Three different descriptions have different jobs.

### TypeScript standard libraries

`lib.dom.d.ts` and related TypeScript libraries define what application source can say and how the TypeScript checker resolves names, overloads and source types.

They are the source contract, not the Blink ABI.

### Blink WebIDL database

Chromium's `web_idl` compiler parses and normalizes Blink's IDL into `web_idl_database.pickle`. That database includes interface composition, inheritance, overloads, dictionaries, unions, callbacks, extended attributes, exposure and implementation metadata.

It defines the exact semantic input for the pinned Chromium backend.

### Native Web schema

A Native TypeScript bindings backend, `nts_bind_gen`, consumes Chromium's normalized database and emits a deterministic Native Web schema.

The schema is the contract between Chromium and ScriptC. It records:

- canonical type/member identities;
- WebIDL conversions and defaults;
- implementation call plans;
- exposure/runtime conditions;
- exception, callback and promise requirements;
- generated ABI identities;
- supported/refused status;
- Chromium/generator/runtime provenance.

ScriptC consumes this schema. It does not import Chromium's Python pickle directly.

## First-class Blink bindings backend

Native TypeScript is implemented as a sibling backend to Blink's existing V8 generator:

```text
Blink *.idl
    |
    v
Chromium web_idl compiler
    |
    v
web_idl_database.pickle
    |
    +-----------------------------+
    |                             |
    v                             v
Blink bind_gen                 nts_bind_gen
    |                             |
    v                             +-- Native Web schema
V8 bindings                      +-- C ABI declarations
                                 +-- direct Blink C++ capsules
                                 +-- type/callback metadata
                                 +-- coverage/refusal report
```

The project does not maintain a second raw-IDL compiler. `nts_bind_gen` may reuse target-neutral Chromium code-emission utilities, but it remains separate from V8-specific conversion logic.

Detailed generation architecture is specified in [`bindings-generation.md`](bindings-generation.md).

## Revision-wide schema and application selection

A Chromium revision first produces a revision-wide schema. ScriptC then emits an application-specific operation selection based on actual reachability.

```text
pinned Chromium -> Native Web schema
                         |
TypeScript app ----------+
        |
        v
ScriptC reachability/type resolution
        |
        +-- compiled C/LLVM object
        +-- app.webops selection manifest
                         |
                         v
selected generated Blink capsules
                         |
                         v
final Chromium renderer product
```

The operation selection is closed over required dictionaries, unions, callbacks, return types and runtime features. Unreached bindings are not required in the final application artifact.

A development build may use a curated superset, but source/member resolution remains static and typed.

## Native Web realm

`NtsWebRealm` binds exactly one ScriptC runtime instance to one Blink execution realm.

```text
Blink ExecutionContext
        ↕
    NtsWebRealm
        ↕
ScriptC RuntimeInstance
```

The realm owns or references:

- the current Blink `ExecutionContext` and `Document` where applicable;
- the renderer owner sequence/task runner;
- the ScriptC runtime instance;
- the generic Blink interface-object registry;
- callback tokens and ingress gateway;
- event/subscription resources;
- native promise settlement integration;
- task/microtask checkpoint integration;
- exception conversion state;
- realm identity, feature state and lifecycle.

`NtsWebRealm` is permanent runtime infrastructure. It is not generated per WebIDL interface.

## Runtime ownership

Only the realm owner sequence may:

- enter compiled TypeScript;
- access ScriptC heap objects;
- resolve or mutate Blink handles;
- call thread-affine Blink implementations;
- dispatch native callbacks;
- settle ScriptC promises;
- alter subscription state;
- release Oilpan roots;
- destroy the runtime/realm.

Wrong-sequence access is a checked failure. A synchronous DOM API never silently posts to another sequence.

Chromium remains the host scheduler. ScriptC does not run a competing browser event loop.

## Realm lifetime

A window/document Native TypeScript runtime follows Blink execution-context lifetime, not renderer-process lifetime.

```text
ExecutionContext created
        |
        v
NtsWebRealm + ScriptC runtime created
        |
        v
compiled application entry
        |
        v
DOM / events / tasks / promises
        |
navigation, frame detach or context shutdown
        |
        v
stop new admissions
cancel native subscriptions
reject/cancel permitted pending async work
invalidate all handle generations
release Oilpan roots on owner sequence
stop ScriptC runtime
        |
        v
realm destroyed
```

Workers use the same ownership model with their own execution contexts and runtime instances.

## Native object model

Generated C sees opaque handles:

```c
typedef struct {
  uint32_t slot;
  uint32_t generation;
} NtsWebHandle;
```

A handle is scoped to one realm and has a generated WebIDL type identity.

The final registry is a generic Blink interface-object registry, not a Node-only registry. It associates live native handles with Oilpan-rooted Blink interface objects and generated type descriptors.

It preserves:

- realm affinity;
- generation checking;
- exact type validation and generated WebIDL upcasts;
- identity interning;
- deterministic invalidation;
- retain/release accounting for ScriptC-visible references;
- release of Oilpan roots when the final native edge disappears.

Blink/Oilpan owns object memory. The Native TypeScript registry owns only the strong GC edges required by live native handles.

Repeated acquisition of the same Blink object returns the same logical native identity. Navigation cannot make an old handle refer to an object in a new realm.

## Typed C ABI

The generated C ABI is the compiler/backend seam.

It permits ScriptC's readable C backend and LLVM backend to target identical semantics while remaining independent of Chromium's C++ ABI.

Representative generated calls:

```c
NtsWebHandleResult nts_web_Document_createElement_1(
    NtsWebRealm *realm,
    NtsWebHandle receiver,
    NtsDomStringView local_name);

NtsWebVoidResult nts_web_Node_appendChild_1(
    NtsWebRealm *realm,
    NtsWebHandle receiver,
    NtsWebHandle child);
```

The exact symbol names and ABI records are generated from the Native Web schema.

There is no runtime equivalent of:

```text
invoke(handle, "createElement", dynamic_args)
get(handle, "body")
eval(source)
```

Member and overload resolution occur before code generation. LTO may inline the C/C++ capsule boundary without weakening it as an architectural contract.

## Generated Blink capsules

A generated capsule may:

- validate realm/sequence state;
- resolve typed native handles;
- materialize exact WebIDL argument conversions;
- apply exposure/runtime-feature checks;
- construct binding-neutral context/exception/promise carriers;
- call the exact Blink implementation entry;
- intern returned interface objects;
- translate values or failures into the typed native ABI.

It may not decide:

- TypeScript whole-program reachability;
- closure escape/lifetime policy;
- ScriptC heap ownership;
- application process partitioning;
- browser authority policy;
- generic runtime scheduling semantics.

Those belong to ScriptC, Native IR, the realm runtime or the product host.

## WebIDL/native values

WebIDL maps into a closed Native IR/ABI algebra.

```text
boolean                    -> bool
integer WebIDL types       -> exact generated numeric conversion
float/double               -> exact generated numeric conversion
DOMString                  -> code-unit-preserving string form
USVString                  -> Unicode-scalar string form
ByteString                 -> checked byte string
interface T                -> typed handle<T>
T?                         -> nullable handle/optional form
sequence<T>                -> typed span or owned array
dictionary                 -> generated record + presence bits
enum                       -> generated closed value
union                      -> generated tagged union
callback                   -> typed ScriptC callback token
Promise<T>                 -> ScriptC promise/resolver contract
ArrayBuffer/typed arrays   -> explicit buffer ownership/view forms
```

The current UTF-8 experiment ABI is scaffolding. The final generator must preserve each WebIDL string type's actual semantics.

The compiler may eliminate a conversion check only when static analysis proves it cannot fail.

## Horizontal Blink seams

Scalability depends on neutralizing a small number of binding primitives rather than adding Native TypeScript overloads to every API.

### Exception sink

The preferred final design keeps Blink implementation methods accepting a common exception-state object while making that object delegate to a pluggable sink:

```text
Blink implementation
       |
ExceptionState
   /       \
V8 sink   NTS sink
```

Existing V8 behavior remains. The Native TypeScript sink records DOMException, TypeError, RangeError, SyntaxError and security-safe messages without creating V8 values.

The current binding-neutral `Document.createElement` overload proves the cut but is not the desired per-member pattern.

### Binding realm/context

Operations that currently require `ScriptState` should, where their semantics permit, depend on a binding-neutral realm/context abstraction that supplies:

- `ExecutionContext`;
- realm identity;
- owner task runner;
- microtask integration;
- exception sink creation;
- promise resolver creation.

V8 `ScriptState` and `NtsWebRealm` become sibling adapters.

An API that genuinely consumes or produces arbitrary JavaScript values remains unsupported until its semantics are represented in the native type algebra.

### Promise resolver

Async Blink implementations should settle a binding-neutral resolver/completion object:

```text
Blink async completion
        |
WebPromiseResolver<T>
     /              \
V8 adapter       ScriptC adapter
```

Native TypeScript does not construct an intermediate V8 Promise.

### Native callbacks

Blink's native `EventListener` path is used directly. The runtime owns callback-token lifetime and subscription cancellation; generated callback stubs marshal each WebIDL callback signature.

Other callback interfaces use generated Oilpan-aware native implementations that enter ScriptC through the same realm gateway.

## Exceptions

No C++ exception unwinds through generated C or ScriptC frames.

A binding-neutral failure is translated into a typed native result and then into ScriptC's language exception model.

The observable distinctions required by the reached API are retained, including:

- DOMException name, legacy code and safe message;
- TypeError;
- RangeError;
- SyntaxError;
- security/permission failure;
- operation disabled by exposure/runtime feature;
- explicit target/runtime internal failure.

## Events and callbacks

For:

```ts
button.addEventListener("click", callback);
```

the architecture is:

```text
compiled closure
      |
typed ScriptC callback token
      |
generated EventListener argument capsule
      |
generic native Blink EventListener adapter
      |
blink::EventTarget
```

On dispatch, Blink invokes the native listener on the realm sequence. The generated callback stub converts the reached event signature into handles/native values and enters compiled TypeScript.

A subscription is an owned native resource. Removal unregisters the exact listener identity before releasing the callback token. Realm shutdown cancels all remaining subscriptions before callback storage is destroyed.

## Promises and task ordering

`Promise<T>` maps directly to ScriptC's promise model through the binding-neutral resolver.

Chromium owns task scheduling for input, timers, animation, networking and rendering. ScriptC jobs participate in a declared realm microtask checkpoint.

The implementation must test Web-observable ordering across:

- current native application turn;
- Blink task completion;
- native callback/promise ingress;
- ScriptC promise settlement;
- ScriptC microtasks;
- subsequent Chromium tasks and rendering updates.

"Eventually resolved" is insufficient evidence.

## Browser-process capabilities

Privileged application features remain in the trusted browser process unless Chromium already implements them safely in the renderer.

Examples include:

- filesystem authority;
- process execution;
- application windows;
- native menus and dialogs;
- unrestricted OS integration;
- application update/install services.

The renderer calls finite typed asynchronous capabilities over Mojo or an equivalent generated transport. DOM objects, Blink pointers and ScriptC closures never cross the process boundary.

## Generated, runtime and host source ownership

Final Chromium-side source is divided into three categories:

```text
third_party/blink/renderer/native_typescript/
├── runtime/       # handwritten permanent mechanisms
├── generated/     # nts_bind_gen outputs
└── host/          # product/embedder integration
```

### Permanent runtime

- realm/lifecycle;
- generic interface-object registry;
- callback gateway;
- subscription registry;
- promise/microtask integration;
- exception adapter;
- conversion primitives;
- runtime compatibility checks.

### Generated

- per-member C ABI declarations/definitions;
- direct Blink call capsules;
- type IDs and inheritance;
- dictionaries, unions and enums;
- callback signature stubs;
- operation selection/build source lists;
- coverage/refusal/provenance data.

### Product host

- renderer-frame/application startup;
- compiled application loading/linking;
- browser-process capabilities;
- window/application lifecycle;
- origin and admission policy.

The current counter-specific host is temporary experiment scaffolding.

## Current prototype mapping

The present source is an executable specification of the target boundary.

| Current source | Target disposition |
| --- | --- |
| `nts_blink_dom.cc` | replaced operation by operation by generated capsules |
| `nts_blink_events.cc` | generated overload/conversion capsules plus permanent generic event runtime |
| `nts_blink_event_listener.*` | permanent generic runtime category |
| `nts_blink_node_registry.*` | generalized into permanent interface-object registry; type metadata generated |
| `nts_blink_subscription_registry.*` | permanent runtime category |
| `nts_blink_realm.*` | permanent runtime category |
| `nts_blink_counter_host.*` | removed when the real renderer application host exists |
| `chromium/patches/*` | reduced toward horizontal binding-neutral seams and product integration |

Handwritten operation wrappers remain until generated replacements pass equal or stronger tests.

## Build architecture

The reproducible build is:

```text
pinned Chromium source
        |
        +-- web_idl_database
        |       |
        |       +-- nts_bind_gen schema
        |                       |
TypeScript + lib.dom.d.ts       |
        |                       |
        +-- ScriptC ------------+
              |        |
              |        +-- app.webops selection
              +----------- C/LLVM object
                           |
nts_bind_gen selected capsules
                           |
handwritten NTS Blink runtime
                           |
Chromium GN/Ninja graph
                           |
                           v
browser process + sandboxed renderer product
```

Generated artifacts include the Chromium commit, WebIDL database digest, schema format, generator version, runtime contract and ScriptC ABI version in their identity.

The recommended multi-repository implementation workflow is documented in [`development-workflow.md`](development-workflow.md).

## Compatibility and conformance

A compatibility claim is tied to:

- an exact Chromium revision;
- an exact Native Web schema digest;
- a compatible ScriptC/runtime ABI version;
- the TypeScript standard library revision used for source checking;
- an application operation-selection manifest;
- a coverage/refusal report;
- compile and behavioral test evidence.

Required test categories include:

- schema and generator golden tests;
- generated capsule compile tests;
- DOM identity and invalidation;
- DOMException/type/range error translation;
- event registration, delivery and cancellation;
- navigation/shutdown cleanup;
- promise/task/microtask ordering;
- feature/exposure guards;
- relevant Web Platform Tests or equivalent semantic comparisons.

Source-level resemblance to the DOM API is not compatibility evidence.

## Non-goals

The architecture does not:

- emulate DOM calls in the browser process;
- expose Blink pointers to generated application code;
- use V8 as a hidden value/callback/promise bridge;
- evaluate application JavaScript;
- parse generated V8 C++ to discover Blink calls;
- maintain a second raw WebIDL compiler;
- derive Blink semantics from `lib.dom.d.ts` alone;
- use a dynamic reflection/command registry for statically resolved members;
- generate realm, handle, subscription and scheduler algorithms per interface;
- claim support for APIs whose binding-neutral semantics have not been implemented.
