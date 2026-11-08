#include "classic_8bitdo_host.h"
#include "controllers/switch_8bitdo.h"
#include "controllers/switch_player_led.h"
#include "led/status_led.h"
#include "core/input_owner.h"
#include "btstack.h"
#include "classic/hid_host.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "btstack_tlv.h"
#include "pico/time.h"
#if !BRIDGE_USB_XINPUT
#include "pico/stdio_usb.h"
#endif
#include <stdio.h>
#include <string.h>

enum { CLASSIC_DESCRIPTOR_MAX = 512 };
static uint8_t descriptor[CLASSIC_DESCRIPTOR_MAX];
static uint16_t hid_cid;
static uint32_t classic_generation;
static bd_addr_t selected_peer;
static bd_addr_t known_peer;
static bool known_peer_valid, descriptor_ready, inquiry_active, hid_connected;
static const btstack_tlv_t *classic_tlv;
static void *classic_tlv_context;
static const btstack_link_key_db_t *classic_key_db;
enum { CLASSIC_PEER_TAG = 0x50433842u };
typedef struct { uint8_t version; bd_addr_t address; } classic_peer_record_t;
static btstack_packet_callback_registration_t hci_registration;
static btstack_timer_source_t scan_timer;
static btstack_timer_source_t reconnect_timer;
static uint32_t directed_attempts, directed_failures, incoming_accepted;
static uint8_t last_acl_status = 0xff, last_auth_status = 0xff;
static uint8_t last_encrypt_status = 0xff, last_encrypt_enabled;
static void reconnect_known(btstack_timer_source_t *ts);
static void schedule_reconnect(uint32_t ms) {
  btstack_run_loop_remove_timer(&reconnect_timer);
  btstack_run_loop_set_timer(&reconnect_timer, ms);
  btstack_run_loop_add_timer(&reconnect_timer);
}
static void remember_peer(const bd_addr_t address) {
  if (!classic_tlv || !classic_key_db) return;
  link_key_t key;
  link_key_type_t type;
  if (classic_key_db->get_link_key((uint8_t *)address, key, &type) != 1) {
    printf("[BT-CLASSIC] HID open but no stored link key yet; identity not committed\r\n");
    return;
  }
  if (known_peer_valid && memcmp(known_peer, address, sizeof(known_peer)) == 0) return;
  classic_peer_record_t record = { .version = 1 };
  memcpy(record.address, address, sizeof(record.address));
  if (classic_tlv->store_tag(classic_tlv_context, CLASSIC_PEER_TAG,
                             (const uint8_t *)&record, sizeof(record)) == 0) {
    memcpy(known_peer, address, sizeof(known_peer));
    known_peer_valid = true;
    printf("[BT-CLASSIC] bonded identity stored for directed reconnect\r\n");
  } else printf("[BT-CLASSIC] failed to store bonded identity\r\n");
}
static void load_peer(void) {
  btstack_tlv_get_instance(&classic_tlv, &classic_tlv_context);
  if (!classic_tlv) return;
  classic_key_db = btstack_link_key_db_tlv_get_instance(classic_tlv, classic_tlv_context);
  classic_peer_record_t record;
  if (classic_tlv->get_tag(classic_tlv_context, CLASSIC_PEER_TAG,
                           (uint8_t *)&record, sizeof(record)) != (int)sizeof(record) ||
      record.version != 1) {
    /* Preserve a bond made by an earlier UF2 that did not store the address. */
    btstack_link_key_iterator_t it;
    if (classic_key_db->iterator_init(&it) != 1) return;
    bd_addr_t only = {0}, candidate;
    link_key_t existing_key;
    link_key_type_t existing_type;
    unsigned count = 0;
    while (classic_key_db->iterator_get_next(&it, candidate, existing_key,
                                              &existing_type)) {
      memcpy(only, candidate, sizeof(only));
      count++;
    }
    classic_key_db->iterator_done(&it);
    if (count == 1) {
      printf("[BT-CLASSIC] migrating sole legacy Classic link-key identity\r\n");
      remember_peer(only);
    } else if (count > 1) {
      printf("[BT-CLASSIC] multiple legacy keys without selected identity; awaiting Pair\r\n");
    }
    return;
  }
  link_key_t key;
  link_key_type_t type;
  if (classic_key_db->get_link_key(record.address, key, &type) != 1) {
    printf("[BT-CLASSIC] stored identity has no link key; awaiting explicit Pair\r\n");
    return;
  }
  memcpy(known_peer, record.address, sizeof(known_peer));
  known_peer_valid = true;
  printf("[BT-CLASSIC] restored bonded peer %s (key present)\r\n",
         bd_addr_to_str(known_peer));
}
static void reconnect_known(btstack_timer_source_t *ts) {
  (void)ts;
  if (!known_peer_valid || hid_connected) return;
  if (hid_cid) {
    printf("[BT-CLASSIC] directed connection timeout cid=%u\r\n", hid_cid);
    directed_failures++;
    hid_host_disconnect(hid_cid);
    schedule_reconnect(2000);
    return;
  }
  if (inquiry_active) { gap_inquiry_stop(); inquiry_active = false; }
  memcpy(selected_peer, known_peer, sizeof(selected_peer));
  uint8_t status = hid_host_connect(known_peer, HID_PROTOCOL_MODE_REPORT, &hid_cid);
  directed_attempts++;
  printf("[BT-CLASSIC] directed reconnect attempt=%lu status=0x%02x addr=%s\r\n",
         (unsigned long)directed_attempts, status, bd_addr_to_str(known_peer));
  if (status == ERROR_CODE_SUCCESS) schedule_reconnect(8000);
  else { directed_failures++; hid_cid = 0; schedule_reconnect(5000); }
}
static switch_parser_stats_t parser_stats;
static const char *diagnostic_stage = "boot";
static uint16_t diagnostic_descriptor_len;
static uint8_t descriptor_copy[CLASSIC_DESCRIPTOR_MAX];
static uint16_t descriptor_copy_len;
static bool descriptor_dumped;
static uint32_t id_3f, id_30, id_21, id_other, rejected_short, rejected_unknown;
static uint32_t routed_reports;
static btstack_timer_source_t diagnostic_timer;
enum { CAPTURE_SLOTS = 32, CAPTURE_MAX_BYTES = 1024 };
typedef struct { uint32_t ms; uint16_t len; uint16_t copied; uint8_t bytes[CAPTURE_MAX_BYTES]; } capture_t;
static capture_t captures[CAPTURE_SLOTS];
static uint8_t capture_read, capture_write, capture_count;
static uint32_t capture_omitted, capture_oversize, capture_total;
static uint32_t last_capture_ms;
static uint8_t last_capture_id;
static uint16_t last_capture_len;
static uint8_t last_button0, last_button1, last_hat;
static uint32_t init_sent_ms;
static uint8_t init_send_status;
static bool init_pending, init_reply_seen, init_ack_valid, init_timed_out;
static int8_t player_led_slot = -1;
static bool player_led_pending, player_led_waiting, player_led_ack_valid;
static uint8_t player_led_attempts, player_led_sequence, player_led_send_status;
static uint32_t player_led_sent_ms;
static bool first_state_seen;
static controller_state_t first_state;
static uint32_t last_summary_ms;
static void capture_report(const uint8_t *data, uint16_t len) {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  uint8_t id = len ? data[0] : 0;
  capture_total++;
  bool controls_changed = len >= 5 && id == 0xa1 && data[1] == 0x3f &&
      (data[2] != last_button0 || data[3] != last_button1 || data[4] != last_hat);
  if (!controls_changed && id == last_capture_id && len == last_capture_len &&
      (uint32_t)(now - last_capture_ms) < 500) return;
  if (capture_count == CAPTURE_SLOTS) { capture_omitted++; return; }
  capture_t *c = &captures[capture_write];
  c->ms = now; c->len = len;
  c->copied = len <= CAPTURE_MAX_BYTES ? len : CAPTURE_MAX_BYTES;
  if (c->copied) memcpy(c->bytes, data, c->copied);
  if (len > CAPTURE_MAX_BYTES) capture_oversize++;
  capture_write = (capture_write + 1) % CAPTURE_SLOTS;
  capture_count++;
  last_capture_ms = now; last_capture_id = id; last_capture_len = len;
  if (len >= 5 && id == 0xa1 && data[1] == 0x3f) {
    last_button0 = data[2]; last_button1 = data[3]; last_hat = data[4];
  }
}
static void diagnostic_tick(btstack_timer_source_t *ts) {
  (void)ts;
#if !BRIDGE_USB_XINPUT
  if (!stdio_usb_connected()) goto reschedule;
#endif
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (player_led_pending && hid_connected && descriptor_ready && hid_cid) {
    if (player_led_waiting && (uint32_t)(now - player_led_sent_ms) > 1000)
      player_led_waiting = false;
    if (!player_led_waiting && player_led_attempts < 3) {
      uint8_t payload[SWITCH_PLAYER_LED_REPORT_LEN];
      switch_player_led_build((uint8_t)player_led_slot, player_led_sequence++, payload);
      player_led_send_status = hid_host_send_report(hid_cid, 1, payload, sizeof(payload));
      player_led_attempts++;
      player_led_sent_ms = now;
      player_led_waiting = player_led_send_status == ERROR_CODE_SUCCESS;
      printf("[8BITDO-LED] report_id=01 subcmd=30 mask=%02x attempt=%u send=0x%02x\r\n",
             payload[10], player_led_attempts, player_led_send_status);
      if (!player_led_waiting && player_led_attempts == 3) player_led_pending = false;
    } else if (!player_led_waiting && player_led_attempts == 3) {
      player_led_pending = false;
      printf("[8BITDO-LED] no ACK after 3 attempts\r\n");
    }
  }
  if (init_pending && (uint32_t)(now - init_sent_ms) > 3000) {
    init_pending = false; init_timed_out = true;
  }
  if ((uint32_t)(now - last_summary_ms) >= 5000) {
  last_summary_ms = now;
  printf("[8BITDO-DIAG] stage=%s cid=%u bonded=%u connected=%u reconnect=%lu failures=%lu incoming=%lu acl=0x%02x auth=0x%02x encrypt=0x%02x/%u descriptor=%u descriptor_len=%u rx=%lu ok=%lu rejected=%lu id3f=%lu id30=%lu id21=%lu other=%lu short=%lu unknown=%lu routed=%lu captured=%lu buffered=%u omitted=%lu oversize=%lu init_status=%u init_reply_seen=%u init_ack_valid=%u init_timeout=%u player=%d led_ack=%u led_attempts=%u\r\n",
         diagnostic_stage, hid_cid, known_peer_valid, hid_connected,
         (unsigned long)directed_attempts, (unsigned long)directed_failures,
         (unsigned long)incoming_accepted, last_acl_status, last_auth_status,
         last_encrypt_status, last_encrypt_enabled,
         descriptor_ready, diagnostic_descriptor_len,
         (unsigned long)parser_stats.reports_received,
         (unsigned long)parser_stats.reports_processed,
         (unsigned long)parser_stats.reports_rejected,
         (unsigned long)id_3f, (unsigned long)id_30, (unsigned long)id_21,
         (unsigned long)id_other, (unsigned long)rejected_short,
         (unsigned long)rejected_unknown, (unsigned long)routed_reports,
         (unsigned long)capture_total, capture_count,
         (unsigned long)capture_omitted, (unsigned long)capture_oversize,
         init_send_status, init_reply_seen, init_ack_valid, init_timed_out,
         player_led_slot + 1, player_led_ack_valid, player_led_attempts);
  }
  if (descriptor_copy_len && !descriptor_dumped) {
    printf("[8BITDO-DESCRIPTOR] len=%u bytes=", descriptor_copy_len);
    for (uint16_t i = 0; i < descriptor_copy_len; i++) printf("%02X", descriptor_copy[i]);
    printf("\r\n");
    descriptor_dumped = true;
  }
  if (capture_count) {
    capture_t *c = &captures[capture_read];
    uint8_t report_id = c->len ? c->bytes[0] : 0;
    if (report_id == 0xa1 && c->len > 1) report_id = c->bytes[1];
    printf("[8BITDO-RAW] ms=%lu id=%02X len=%u bytes=", (unsigned long)c->ms,
           report_id, c->len);
    for (unsigned i = 0; i < c->copied; ++i) printf("%02X", c->bytes[i]);
    printf("%s\r\n", c->len > c->copied ? " TRUNCATED" : "");
    capture_read = (capture_read + 1) % CAPTURE_SLOTS;
    capture_count--;
  }
  if (first_state_seen && (uint32_t)(now - last_summary_ms) < 100) {
    printf("[8BITDO-STATE] buttons=%04x dpad=%u lx=%ld ly=%ld rx=%ld ry=%ld lt=%ld rt=%ld\r\n",
           first_state.buttons, first_state.dpad, (long)first_state.left_x,
           (long)first_state.left_y, (long)first_state.right_x,
           (long)first_state.right_y, (long)first_state.left_trigger,
           (long)first_state.right_trigger);
    first_state_seen = false;
  }
reschedule:
  btstack_run_loop_set_timer(&diagnostic_timer, 100);
  btstack_run_loop_add_timer(&diagnostic_timer);
}
static bd_addr_t name_query_addr;
static uint8_t name_query_page_mode;
static uint16_t name_query_clock_offset;
static bool name_query_pending;

