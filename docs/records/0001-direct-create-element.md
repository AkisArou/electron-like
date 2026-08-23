# 0001 — direct `Document.createElement` seam

Date: 2026-08-23  
Chromium: `96324a4012fe62f48b9463a67486eeb645bc5c78`

## Question

Can a plain native C caller reach Blink's real `Document.createElement` DOM algorithm without creating a V8 value/context/wrapper, evaluating JavaScript, or copying the DOM algorithm into the Native TypeScript adapter?

## Pinned finding

Yes, after one narrow Blink refactor.

At the pinned Chromium revision, the one-argument binding-facing entry is:

```cpp
Element* Document::CreateElementForBinding(const AtomicString& name,
                                           ExceptionState& exception_state);
```

The body contains the real DOM algorithm for the one-argument operation: element-name validation, HTML local-name normalization, custom-element creation, the HTML element factory, `HTMLUnknownElement` fallback, and the non-HTML path. The only binding-specific dependency used by that body is `ExceptionState::ThrowDOMException` for invalid names.

`ExceptionState` itself is V8-specific: it owns a `v8::Isolate*` and materializes V8 exceptions. Therefore passing a null/testing state would erase observable failure semantics, while invoking the V8 wrapper would violate the native call-path invariant.

## Change

`chromium/patches/0001-binding-neutral-web-exception-state.patch` introduces `WebExceptionState`, a stack-owned value containing only Web-observable failure information, and adds a sibling overload:

```cpp
Element* Document::CreateElementForBinding(const AtomicString& name,
                                           WebExceptionState& exception_state);
```

The existing `ExceptionState` overload becomes an adapter:

```text
V8 binding
   |
ExceptionState
   |
WebExceptionState
   |
shared Document createElement algorithm
```

The Native TypeScript side calls the neutral overload directly:

```text
plain C
  -> nts_web_document_create_element
  -> checked Document handle
  -> WebExceptionState
  -> Document::CreateElementForBinding
  -> blink::Element
  -> Oilpan-rooted NtsWebHandle
```

There is one DOM algorithm and two binding-runtime exception adapters. Native TypeScript never asks V8 to perform the operation.

## Native lifetime

`NtsWebRealm` owns the document and node registry on one Chromium renderer sequence. The node registry roots live Blink nodes using Oilpan `Persistent<Node>` values and exposes only slot/generation handles to C.

A GC-owned `ExecutionContextLifecycleObserver` invalidates the off-heap realm when Blink destroys its execution context. Invalidation rejects later calls, advances/stales native handle identity through the handle table, and releases all persistent node roots on the owning sequence.

## Exception proof arm

`examples/create-element/app.c` contains two plain-C probes:

- creating `"button"` and releasing the returned native handle;
- creating `"invalid name"` and requiring a DOM exception with legacy code `5` (`InvalidCharacterError`).

The second probe exists specifically to prevent a future implementation from making the happy path work by swallowing Blink's exception behavior.

## Build/provenance contract

The patch series is applied only to the exact revision in `chromium/revision.json`. `scripts/verify_chromium_patches.py` downloads the exact affected Chromium inputs and runs `git apply --check` before applying the patch. CI runs that verifier for every push to `main`.

`scripts/apply_chromium.py` installs the bridge under:

```text
third_party/blink/renderer/native_typescript
```

so the GN target is within Blink renderer visibility rather than bypassing Blink's dependency rules:

```text
//third_party/blink/renderer/native_typescript:nts_blink_bridge
```

A separate CI gate rejects bridge source that introduces `v8::`, `ScriptState`, generated V8 DOM wrappers, or script evaluation.

## What this record does not claim

This record proves the source-level architectural seam and pins it to concrete Chromium inputs. A full Chromium GN/Ninja build and rendered-browser fixture are a separate execution proof and must be recorded when run. It also does not claim the current UTF-8 temporary C carrier is the final JavaScript `DOMString` representation; exact string-code-unit compatibility belongs to the later generated WebIDL/ScriptC binding contract.
