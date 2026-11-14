#include "ble_host.h"
#if BRIDGE_CLASSIC_8BITDO
#include "classic_8bitdo_host.h"
#include "core/input_owner.h"
#endif
#include "bluetooth/gatt_audit.h"
#include "bluetooth/reconnect_policy.h"
#include "ble/le_device_db.h"
#include "ble/gatt-service/hids_host.h"
#include "ble/gatt_client.h"
#include "btstack.h"
#include "btstack_tlv.h"
#include "controllers/hid_gamepad.h"
#include "diagnostics/diag.h"
#include "led/status_led.h"
#include "usb/usb_output.h"
#include "pico/time.h"
#include "pico/stdio.h"
#include <stdio.h>
#include <string.h>
#if BRIDGE_CLASSIC_8BITDO && BRIDGE_XINPUT_SLOTS == 2
static uint32_t xbox_generation;
#undef usb_output_submit
#undef usb_output_disconnect
#define usb_output_submit(s,t) input_report_for(INPUT_XBOX,xbox_generation,s,t)
#define usb_output_disconnect() input_release_for(INPUT_XBOX,xbox_generation)
#endif

#define REPORT_MAP_MAX 512
#define TLV_TAG_CONTROLLER 0x50474342u
typedef struct {
  bd_addr_t address;
  bd_addr_type_t type;
} bonded_peer_t;
static btstack_packet_callback_registration_t hci_registration, sm_registration;
static btstack_timer_source_t reconnect_timer;
static btstack_timer_source_t connect_timer, command_timer;
static btstack_timer_source_t stats_timer;
static hci_con_handle_t connection = HCI_CON_HANDLE_INVALID;
static bd_addr_t peer_address;
static bd_addr_type_t peer_type;
static bool connected, connecting, scanning, lookup_pending;
static bool hids_started, hids_ready;
static bool legacy_lookup_pending, bond_recovery_needed;
static bd_addr_t lookup_address;
static bd_addr_type_t lookup_type;
static reconnect_policy_t policy;
static bool subscription_requested, subscription_reported, first_report;
static bool first_interpreted_connection, notifications_logged;
static bool parser_ready;
static uint16_t hids_cid;
static uint8_t report_map[REPORT_MAP_MAX];
static hid_gamepad_parser_t parser;
static controller_state_t state;
static const btstack_tlv_t *tlv_impl;
static void *tlv_context;
static uint32_t reports_received, reports_processed, reports_discarded;
static uint32_t parse_errors, connection_errors, last_window_reports;
static uint32_t disconnects, reconnect_attempts, reconnect_successes;
static uint32_t lookup_successes, lookup_failures, connect_timeouts;
static uint32_t connection_started_ms;
static uint32_t lookup_started_ms;
static uint32_t last_disconnect_ms, last_reconnect_ms;
static uint32_t last_window_ms;
static void start_scan(void);
static void schedule_reconnect(uint32_t delay_ms);
static void connect_to(const bd_addr_t address, bd_addr_type_t type);
static void app_handler(uint8_t, uint16_t, uint8_t *, uint16_t);
static void hids_handler(uint8_t, uint16_t, uint8_t *, uint16_t);

static void stats_timeout(btstack_timer_source_t *ts) {
  (void)ts;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  uint32_t elapsed = now - last_window_ms;
  uint32_t delta = reports_received - last_window_reports;
  bool hci_live = connected && connection != HCI_CON_HANDLE_INVALID &&
                  hci_connection_for_handle(connection) != NULL;
  printf("[STATS] link=%u hci_live=%u handle=0x%04x hids=%u "
         "subscribe=%s first=%u rx=%lu ok=%lu "
         "drop=%lu parse_err=%lu conn_err=%lu rate=%lu/%lums audit=%s\r\n",
         connected, hci_live, connection, hids_ready,
         gatt_audit_input_confirmed() ? "confirmed" :
         subscription_reported ? "reported" :
         subscription_requested ? "requested" : "none", first_report,
         (unsigned long)reports_received, (unsigned long)reports_processed,
         (unsigned long)reports_discarded, (unsigned long)parse_errors,
         (unsigned long)connection_errors, (unsigned long)delta,
         (unsigned long)elapsed, gatt_audit_status());
  usb_output_print_stats();
  printf("[RECONNECT] target=%u pairing_allowed=%u scan=%u connecting=%u "
         "pending_lookup=%u attempts=%lu successes=%lu disconnects=%lu "
         "timeouts=%lu resolve_ok=%lu resolve_fail=%lu last_reconnect_ms=%lu backoff_step=%u "
         "recovery_needed=%u\r\n",
         policy.target.valid, policy.pairing_allowed, scanning, connecting,
         lookup_pending, (unsigned long)reconnect_attempts,
         (unsigned long)reconnect_successes, (unsigned long)disconnects,
         (unsigned long)connect_timeouts, (unsigned long)lookup_successes,
         (unsigned long)lookup_failures, (unsigned long)last_reconnect_ms,
         policy.failures, bond_recovery_needed);
  last_window_reports = reports_received;
  last_window_ms = now;
  btstack_run_loop_set_timer(&stats_timer, 5000);
  btstack_run_loop_add_timer(&stats_timer);
}

