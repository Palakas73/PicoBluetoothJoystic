#include "gatt_audit.h"
#include "ble/gatt_client.h"
#include <stdio.h>
#include <string.h>

#define MAX_REPORT_CHARACTERISTICS 8
#define UUID_HIDS 0x1812
#define UUID_REPORT 0x2a4d
#define UUID_CCCD 0x2902
#define UUID_REPORT_REFERENCE 0x2908

typedef enum {
  AUDIT_IDLE, AUDIT_SERVICE, AUDIT_CHARACTERISTICS, AUDIT_DESCRIPTORS,
  AUDIT_READ_REFERENCE, AUDIT_READ_CCCD, AUDIT_DONE, AUDIT_ERROR
} audit_step_t;
typedef struct {
  gatt_client_characteristic_t characteristic;
  uint16_t reference_handle, cccd_handle;
  uint8_t report_id, report_type;
  uint16_t cccd_value;
  bool reference_read, cccd_read;
} audited_report_t;
static hci_con_handle_t con_handle;
static gatt_client_service_t service;
static audited_report_t reports[MAX_REPORT_CHARACTERISTICS];
static uint8_t count, report_index;
static bool service_found;
static bool input_confirmed;
static audit_step_t step;

const char *gatt_audit_status(void) {
  switch (step) {
  case AUDIT_IDLE: return "idle";
  case AUDIT_DONE: return "done";
  case AUDIT_ERROR: return "error";
  default: return "running";
  }
}
bool gatt_audit_input_confirmed(void) { return input_confirmed; }

void gatt_audit_reset(void) {
  step = AUDIT_IDLE;
  con_handle = HCI_CON_HANDLE_INVALID;
  count = report_index = 0;
  service_found = false;
  input_confirmed = false;
  memset(reports, 0, sizeof(reports));
}

static void audit_handler(uint8_t packet_type, uint16_t channel,
                          uint8_t *packet, uint16_t size);

static bool audit_query(uint8_t status, const char *operation) {
  if (status == ERROR_CODE_SUCCESS) return true;
  printf("[GATT-AUDIT] %s request failed status=0x%02x\r\n", operation, status);
  step = AUDIT_ERROR;
  return false;
}

static void next_report(void);
static void read_cccd(void) {
  audited_report_t *r = &reports[report_index];
  if (!r->cccd_handle) { next_report(); return; }
  step = AUDIT_READ_CCCD;
  audit_query(gatt_client_read_characteristic_descriptor_using_descriptor_handle(
                  audit_handler, con_handle, r->cccd_handle), "read CCCD");
}
static void read_reference(void) {
  audited_report_t *r = &reports[report_index];
  if (!r->reference_handle) { read_cccd(); return; }
  step = AUDIT_READ_REFERENCE;
  audit_query(gatt_client_read_characteristic_descriptor_using_descriptor_handle(
                  audit_handler, con_handle, r->reference_handle), "read Report Reference");
}
static void next_report(void) {
  if (++report_index >= count) {
    uint8_t inputs = 0, enabled = 0;
    for (uint8_t i = 0; i < count; i++) {
      audited_report_t *r = &reports[i];
      if (r->reference_read && r->report_type == 1) {
        inputs++;
        if (r->cccd_read && r->cccd_value == 1) enabled++;
      }
    }
    printf("[GATT-AUDIT] complete reports=%u input=%u cccd_enabled=%u\r\n",
           count, inputs, enabled);
    input_confirmed = inputs > 0 && enabled == inputs;
    printf("[SUBSCRIBE] %s by CCCD readback\r\n",
           input_confirmed ? "confirmed" : "NOT confirmed");
    step = AUDIT_DONE;
    return;
  }
  step = AUDIT_DESCRIPTORS;
  audit_query(gatt_client_discover_characteristic_descriptors(
                  audit_handler, con_handle, &reports[report_index].characteristic),
              "discover descriptors");
}

void gatt_audit_start(hci_con_handle_t connection) {
  if (connection == HCI_CON_HANDLE_INVALID ||
      (step != AUDIT_IDLE && step != AUDIT_ERROR)) return;
  gatt_audit_reset();
  con_handle = connection;
  step = AUDIT_SERVICE;
  printf("[GATT-AUDIT] read-only HIDS/Report Reference/CCCD verification started\r\n");
  audit_query(gatt_client_discover_primary_services_by_uuid16(
                  audit_handler, con_handle, UUID_HIDS), "discover HIDS");
}

