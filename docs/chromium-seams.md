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

No V8 listener object is required. `chromium/bridge/nts_blink_event_listener.*` carries the first adapter for this path.

Sources:
- `third_party/blink/renderer/core/dom/events/event_target.h`
- `third_party/blink/renderer/core/dom/events/native_event_listener.h`

## Document.createElement

The first binding-neutral Blink cut is now represented in this repository.

At the pinned revision the binding-facing operation is:

```cpp
Element* Document::CreateElementForBinding(const AtomicString& name,
                                           ExceptionState& exception_state);
```

The body is the real one-argument DOM create-element algorithm. It validates the name, performs HTML local-name normalization, routes custom elements through Blink's custom-element implementation, uses the HTML element factory, preserves the unknown-element fallback, and handles non-HTML documents. Its only binding-specific dependency in that body is `ExceptionState::ThrowDOMException` for an invalid name.

`ExceptionState` is V8-oriented: it stores a `v8::Isolate*` and materializes V8 exceptions. Passing a null/testing state would erase observable failure semantics, while calling the generated V8 wrapper would violate the Native TypeScript call-path invariant.

`chromium/patches/0001-binding-neutral-web-exception-state.patch` therefore adds a stack-owned `WebExceptionState` with no V8 value/isolate/state and a sibling overload:

```cpp
Element* Document::CreateElementForBinding(const AtomicString& name,
                                           WebExceptionState& exception_state);
```

The existing V8-facing overload becomes an adapter to the same DOM algorithm:

```text
V8 caller                        Native TypeScript caller
   |                                      |
ExceptionState                      C ABI / capsule
   |                                      |
   +---------- WebExceptionState ---------+
                      |
                      v
       one Document createElement body
```

The Native TypeScript capsule in `chromium/bridge/nts_blink_dom.cc` resolves a generation/type-checked `Document` handle, performs the current narrow string conversion, calls the neutral overload directly, converts a `WebExceptionState` into the C result, and interns the returned real `blink::Element` in the Oilpan-rooted node registry.

The plain-C probe in `examples/create-element/app.c` also exercises an invalid tag name and requires `InvalidCharacterError`'s legacy code `5`, so the implementation cannot make only the happy path work by swallowing exceptions.

This source seam is implemented. A full Chromium GN/Ninja compile and rendered fixture remain execution evidence rather than something this register infers from source.

Sources:
- `third_party/blink/renderer/core/dom/document.h`
- `third_party/blink/renderer/core/dom/document.cc`
- `third_party/blink/renderer/platform/bindings/exception_state.h`
- `chromium/patches/0001-binding-neutral-web-exception-state.patch`
- `chromium/bridge/nts_blink_dom.cc`

## Execution-context lifetime

A native realm follows `ExecutionContext` lifetime rather than renderer-process lifetime.

Blink already has `ExecutionContextLifecycleObserver` and `ContextDestroyed()`. `NtsWebRealm` now roots a GC-owned lifecycle observer. `ContextDestroyed()` invalidates the off-heap realm on the owner sequence before the execution context disappears.

The current invalidation path:

1. rejects later DOM operations through the realm alive check;
2. invalidates the native handle table;
3. destroys/relinquishes every node `Persistent` root;
4. clears the rooted document;
5. leaves later ScriptC runtime/callback shutdown to be attached to this same realm transition as those services land.

No handle from the old context can become valid in a later document because old slot/generation identities become stale when the registry is invalidated.

Sources:
- `third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h`
- `chromium/bridge/nts_blink_realm.*`

## Oilpan roots

The C handle table does not own Blink pointers. A Blink-side token owns the GC edge represented by each live native handle.

The current `BlinkNodeRegistry` uses one `Persistent<Node>` per distinct live native identity. Tokens are destroyed when the final C alias is released or when the realm is invalidated. Cross-thread persistent handles are not part of the design: a realm and all of its DOM handles are owner-sequence confined.

Source:
- `third_party/blink/renderer/platform/heap/persistent.h`
- `chromium/bridge/nts_blink_node_registry.*`

## Handle identity

The target-neutral table in `src/runtime/nts_handle_table.*` owns only slot/generation/reference/type state.

The Blink registry owns object identity. Interning the same Blink object twice returns the same logical handle slot with an incremented reference count.

The registry does not keep an untraced raw Blink pointer as a long-lived identity key. The first implementation scans the realm's live rooted entries; a later GC-aware reverse map may replace the scan without changing handle semantics.

## Type identity

The future WebIDL generator supplies a closed interface-inheritance table to the handle-table `type_accepts` hook. The current narrow registry contains explicit IDs for `Node`, `Document`, `Element`, `HTMLElement`, and `HTMLBodyElement` so the first direct calls can validate receiver identity before dereferencing the backend token.

Type IDs are manifest data in the final generator. Unknown or mismatched identities fail before a Blink object is used.

## Build placement

Blink core's GN visibility admits dependents under `//third_party/blink/renderer/*`, so the overlay installs at:

```text
third_party/blink/renderer/native_typescript
```

and exposes:

```text
//third_party/blink/renderer/native_typescript:nts_blink_bridge
```

The portable C runtime is a separate GN source set from the Blink C++ capsule so it does not inherit Blink's C++ PCH/configuration. The C++ bridge receives the normal Blink renderer/core configs.

`scripts/verify_chromium_patches.py` checks the patch series against the exact pinned Chromium inputs on every `main` push, and `scripts/check_no_v8_bridge.py` rejects direct bridge source that introduces V8 value/state/wrapper or script-evaluation paths.

## First direct call chain

The direct source path now has this shape:

```text
plain generated-style C
        |
        v
nts_web_document_create_element
        |
        v
checked NtsWebRealm + Document handle
        |
        v
WebExceptionState
        |
        v
blink::Document::CreateElementForBinding
        |
        v
real blink::Element
        |
        v
Oilpan-rooted realm handle
```

The remaining counter operations can build on already-identified direct seams: `Document::body`, `Node::setTextContent`, tree insertion/removal, and Blink's native event-listener path.

At no point in this direct call chain is a V8 context, V8 object, V8 function, V8 promise, JavaScript property lookup, or evaluated JavaScript source used as the Native TypeScript bridge.