static void reconnect_timeout(btstack_timer_source_t *ts) {
  (void)ts;
  start_scan();
}
static void schedule_reconnect(uint32_t delay_ms) {
  btstack_run_loop_remove_timer(&reconnect_timer);
  btstack_run_loop_set_timer(&reconnect_timer, delay_ms);
  btstack_run_loop_set_timer_handler(&reconnect_timer, reconnect_timeout);
  btstack_run_loop_add_timer(&reconnect_timer);
}
static void start_scan(void) {
  if (connected || connecting || scanning)
    return;
  if (bond_recovery_needed) {
    status_led_set(LED_RECOVERABLE_ERROR);
    diag_state(DIAG_ERROR, "bond recovery required; BOND RESET over CDC");
    return;
  }
  lookup_pending = false;
  hids_started = false;
  hids_ready = false;
  subscription_requested = false;
  subscription_reported = false;
  first_report = false;
  first_interpreted_connection = false;
  notifications_logged = false;
  parser_ready = false;
  status_led_set(LED_SCANNING);
  printf("[RECONNECT] scanning for %s\r\n",
         policy.target.valid ? "bonded controller" : "Xbox pairing candidate");
  diag_state(DIAG_SCANNING, policy.target.valid ?
             "searching bonded identity, including private addresses" :
             policy.pairing_allowed ? "searching Xbox in pairing mode" :
             "bond recovery required; no new pairing allowed");
  gap_set_scan_parameters(1, 0x60, 0x30);
  gap_start_scan();
  scanning = true;
}

static void connect_timeout(btstack_timer_source_t *ts) {
  (void)ts;
  if (!connecting || connected) return;
  connect_timeouts++;
  connection_errors++;
  connecting = false;
  printf("[RECONNECT] connection timeout after %lums; cancelling\r\n",
         (unsigned long)(to_ms_since_boot(get_absolute_time()) - connection_started_ms));
  uint8_t s = gap_connect_cancel();
  if (s != ERROR_CODE_SUCCESS) diag_error("gap_connect_cancel", s);
  status_led_set(LED_SCANNING);
  schedule_reconnect(reconnect_policy_failed(&policy));
}

static void connect_to(const bd_addr_t address, bd_addr_type_t type) {
  if (connected || connecting) return;
  if (scanning) { gap_stop_scan(); scanning = false; }
  lookup_pending = false;
  memcpy(peer_address, address, sizeof(bd_addr_t));
  peer_type = type;
  connecting = true;
  reconnect_attempts++;
  connection_started_ms = to_ms_since_boot(get_absolute_time());
  diag_device("bonded/pairing candidate", bd_addr_to_str(peer_address),
              (uint8_t)peer_type, 0);
  diag_state(DIAG_CONNECTING, "connecting to current advertising address");
  uint8_t s = gap_connect(peer_address, peer_type);
  if (s != ERROR_CODE_SUCCESS) {
    connecting = false;
    connection_errors++;
    diag_error("gap_connect", s);
    schedule_reconnect(reconnect_policy_failed(&policy));
    return;
  }
  btstack_run_loop_remove_timer(&connect_timer);
  btstack_run_loop_set_timer_handler(&connect_timer, connect_timeout);
  btstack_run_loop_set_timer(&connect_timer, 10000);
  btstack_run_loop_add_timer(&connect_timer);
}