static void audit_handler(uint8_t packet_type, uint16_t channel,
                          uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || step == AUDIT_IDLE ||
      step == AUDIT_DONE || step == AUDIT_ERROR) return;
  switch (hci_event_packet_get_type(packet)) {
  case GATT_EVENT_SERVICE_QUERY_RESULT:
    if (step == AUDIT_SERVICE && !service_found) {
      gatt_event_service_query_result_get_service(packet, &service);
      service_found = true;
      printf("[GATT-AUDIT] HIDS handles=0x%04x..0x%04x\r\n",
             service.start_group_handle, service.end_group_handle);
    }
    break;
  case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
    if (step == AUDIT_CHARACTERISTICS && count == MAX_REPORT_CHARACTERISTICS) {
      printf("[GATT-AUDIT] report characteristic limit=%u exceeded\r\n",
             MAX_REPORT_CHARACTERISTICS);
      step = AUDIT_ERROR;
    } else if (step == AUDIT_CHARACTERISTICS) {
      audited_report_t *r = &reports[count++];
      gatt_event_characteristic_query_result_get_characteristic(
          packet, &r->characteristic);
      printf("[GATT-AUDIT] Report value=0x%04x props=0x%04x\r\n",
             r->characteristic.value_handle, r->characteristic.properties);
    }
    break;
  case GATT_EVENT_ALL_CHARACTERISTIC_DESCRIPTORS_QUERY_RESULT:
    if (step == AUDIT_DESCRIPTORS) {
      gatt_client_characteristic_descriptor_t d;
      gatt_event_all_characteristic_descriptors_query_result_get_characteristic_descriptor(packet, &d);
      if (d.uuid16 == UUID_REPORT_REFERENCE)
        reports[report_index].reference_handle = d.handle;
      else if (d.uuid16 == UUID_CCCD)
        reports[report_index].cccd_handle = d.handle;
    }
    break;
  case GATT_EVENT_CHARACTERISTIC_DESCRIPTOR_QUERY_RESULT:
    if (step == AUDIT_READ_REFERENCE) {
      uint16_t n = gatt_event_characteristic_descriptor_query_result_get_descriptor_length(packet);
      if (n == 2) {
        const uint8_t *d = gatt_event_characteristic_descriptor_query_result_get_descriptor(packet);
        reports[report_index].report_id = d[0];
        reports[report_index].report_type = d[1];
        reports[report_index].reference_read = true;
      }
    } else if (step == AUDIT_READ_CCCD) {
      uint16_t n = gatt_event_characteristic_descriptor_query_result_get_descriptor_length(packet);
      if (n == 2) {
        const uint8_t *d = gatt_event_characteristic_descriptor_query_result_get_descriptor(packet);
        reports[report_index].cccd_value = little_endian_read_16(d, 0);
        reports[report_index].cccd_read = true;
      }
    }
    break;
  case GATT_EVENT_QUERY_COMPLETE: {
    uint8_t att = gatt_event_query_complete_get_att_status(packet);
    if (att != ATT_ERROR_SUCCESS) {
      printf("[GATT-AUDIT] ATT error step=%u status=0x%02x\r\n", step, att);
      step = AUDIT_ERROR;
      return;
    }
    switch (step) {
    case AUDIT_SERVICE:
      if (!service_found) {
        printf("[GATT-AUDIT] HIDS not found\r\n");
        step = AUDIT_ERROR;
      } else {
        step = AUDIT_CHARACTERISTICS;
        audit_query(gatt_client_discover_characteristics_for_service_by_uuid16(
                        audit_handler, con_handle, &service, UUID_REPORT),
                    "discover Report characteristics");
      }
      break;
    case AUDIT_CHARACTERISTICS:
      if (count == 0) {
        printf("[GATT-AUDIT] no Report characteristics\r\n");
        step = AUDIT_ERROR;
      } else {
        report_index = 0;
        step = AUDIT_DESCRIPTORS;
        audit_query(gatt_client_discover_characteristic_descriptors(
                        audit_handler, con_handle, &reports[report_index].characteristic),
                    "discover descriptors");
      }
      break;
    case AUDIT_DESCRIPTORS:
      printf("[GATT-AUDIT] value=0x%04x ref=0x%04x cccd=0x%04x\r\n",
             reports[report_index].characteristic.value_handle,
             reports[report_index].reference_handle, reports[report_index].cccd_handle);
      read_reference();
      break;
    case AUDIT_READ_REFERENCE:
      printf("[GATT-AUDIT] ref id=%u type=%u read=%u\r\n",
             reports[report_index].report_id, reports[report_index].report_type,
             reports[report_index].reference_read);
      read_cccd();
      break;
    case AUDIT_READ_CCCD:
      printf("[GATT-AUDIT] cccd=0x%04x read=%u\r\n",
             reports[report_index].cccd_value, reports[report_index].cccd_read);
      next_report();
      break;
    default: break;
    }
    break;
  }
  default: break;
  }
}
