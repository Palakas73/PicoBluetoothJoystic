#pragma once
#include "btstack.h"

/* One-shot, read-only audit after HOGP has configured notifications. */
void gatt_audit_start(hci_con_handle_t connection);
void gatt_audit_reset(void);
const char *gatt_audit_status(void);
bool gatt_audit_input_confirmed(void);
