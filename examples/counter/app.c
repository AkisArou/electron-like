#include "nts_web.h"

#include <string.h>

static NtsUtf8View nts_utf8_literal(const char *text) {
  NtsUtf8View value;
  value.data = (const uint8_t *)text;
  value.length = strlen(text);
  return value;
}

/* This file intentionally mirrors the shape ScriptC-generated C should target.
 * It does not know about Blink C++ types and it does not evaluate JavaScript. */
bool nts_counter_start(NtsWebRealm *realm) {
  if (realm == NULL || !nts_web_realm_is_current(realm) ||
      !nts_web_realm_is_alive(realm)) {
    return false;
  }

  NtsWebHandle document = nts_web_document(realm);
  NtsWebHandleResult body = nts_web_document_body(realm, document);
  if (body.status != NTS_WEB_OK) return false;

  NtsWebHandleResult heading = nts_web_document_create_element(
      realm, document, nts_utf8_literal("h1"));
  if (heading.status != NTS_WEB_OK) return false;

  NtsWebVoidResult heading_text = nts_web_node_set_text_content(
      realm, heading.value, nts_utf8_literal("Native TypeScript"));
  if (heading_text.status != NTS_WEB_OK) return false;

  NtsWebHandleResult button = nts_web_document_create_element(
      realm, document, nts_utf8_literal("button"));
  if (button.status != NTS_WEB_OK) return false;

  NtsWebVoidResult button_text = nts_web_node_set_text_content(
      realm, button.value, nts_utf8_literal("Count: 0"));
  if (button_text.status != NTS_WEB_OK) return false;

  NtsWebVoidResult append_heading =
      nts_web_node_append_child(realm, body.value, heading.value);
  if (append_heading.status != NTS_WEB_OK) return false;

  NtsWebVoidResult append_button =
      nts_web_node_append_child(realm, body.value, button.value);
  if (append_button.status != NTS_WEB_OK) return false;

  /* The example deliberately releases its C-owned aliases after insertion.
   * The DOM tree may keep the underlying Blink nodes alive independently. */
  if (nts_web_handle_release(realm, heading.value) != NTS_WEB_OK) return false;
  if (nts_web_handle_release(realm, button.value) != NTS_WEB_OK) return false;
  if (nts_web_handle_release(realm, body.value) != NTS_WEB_OK) return false;

  return true;
}
