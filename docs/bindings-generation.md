# Native Web bindings generation

Status: normative target design

## Decision

Native TypeScript is a **first-class sibling bindings backend** in Blink's existing Web IDL compilation pipeline.

It does not:

- parse raw Blink IDL with a second parser;
- derive Blink behavior from `lib.dom.d.ts`;
- call generated V8 wrappers;
- parse generated V8 C++ as an implementation description;
- discover C++ methods dynamically at runtime;
- maintain a handwritten wrapper for every Web API member.

The pinned Chromium build already creates `web_idl_database.pickle` by parsing and normalizing all Blink IDL. Chromium's existing V8 bindings driver consumes that database and emits V8 bindings. The Native TypeScript generator consumes the **same normalized database** and emits a separate native schema and direct Blink capsules.

```text
Blink *.idl files
        |
        v
Chromium web_idl compiler
        |
        v
web_idl_database.pickle
        |
        +---------------------------+
        |                           |
        v                           v
Blink V8 bind_gen              nts_bind_gen
        |                           |
        v                           +-- Native Web schema
V8 bindings                        +-- typed C ABI
                                    +-- direct Blink C++ capsules
                                    +-- coverage/refusal report
```

This is smarter than an independent generator because Blink's compiler has already performed partial-interface composition, mixin inclusion, inheritance resolution, overload grouping, extended-attribute propagation, runtime feature association, and construction of immutable Web IDL objects. Native TypeScript should reuse that work rather than reproduce it.

The implementation should be a sibling package such as:

```text
third_party/blink/renderer/bindings/scripts/
├── web_idl/          # Chromium-owned semantic compiler
├── bind_gen/         # Chromium-owned Blink-V8 backend
└── nts_bind_gen/     # Native TypeScript backend
```

`nts_bind_gen` may reuse target-neutral utilities such as Chromium's `CodeNode` C++ emitter where useful. It must not depend on V8-specific conversion or callback code from `blink_v8_bridge.py`.

## The three contracts

The system deliberately separates three contracts that change at different rates.

### 1. TypeScript source contract

The application is checked against the TypeScript standard libraries, principally `lib.dom.d.ts`.

This contract owns:

- TypeScript-visible names;
- overloads as represented to TypeScript;
- structural source types;
- nullable/optional source syntax;
- source-level inheritance and generic declarations.

It does not own Blink conversion semantics or implementation mapping.

### 2. Native Web schema

The Native Web schema is generated from the exact pinned Chromium `web_idl_database` plus Native TypeScript binding policy.

It owns:

- canonical interface, namespace, dictionary, enum, callback, typedef and union identities;
- interface inheritance and mixin composition;
- attributes, operations, constructors and overload groups;
- exact Web IDL argument and result types;
- optional/default/nullable distinctions;
- exposure and runtime-feature conditions;
- implementation class and member names;
- required call context such as `ExecutionContext`, binding realm or exception state;
- callback and promise shapes;
- supported/refused status and the reason for refusal;
- generated ABI type and operation identities;
- provenance for the Chromium and generator revisions.

The schema is the stable cross-repository contract. ScriptC must not import Chromium's Python pickle format or Python classes directly.

### 3. Generated Blink capsule contract

The generated C/C++ binding capsule is compiled inside the pinned Chromium build.

It owns the exact mechanical call:

```text
typed C arguments
    -> realm/handle checks
    -> Web IDL conversion
    -> exact Blink C++ call
    -> result/exception conversion
    -> typed C result
```

It does not own TypeScript reachability, closure lifetime, whole-program escape analysis, language exception semantics or ScriptC heap policy.

## Revision-wide schema and application selection

Schema generation and application reachability are separate steps.

A Chromium revision produces one canonical Native Web schema for every definition that the backend understands. An application compile then selects the operations it actually reaches.

