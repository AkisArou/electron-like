# Chromium seam register

Status: implementation evidence for the pinned Chromium revision  
Chromium: `96324a4012fe62f48b9463a67486eeb645bc5c78`

This file records concrete facts about one Chromium tree. The normative product and generator design is in [`architecture.md`](architecture.md) and [`bindings-generation.md`](bindings-generation.md).

## Rule

A blocked Web API is never implemented by entering a generated V8 wrapper, evaluating JavaScript, swallowing a Web-observable failure, or copying a standards algorithm into an application adapter.

The scalable correction is a **horizontal binding-neutral seam** shared by classes of APIs:

```text
Blink implementation
        |
binding-neutral exception / realm / promise / callback primitive
        |
        +------------------+
        |                  |
     V8 adapter      Native TypeScript adapter
```

A per-member overload or handwritten capsule is acceptable as a feasibility specimen, but it is not automatically the final pattern. The final backend generates member capsules and keeps Chromium patches concentrated in cross-cutting seams.

## Chromium WebIDL pipeline

The pinned tree already provides the semantic compiler that Native TypeScript should reuse:

```text
third_party/blink/renderer/bindings/scripts/web_idl/
third_party/blink/renderer/bindings/scripts/build_web_idl_database.py
third_party/blink/renderer/bindings/scripts/generate_bindings.py
third_party/blink/renderer/bindings/scripts/bind_gen/
```

`web_idl_database.pickle` is produced as a GN target and is consumed by Blink's V8 bindings generator. The Native TypeScript backend should consume the same database through a sibling `nts_bind_gen` package rather than parse raw IDL separately.

Relevant source:

- `third_party/blink/renderer/bindings/BUILD.gn`
- `third_party/blink/renderer/bindings/scripts/web_idl/README.md`
- `third_party/blink/renderer/bindings/scripts/bind_gen/README.md`

## `Document.body`

Blink exposes a direct implementation call:

```cpp
HTMLElement* Document::body() const;
```

It needs no V8 value, `ScriptState` or generated V8 wrapper. A generated native capsule resolves a `Document` handle, calls `body()`, represents null according to the generated ABI and interns a non-null result.

Current specimen:

- `chromium/bridge/nts_blink_dom.cc`

Blink source:

- `third_party/blink/renderer/core/dom/document.h`

## `Node.textContent`

Blink exposes:

```cpp
virtual void Node::setTextContent(const String&);
```

For ordinary elements in the counter fixture, the current capsule performs the narrow string conversion and calls this implementation directly.

The WebIDL-facing setter also participates in Trusted Types for relevant receivers, especially script text. The current bridge refuses the script-element shape instead of bypassing that policy. `nts_bind_gen` must classify the reached operation by the exact WebIDL/receiver semantics and select a binding-neutral Trusted Types path before claiming the complete setter surface.

Current specimen:

- `chromium/bridge/nts_blink_dom.cc`

Blink source:

- `third_party/blink/renderer/core/dom/node.h`

## `Node.appendChild`

Blink's complete Web-exposed path accepts `ExceptionState&` and performs pre-insertion validity, adoption/move behavior and observable DOM exceptions.

The current counter accepts only the proven no-failure shape:

```text
Element parent
+ detached Element child
+ valid child type
+ no ancestor cycle
```

It prevalidates that shape and calls Blink's direct append implementation. Other shapes return an explicit unsupported status rather than use `ASSERT_NO_EXCEPTION` where a DOM exception may be observable.

This is sufficient for the counter specimen, not complete `Node.appendChild` support. The scalable completion is the central pluggable exception sink described below, allowing the generated capsule to call the normal throwing Blink implementation without V8.

Current specimen:

- `chromium/bridge/nts_blink_dom.cc`

Blink sources:

- `third_party/blink/renderer/core/dom/node.cc`
- `third_party/blink/renderer/core/dom/container_node.cc`

## `EventTarget.addEventListener`

Blink has a native listener path:

```cpp
bool EventTarget::addEventListener(const AtomicString& event_type,
                                   EventListener* listener,
                                   bool use_capture = false);
```

`NativeEventListener` receives:

```cpp
void Invoke(ExecutionContext*, Event*) override;
```

This permits direct event delivery:

```text
Blink event dispatch
      |
BlinkNativeEventListener
      |
NtsWebCallbackToken
      |
ScriptC/native callback dispatcher
```

The same listener object is retained for deterministic `removeEventListener` identity. The realm-owned subscription registry cancels all registrations before callback storage disappears.

