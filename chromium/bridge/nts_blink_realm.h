#ifndef NTS_BLINK_REALM_H
#define NTS_BLINK_REALM_H

#include "base/sequence_checker.h"
#include "chromium/bridge/nts_blink_node_registry.h"
#include "nts_web.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"

namespace blink {
class Document;
}

/* C sees only the opaque forward declaration from nts_web.h. The Chromium
 * adapter sees this owner-sequence-confined definition. It is deliberately an
 * off-heap object: its Persistent<Document> and BlinkNodeRegistry entries are
 * the explicit Oilpan roots associated with the Native TypeScript realm. */
struct NtsWebRealm final {
 public:
  explicit NtsWebRealm(blink::Document* document);
  NtsWebRealm(const NtsWebRealm&) = delete;
  NtsWebRealm& operator=(const NtsWebRealm&) = delete;
  ~NtsWebRealm();

  bool IsCurrent() const;
  bool IsAlive() const;
  void Invalidate();

  blink::Document* Document() const;
  nts::blink_bridge::BlinkNodeRegistry& Nodes() { return nodes_; }

 private:
  base::SequenceChecker sequence_checker_;
  bool alive_ = true;
  blink::Persistent<blink::Document> document_;
  nts::blink_bridge::BlinkNodeRegistry nodes_;
};

namespace nts::blink_bridge {

/* Host-facing construction seam. The embedder creates one realm for the
 * document/execution context whose renderer sequence is currently executing.
 * Realm destruction must run on that same sequence. */
NtsWebRealm* CreateWebRealm(blink::Document* document);
void DestroyWebRealm(NtsWebRealm* realm);
void InvalidateWebRealm(NtsWebRealm* realm);

}  // namespace nts::blink_bridge

#endif