```text
pinned Chromium
      |
      v
revision-wide Native Web schema
      |
      +-----------------------------+
                                    |
TypeScript application              |
      |                             |
      v                             |
ScriptC type/reachability analysis  |
      |                             |
      +---- consumes schema --------+
      |
      v
application Web operation selection
      |
      +-- operation IDs
      +-- callback signatures
      +-- dictionary/union closure
      +-- required runtime features
      +-- compiled C/LLVM objects
```

The application selection is closed over dependencies. Selecting `Document.createElement` also selects all generated ABI types and conversions required by that overload. Selecting `EventTarget.addEventListener` selects the callback signature, options dictionary, event object projection and subscription runtime entry points it requires.

The final capsule generation/link step uses that selection so unreached API code is absent from the application artifact. A development build may generate a curated superset, but the product architecture remains selection-driven.

This division prevents a circular dependency:

1. Chromium builds `web_idl_database` and the Native Web schema.
2. ScriptC compiles the application against that schema and emits an operation-selection manifest plus native object files.
3. `nts_bind_gen` emits or selects the required C++ capsules.
4. GN/Ninja links the runtime, capsules and compiled application into the renderer product.

## Native Web schema format

The authoritative schema should have a deterministic machine format and a readable debug form.

Recommended outputs:

```text
native_web_schema.bin       # canonical deterministic compiler input
native_web_schema.json      # readable diagnostic form
native_web_schema.digest    # content digest and provenance
native_web_coverage.json    # support/refusal report
```

The schema header includes at least:

```text
schema format version
Chromium commit
web_idl_database digest
nts_bind_gen commit/version
Native ABI version
runtime contract version
TypeScript DOM compatibility range or validated digest
```

The format must be independent of Python pickle implementation details. Changing Chromium's Python object layout must not silently change ScriptC's input contract.

## Operation identity

Every generated operation receives a canonical identity derived from normalized semantic data, for example:

```text
interface: Document
member kind: operation
member: createElement
overload: (DOMString, optional ElementCreationOptionsOrString)
exposure: Window
```

The human-readable identity is retained for diagnostics. A fixed-width digest or generated integer is used in manifests and compiler IR.

Operation numbers are not assumed to be globally stable forever. Stability is scoped by the schema digest and Chromium revision. A compiled application and a capsule set with different schema digests must fail to link or fail a startup compatibility check; they must never be treated as compatible by accident.

Generated C symbols remain typed and inspectable, for example:

```c
NtsWebHandleResult nts_web_Document_createElement_1(
    NtsWebRealm *realm,
    NtsWebHandle receiver,
    NtsDomStringView local_name);
```

There is no runtime `invoke(operation_id, values)` fallback. Operation IDs identify compile-time artifacts and provenance; they do not turn the ABI into a dynamic command protocol.

## TypeScript-to-WebIDL symbol mapping

`lib.dom.d.ts` remains a source declaration library, not a Blink metadata source.

ScriptC's TypeScript frontend resolves an expression such as:

```ts
const button = document.createElement("button");
```

to a concrete TypeScript symbol and overload. The Native Web target maps that resolved source symbol to a schema operation identity.

The mapping uses:

- declaration-library identity;
- interface/member name;
- static versus instance member kind;
- resolved source overload;
- argument and return compatibility;
- inheritance/mixin ownership;
- target exposure.

The mapping must be validated. A TypeScript declaration that has no compatible member in the pinned Blink schema is a compile-time target error, not a generated stub. A Blink member missing from `lib.dom.d.ts` is not source-reachable through the ordinary DOM declaration library unless an explicit source declaration is provided.

The Chromium generator should not parse `lib.dom.d.ts` during C++ generation. A separate compatibility tool may compare a chosen TypeScript library revision with the Native Web schema and produce a report, but the boundary remains:

```text
TypeScript compiler: source symbols
Chromium Web IDL compiler: Web semantics
Native Web schema: stable join contract
```

## Generated outputs

For a selected application surface, `nts_bind_gen` emits logical artifacts such as:

