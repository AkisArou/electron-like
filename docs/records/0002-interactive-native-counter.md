# 0002 — interactive native counter

Date: 2026-08-23  
Chromium: `96324a4012fe62f48b9463a67486eeb645bc5c78`

## Question

Can one real DOM interaction be expressed as plain C application state and direct Blink operations, with no JavaScript listener object, V8 value, generic command bridge, or renderer IPC in the Web API path?

## Program

The acceptance source is `examples/counter/app.c`. It is deliberately written in the style expected from future ScriptC C emission rather than as a C++ Blink demo.

It performs:

```text
Document
  -> body
  -> create h1
  -> set text
  -> create button
  -> set text
  -> add click listener
  -> append h1
  -> append button

browser click
  -> opaque callback token
  -> native count++
  -> captured button handle
  -> set text
```

The HTML resource contains no script.

## Findings

### `Document.body` needs no binding-runtime object

The pinned Blink `Document::body()` implementation can be called directly. A missing body is represented by a successful zero native handle, preserving WebIDL null separately from transport/runtime failure.

### `textContent` has a direct core path, but Trusted Types matters

`Node::setTextContent(const String&)` is directly callable for the ordinary h1/button shape in the acceptance program. Blink's web-facing binding has additional Trusted Types behavior for script text. The capsule therefore refuses `HTMLScriptElement` instead of pretending the raw setter is universally equivalent to the Web API.

This is the rule to keep: a direct core method is usable only after every binding-visible semantic between WebIDL and that core method has been accounted for.

### Generic `appendChild` is not crossed yet

Blink's real `ContainerNode::AppendChild(Node*, ExceptionState&)` owns hierarchy validation, removal from an old parent, adoption and exception behavior. The acceptance program has a narrower statically provable shape: a newly created, detached `Element` appended to an `Element` parent.

For exactly that shape the adapter proves the relevant prerequisites before calling the no-exception overload. All other shapes return `NTS_WEB_OPERATION_DISABLED`.

This is not the final `appendChild` binding. It is a closed acceptance subset that cannot silently swallow an exception. The general operation is admitted when the binding-neutral exception carrier is propagated through `Node`/`ContainerNode`.

### Blink already has the correct native event-listener object model

`NativeEventListener` is a first-class Blink `EventListener`. The Native TypeScript adapter uses it directly and stores an opaque callback token; there is no JavaScript function wrapper.

A separate generation-checked subscription table owns each registration. Its backend root retains the exact `EventTarget` and exact listener instance. Disposal calls Blink's native `removeEventListener` overload with the same values before dropping those roots.

### Native listener registration had one hidden V8 touch

`EventTarget::AddEventListenerInternal` queried `execution_context->GetIsolate()` for isolated-world activity logging even when the listener itself was native. Patch `0002-native-event-listener-without-v8-logger.patch` gates that JavaScript instrumentation behind `!listener->IsNativeEventListener()`.

The rest of the reached click registration/removal path is native listener bookkeeping, option resolution and use counters.

### A retained callback needs an independently owned capture

The C counter keeps the button handle alive after top-level initialization returns. The listener subscription and button handle are released only by `nts_counter_stop()` or realm invalidation. This mirrors the ownership shape a compiled closure will create later: callback-table reachability keeps its captures alive.

The standalone `counter-contract` test pins this behavior without Chromium: it starts the C program against a fake typed WebAPI backend, captures the listener token, delivers two events, verifies `Count: 1` and `Count: 2`, then verifies one subscription disposal and final button-handle release.

### Realm teardown order matters

`NtsWebRealm::Invalidate()` cancels subscriptions before releasing DOM object roots and the document root. Native listeners contain an off-heap realm pointer, so cancellation/detach must happen while that realm is still valid.

The existing `ExecutionContextLifecycleObserver` makes navigation/context destruction the authoritative invalidation event.

## Harness

`content_shell` is patched only as an executable proving ground. With `--native-typescript-counter`, a main-frame `RenderFrameObserver` starts the Blink-owned counter host at `DOMContentLoaded` and destroys it before the next document/frame disappears.

The content embedder sees only `WebLocalFrame*`. The Blink-owned host alone knows `WebLocalFrameImpl`, `LocalFrame`, `Document`, `NtsWebRealm` and the direct DOM capsules.

This prevents the experiment from turning `content_shell` into a second owner of Blink internals.

## Evidence boundary

The repository now contains:

- the exact C acceptance program and a standalone behavioral test;
- the typed C ABI and lifecycle/resource machinery;
- direct Blink capsules for the reached counter operations;
- the native listener and subscription implementation;
- the pinned Chromium patch series;
- an opt-in content-shell harness;
- GN overlay targets and a build helper.

A full Chromium GN/Ninja compile and rendered click run have not been executed in the current ChatGPT environment because it has no Chromium checkout or external network access. That execution is the next empirical gate; the repository does not record it as passed until it is actually run.

## Decision

Keep this architecture. The first interactive DOM program does not require a V8 value/function bridge or JavaScript glue. Continue widening Web API coverage by moving binding-observable semantics into neutral contracts only when a reached operation requires them; never substitute unchecked internal DOM calls for an unimplemented semantic boundary.
