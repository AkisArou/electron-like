#include "third_party/blink/renderer/native_typescript/nts_blink_realm.h"

#include <new>

#include "base/check.h"
#include "third_party/blink/renderer/core/dom/document.h"

NtsWebRealm::NtsWebRealm(blink::Document* document) : document_(document) {
  CHECK(document);
  CHECK(IsCurrent());
}

NtsWebRealm::~NtsWebRealm() {
  CHECK(IsCurrent());
  Invalidate();
}

bool NtsWebRealm::IsCurrent() const {
  return sequence_checker_.CalledOnValidSequence();
}

bool NtsWebRealm::IsAlive() const {
  return IsCurrent() && alive_ && document_.Get() != nullptr;
}

void NtsWebRealm::Invalidate() {
  CHECK(IsCurrent());
  if (!alive_) return;
  alive_ = false;
  nodes_.Invalidate();
  document_ = nullptr;
}

blink::Document* NtsWebRealm::Document() const {
  CHECK(IsCurrent());
  return alive_ ? document_.Get() : nullptr;
}

namespace nts::blink_bridge {

NtsWebRealm* CreateWebRealm(blink::Document* document) {
  if (!document) return nullptr;
  return new (std::nothrow) NtsWebRealm(document);
}

void DestroyWebRealm(NtsWebRealm* realm) {
  if (!realm) return;
  CHECK(realm->IsCurrent());
  delete realm;
}

void InvalidateWebRealm(NtsWebRealm* realm) {
  if (!realm) return;
  CHECK(realm->IsCurrent());
  realm->Invalidate();
}

}  // namespace nts::blink_bridge

extern "C" bool nts_web_realm_is_current(const NtsWebRealm* realm) {
  return realm != nullptr && realm->IsCurrent();
}

extern "C" bool nts_web_realm_is_alive(const NtsWebRealm* realm) {
  return realm != nullptr && realm->IsAlive();
}

extern "C" NtsWebStatus nts_web_handle_retain(NtsWebRealm* realm,
                                               NtsWebHandle handle) {
  if (!realm) return NTS_WEB_INVALID_ARGUMENT;
  if (!realm->IsCurrent()) return NTS_WEB_WRONG_SEQUENCE;
  if (!realm->IsAlive()) return NTS_WEB_CONTEXT_DESTROYED;
  return realm->Nodes().Retain(handle);
}

extern "C" NtsWebStatus nts_web_handle_release(NtsWebRealm* realm,
                                                NtsWebHandle handle) {
  if (!realm) return NTS_WEB_INVALID_ARGUMENT;
  if (!realm->IsCurrent()) return NTS_WEB_WRONG_SEQUENCE;
  if (!realm->IsAlive()) return NTS_WEB_CONTEXT_DESTROYED;
  return realm->Nodes().Release(handle);
}