```text
gen/native_typescript/
├── schema/
│   ├── native_web_schema.bin
│   ├── native_web_schema.json
│   └── native_web_coverage.json
├── abi/
│   ├── nts_web_types.h
│   ├── nts_web_Document.h
│   ├── nts_web_Node.h
│   └── nts_web_EventTarget.h
├── blink/
│   ├── nts_document_bindings.cc
│   ├── nts_node_bindings.cc
│   ├── nts_event_target_bindings.cc
│   ├── nts_callback_bindings.cc
│   ├── nts_dictionary_bindings.cc
│   └── nts_type_metadata.cc
└── build/
    └── generated_sources.gni
```

Physical sharding may be by interface, component, hash bucket or generated jumbo unit. The logical ownership above remains the same.

Generated type metadata includes:

- interface type IDs;
- inheritance/upcast tables;
- callback signature IDs;
- enum value tables;
- dictionary presence layouts;
- union tags;
- conversion descriptors;
- operation provenance.

## Runtime versus generated code

The generator emits API-specific mechanics. The runtime implements language- and object-lifetime mechanisms shared by all APIs.

| Concern | Owner |
| --- | --- |
| `Document.createElement` argument/result capsule | Generated |
| `Document.body` getter capsule | Generated |
| `Node.textContent` setter capsule | Generated |
| `EventTarget.addEventListener` overload conversion | Generated |
| Type IDs and inheritance graph | Generated |
| Dictionary/union/enum layouts | Generated |
| Per-callback-signature argument marshalling | Generated |
| Realm creation and destruction | Handwritten runtime |
| Renderer-sequence ownership | Handwritten runtime |
| Oilpan/native handle registry | Handwritten runtime |
| Handle generations and stale-handle checks | Handwritten runtime |
| Callback-token storage and ScriptC entry gateway | Handwritten runtime |
| Subscription ownership/cancellation | Handwritten runtime |
| Promise settlement gateway | Handwritten runtime |
| Task/microtask integration | Handwritten runtime |
| Browser-process capability transport | Handwritten/generated transport runtime |
| Product renderer host | Handwritten product integration |

The current `chromium/bridge/nts_blink_dom.cc` and most of `nts_blink_events.cc` are handwritten prototypes of future generated output. `nts_blink_realm.*`, the handle registry, callback gateway and subscription registry represent permanent runtime categories. The counter host is experiment-only product scaffolding.

The final runtime should generalize `BlinkNodeRegistry` into an interface-object registry capable of representing all supported Oilpan-backed WebIDL interface objects, not only `Node` subclasses. The generator supplies type descriptors; the registry supplies identity, rooting and lifetime.

## Call planning

Generating correct C++ requires more than matching a WebIDL member name to a C++ method name.

The generator constructs a `BlinkCallPlan` for every supported member. A plan contains at least:

```text
receiver implementation type
implementation member name
static/instance/constructor form
argument conversion sequence
optional/default argument materialization
CallWith/constructor context arguments
exception carrier
return conversion
runtime-feature and exposure guards
custom-element reaction requirements
ownership/result rooting policy
```

Blink's `code_generator_info` metadata and normalized extended attributes are inputs to the plan.

Long term, target-neutral call-planning logic should be factored so both Blink's V8 backend and `nts_bind_gen` can consume it. Until that is practical, `nts_bind_gen` owns an independently tested native call planner built on the same normalized database. It must not infer behavior by parsing generated V8 C++.

Every generated plan should be testable as data before C++ emission.

## Binding policy and overrides

Some Blink implementation seams are already binding-neutral. Some assume V8-specific carriers such as `ExceptionState`, `ScriptState` or `ScriptPromise`. The generator therefore consumes a small declarative Native TypeScript policy/override catalog.

Conceptual entries:

```json5
{
  "Document.createElement(DOMString)": {
    "support": "direct",
    "exception": "native_exception_sink"
  },
  "Window.fetch(RequestInfo, RequestInit?)": {
    "support": "requires_seam",
    "seam": "binding_neutral_promise"
  },
  "LegacyThing.customOperation()": {
    "support": "custom_capsule",
    "implementation": "NtsLegacyThingCustomOperation"
  }
}
```