At the pinned revision, `EventTarget::AddEventListenerInternal` also consults an isolated-world V8 activity logger even for a native listener. `chromium/patches/0002-native-event-listener-without-v8-logger.patch` gates that diagnostic path to JavaScript-based listeners so the Native TypeScript registration path does not enter V8 merely for logging.

Current runtime/specimen:

- `chromium/bridge/nts_blink_event_listener.*`
- `chromium/bridge/nts_blink_subscription_registry.*`
- `chromium/bridge/nts_blink_events.cc`

Blink sources:

- `third_party/blink/renderer/core/dom/events/event_target.h`
- `third_party/blink/renderer/core/dom/events/event_target.cc`
- `third_party/blink/renderer/core/dom/events/native_event_listener.h`

## `Document.createElement`

The first binding-neutral exception cut is represented in the repository.

At the pinned revision the binding-facing operation is:

```cpp
Element* Document::CreateElementForBinding(const AtomicString& name,
                                           ExceptionState& exception_state);
```

Its body is Blink's actual one-argument DOM algorithm: name validation, HTML ASCII-lowercasing, custom-element routing, HTML element factory selection, unknown-element fallback and non-HTML document behavior.

The blocking dependency was `ExceptionState`, which stores a V8 isolate and materializes V8 exceptions. A null/testing state would erase the observable `InvalidCharacterError` path.

`chromium/patches/0001-binding-neutral-web-exception-state.patch` adds a V8-free `WebExceptionState` and a sibling overload. The existing V8 overload and the Native TypeScript capsule reach one DOM algorithm:

```text
V8 caller                        Native TypeScript caller
   |                                      |
ExceptionState                      typed C capsule
   |                                      |
   +---------- WebExceptionState ---------+
                      |
                      v
       one Document createElement body
```

The C fixture also requires invalid-name failure with `InvalidCharacterError` legacy code `5`, preventing a happy-path-only implementation.

### Scalable destination

The overload proves the semantic cut. The preferred final architecture is not one `WebExceptionState` overload per throwing member.

Instead, existing Blink implementation signatures should continue accepting an exception state whose throw operations delegate to a pluggable sink:

```text
ExceptionState
   /       \
V8 sink   Native exception collector
```

That horizontal refactor would unlock ordinary throwing `Node`, `Element`, `Document` and other WebIDL methods for generated capsules while leaving V8 behavior intact. V8-only actions such as rethrowing an arbitrary `v8::Value` remain explicitly unavailable to the native backend.

Current specimen/evidence:

- `chromium/patches/0001-binding-neutral-web-exception-state.patch`
- `chromium/bridge/nts_blink_dom.cc`
- `examples/create-element/app.c`
- `docs/records/0001-direct-create-element.md`

Blink sources:

- `third_party/blink/renderer/core/dom/document.h`
- `third_party/blink/renderer/core/dom/document.cc`
- `third_party/blink/renderer/platform/bindings/exception_state.h`

## Execution-context lifetime

The native realm follows `ExecutionContext` lifetime rather than renderer-process lifetime.

Blink's `ExecutionContextLifecycleObserver::ContextDestroyed()` is the deterministic shutdown hook. `NtsWebRealm` roots a GC-owned lifecycle observer that invalidates the off-heap realm on the owner sequence before the context disappears.

The current transition:

1. rejects later DOM operations;
2. cancels registered event subscriptions;
3. invalidates handle generations;
4. releases node/object Oilpan roots;
5. clears the rooted document;
6. leaves ScriptC runtime/promise shutdown to attach to this same transition.

No old handle can become valid in a later document.

Current runtime:

- `chromium/bridge/nts_blink_realm.*`

Blink source:

- `third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h`

## Oilpan roots and identity

The portable C handle table owns only slot, generation, reference count and generated type identity. It never owns a Blink raw pointer.

The current `BlinkNodeRegistry` stores one same-thread `Persistent<Node>` per distinct live native identity. Interning the same object returns the same slot and increments its native reference count. Realm invalidation destroys all roots and advances generations.

The final runtime must generalize this algorithm to all supported WebIDL interface objects, including non-Node objects such as events, responses and controllers. `nts_bind_gen` supplies type IDs and inheritance; the registry supplies identity/rooting/lifetime.

A long-lived untraced raw-pointer reverse map is forbidden. The current implementation scans rooted entries; a later GC-aware reverse map may replace the scan without changing observable handle identity.

Current runtime:

- `src/runtime/nts_handle_table.*`
- `chromium/bridge/nts_blink_node_registry.*`

Blink source:

- `third_party/blink/renderer/platform/heap/persistent.h`

