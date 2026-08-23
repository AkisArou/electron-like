#include "third_party/blink/renderer/native_typescript/nts_web.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "third_party/blink/renderer/native_typescript/nts_blink_realm.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/platform/bindings/exception_code.h"
#include "third_party/blink/renderer/platform/bindings/web_exception_state.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace {

NtsWebHandleResult HandleFailure(NtsWebStatus status) {
  NtsWebHandleResult result{};
  result.status = status;
  result.exception.status = status;
  return result;
}

bool CopyUtf8(std::string_view source, NtsOwnedUtf8* out) {
  out->data = nullptr;
  out->length = 0;
  if (source.empty()) return true;

  auto* copy = static_cast<uint8_t*>(std::malloc(source.size()));
  if (!copy) return false;
  std::memcpy(copy, source.data(), source.size());
  out->data = copy;
  out->length = source.size();
  return true;
}

uint16_t LegacyDOMCode(blink::DOMExceptionCode code) {
  const int value = static_cast<int>(code);
  const int minimum =
      static_cast<int>(blink::DOMExceptionCode::kLegacyErrorCodeMin);
  const int maximum =
      static_cast<int>(blink::DOMExceptionCode::kLegacyErrorCodeMax);
  return value >= minimum && value <= maximum ? static_cast<uint16_t>(value)
                                               : 0;
}

NtsWebStatus NativeStatus(const blink::WebExceptionState& source) {
  using Kind = blink::WebExceptionState::Kind;
  switch (source.GetKind()) {
    case Kind::kNone:
      return NTS_WEB_OK;
    case Kind::kDOMException:
    case Kind::kSecurityError:
      return NTS_WEB_DOM_EXCEPTION;
    case Kind::kRangeError:
      return NTS_WEB_RANGE_ERROR;
    case Kind::kTypeError:
      return NTS_WEB_TYPE_ERROR;
    case Kind::kSyntaxError:
      return NTS_WEB_SYNTAX_ERROR;
  }
  return NTS_WEB_INVALID_ARGUMENT;
}

blink::String ExceptionName(const blink::WebExceptionState& source) {
  using Kind = blink::WebExceptionState::Kind;
  switch (source.GetKind()) {
    case Kind::kNone:
      return blink::String();
    case Kind::kDOMException:
    case Kind::kSecurityError:
      return blink::DOMException::GetErrorName(source.DOMCode());
    case Kind::kRangeError:
      return blink::String("RangeError");
    case Kind::kTypeError:
      return blink::String("TypeError");
    case Kind::kSyntaxError:
      return blink::String("SyntaxError");
  }
  return blink::String();
}

NtsWebStatus CopyException(const blink::WebExceptionState& source,
                           NtsWebException* out) {
  *out = {};
  const NtsWebStatus status = NativeStatus(source);
  out->status = status;
  if (status == NTS_WEB_OK) return status;

  if (source.GetKind() == blink::WebExceptionState::Kind::kDOMException ||
      source.GetKind() == blink::WebExceptionState::Kind::kSecurityError) {
    out->legacy_code = LegacyDOMCode(source.DOMCode());
  }

  const std::string name = ExceptionName(source).Utf8();
  const std::string message = source.Message().Utf8();
  if (!CopyUtf8(name, &out->name) || !CopyUtf8(message, &out->message)) {
    std::free(out->name.data);
    std::free(out->message.data);
    *out = {};
    out->status = NTS_WEB_OUT_OF_MEMORY;
    return NTS_WEB_OUT_OF_MEMORY;
  }
  return status;
}

NtsWebStatus CheckRealm(NtsWebRealm* realm) {
  if (!realm) return NTS_WEB_INVALID_ARGUMENT;
  if (!realm->IsCurrent()) return NTS_WEB_WRONG_SEQUENCE;
  if (!realm->IsAlive()) return NTS_WEB_CONTEXT_DESTROYED;
  return NTS_WEB_OK;
}

}  // namespace

extern "C" NtsWebHandleResult nts_web_document(NtsWebRealm* realm) {
  const NtsWebStatus realm_status = CheckRealm(realm);
  if (realm_status != NTS_WEB_OK) return HandleFailure(realm_status);

  blink::Document* document = realm->Document();
  if (!document) return HandleFailure(NTS_WEB_CONTEXT_DESTROYED);

  NtsWebHandleResult result{};
  result.status = realm->Nodes().Intern(document, &result.value);
  result.exception.status = result.status;
  return result;
}

extern "C" NtsWebHandleResult nts_web_document_create_element(
    NtsWebRealm* realm,
    NtsWebHandle document_handle,
    NtsUtf8View local_name) {
  const NtsWebStatus realm_status = CheckRealm(realm);
  if (realm_status != NTS_WEB_OK) return HandleFailure(realm_status);
  if (!local_name.data && local_name.length != 0) {
    return HandleFailure(NTS_WEB_INVALID_ARGUMENT);
  }

  blink::Node* document_node = nullptr;
  NtsWebStatus status = realm->Nodes().Resolve(
      document_handle, nts::blink_bridge::WebTypeId::kDocument, &document_node);
  if (status != NTS_WEB_OK) return HandleFailure(status);

  auto* document = static_cast<blink::Document*>(document_node);
  const auto bytes = base::span(local_name.data, local_name.length);
  blink::String name = blink::String::FromUtf8(bytes);
  if (name.IsNull() && local_name.length != 0) {
    return HandleFailure(NTS_WEB_INVALID_ARGUMENT);
  }

  blink::WebExceptionState exception_state;
  blink::Element* element = document->CreateElementForBinding(
      blink::AtomicString(name), exception_state);

  if (exception_state.HadException()) {
    NtsWebHandleResult result{};
    result.status = CopyException(exception_state, &result.exception);
    return result;
  }
  if (!element) return HandleFailure(NTS_WEB_INVALID_HANDLE);

  NtsWebHandleResult result{};
  result.status = realm->Nodes().Intern(element, &result.value);
  result.exception.status = result.status;
  return result;
}
