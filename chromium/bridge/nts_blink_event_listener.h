#ifndef NTS_BLINK_EVENT_LISTENER_H
#define NTS_BLINK_EVENT_LISTENER_H

#include "nts_web.h"
#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"

namespace blink {
class Event;
class ExecutionContext;
class Visitor;
}

namespace nts::blink_bridge {

/* Implemented by the realm. It is a GC mixin so a live Blink listener traces
 * the realm-side callback dispatcher rather than retaining an untraced raw
 * pointer. */
class BlinkEventDispatcher : public blink::GarbageCollectedMixin {
 public:
  virtual ~BlinkEventDispatcher() = default;
  virtual void DispatchNativeEvent(NtsWebCallbackToken token,
                                   blink::ExecutionContext* context,
                                   blink::Event* event) = 0;
  void Trace(blink::Visitor*) const override {}
};

/* Blink already has a native event-listener path. This adapter deliberately
 * subclasses it rather than manufacturing a V8 EventListener wrapper. */
class BlinkNativeEventListener final : public blink::NativeEventListener {
 public:
  BlinkNativeEventListener(BlinkEventDispatcher* dispatcher,
                           NtsWebCallbackToken token);
  ~BlinkNativeEventListener() override = default;

  void Invoke(blink::ExecutionContext* context, blink::Event* event) override;
  void Trace(blink::Visitor* visitor) const override;

 private:
  blink::Member<BlinkEventDispatcher> dispatcher_;
  NtsWebCallbackToken token_;
};

}  // namespace nts::blink_bridge

#endif
