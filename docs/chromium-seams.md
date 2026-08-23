# Chromium seam register

Status: implementation evidence for the pinned Chromium revision  
Chromium: `96324a4012fe62f48b9463a67486eeb645bc5c78`

This file records the concrete Blink seams the native binding must use or refactor. It is deliberately narrower than `architecture.md`: these are facts about one pinned Chromium tree, not permanent Web API semantics.

## Rule

The Native TypeScript path must never solve a blocked Blink API by going through a V8 wrapper, evaluating JavaScript, or reimplementing Web-standard behavior in the adapter.

If Blink's public binding-facing entry contains V8-only machinery, the correct change is to split the Blink implementation into:

```text
binding-specific conversion / exception carrier
                    |
                    v
          binding-neutral Blink operation
                    |
                    v
             DOM implementation
```

V8 and Native TypeScript then become sibling callers of the binding-neutral operation.

## Document.body

Blink already exposes a direct implementation call:

```cpp
HTMLElement* Document::body() const;
```

This needs no V8 value, `ScriptState`, or generated V8 binding wrapper. The native capsule may call it directly and intern the returned object in the realm handle registry.

Source:
`third_party/blink/renderer/core/dom/document.h`

## Node.textContent setter

Blink already has a direct implementation entry:

```cpp
virtual void Node::setTextContent(const String&);
```

The binding-facing overload additionally handles Trusted Types/WebIDL conversion. The native binding generator must select the correct WebIDL conversion first; once it has produced the effective Blink `String`, the capsule calls the direct implementation entry.

Source:
`third_party/blink/renderer/core/dom/node.h`

## EventTarget.addEventListener

Blink's event system already supports a non-JavaScript listener path:

```cpp
bool EventTarget::addEventListener(const AtomicString& event_type,
                                   EventListener* listener,
                                   bool use_capture = false);
```

`NativeEventListener` is an existing Blink listener base. Blink code subclasses it and receives:

```cpp
void Invoke(ExecutionContext*, Event*) override;
```

This is exactly the shape required by Native TypeScript events: the generated/native listener stores a callback token, receives the real Blink `Event*`, interns that event as a realm handle when the callback signature needs it, and enters the ScriptC callback directly on the owner sequence.

No V8 listener object is required.

Sources:
- `third_party/blink/renderer/core/dom/events/event_target.h`
- `third_party/blink/renderer/core/dom/events/native_event_listener.h`

## Document.createElement

This is the first mandatory Blink refactor.

The binding-facing operation is:

```cpp
Element* Document::CreateElementForBinding(const AtomicString& name,
                                           ExceptionState& exception_state);
```

and the overload handling the second WebIDL argument also receives `ExceptionState&`.

`ExceptionState` is currently a V8-oriented binding object: it stores a `v8::Isolate*`, creates V8 exception values, and its destruction path can throw into V8. Passing a null isolate or a testing exception state is not an acceptable Native TypeScript implementation because it would discard Web-observable failures.

We must not call `CreateRawElement` or copy the create-element algorithm into our capsule. That would fork DOM semantics and bypass custom-element behavior, validation, use counters, and future Chromium changes.

The required Blink shape is instead:

```text
V8 binding conversion
        |
        v
V8ExceptionSink --------+
                        |
                        v
            Document create-element core
                        ^
                        |
NtsExceptionSink -------+
        ^
        |
Native WebIDL conversion
```

The concrete refactor should make the DOM implementation depend on a binding-neutral exception interface/value rather than directly on V8 exception construction. The existing V8 `ExceptionState` becomes one adapter to that interface; the Native TypeScript realm provides another.

Sources:
- `third_party/blink/renderer/core/dom/document.h`
- `third_party/blink/renderer/core/dom/document.cc`
- `third_party/blink/renderer/platform/bindings/exception_state.h`

## Execution-context lifetime

A native realm must follow `ExecutionContext` lifetime rather than renderer-process lifetime.

Blink already has `ExecutionContextLifecycleObserver` and `ContextDestroyed()`. The Native TypeScript realm attaches an observer when the realm is created. `ContextDestroyed()` begins deterministic shutdown:

1. reject new DOM operations and callback registrations;
2. cancel native event subscriptions;
3. invalidate the C handle table;
4. release every Oilpan root on the owner sequence;
5. stop the ScriptC runtime instance;
6. detach/destroy the realm host.

No handle from the old context can become valid in a later document because slot generations advance during invalidation.

Source:
`third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h`

## Oilpan roots

The C handle table does not own Blink pointers. A Blink-side token owns the GC edge represented by each live native handle.

For an off-heap token, Oilpan requires `Persistent<T>` for a strong same-thread reference. Tokens are destroyed when the final C handle reference is released or when the realm is invalidated.

The initial implementation may use one persistent root per distinct live native identity. A later optimization may move roots into a GC-aware realm registry, but it must preserve the same externally visible retain/release and generation semantics.

Cross-thread persistent handles are not part of this design: a realm and all of its DOM handles are owner-sequence confined.

Source:
`third_party/blink/renderer/platform/heap/BlinkGCAPIReference.md`

## Handle identity

The target-neutral table in `src/runtime/nts_handle_table.*` owns only slot/generation/reference/type state.

The Blink registry owns object identity. Interning the same Blink object twice must return the same logical handle slot with an incremented reference count.

The registry must not keep an untraced raw Blink pointer as a long-lived identity key. Identity data that survives a call must itself obey Oilpan's tracing/rooting rules. A simple correct implementation may scan the realm's live rooted entries before introducing a GC-aware reverse map.

## Type identity

The future WebIDL generator supplies a closed interface-inheritance table to the handle-table `type_accepts` hook. A handle created as `HTMLButtonElement` can therefore be consumed by generated `HTMLElement`, `Element`, `Node`, and `EventTarget` operations without runtime reflection by string name.

Type IDs are manifest data. Unknown or mismatched identities fail before dereferencing the backend token.

## First direct call chain

The first complete native counter must have this call shape:

```text
plain generated-style C
        |
        v
nts_web_document_create_element
        |
        v
generated/handwritten C++ capsule
        |
        v
binding-neutral Document create-element core
        |
        v
real blink::Element
        |
        v
Oilpan-rooted realm handle
```

Followed by direct `Node::setTextContent`, `Document::body`, `Node` insertion, and a `NativeEventListener` click callback.

At no point does this path create a V8 context, V8 object, V8 function, V8 promise, or evaluate JavaScript.