static bool xbox_candidate(const uint8_t *packet) {
  const uint8_t *d = gap_event_advertising_report_get_data(packet);
  uint8_t n = gap_event_advertising_report_get_data_length(packet);
  if (ad_data_contains_uuid16(n, d, 0x1812))
    return true;
  ad_context_t it;
  ad_iterator_init(&it, n, d);
  while (ad_iterator_has_more(&it)) {
    uint8_t type = ad_iterator_get_data_type(&it),
            len = ad_iterator_get_data_len(&it);
    const uint8_t *v = ad_iterator_get_data(&it);
    if ((type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
         type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) &&
        len >= 4) {
      char name[40];
      size_t k = len < 39 ? len : 39;
      memcpy(name, v, k);
      name[k] = 0;
      if (strstr(name, "Xbox") || strstr(name, "XBOX"))
        return true;
    }
    if (type == BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA && len >= 2 &&
        v[0] == 0x06 && v[1] == 0x00)
      return true;
    ad_iterator_next(&it);
  }
  return false;
}
static void begin_hids(void) {
  if (hids_started)
    return;
  hids_started = true;
  diag_state(DIAG_DISCOVERING_GATT, "BTstack HOGP client discovers HID 0x1812, "
                                    "Report Map, report refs and CCCDs");
  uint8_t s = hids_host_connect(connection, hids_handler,
                                HID_PROTOCOL_MODE_REPORT, &hids_cid);
  if (s != ERROR_CODE_SUCCESS) {
    connection_errors++;
    status_led_set(LED_RECOVERABLE_ERROR);
    diag_error("hids_host_connect", s);
    gap_disconnect(connection);
  }
}
static void hids_handler(uint8_t type, uint16_t ch, uint8_t *p, uint16_t size) {
  (void)ch;
  if ((type != HCI_EVENT_PACKET && type != HCI_EVENT_GATTSERVICE_META) ||
      hci_event_packet_get_type(p) != HCI_EVENT_GATTSERVICE_META)
    return;
  switch (hci_event_gattservice_meta_get_subevent_code(p)) {
  case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
    uint8_t s = gattservice_subevent_hid_service_connected_get_status(p);
    if (s != ERROR_CODE_SUCCESS) {
      connection_errors++;
      status_led_set(LED_RECOVERABLE_ERROR);
      diag_error("HID service discovery", s);
      gap_disconnect(connection);
      return;
    }
    hids_ready = true;
    reconnect_policy_succeeded(&policy);
    printf("[RECONNECT] HOGP restored after %lums; awaiting HID reports\r\n",
           (unsigned long)(to_ms_since_boot(get_absolute_time()) - connection_started_ms));
    hids_cid = gattservice_subevent_hid_service_connected_get_hids_cid(p);
#if BRIDGE_CLASSIC_8BITDO && BRIDGE_XINPUT_SLOTS == 2
    xbox_generation = input_connection_begin(INPUT_XBOX);
#endif
    uint8_t count =
        gattservice_subevent_hid_service_connected_get_num_instances(p);
    printf("[HID] HIDS ready: services=%u\r\n", count);
    for (uint8_t i = 0; i < count; i++) {
      const uint8_t *d =
          hids_host_descriptor_storage_get_descriptor_data(hids_cid, i);
      uint16_t n = hids_host_descriptor_storage_get_descriptor_len(hids_cid, i);
      printf("[HID] Report Map instance=%u length=%u\r\n", i, n);
      diag_report_map(d, n);
      if (i == 0 && d && n) {
        parser_ready = hid_gamepad_parser_reset(&parser, d, n);
        printf("[PARSER] map=%s fields=%u report_ids=%u\r\n",
               parser_ready ? "accepted" : "rejected", parser.field_count,
               parser.has_report_ids);
        if (!parser_ready)
          diag_error("unsupported HID Report Map",
                     ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE);
      }
    }
    diag_state(DIAG_HID_READY, "HIDS discovery complete; awaiting CCCD audit and reports");
    printf("[SUBSCRIBE] HOGP discovery attempted Input Report CCCD writes; "
           "requesting explicit configuration\r\n");
    s = hids_host_enable_notifications(hids_cid);
    if (s == ERROR_CODE_SUCCESS) {
      subscription_requested = true;
      printf("[SUBSCRIBE] requested\r\n");
    } else {
      connection_errors++;
      status_led_set(LED_RECOVERABLE_ERROR);
      diag_error("enable notifications", s);
      gatt_audit_start(connection);
      gap_disconnect(connection);
    }
    break;
  }
  case GATTSERVICE_SUBEVENT_HID_SERVICE_REPORTS_NOTIFICATION: {
    uint8_t configuration =
        gattservice_subevent_hid_service_reports_notification_get_configuration(p);
    subscription_reported =
        configuration == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
    printf("[SUBSCRIBE] HOGP configuration event=0x%02x %s; "
           "CCCD readback follows\r\n", configuration,
           subscription_reported ? "requested enabled" : "reported disabled");
    gatt_audit_start(connection);
    break;
  }
  case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
#if BRIDGE_CLASSIC_8BITDO && BRIDGE_XINPUT_SLOTS == 2
    if (gattservice_subevent_hid_service_disconnected_get_hids_cid(p) != hids_cid) break;
#endif
    hids_ready = false;
    parser_ready = false;
    hids_started = false;
    hids_cid = 0;
    first_interpreted_connection = false;
    notifications_logged = false;
    subscription_requested = false;
    subscription_reported = false;
    gatt_audit_reset();
    usb_output_disconnect();
    status_led_set(LED_RECOVERABLE_ERROR);
    diag_state(DIAG_DISCONNECTED, "HID GATT closed");
    break;
  case GATTSERVICE_SUBEVENT_HID_REPORT: {
#if BRIDGE_CLASSIC_8BITDO && BRIDGE_XINPUT_SLOTS == 2
    if (gattservice_subevent_hid_report_get_hids_cid(p) != hids_cid || !hids_ready) break;
#endif
    uint64_t received_us = time_us_64();
    reports_received++;
    const uint8_t *d = gattservice_subevent_hid_report_get_report(p);
    uint16_t n = gattservice_subevent_hid_report_get_report_len(p);
    uint8_t id = gattservice_subevent_hid_report_get_report_id(p);
    uint8_t service = gattservice_subevent_hid_report_get_service_index(p);
    if (!first_report) {
      first_report = true;
      printf("[REPORT] first received service=%u id=%u len=%u packet_type=0x%02x\r\n",
             service, id, n, type);
    }
    if (!d || n == 0 || size < 9u || n > size - 9u) {
      reports_discarded++;
      if (reports_discarded <= 4)
        printf("[PARSER] invalid HIDS event length event=%u report=%u\r\n", size, n);
      break;
    }
    if (diag_verbose_enabled() && reports_received <= 4) {
      printf("[REPORT] raw service=%u id=%u payload_len=%u\r\n", service, id, n);
      diag_report_raw(d, n);
    }
    const uint8_t *map = hids_host_descriptor_storage_get_descriptor_data(
        hids_cid, service);
    uint16_t mn = hids_host_descriptor_storage_get_descriptor_len(
        hids_cid, service);
    /* BTstack HIDS prepends a synthetic Report ID byte even for maps without IDs. */
    const uint8_t *decode_data = d;
    uint16_t decode_len = n;
    if (parser_ready && !parser.has_report_ids && id == 0 && d[0] == 0) {
      decode_data++;
      decode_len--;
    }
    bool parse_attempted = map && mn && service == 0 && parser_ready;
    if (parse_attempted &&
        hid_gamepad_parser_decode(&parser, id, decode_data, decode_len, &state)) {
      reports_processed++;
      usb_output_submit(&state, received_us);
      status_led_set(LED_READY);
      if (!first_interpreted_connection) {
        first_interpreted_connection = true;
        reconnect_successes++;
        last_reconnect_ms = to_ms_since_boot(get_absolute_time()) -
            (last_disconnect_ms ? last_disconnect_ms : connection_started_ms);
        if (!notifications_logged) {
          notifications_logged = true;
          printf("[RECONNECT] HID notifications active (report received)\r\n");
        }
        printf("[RECONNECT] completed first decoded report; time=%lums\r\n",
               (unsigned long)last_reconnect_ms);
      }
      if (reports_processed == 1)
        printf("[PARSER] first report interpreted id=%u len=%u\r\n", id, n);
      if (diag_verbose_enabled() && (reports_processed % 30) == 1)
        printf("[STATE] buttons=0x%04x hat=%u LX=%ld LY=%ld RX=%ld RY=%ld "
               "LT=%ld RT=%ld Share=%lu\r\n",
               state.buttons, state.dpad, (long)state.left_x,
               (long)state.left_y, (long)state.right_x, (long)state.right_y,
               (long)state.left_trigger, (long)state.right_trigger,
               (unsigned long)state.share);
    } else {
      reports_discarded++;
      if (parse_attempted)
        parse_errors++;
      if (reports_discarded <= 4)
        printf("[PARSER] rejected service=%u id=%u len=%u expected_bits=%u "
               "map=%u ready=%u first_byte=0x%02x\r\n",
               service, id, n, parser_ready ? parser.input_bits[id] : 0,
               mn, parser_ready,
               n ? d[0] : 0);
    }
    break;
  }
  default:
    break;
  }
}