## Type identity

The current registry has handwritten IDs for the first DOM inheritance chain. These are experiment data.

The final `nts_bind_gen` emits:

- canonical interface type IDs;
- inheritance/mixin upcast tables;
- callback signature IDs;
- dictionary/union/enum identities;
- schema digest and provenance.

A generated `HTMLButtonElement` handle can be consumed by reached operations expecting `HTMLElement`, `Element`, `Node` or `EventTarget` according to that table. A mismatch fails before a Blink object is dereferenced.

## Binding realm and `ScriptState`

Many direct DOM members need only an `ExecutionContext` or no script state. Other APIs are annotated or implemented with `ScriptState` because the V8 binding needs realm identity, promise creation or JavaScript values.

The scalable seam is a binding-neutral realm/context contract that exposes only the semantics required by the implementation:

```text
BindingRealm
  - ExecutionContext
  - realm identity
  - task runner
  - exception sink factory
  - promise resolver factory
  - microtask integration
```

V8 `ScriptState` and `NtsWebRealm` become sibling adapters. An operation that truly accepts or returns arbitrary JavaScript values stays unsupported until its semantics can be expressed in the native value algebra.

This seam is not implemented yet; the generator coverage report must name it when it blocks an operation.

## Promise seam

Blink methods returning `ScriptPromise<T>` are not made native by wrapping or polling a V8 Promise.

The desired horizontal shape is:

```text
Blink asynchronous implementation
              |
     WebPromiseResolver<T>
          /            \
     V8 adapter     ScriptC adapter
```

The ScriptC adapter settles a ScriptC promise on the realm owner sequence and participates in the declared Chromium/ScriptC microtask checkpoint.

Promise support requires ordering tests; eventual completion alone is insufficient.

## Build placement

The overlay is installed under:

```text
third_party/blink/renderer/native_typescript
```

Current targets include the portable C runtime, Blink C++ bridge, plain-C counter and counter host.

The final tree separates:

```text
native_typescript/
├── runtime/       # maintained handwritten code
├── generated/     # nts_bind_gen output
└── host/          # product/embedder integration
```

`nts_bind_gen` itself belongs beside Chromium's bindings scripts and consumes the existing `web_idl_database` GN output. Generated source lists enter GN through deterministic generated `.gni` or equivalent declared outputs.

`scripts/verify_chromium_patches.py` checks the patch series against the exact pin. `scripts/check_no_v8_bridge.py` rejects direct bridge source that introduces V8 values/state/wrappers or script evaluation.

## Product host seam

The current `content_shell` observer and `nts_blink_counter_host.*` are experiment harnesses. They prove the public-frame-to-internal-document startup path and deterministic teardown.

The final product host will instead:

- admit the intended document/origin;
- create `NtsWebRealm` and a ScriptC runtime instance;
- load or link the compiled application artifact;
- call its generated entry point;
- provide typed browser-process capabilities;
- destroy the application/runtime on execution-context shutdown.

No counter-specific concept remains.

## Generator consequences

The present bridge files divide into two categories.

Generated later:

- operation/getter/setter capsules in `nts_blink_dom.cc`;
- `addEventListener` overload/conversion capsule in `nts_blink_events.cc`;
- interface type IDs/inheritance;
- callback signature marshalling;
- selected GN source lists and coverage reports.

Permanent runtime categories:

- realm/lifecycle;
- generic interface-object registry;
- callback dispatcher/listener base;
- subscription registry;
- promise/scheduler integration;
- native exception adapter.

The handwritten counter path is retained as an executable oracle until generated replacements pass its tests.

## Direct interactive call chain

The represented counter path is:

```text
plain generated-style C
        |
        v
Document root / body / createElement capsules
        |
        v
checked realm and generated-type handles
        |
        v
real Blink Document / Node operations
        |
        v
Oilpan-rooted heading and button
        |
        v
native EventTarget listener registration
        |
        v
real browser click -> callback token
        |
        v
plain-C state update -> Node.textContent
        |
        v
Blink layout / paint
```

No V8 object, V8 function, V8 DOM wrapper, V8 promise, JavaScript property lookup, evaluated source or remote DOM call is a Native TypeScript bridge carrier in this chain.

## Evidence boundary

The repository distinguishes:

- source evidence: the direct call path and patches are represented;
- patch evidence: patches apply to the exact pin;
- compile evidence: the GN/Ninja targets compile in a real Chromium checkout;
- behavioral evidence: the rendered script-free page responds to real input and passes assertions.

Documentation must state which level has actually been observed for a given commit and environment.
