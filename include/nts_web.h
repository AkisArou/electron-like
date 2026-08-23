#ifndef NTS_WEB_H
#define NTS_WEB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtsWebRealm NtsWebRealm;

typedef struct {
  const uint8_t *data;
  size_t length;
} NtsUtf8View;

typedef struct {
  uint32_t slot;
  uint32_t generation;
} NtsWebHandle;

typedef struct {
  uint32_t slot;
  uint32_t generation;
} NtsWebCallbackToken;

typedef enum {
  NTS_WEB_OK = 0,
  NTS_WEB_INVALID_ARGUMENT,
  NTS_WEB_INVALID_HANDLE,
  NTS_WEB_WRONG_REALM,
  NTS_WEB_WRONG_SEQUENCE,
  NTS_WEB_CONTEXT_DESTROYED,
  NTS_WEB_TYPE_ERROR,
  NTS_WEB_RANGE_ERROR,
  NTS_WEB_DOM_EXCEPTION,
  NTS_WEB_OPERATION_DISABLED,
  NTS_WEB_OUT_OF_MEMORY
} NtsWebStatus;

typedef struct {
  NtsWebStatus status;
  uint16_t legacy_code;
  NtsUtf8View name;
  NtsUtf8View message;
} NtsWebException;

typedef struct {
  NtsWebStatus status;
  NtsWebHandle value;
  NtsWebException exception;
} NtsWebHandleResult;

typedef struct {
  NtsWebStatus status;
  NtsWebException exception;
} NtsWebVoidResult;

/* Realm lifecycle. A realm is bound to one Blink ExecutionContext and one
 * renderer owner sequence. The Blink adapter constructs/destroys it. */
bool nts_web_realm_is_current(const NtsWebRealm *realm);
bool nts_web_realm_is_alive(const NtsWebRealm *realm);

/* Root objects. */
NtsWebHandle nts_web_window(NtsWebRealm *realm);
NtsWebHandle nts_web_document(NtsWebRealm *realm);
NtsWebHandleResult nts_web_document_body(NtsWebRealm *realm,
                                          NtsWebHandle document);

/* First deliberately narrow DOM surface. These are statically identified
 * operations, not a generic property/method dispatch facility. */
NtsWebHandleResult nts_web_document_create_element(
    NtsWebRealm *realm,
    NtsWebHandle document,
    NtsUtf8View local_name);

NtsWebVoidResult nts_web_node_append_child(
    NtsWebRealm *realm,
    NtsWebHandle parent,
    NtsWebHandle child);

NtsWebVoidResult nts_web_node_remove(
    NtsWebRealm *realm,
    NtsWebHandle node);

NtsWebVoidResult nts_web_node_set_text_content(
    NtsWebRealm *realm,
    NtsWebHandle node,
    NtsUtf8View text);

NtsWebVoidResult nts_web_element_set_attribute(
    NtsWebRealm *realm,
    NtsWebHandle element,
    NtsUtf8View name,
    NtsUtf8View value);

/* Handle lifetime. Releasing the final Native TypeScript edge allows the
 * realm registry to release its corresponding Oilpan strong edge. */
NtsWebStatus nts_web_handle_retain(NtsWebRealm *realm, NtsWebHandle handle);
NtsWebStatus nts_web_handle_release(NtsWebRealm *realm, NtsWebHandle handle);

#ifdef __cplusplus
}
#endif

#endif