static bool db_identity(int index, bd_addr_t address, bd_addr_type_t *type) {
  if (index < 0 || index >= le_device_db_max_count()) return false;
  int db_type = BD_ADDR_TYPE_UNKNOWN;
  sm_key_t irk;
  le_device_db_info(index, &db_type, address, irk);
  if (db_type != BD_ADDR_TYPE_LE_PUBLIC && db_type != BD_ADDR_TYPE_LE_RANDOM)
    return false;
  *type = (bd_addr_type_t)db_type;
  return true;
}

static void store_identity(const bd_addr_t address, bd_addr_type_t type) {
  if (reconnect_policy_is_target(&policy, address, (uint8_t)type)) return;
  bonded_peer_t b;
  memcpy(b.address, address, 6);
  b.type = type;
  if (tlv_impl && tlv_impl->store_tag(tlv_context, TLV_TAG_CONTROLLER,
                                      (const uint8_t *)&b, sizeof(b)) != 0) {
    connection_errors++;
    printf("[BOND] failed to persist identity tag; LE DB remains authoritative\r\n");
  }
  reconnect_policy_set_target(&policy, address, (uint8_t)type);
  bond_recovery_needed = false;
  printf("[BOND] identity stored addr=%s type=%u\r\n",
         bd_addr_to_str(address), (unsigned)type);
}

