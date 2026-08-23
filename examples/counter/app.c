#include "nts_web.h"

#include <string.h>

static NtsUtf8View nts_utf8_literal(const char *text) {
  NtsUtf8View value;
  value.data = (const uint8_t *)text;
  value.length = strlen(text);
  return value;
}

static bool nts_take_handle_result(NtsWebHandleResult *result,
                                   NtsWebHandle *out_handle) {
  if (result->status == NTS_WEB_OK) {
    *out_handle = result->value;
    return true;
  }
  nts_web_exception_dispose(&result->exception);
  return false;
}

static bool nts_take_void_result(NtsWebVoidResult *result) {
  if (result->status == NTS_WEB_OK) return true;
  nts_web_exception_dispose(&result->exception);
  return false;
}

static void nts_release_if_live(NtsWebRealm *realm, NtsWebHandle *handle) {
  if (handle->generation == 0) return;
  (void)nts_web_handle_release(realm, *handle);
  handle->slot = 0;
  handle->generation = 0;
}

/* This file intentionally mirrors the shape ScriptC-generated C should target.
 * It does not know about Blink C++ types and it does not evaluate JavaScript.
 * The cleanup path is deliberate: generated code must unwind owned native
 * aliases and owned exception payloads on every failing boundary. */
bool nts_counter_start(NtsWebRealm *realm) {
  NtsWebHandle document = {0};
  NtsWebHandle body = {0};
  NtsWebHandle heading = {0};
  NtsWebHandle button = {0};
  bool success = false;

  if (realm == NULL || !nts_web_realm_is_current(realm) ||
      !nts_web_realm_is_alive(realm)) {
    return false;
  }

  NtsWebHandleResult document_result = nts_web_document(realm);
  if (!nts_take_handle_result(&document_result, &document)) goto cleanup;

  NtsWebHandleResult body_result = nts_web_document_body(realm, document);
  if (!nts_take_handle_result(&body_result, &body)) goto cleanup;

  NtsWebHandleResult heading_result = nts_web_document_create_element(
      realm, document, nts_utf8_literal("h1"));
  if (!nts_take_handle_result(&heading_result, &heading)) goto cleanup;

  NtsWebVoidResult heading_text = nts_web_node_set_text_content(
      realm, heading, nts_utf8_literal("Native TypeScript"));
  if (!nts_take_void_result(&heading_text)) goto cleanup;

  NtsWebHandleResult button_result = nts_web_document_create_element(
      realm, document, nts_utf8_literal("button"));
  if (!nts_take_handle_result(&button_result, &button)) goto cleanup;

  NtsWebVoidResult button_text = nts_web_node_set_text_content(
      realm, button, nts_utf8_literal("Count: 0"));
  if (!nts_take_void_result(&button_text)) goto cleanup;

  NtsWebVoidResult append_heading =
      nts_web_node_append_child(realm, body, heading);
  if (!nts_take_void_result(&append_heading)) goto cleanup;

  NtsWebVoidResult append_button =
      nts_web_node_append_child(realm, body, button);
  if (!nts_take_void_result(&append_button)) goto cleanup;

  success = true;

cleanup:
  /* Insertion changes Blink's DOM ownership graph; it does not transfer the
   * native aliases held by this compiled frame. Those aliases are released in
   * the same way on success and unwind. */
  nts_release_if_live(realm, &button);
  nts_release_if_live(realm, &heading);
  nts_release_if_live(realm, &body);
  nts_release_if_live(realm, &document);
  return success;
}
