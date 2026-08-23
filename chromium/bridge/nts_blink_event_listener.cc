#include "chromium/bridge/nts_blink_event_listener.h"

#include "base/check.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"

namespace nts::blink_bridge {

BlinkNativeEventListener::BlinkNativeEventListener(
    BlinkEventDispatcher* dispatcher,
    NtsWebCallbackToken token)
    : dispatcher_(dispatcher), token_(token) {
  CHECK(dispatcher);
  CHECK_NE(token_.generation, 0u);
}

void BlinkNativeEventListener::Invoke(blink::ExecutionContext* context,
                                      blink::Event* event) {
  CHECK(context);
  CHECK(event);
  dispatcher_->DispatchNativeEvent(token_, context, event);
}

void BlinkNativeEventListener::Trace(blink::Visitor* visitor) const {
  visitor->Trace(dispatcher_);
  blink::NativeEventListener::Trace(visitor);
}

}  // namespace nts::blink_bridge