static bool store_identity_from_connection(void) {
  bd_addr_t address;
  bd_addr_type_t type;
  int index = sm_le_device_index(connection);
  if (!db_identity(index, address, &type)) {
    printf("[BOND] security succeeded but no valid identity in LE device DB\r\n");
    return false;
  }
  store_identity(address, type);
  return true;
}

static void load_bond(void) {
  bonded_peer_t saved;
  bool have_saved = tlv_impl &&
      tlv_impl->get_tag(tlv_context, TLV_TAG_CONTROLLER, (uint8_t *)&saved,
                        sizeof(saved)) == (int)sizeof(saved);
  if (have_saved) policy.pairing_allowed = false;
  int count = 0, only_index = -1, matching_index = -1;
  for (int i = 0; i < le_device_db_max_count(); ++i) {
    bd_addr_t address;
    bd_addr_type_t type;
    if (!db_identity(i, address, &type)) continue;
    count++;
    only_index = i;
    if (have_saved && type == saved.type &&
        memcmp(address, saved.address, 6) == 0)
      matching_index = i;
  }
  printf("[BOND] saved_tag=%u LE_DB_entries=%d\r\n", have_saved, count);
  if (matching_index >= 0) {
    reconnect_policy_set_target(&policy, saved.address, (uint8_t)saved.type);
    printf("[BOND] stored identity matched LE DB\r\n");
    start_scan();
    return;
  }
  if (!have_saved && count == 1) {
    bd_addr_t address;
    bd_addr_type_t type;
    if (db_identity(only_index, address, &type)) {
      printf("[BOND] sole LE DB identity selected (legacy tag absent)\r\n");
      store_identity(address, type);
      start_scan();
      return;
    }
  }
  if (have_saved && count > 0) {
    legacy_lookup_pending = true;
    lookup_started_ms = to_ms_since_boot(get_absolute_time());
    memcpy(lookup_address, saved.address, 6);
    lookup_type = saved.type;
    int s = sm_address_resolution_lookup((uint8_t)lookup_type, lookup_address);
    if (s == 0) {
      printf("[BOND] resolving legacy advertising address against LE DB\r\n");
      return;
    }
    legacy_lookup_pending = false;
    diag_error("legacy bond resolution queue", (uint8_t)s);
  }
  bond_recovery_needed = have_saved || count > 0;
  policy.pairing_allowed = !bond_recovery_needed;
  if (bond_recovery_needed)
    printf("[BOND] saved identity unavailable; send BOND RESET over CDC for explicit recovery\r\n");
  start_scan();
}
static void sm_handler(uint8_t t, uint16_t c, uint8_t *p, uint16_t n) {
  (void)c;
  (void)n;
  if (t != HCI_EVENT_PACKET)
    return;
  switch (hci_event_packet_get_type(p)) {
  case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED: {
    if (sm_event_identity_resolving_succeeded_get_handle(p) != 0) break;
    bd_addr_t address, identity;
    sm_event_identity_resolving_succeeded_get_address(p, address);
    sm_event_identity_resolving_succeeded_get_identity_address(p, identity);
    uint8_t type = sm_event_identity_resolving_succeeded_get_addr_type(p);
    uint8_t identity_type =
        sm_event_identity_resolving_succeeded_get_identity_addr_type(p);
    if (legacy_lookup_pending && type == lookup_type &&
        memcmp(address, lookup_address, 6) == 0) {
      legacy_lookup_pending = false;
      store_identity(identity, (bd_addr_type_t)identity_type);
      start_scan();
      break;
    }
    if (!lookup_pending || type != lookup_type ||
        memcmp(address, lookup_address, 6) != 0) break;
    lookup_pending = false;
    if (reconnect_policy_is_target(&policy, identity, identity_type)) {
      lookup_successes++;
      printf("[RECONNECT] private address resolved to bonded identity\r\n");
      printf("[RECONNECT] bonded controller detected\r\n");
      connect_to(address, (bd_addr_type_t)type);
    } else {
      lookup_failures++;
      reconnect_policy_reject(&policy, address, type,
                              to_ms_since_boot(get_absolute_time()));
    }
    break;
  }
  case SM_EVENT_IDENTITY_RESOLVING_FAILED: {
    if (sm_event_identity_resolving_failed_get_handle(p) != 0) break;
    bd_addr_t address;
    sm_event_identity_resolving_failed_get_address(p, address);
    uint8_t type = sm_event_identity_resolving_failed_get_addr_type(p);
    if (legacy_lookup_pending && type == lookup_type &&
        memcmp(address, lookup_address, 6) == 0) {
      legacy_lookup_pending = false;
      bond_recovery_needed = true;
      policy.pairing_allowed = false;
      printf("[BOND] legacy address not found; BOND RESET required\r\n");
      start_scan();
      break;
    }
    if (!lookup_pending || type != lookup_type ||
        memcmp(address, lookup_address, 6) != 0) break;
    lookup_pending = false;
    lookup_failures++;
    reconnect_policy_reject(&policy, address, type,
                            to_ms_since_boot(get_absolute_time()));
    break;
  }
  case SM_EVENT_IDENTITY_CREATED: {
    if (sm_event_identity_created_get_handle(p) != connection) break;
    printf("[BOND] identity created in LE DB index=%u; awaiting pairing success\r\n",
           sm_event_identity_created_get_index(p));
    break;
  }
  case SM_EVENT_JUST_WORKS_REQUEST:
    diag_state(DIAG_PAIRING, "confirm Just Works; bonding requested");
    sm_just_works_confirm(sm_event_just_works_request_get_handle(p));
    break;
  case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
    printf("[PAIR] numeric=%lu; auto-confirmed headless\r\n",
           (unsigned long)sm_event_numeric_comparison_request_get_passkey(p));
    sm_numeric_comparison_confirm(
        sm_event_numeric_comparison_request_get_handle(p));
    break;
  case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
    printf("[PAIR] passkey=%06lu\r\n",
           (unsigned long)sm_event_passkey_display_number_get_passkey(p));
    break;
  case SM_EVENT_PAIRING_COMPLETE:
    if (sm_event_pairing_complete_get_handle(p) != connection) break;
    if (sm_event_pairing_complete_get_status(p) == ERROR_CODE_SUCCESS) {
      store_identity_from_connection();
      printf("[PAIR] pairing/bonding success\r\n");
      begin_hids();
    } else {
      connection_errors++;
      status_led_set(LED_RECOVERABLE_ERROR);
      uint8_t status = sm_event_pairing_complete_get_status(p);
      diag_error("pairing", status);
      if (policy.target.valid &&
          (status == ERROR_CODE_PIN_OR_KEY_MISSING ||
           status == ERROR_CODE_AUTHENTICATION_FAILURE)) {
        bond_recovery_needed = true;
        printf("[BOND] paired target rejected key; BOND RESET required\r\n");
      }
      gap_disconnect(connection);
    }
    break;
  case SM_EVENT_REENCRYPTION_COMPLETE:
    if (sm_event_reencryption_complete_get_handle(p) != connection) break;
    if (sm_event_reencryption_complete_get_status(p) == ERROR_CODE_SUCCESS) {
      store_identity_from_connection();
      printf("[PAIR] stored bond restored\r\n");
      begin_hids();
    } else {
      connection_errors++;
      status_led_set(LED_RECOVERABLE_ERROR);
      uint8_t status = sm_event_reencryption_complete_get_status(p);
      diag_error("bond re-encryption", status);
      if (status == ERROR_CODE_PIN_OR_KEY_MISSING ||
          status == ERROR_CODE_AUTHENTICATION_FAILURE) {
        bond_recovery_needed = true;
        printf("[BOND] key rejected; controller off then BOND RESET over CDC\r\n");
      }
      gap_disconnect(connection);
    }
    break;
  default:
    break;
  }
}
static void command_timeout(btstack_timer_source_t *ts) {
  (void)ts;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (legacy_lookup_pending && now - lookup_started_ms > 3000u) {
    legacy_lookup_pending = false;
    bond_recovery_needed = true;
    policy.pairing_allowed = false;
    printf("[BOND] legacy identity resolution timed out; BOND RESET required\r\n");
    start_scan();
  }
  if (lookup_pending && now - lookup_started_ms > 3000u) {
    lookup_pending = false;
    lookup_failures++;
    reconnect_policy_reject(&policy, lookup_address, (uint8_t)lookup_type, now);
    printf("[RECONNECT] address resolution timeout; continuing scan\r\n");
  }
  static char line[24];
  static unsigned length;
  for (unsigned i = 0; i < 64; ++i) {
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) break;
    if (ch == '\r' || ch == '\n') {
      line[length] = 0;
      if (strcmp(line, "BOND RESET") == 0) {
        if (connected || connecting) {
          printf("[BOND] turn controller off before BOND RESET\r\n");
        } else {
          if (scanning) { gap_stop_scan(); scanning = false; }
          lookup_pending = false;
          legacy_lookup_pending = false;
          for (int index = 0; index < le_device_db_max_count(); ++index) {
            bd_addr_t address;
            bd_addr_type_t type;
            if (db_identity(index, address, &type)) le_device_db_remove(index);
          }
          bool tlv_available = tlv_impl != NULL;
          if (tlv_impl) tlv_impl->delete_tag(tlv_context, TLV_TAG_CONTROLLER);
          reconnect_policy_init(&policy, tlv_available && le_device_db_count() == 0);
          bond_recovery_needed = !policy.pairing_allowed;
          usb_output_disconnect();
          if (bond_recovery_needed) {
            printf("[BOND] reset incomplete tlv_available=%u db_count=%d; retry after reboot\r\n",
                   tlv_available, le_device_db_count());
          } else {
            printf("[BOND] cleared explicitly; put Xbox in pairing mode\r\n");
            start_scan();
          }
        }
      } else if (length) {
        printf("[CDC] command: BOND RESET (controller off)\r\n");
      }
      length = 0;
    } else if (ch >= 32 && ch <= 126) {
      if (length + 1 < sizeof(line)) line[length++] = (char)ch;
      else length = 0;
    }
  }
  btstack_run_loop_set_timer(&command_timer, 100);
  btstack_run_loop_add_timer(&command_timer);
}
static void app_handler(uint8_t t, uint16_t c, uint8_t *p, uint16_t n) {
  (void)c;
  (void)n;
  if (t != HCI_EVENT_PACKET)
    return;
  switch (hci_event_packet_get_type(p)) {
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(p) == HCI_STATE_WORKING) {
      btstack_run_loop_set_timer_handler(&stats_timer, stats_timeout);
      last_window_ms = to_ms_since_boot(get_absolute_time());
      btstack_run_loop_set_timer(&stats_timer, 5000);
      btstack_run_loop_add_timer(&stats_timer);
      btstack_tlv_get_instance(&tlv_impl, &tlv_context);
      btstack_run_loop_set_timer_handler(&command_timer, command_timeout);
      btstack_run_loop_set_timer(&command_timer, 100);
      btstack_run_loop_add_timer(&command_timer);
      load_bond();
    }
    break;
  case GAP_EVENT_ADVERTISING_REPORT:
    if (!scanning || connecting || connected || legacy_lookup_pending) break;
    {
      uint8_t event_type = gap_event_advertising_report_get_advertising_event_type(p);
      if (event_type != 0 && event_type != 1 && event_type != 4) break;
      bd_addr_t address;
      gap_event_advertising_report_get_address(p, address);
      bd_addr_type_t type = gap_event_advertising_report_get_address_type(p);
      if (reconnect_policy_allow_advertisement(&policy, address, (uint8_t)type,
                                                xbox_candidate(p))) {
        if (policy.target.valid)
          printf("[RECONNECT] bonded controller detected\r\n");
        connect_to(address, type);
      } else if (!lookup_pending && reconnect_policy_should_resolve(
                     &policy, address, (uint8_t)type,
                     to_ms_since_boot(get_absolute_time()))) {
        lookup_pending = true;
        memcpy(lookup_address, address, 6);
        lookup_type = type;
        lookup_started_ms = to_ms_since_boot(get_absolute_time());
        int s = sm_address_resolution_lookup((uint8_t)type, address);
        if (s != 0) {
          lookup_pending = false;
          lookup_failures++;
          diag_error("address resolution queue", (uint8_t)s);
        }
      }
    }
    break;
  case HCI_EVENT_META_GAP:
    if (hci_event_gap_meta_get_subevent_code(p) ==
        GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
      uint8_t s = gap_subevent_le_connection_complete_get_status(p);
      if (s != ERROR_CODE_SUCCESS) {
        if (!connecting) break; /* expected result of our timeout cancellation */
        connecting = false;
        btstack_run_loop_remove_timer(&connect_timer);
        connection_errors++;
        diag_error("LE connection", s);
        schedule_reconnect(reconnect_policy_failed(&policy));
        break;
      }
      hci_con_handle_t handle =
          gap_subevent_le_connection_complete_get_connection_handle(p);
      if (!connecting) { gap_disconnect(handle); break; }
      btstack_run_loop_remove_timer(&connect_timer);
      connecting = false;
      connection = handle;
      connected = true;
      status_led_set(policy.target.valid ? LED_SCANNING : LED_PAIRING);
      printf("[LINK] connected handle=0x%04x\r\n", connection);
      printf("[RECONNECT] connection established\r\n");
      diag_state(DIAG_PAIRING, policy.target.valid ?
                 "restore stored bond/encryption" : "request pairing/bonding");
      sm_request_pairing(connection);
    }
    break;
  case HCI_EVENT_DISCONNECTION_COMPLETE:
    if (hci_event_disconnection_complete_get_connection_handle(p) != connection) break;
    printf("[BLE] disconnected handle=0x%04x reason=0x%02x\r\n",
           hci_event_disconnection_complete_get_connection_handle(p),
           hci_event_disconnection_complete_get_reason(p));
    disconnects++;
    last_disconnect_ms = to_ms_since_boot(get_absolute_time());
    connected = false;
    connecting = false;
    scanning = false;
    btstack_run_loop_remove_timer(&connect_timer);
    connection = HCI_CON_HANDLE_INVALID;
    hids_ready = false;
    hids_started = false;
    parser_ready = false;
    hids_cid = 0;
    first_interpreted_connection = false;
    notifications_logged = false;
    lookup_pending = false;
    subscription_requested = false;
    subscription_reported = false;
    gatt_audit_reset();
    controller_state_neutral(&state);
    usb_output_disconnect();
    printf("[RECONNECT] starting; USB output neutral\r\n");
    status_led_set(LED_SCANNING);
    diag_state(DIAG_DISCONNECTED, "neutral state; scanning resumes");
    schedule_reconnect(reconnect_policy_failed(&policy));
    break;
  default:
    break;
  }
}
void ble_host_init(void) {
  gatt_audit_reset();
  controller_state_neutral(&state);
  l2cap_init();
#if BRIDGE_CLASSIC_8BITDO
  classic_8bitdo_host_init();
#endif
  sm_init();
  gatt_client_init();
  sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  sm_set_authentication_requirements(SM_AUTHREQ_BONDING |
                                     SM_AUTHREQ_SECURE_CONNECTION);
  hids_host_init(report_map, sizeof(report_map));
  hci_registration.callback = app_handler;
  hci_add_event_handler(&hci_registration);
  sm_registration.callback = sm_handler;
  sm_add_event_handler(&sm_registration);
  connection = HCI_CON_HANDLE_INVALID;
  reconnect_policy_init(&policy, true);
  gap_set_scan_duplicate_filter(false);
  diag_state(DIAG_SCANNING, "powering CYW43/BTstack");
  hci_power_control(HCI_POWER_ON);
}