Allowed classifications are finite:

- `direct`: generated call to an existing binding-neutral Blink entry;
- `requires_seam`: generated call is enabled only when a named horizontal Blink seam exists;
- `custom_capsule`: rare reviewed handwritten target adapter with an explicit reason;
- `unsupported`: precise build-time refusal.

There is no generic dynamic fallback.

Overrides are revision-controlled and included in the schema digest. They should become smaller as horizontal Blink seams improve.

## Horizontal Blink seams

The scalable solution is to neutralize a small number of cross-cutting binding primitives, not add a special Native TypeScript overload to thousands of APIs.

### Exceptions

The preferred final shape keeps existing Blink implementation signatures that accept `ExceptionState&`, but makes the state delegate to a pluggable exception sink:

```text
Blink DOM implementation
        |
        v
ExceptionState
   /          \
V8 sink      Native TypeScript sink
```

Existing V8 callers retain their behavior. Native TypeScript constructs an exception state backed by a native collector and can call the same implementation method. V8-only operations such as rethrowing an arbitrary `v8::Value` remain explicitly V8-only.

The current `WebExceptionState` overload for `Document.createElement` proves the semantic cut, but a pluggable central exception carrier is the scalable destination.

### Realm/context

APIs annotated with context requirements should depend on a binding-neutral realm/context contract where possible:

```text
BindingRealm
  - ExecutionContext
  - realm identity
  - task runner
  - microtask checkpoint
  - exception sink factory
  - promise resolver factory
```

V8 `ScriptState` and `NtsWebRealm` become sibling adapters. Blink APIs that genuinely require JavaScript values remain unsupported until their behavior is expressed in binding-neutral terms.

### Promises

Blink asynchronous implementations should settle a binding-neutral resolver/completion object. V8 and ScriptC adapters then expose their own promise objects:

```text
Blink async operation
        |
        v
WebPromiseResolver<T>
      /             \
V8 adapter       ScriptC adapter
```

Native TypeScript must not create an intermediate V8 Promise.

### Callbacks and events

Blink already has a native `EventListener` path. Generic listener ownership and cancellation stay in the runtime; generated callback stubs marshal each WebIDL callback signature into ScriptC values.

Other callback interfaces follow the same pattern: a generated Oilpan-aware native implementation delegates through a callback token to the realm owner sequence.

## Values and ABI forms

The schema maps WebIDL into a closed native algebra. Representative forms are:

```text
boolean                 -> bool
integer types           -> exact generated conversion policy
float/double            -> exact generated conversion policy
DOMString               -> UTF-16/code-unit-preserving native view
USVString               -> Unicode-scalar native view
ByteString              -> checked byte view
interface T             -> typed opaque handle<T>
T?                      -> nullable zero handle or explicit optional form
sequence<T>             -> typed span/owned array
FrozenArray<T>          -> immutable native array
record<K,V>             -> generated map projection
dictionary              -> generated record + presence bits
enum                    -> generated closed value
a or b                  -> generated tagged union
callback                -> typed ScriptC callback token
Promise<T>              -> ScriptC promise handle/resolver contract
ArrayBuffer/view        -> explicit buffer ownership/view contract
```

`NtsUtf8View` is acceptable experiment scaffolding, but the final `DOMString` ABI must preserve WebIDL/TypeScript string semantics rather than treating every DOM string as UTF-8 by definition. The generator chooses conversion forms from WebIDL, not from convenience.

## Generated capsule shape

A generated capsule remains simple and auditable:

```cpp
extern "C" NtsWebHandleResult nts_web_Document_createElement_1(
    NtsWebRealm* realm,
    NtsWebHandle receiver,
    NtsDomStringView local_name) {
  NTS_WEB_REQUIRE_REALM(realm);

  blink::Document* document =
      realm->Objects().Resolve<blink::Document>(receiver, kTypeDocument);
  if (!document)
    return realm->TakeFailure();

  blink::AtomicString blink_name =
      ConvertDOMString(local_name, realm->Conversions());

  NativeExceptionCollector collector;
  blink::ExceptionState exception_state(collector, kDocumentCreateElementContext);

  blink::Element* result =
      document->CreateElementForBinding(blink_name, exception_state);

  if (collector.HadException())
    return realm->ConvertException(collector);
  return realm->Objects().InternAs<blink::Element>(result, kTypeElement);
}
```

Exact names will follow Chromium style and the implemented central seams. The architectural requirements are typed entry, checked resolution, exact conversion, direct Blink call and explicit result translation.

## Coverage and refusal report

Every generator run emits a report for the selected surface.

For each reached member it records:

- supported/refused;
- selected overload;
- implementation call plan;
- required native seam;
- conversion forms;
- generated files and symbols;
- runtime-feature/exposure conditions;
- tests covering it.

A refusal names the missing semantic capability, for example:

```text
Window.fetch: requires binding-neutral promise resolver
Element.setHTMLUnsafe: requires Trusted Types/native union conversion
SomeInterface.callback: callback interface return promise not implemented
```

This is preferable to a partial wrapper that silently changes Web behavior.

## Testing

The backend requires tests at four levels.

### Schema tests

Given small IDL fixtures, assert normalized Native Web schema entries, operation identities, type graphs, defaults, unions, dictionaries and refusal reasons.

### Generator golden tests

Given schema fixtures and operation selections, compare generated C headers/C++ capsules and provenance files with reviewed golden output.

### Compile tests

Build generated capsules inside the pinned Chromium tree. Signature drift or missing implementation methods must fail compilation.

### Behavioral tests

Run the compiled application in the Chromium host and verify Web-observable behavior: DOM identity, exceptions, events, navigation invalidation, task/microtask order, promises, feature gates and shutdown.

Where applicable, compare behavior with an equivalent ordinary JavaScript page or relevant Web Platform Tests. The comparison tests semantics, not the use of V8 in the Native TypeScript path.

## Current prototype migration

The present bridge is retained as executable specification while the generator is introduced.

| Current path | Migration |
| --- | --- |
| `chromium/bridge/nts_blink_dom.cc` | Replace operation by operation with generated capsules |
| `chromium/bridge/nts_blink_events.cc` | Replace overload/conversion code with generated capsules |
| `chromium/bridge/nts_blink_event_listener.*` | Keep as generic callback runtime, then generalize |
| `chromium/bridge/nts_blink_node_registry.*` | Keep algorithm; generalize to all WebIDL interface objects; generate type metadata |
| `chromium/bridge/nts_blink_subscription_registry.*` | Keep as runtime ownership machinery |
| `chromium/bridge/nts_blink_realm.*` | Keep and integrate ScriptC runtime/scheduler/promises |
| `chromium/bridge/nts_blink_counter_host.*` | Remove when the real renderer application host exists |
| `chromium/bridge/BUILD.gn` | Keep target structure; replace handwritten source lists with generated `.gni` inputs |
| `chromium/patches/*` | Keep only horizontal Blink seams and product-host integration; avoid per-member patches |

Handwritten wrappers should disappear only after generated replacements have equal or stronger tests. The prototype is not deleted merely to make the tree look generated.

## Invariant

The final bindings pipeline is:

```text
ordinary TypeScript + lib.dom.d.ts
        |
        v
ScriptC frontend and reachability
        |
        | Native Web schema operation identities
        v
C/LLVM with typed native calls
        |
        v
generated extern "C" Blink capsules
        |
        v
binding-neutral Blink implementation seams
        |
        v
real Blink DOM/Web APIs
```

No V8 value, V8 wrapper, JavaScript source, generic command protocol or remote DOM proxy exists in that call path.