static bool is_8bitdo(const uint8_t *name, uint8_t len) {
  char text[64]; if (len >= sizeof(text)) len = sizeof(text)-1;
  memcpy(text, name, len); text[len] = 0;
  for (unsigned i=0; i<len; i++) if (text[i]>='A' && text[i]<='Z') text[i]+=32;
  return strstr(text, "8bitdo") != NULL || strcmp(text,"pro controller")==0;
}
static uint8_t bounded_name_len(const char *name) {
  uint8_t n = 0; while (n < 63 && name[n]) n++; return n;
}
static void scan_again(btstack_timer_source_t *ts) {
  (void)ts;
  if (known_peer_valid) { schedule_reconnect(1); return; }
  if (!hid_cid && !inquiry_active) {
    status_led_set(LED_SCANNING);
    diagnostic_stage = "scanning";
    inquiry_active = gap_inquiry_start(5) == ERROR_CODE_SUCCESS;
    printf("[BT-CLASSIC] scanning HID names for 8BitDo\r\n");
  }
}
static void connect_if_8bitdo(bd_addr_t addr, const char *name, uint8_t len) {
  if (hid_cid || !is_8bitdo((const uint8_t *)name, len)) return;
  gap_inquiry_stop(); inquiry_active = false; name_query_pending = false;
  status_led_set(LED_PAIRING);
  diagnostic_stage = "connecting";
  memcpy(selected_peer, addr, sizeof(selected_peer));
  uint8_t s = hid_host_connect(addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
  printf("[BT-CLASSIC] 8BitDo selected %s connect=0x%02x\r\n", bd_addr_to_str(addr), s);
  if (s == ERROR_CODE_SUCCESS) schedule_reconnect(8000);
  else { hid_cid = 0; btstack_run_loop_set_timer(&scan_timer, 1000); btstack_run_loop_add_timer(&scan_timer); }
}
static void packet_handler(uint8_t type, uint16_t channel, uint8_t *p, uint16_t size) {
  (void)channel; (void)size;
  if (type != HCI_EVENT_PACKET) return;
  uint8_t event = hci_event_packet_get_type(p);
  if (event == HCI_EVENT_USER_CONFIRMATION_REQUEST) {
    bd_addr_t peer;
    hci_event_user_confirmation_request_get_bd_addr(p, peer);
    if ((hid_cid && memcmp(peer, selected_peer, sizeof(peer)) == 0) ||
        (known_peer_valid && memcmp(peer, known_peer, sizeof(peer)) == 0)) {
      printf("[BT-CLASSIC] confirming SSP for selected controller\r\n");
      gap_ssp_confirmation_response(peer);
    } else {
      gap_ssp_confirmation_negative(peer);
    }
    return;
  }
  if (event == HCI_EVENT_CONNECTION_COMPLETE) {
    last_acl_status = hci_event_connection_complete_get_status(p);
    bd_addr_t addr; hci_event_connection_complete_get_bd_addr(p, addr);
    printf("[BT-CLASSIC] ACL connection status=0x%02x peer=%s\r\n",
           hci_event_connection_complete_get_status(p), bd_addr_to_str(addr));
  }
  if (event == HCI_EVENT_AUTHENTICATION_COMPLETE) {
    last_auth_status = hci_event_authentication_complete_get_status(p);
    printf("[BT-CLASSIC] authentication handle=0x%04x status=0x%02x\r\n",
           hci_event_authentication_complete_get_connection_handle(p),
           hci_event_authentication_complete_get_status(p));
    if (hci_event_authentication_complete_get_status(p) == ERROR_CODE_SUCCESS && hid_cid)
      remember_peer(selected_peer);
  }
  if (event == HCI_EVENT_ENCRYPTION_CHANGE) {
    last_encrypt_status = hci_event_encryption_change_get_status(p);
    last_encrypt_enabled = hci_event_encryption_change_get_encryption_enabled(p);
    printf("[BT-CLASSIC] encryption handle=0x%04x status=0x%02x enabled=%u\r\n",
           hci_event_encryption_change_get_connection_handle(p),
           hci_event_encryption_change_get_status(p),
           hci_event_encryption_change_get_encryption_enabled(p));
  }
  if (event == BTSTACK_EVENT_STATE && btstack_event_state_get_state(p) == HCI_STATE_WORKING) {
    gap_connectable_control(1);
    gap_discoverable_control(0);
    load_peer();
    if (known_peer_valid) reconnect_known(NULL); else scan_again(NULL);
    return;
  }
  if (event == GAP_EVENT_INQUIRY_RESULT) {
    if (gap_event_inquiry_result_get_name_available(p)) {
      const uint8_t *name = gap_event_inquiry_result_get_name(p);
      uint8_t len = gap_event_inquiry_result_get_name_len(p);
      printf("[BT-CLASSIC] found %.*s\r\n", len, name);
      bd_addr_t addr; gap_event_inquiry_result_get_bd_addr(p, addr);
      connect_if_8bitdo(addr, (const char *)name, len);
    } else if (!name_query_pending) {
      gap_event_inquiry_result_get_bd_addr(p, name_query_addr);
      name_query_page_mode = gap_event_inquiry_result_get_page_scan_repetition_mode(p);
      name_query_clock_offset = gap_event_inquiry_result_get_clock_offset(p);
      name_query_pending = gap_remote_name_request(name_query_addr,
          name_query_page_mode, name_query_clock_offset) == ERROR_CODE_SUCCESS;
      printf("[BT-CLASSIC] inquiry result has no name; remote-name request=%u\r\n", name_query_pending);
    }
    return;
  }
  if (event == HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE) {
    name_query_pending = false;
    if (hci_event_remote_name_request_complete_get_status(p) == ERROR_CODE_SUCCESS) {
      bd_addr_t addr; hci_event_remote_name_request_complete_get_bd_addr(p, addr);
      const char *name = hci_event_remote_name_request_complete_get_remote_name(p);
      connect_if_8bitdo(addr, name, size >= 72 ? bounded_name_len(name) : (size > 9 ? size-9 : 0));
    }
    return;
  }
  if (event == GAP_EVENT_INQUIRY_COMPLETE) {
    inquiry_active = false;
    if (!hid_cid) {
      btstack_run_loop_remove_timer(&scan_timer);
      btstack_run_loop_set_timer(&scan_timer, 1000);
      btstack_run_loop_add_timer(&scan_timer);
    }
    return;
  }
  if (event != HCI_EVENT_HID_META) return;
  switch (hci_event_hid_meta_get_subevent_code(p)) {
    case HID_SUBEVENT_INCOMING_CONNECTION: {
      uint16_t incoming_cid = hid_subevent_incoming_connection_get_hid_cid(p);
      bd_addr_t addr; hid_subevent_incoming_connection_get_address(p, addr);
      uint8_t status = hid_subevent_incoming_connection_get_status(p);
      bool allowed = status == ERROR_CODE_SUCCESS && !hid_connected &&
          (!known_peer_valid || memcmp(addr, known_peer, sizeof(addr)) == 0);
      printf("[BT-CLASSIC] incoming HID peer=%s cid=%u status=0x%02x allowed=%u\r\n",
             bd_addr_to_str(addr), incoming_cid, status, allowed);
      if (status != ERROR_CODE_SUCCESS) break;
      if (!allowed) { hid_host_decline_connection(incoming_cid); break; }
      if (inquiry_active) { gap_inquiry_stop(); inquiry_active = false; }
      btstack_run_loop_remove_timer(&reconnect_timer);
      hid_cid = incoming_cid;
      memcpy(selected_peer, addr, sizeof(selected_peer));
      uint8_t result = hid_host_accept_connection(incoming_cid, HID_PROTOCOL_MODE_REPORT);
      printf("[BT-CLASSIC] incoming accept status=0x%02x\r\n", result);
      if (result == ERROR_CODE_SUCCESS) { incoming_accepted++; schedule_reconnect(8000); }
      else { hid_cid = 0; schedule_reconnect(5000); }
      break;
    }
    case HID_SUBEVENT_CONNECTION_OPENED:
      if (hid_subevent_connection_opened_get_hid_cid(p) != hid_cid || !hid_cid) {
        printf("[BT-CLASSIC] ignoring stale HID open cid=%u expected=%u\r\n",
               hid_subevent_connection_opened_get_hid_cid(p), hid_cid);
        break;
      }
      if (hid_subevent_connection_opened_get_status(p) == ERROR_CODE_SUCCESS) {
        hid_cid = hid_subevent_connection_opened_get_hid_cid(p);
        hid_connected = true;
        classic_generation = input_connection_begin(INPUT_8BITDO);
        player_led_slot = -1; player_led_pending = false;
        player_led_waiting = false; player_led_ack_valid = false;
        player_led_attempts = 0;
        btstack_run_loop_remove_timer(&reconnect_timer);
        bd_addr_t opened_addr;
        hid_subevent_connection_opened_get_bd_addr(p, opened_addr);
        memcpy(selected_peer, opened_addr, sizeof(selected_peer));
        remember_peer(opened_addr);
        descriptor_ready = false; status_led_set(LED_PAIRING);
        diagnostic_stage = "hid_connected";
        printf("[BT-CLASSIC] HID connected cid=%u\r\n", hid_cid);
      } else {
        printf("[BT-CLASSIC] HID open failed status=0x%02x\r\n",
               hid_subevent_connection_opened_get_status(p));
        hid_cid = 0;
        hid_connected = false;
        diagnostic_stage = "open_failed";
        status_led_set(LED_RECOVERABLE_ERROR);
        if (known_peer_valid) schedule_reconnect(5000);
        else { btstack_run_loop_remove_timer(&scan_timer); btstack_run_loop_set_timer(&scan_timer, 1000); btstack_run_loop_add_timer(&scan_timer); }
      }
      break;
    case HID_SUBEVENT_DESCRIPTOR_AVAILABLE:
      if (hid_subevent_descriptor_available_get_hid_cid(p) != hid_cid || !hid_connected) break;
      descriptor_ready = hid_subevent_descriptor_available_get_status(p) == ERROR_CODE_SUCCESS;
      diagnostic_stage = descriptor_ready ? "descriptor_ready" : "descriptor_failed";
      if (descriptor_ready) {
        remember_peer(selected_peer);
        const uint8_t *d = hid_descriptor_storage_get_descriptor_data(hid_cid);
        uint16_t n = hid_descriptor_storage_get_descriptor_len(hid_cid);
        diagnostic_descriptor_len = n;
        descriptor_copy_len = n < sizeof(descriptor_copy) ? n : sizeof(descriptor_copy);
        memcpy(descriptor_copy, d, descriptor_copy_len);
        descriptor_dumped = false;
        static const uint8_t full_mode[] = {0,0,1,0x40,0x40,0,1,0x40,0x40,3,0x30};
        init_send_status = hid_host_send_report(hid_cid, 1, full_mode, sizeof(full_mode));
        init_sent_ms = to_ms_since_boot(get_absolute_time());
        init_pending = init_send_status == ERROR_CODE_SUCCESS;
        printf("[8BITDO-INIT] ms=%lu report_id=01 bytes=0000014040000140400330 status=%u\r\n",
               (unsigned long)init_sent_ms, init_send_status);
        if (n > 0) { printf("[BT-CLASSIC] descriptor="); for (uint16_t i=0;i<n && i<32;i++) printf("%02X",d[i]); printf("\r\n"); }
      }
      break;
    case HID_SUBEVENT_REPORT: {
      if (hid_subevent_report_get_hid_cid(p) != hid_cid || !hid_connected) break;
      const uint8_t *r = hid_subevent_report_get_report(p);
      uint16_t n = hid_subevent_report_get_report_len(p);
      diagnostic_stage = "report_received";
      capture_report(r, n);
      if (n && r[0] == 0xa1) { ++r; --n; }
      if (n && r[0] == 0x21) {
        init_reply_seen = true;
        if (n >= 15 && (r[13] & 0x80) && r[14] == 0x03) {
          init_ack_valid = true; init_pending = false;
        }
        int led_ack = switch_player_led_ack(r, n);
        if (player_led_waiting && led_ack) {
          player_led_ack_valid = led_ack > 0;
          player_led_waiting = false;
          if (led_ack > 0) player_led_pending = false;
          printf("[8BITDO-LED] ACK subcmd=30 result=%d player=%d\r\n",
                 led_ack, player_led_slot + 1);
        }
      }
      if (n == 0) rejected_short++;
      else if (r[0] == 0x3f) id_3f++;
      else if (r[0] == 0x30) id_30++;
      else if (r[0] == 0x21) id_21++;
      else { id_other++; rejected_unknown++; }
      controller_state_t state;
      if (!descriptor_ready || !switch_8bitdo_parse_report(r, n, &state, &parser_stats)) {
        if (!descriptor_ready || (n && (r[0] == 0x21 || r[0] == 0x3f))) break;
        input_quiesce_for(INPUT_8BITDO, classic_generation); break;
      }
      int slot = input_report_for(INPUT_8BITDO, classic_generation,
                                  &state, time_us_64());
      if (slot < 0) break;
      if (player_led_slot != slot) {
        player_led_slot = (int8_t)slot;
        player_led_pending = true;
        player_led_waiting = false;
        player_led_ack_valid = false;
        player_led_attempts = 0;
      }
      routed_reports++;
      diagnostic_stage = "report_parsed";
      status_led_set(LED_READY);
      first_state = state; first_state_seen = true;
      break;
    }
    case HID_SUBEVENT_CONNECTION_CLOSED:
      if (hid_subevent_connection_closed_get_hid_cid(p) != hid_cid || !hid_connected) break;
      printf("[BT-CLASSIC] HID disconnected; neutral and retry\r\n");
      input_release_for(INPUT_8BITDO, classic_generation);
      hid_cid = 0; hid_connected = false; descriptor_ready = false;
      player_led_pending = false; player_led_waiting = false; player_led_slot = -1;
      diagnostic_stage = "disconnected";
      if (known_peer_valid) schedule_reconnect(5000);
      else { btstack_run_loop_set_timer(&scan_timer, 1000); btstack_run_loop_add_timer(&scan_timer); }
      break;
    default: break;
  }
}
void classic_8bitdo_host_init(void) {
  hid_host_init(descriptor, sizeof(descriptor));
  hid_host_register_packet_handler(packet_handler);
  hci_registration.callback = packet_handler; hci_add_event_handler(&hci_registration);
  hci_set_master_slave_policy(HCI_ROLE_MASTER);
  btstack_run_loop_set_timer_handler(&scan_timer, scan_again);
  btstack_run_loop_set_timer_handler(&reconnect_timer, reconnect_known);
  btstack_run_loop_set_timer_handler(&diagnostic_timer, diagnostic_tick);
  btstack_run_loop_set_timer(&diagnostic_timer, 100);
  btstack_run_loop_add_timer(&diagnostic_timer);
  hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
}
