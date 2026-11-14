#include "classic_8bitdo_host.h"
#include "classic_attempt_policy.h"
#include "classic_identity_registry.h"
#include "controllers/switch_8bitdo.h"
#include "controllers/switch_player_led.h"
#include "controllers/switch_protocol.h"
#include "controllers/dualshock4.h"
#include "controllers/classic_profile.h"
#include "usb/xinput_report.h"
#include "led/status_led.h"
#include "core/input_owner.h"
#include "btstack.h"
#include "classic/hid_host.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "btstack_tlv.h"
#include "pico/time.h"
#if !BRIDGE_USB_XINPUT
#include "pico/stdio_usb.h"
#include "pico/stdio.h"
#include "pico/error.h"
#endif
#include <stdio.h>
#include <string.h>

enum { CLASSIC_DESCRIPTOR_MAX = 512 };
static uint8_t descriptor[CLASSIC_DESCRIPTOR_MAX];
static uint16_t hid_cid;
static uint32_t classic_generation;
static bd_addr_t selected_peer;
enum {
  CLASSIC_REMEMBERED_MAX = CLASSIC_IDENTITY_CAPACITY,
  CLASSIC_ACTIVE_CAPACITY = 1,
  CLASSIC_USB_SLOTS_CAPACITY = BRIDGE_XINPUT_SLOTS,
  CLASSIC_PAIRING_WINDOW_MS = 60000,
  CLASSIC_UNVERIFIED_LINK_TIMEOUT_MS = 30000
};
#if NVM_NUM_LINK_KEYS < (CLASSIC_REMEMBERED_MAX + 1)
#error "BTstack must reserve four Classic bonds plus one provisional pairing key"
#endif
static bd_addr_t known_peers[CLASSIC_REMEMBERED_MAX];
static uint8_t known_peer_count;
static classic_peer_rotation_t peer_rotation;
static bool descriptor_ready, inquiry_active, hid_connected, classic_input_ready;
static const btstack_tlv_t *classic_tlv;
static void *classic_tlv_context;
static const btstack_link_key_db_t *classic_key_db;
enum {
  CLASSIC_PEER_TAG = 0x50433842u,
  CLASSIC_BOND_TRANSACTION_TAG = 0x50433850u,
  CLASSIC_BOND_TRANSACTION_VERSION = 1,
  CLASSIC_BOND_PREPARED = 1,
  CLASSIC_BOND_COMMITTING = 2
};
typedef struct {
  uint8_t version, phase, has_victim, reserved;
  bd_addr_t candidate, victim;
} classic_bond_transaction_t;
static classic_bond_transaction_t bond_transaction;
static bool bond_transaction_active, pairing_armed;
static uint32_t pairing_deadline_ms, bond_validation_deadline_ms;
static uint32_t bond_commit_retry_ms;
static char command_buffer[80];
static uint8_t command_length;
static bool ds4_capture_mode, ds4_capture_session, ds4_capture_complete_reported;
static uint8_t ds4_capture_remaining, ds4_capture_requested;
static uint32_t ds4_capture_sequence, ds4_capture_omitted;
static uint64_t ds4_capture_last_us;
static uint8_t capture_read, capture_write, capture_count;
static btstack_packet_callback_registration_t hci_registration;
static btstack_timer_source_t scan_timer;
static btstack_timer_source_t reconnect_timer;
static uint32_t directed_attempts, directed_failures, incoming_accepted;
static classic_attempt_policy_t attempt_policy;
static uint32_t attempt_timer_generation;
static uint8_t last_acl_status = 0xff, last_auth_status = 0xff;
static uint8_t last_encrypt_status = 0xff, last_encrypt_enabled;
static hci_con_handle_t classic_acl_handle = HCI_CON_HANDLE_INVALID;
static uint8_t last_disconnect_reason = 0xff;
static bool candidate_explicit_8bitdo, identity_logged;
static bool candidate_ds4_hint, active_is_ds4, ds4_feature_pending;
static uint32_t ds4_feature_status, ds4_feature_responses;
static uint32_t ds4_feature_sent_ms;
static uint32_t ds4_id01, ds4_id11, ds4_id_other;
static ds4_parser_stats_t ds4_stats;
static bool ds4_input_ready;
static const char *diagnostic_stage = "boot";
static void reconnect_known(btstack_timer_source_t *ts);
static void scan_again(btstack_timer_source_t *ts);
static void print_capacity(const char *event);
static int known_peer_index(const bd_addr_t address) {
  for (uint8_t i = 0; i < known_peer_count; i++)
    if (memcmp(known_peers[i], address, sizeof(bd_addr_t)) == 0) return i;
  return -1;
}
static bool peer_has_link_key(const bd_addr_t address) {
  if (!classic_key_db) return false;
  link_key_t key;
  link_key_type_t type;
  return classic_key_db->get_link_key((uint8_t *)address, key, &type) == 1;
}
static uint8_t link_key_count(void) {
  if (!classic_key_db) return 0;
  btstack_link_key_iterator_t it;
  if (classic_key_db->iterator_init(&it) != 1) return 0;
  uint8_t count = 0;
  bd_addr_t address;
  link_key_t key;
  link_key_type_t type;
  while (classic_key_db->iterator_get_next(&it, address, key, &type))
    if (count < UINT8_MAX) ++count;
  classic_key_db->iterator_done(&it);
  return count;
}
static bool store_peer_list(const bd_addr_t *peers, uint8_t count) {
  if (!classic_tlv || count > CLASSIC_REMEMBERED_MAX) return false;
  classic_identity_record_t record;
  classic_identity_record_encode(&record,
      (const uint8_t (*)[CLASSIC_IDENTITY_ADDRESS_BYTES])peers, count);
  return classic_tlv->store_tag(classic_tlv_context, CLASSIC_PEER_TAG,
                                (const uint8_t *)&record, sizeof(record)) == 0;
}
static void store_known_peers(void) {
  if (!store_peer_list(known_peers, known_peer_count))
    printf("[BT-CLASSIC] failed to store remembered identities count=%u capacity=%u\r\n",
           known_peer_count, CLASSIC_REMEMBERED_MAX);
}
static void schedule_reconnect(uint32_t ms) {
  btstack_run_loop_remove_timer(&reconnect_timer);
  btstack_run_loop_set_timer(&reconnect_timer, ms);
  btstack_run_loop_add_timer(&reconnect_timer);
}
static void start_page_scan_window(uint32_t ms, const char *reason) {
  btstack_run_loop_remove_timer(&scan_timer);
  if (inquiry_active) {
    gap_inquiry_stop();
    inquiry_active = false;
    classic_attempt_inquiry_completed(&attempt_policy);
  }
  gap_connectable_control(1);
  classic_attempt_page_scan_started(&attempt_policy);
  diagnostic_stage = "page_scan_wait";
  printf("[BT-CLASSIC] page-scan window reason=%s duration_ms=%lu remembered=%u\r\n",
         reason, (unsigned long)ms, known_peer_count);
  schedule_reconnect(ms);
}
static bool persist_bond_transaction(uint8_t phase) {
  if (!classic_tlv || !bond_transaction_active) return false;
  bond_transaction.version = CLASSIC_BOND_TRANSACTION_VERSION;
  bond_transaction.phase = phase;
  return classic_tlv->store_tag(classic_tlv_context,
      CLASSIC_BOND_TRANSACTION_TAG, (const uint8_t *)&bond_transaction,
      sizeof(bond_transaction)) == 0;
}
static void clear_bond_transaction_tag(void) {
  if (classic_tlv) classic_tlv->delete_tag(classic_tlv_context,
                                           CLASSIC_BOND_TRANSACTION_TAG);
  memset(&bond_transaction, 0, sizeof(bond_transaction));
  bond_transaction_active = false;
  bond_validation_deadline_ms = 0;
  bond_commit_retry_ms = 0;
}
static bool pending_matches(const bd_addr_t address) {
  return bond_transaction_active &&
      memcmp(bond_transaction.candidate, address, sizeof(bd_addr_t)) == 0;
}
static bool finalize_bond_transaction(void) {
  if (!bond_transaction_active ||
      bond_transaction.phase != CLASSIC_BOND_COMMITTING) return false;
  uint8_t next[CLASSIC_REMEMBERED_MAX][6];
  memcpy(next, known_peers, sizeof(next));
  uint8_t next_count = known_peer_count;
  if (!classic_identity_contains((const uint8_t (*)[6])next, next_count,
                                 bond_transaction.candidate)) {
    if (bond_transaction.has_victim) {
      if (!classic_identity_replace_lru(next, &next_count,
              bond_transaction.victim, bond_transaction.candidate)) {
        printf("[BOND] commit blocked: reserved victim no longer matches\r\n");
        return false;
      }
    } else if (!classic_identity_add(next, &next_count,
                                      bond_transaction.candidate)) {
      printf("[BOND] commit blocked: no identity capacity was reserved\r\n");
      return false;
    }
  }
  if (!store_peer_list((const bd_addr_t *)next, next_count)) {
    printf("[BOND] commit pending: identity list write failed; old key retained\r\n");
    return false;
  }
  memcpy(known_peers, next, sizeof(known_peers));
  known_peer_count = next_count;
  classic_peer_rotation_init(&peer_rotation);
  classic_peer_rotation_set_count(&peer_rotation, known_peer_count);
  if (bond_transaction.has_victim) {
    classic_key_db->delete_link_key(bond_transaction.victim);
    input_classic_forget_autofire_identity(bond_transaction.victim);
    printf("[BOND] replaced disconnected LRU peer=%s key_removed=%u autofire_cleared=1\r\n",
           bd_addr_to_str(bond_transaction.victim),
           !peer_has_link_key(bond_transaction.victim));
  }
  input_classic_bind_autofire_identity(bond_transaction.candidate);
  printf("[BOND] commit complete candidate=%s remembered_count=%u remembered_capacity=%u\r\n",
         bd_addr_to_str(bond_transaction.candidate), known_peer_count,
         CLASSIC_REMEMBERED_MAX);
  print_capacity("replacement-committed");
  clear_bond_transaction_tag();
  return true;
}
static void rollback_bond_transaction(const char *reason) {
  if (!bond_transaction_active) return;
  if (bond_transaction.phase == CLASSIC_BOND_COMMITTING) {
    (void)finalize_bond_transaction();
    return;
  }
  if (known_peer_index(bond_transaction.candidate) < 0)
    classic_key_db->delete_link_key(bond_transaction.candidate);
  input_classic_forget_autofire_identity(bond_transaction.candidate);
  printf("[BOND] provisional pairing rolled back reason=%s candidate=%s old_identity_retained=%u\r\n",
         reason, bd_addr_to_str(bond_transaction.candidate),
         1u);
  clear_bond_transaction_tag();
}
static void recover_bond_transaction(void) {
  if (!classic_tlv) return;
  int size = classic_tlv->get_tag(classic_tlv_context,
      CLASSIC_BOND_TRANSACTION_TAG, (uint8_t *)&bond_transaction,
      sizeof(bond_transaction));
  if (size != (int)sizeof(bond_transaction) ||
      bond_transaction.version != CLASSIC_BOND_TRANSACTION_VERSION ||
      (bond_transaction.phase != CLASSIC_BOND_PREPARED &&
       bond_transaction.phase != CLASSIC_BOND_COMMITTING) ||
      bond_transaction.has_victim > 1) {
    if (size > 0) classic_tlv->delete_tag(classic_tlv_context,
                                          CLASSIC_BOND_TRANSACTION_TAG);
    memset(&bond_transaction, 0, sizeof(bond_transaction));
    return;
  }
  bond_transaction_active = true;
  if (bond_transaction.phase == CLASSIC_BOND_COMMITTING) {
    printf("[BOND] completing interrupted validated replacement after reboot\r\n");
  } else {
    classic_key_db->delete_link_key(bond_transaction.candidate);
    printf("[BOND] removing interrupted provisional key=%s; previous bonds untouched\r\n",
           bd_addr_to_str(bond_transaction.candidate));
    clear_bond_transaction_tag();
  }
}
static void load_peer(void) {
  btstack_tlv_get_instance(&classic_tlv, &classic_tlv_context);
  if (!classic_tlv) return;
  classic_key_db = btstack_link_key_db_tlv_get_instance(classic_tlv, classic_tlv_context);

  uint8_t stored_addresses[CLASSIC_REMEMBERED_MAX][6] = {{0}};
  uint8_t stored_count = 0, stored_version = 0;
  uint8_t raw[sizeof(classic_identity_record_t)] = {0};
  int stored = classic_tlv->get_tag(classic_tlv_context, CLASSIC_PEER_TAG,
                                    raw, sizeof(raw));
  bool valid_record = stored > 0 && classic_identity_record_decode(raw,
      (size_t)stored, stored_addresses, &stored_count, &stored_version);
  if (valid_record) {
    for (uint8_t i = 0; i < stored_count; ++i)
      if (peer_has_link_key(stored_addresses[i]))
        (void)classic_identity_add((uint8_t (*)[6])known_peers,
                                   &known_peer_count, stored_addresses[i]);
  }
  recover_bond_transaction();
  if (bond_transaction_active &&
      bond_transaction.phase == CLASSIC_BOND_COMMITTING)
    (void)finalize_bond_transaction();

  /* For v2 migration, merge the old BTstack key slots; the old identity table
   * could only describe two keys. Once the v3 list exists it is authoritative,
   * so orphan keys from interrupted removals are pruned rather than re-added. */
  btstack_link_key_iterator_t it;
  if (classic_key_db->iterator_init(&it) == 1) {
    bd_addr_t address; link_key_t key; link_key_type_t type;
    while (classic_key_db->iterator_get_next(&it, address, key, &type)) {
      if (valid_record && stored_version == CLASSIC_IDENTITY_RECORD_VERSION) {
        if (!classic_identity_contains((const uint8_t (*)[6])known_peers,
                                       known_peer_count, address) &&
            !pending_matches(address)) {
          classic_key_db->delete_link_key(address);
          printf("[BT-CLASSIC] pruned unlisted Classic link key peer=%s\r\n",
                 bd_addr_to_str(address));
        }
      } else if (!classic_identity_add((uint8_t (*)[6])known_peers,
                                       &known_peer_count, address)) {
        printf("[BT-CLASSIC] key database has more peers than identity registry can retain\r\n");
      }
    }
    classic_key_db->iterator_done(&it);
  }
  bool needs_migration = !valid_record ||
      stored_version != CLASSIC_IDENTITY_RECORD_VERSION ||
      stored_count != known_peer_count;
  if (needs_migration) store_known_peers();
  printf("[BT-CLASSIC] restored remembered_count=%u remembered_capacity=%u link_key_count=%u link_key_capacity=%u active_classic_count=0 active_classic_capacity=%u usb_slots_used=%u usb_slots_capacity=%u migration=%s\r\n",
         known_peer_count, CLASSIC_REMEMBERED_MAX, link_key_count(),
         NVM_NUM_LINK_KEYS, CLASSIC_ACTIVE_CAPACITY, input_usb_slots_used(),
         CLASSIC_USB_SLOTS_CAPACITY, needs_migration ? "complete" : "none");
  classic_peer_rotation_set_count(&peer_rotation, known_peer_count);
}
static void print_capacity(const char *event) {
  printf("[CAPACITY] event=%s remembered_count=%u remembered_capacity=%u link_key_count=%u link_key_capacity=%u active_classic_count=%u active_classic_capacity=%u usb_slots_used=%u usb_slots_capacity=%u\r\n",
         event, known_peer_count, CLASSIC_REMEMBERED_MAX, link_key_count(),
         NVM_NUM_LINK_KEYS, hid_connected ? 1u : 0u, CLASSIC_ACTIVE_CAPACITY,
         input_usb_slots_used(), CLASSIC_USB_SLOTS_CAPACITY);
}
static int select_replacement_victim(bd_addr_t victim) {
  bool connected[CLASSIC_REMEMBERED_MAX] = {false};
  bool reserved[CLASSIC_REMEMBERED_MAX] = {false};
  for (uint8_t i = 0; i < known_peer_count; ++i) {
    connected[i] = hid_connected &&
        memcmp(known_peers[i], selected_peer, sizeof(bd_addr_t)) == 0;
    reserved[i] = (hid_cid &&
        memcmp(known_peers[i], selected_peer, sizeof(bd_addr_t)) == 0) ||
        ((attempt_policy.phase == CLASSIC_ATTEMPT_DIRECT ||
                   attempt_policy.phase == CLASSIC_ATTEMPT_CANCEL_PENDING) &&
        memcmp(known_peers[i], selected_peer, sizeof(bd_addr_t)) == 0);
  }
  int index = classic_identity_lru_victim(
      (const uint8_t (*)[6])known_peers, known_peer_count, connected, reserved);
  if (index >= 0) memcpy(victim, known_peers[index], sizeof(bd_addr_t));
  return index;
}
static bool begin_bond_transaction(const bd_addr_t candidate) {
  if (known_peer_index(candidate) >= 0) return true;
  if (bond_transaction_active || hid_connected || hid_cid ||
      input_usb_slots_used() >= CLASSIC_USB_SLOTS_CAPACITY) {
    printf("[BOND] candidate denied reason=transport-or-usb-capacity\r\n");
    print_capacity("pair-denied");
    return false;
  }
  uint8_t keys = link_key_count();
  if (keys > known_peer_count) {
    printf("[BOND] candidate denied reason=key-registry-mismatch; use BOND LIST/REMOVE diagnostic commands\r\n");
    print_capacity("pair-denied-key-mismatch");
    return false;
  }
  if (keys >= NVM_NUM_LINK_KEYS) {
    printf("[BOND] candidate denied reason=provisional-key-capacity-full; remove a Classic bond explicitly\r\n");
    print_capacity("pair-denied-key-capacity");
    return false;
  }
  classic_bond_transaction_t next = {0};
  next.version = CLASSIC_BOND_TRANSACTION_VERSION;
  next.phase = CLASSIC_BOND_PREPARED;
  memcpy(next.candidate, candidate, sizeof(bd_addr_t));
  if (known_peer_count == CLASSIC_REMEMBERED_MAX) {
    if (!pairing_armed) {
      printf("[BOND] candidate denied reason=full; explicit BOND PAIR required before replacing an identity\r\n");
      print_capacity("pair-denied-explicit-authorization-required");
      return false;
    }
    bd_addr_t victim;
    int victim_index = select_replacement_victim(victim);
    if (victim_index < 0) {
      printf("[BOND] candidate denied reason=no-disconnected-unreserved-LRU-identity\r\n");
      print_capacity("pair-denied-no-victim");
      return false;
    }
    next.has_victim = 1;
    memcpy(next.victim, victim, sizeof(bd_addr_t));
    printf("[BOND] provisional replacement reserved candidate=%s victim=%s lru_index=%d; old key retained until valid INPUT_READY\r\n",
           bd_addr_to_str(candidate), bd_addr_to_str(victim), victim_index);
  }
  if (!classic_tlv || classic_tlv->store_tag(classic_tlv_context,
      CLASSIC_BOND_TRANSACTION_TAG, (const uint8_t *)&next,
      sizeof(next)) != 0) {
    printf("[BOND] candidate denied reason=cannot-persist-transaction-marker; existing bonds retained\r\n");
    return false;
  }
  bond_transaction = next;
  bond_transaction_active = true;
  pairing_armed = false;
  print_capacity("pair-reserved");
  return true;
}
static bool mark_peer_input_ready(const bd_addr_t address) {
  if (known_peer_index(address) >= 0) {
    int index = known_peer_index(address);
    if (index >= 0 && index + 1 < known_peer_count) {
      bd_addr_t recent;
      memcpy(recent, known_peers[index], sizeof(recent));
      memmove(known_peers[index], known_peers[index + 1],
              (size_t)(known_peer_count - index - 1) * sizeof(bd_addr_t));
      memcpy(known_peers[known_peer_count - 1], recent, sizeof(recent));
      store_known_peers(); /* Once at INPUT_READY, never in the report loop. */
      classic_peer_rotation_init(&peer_rotation);
      classic_peer_rotation_set_count(&peer_rotation, known_peer_count);
      printf("[BOND] recency updated at INPUT_READY peer=%s lru_index=%u\r\n",
             bd_addr_to_str(address), known_peer_count - 1);
    }
    print_capacity("input-ready");
    return true;
  }
  if (!pending_matches(address) || !peer_has_link_key(address)) {
    printf("[BOND] INPUT_READY denied: no staged identity transaction or link key for %s\r\n",
           bd_addr_to_str(address));
    return false;
  }
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (bond_commit_retry_ms && (uint32_t)(now - bond_commit_retry_ms) < 1000)
    return false;
  bond_commit_retry_ms = now;
  if (bond_transaction.phase != CLASSIC_BOND_COMMITTING) {
    bond_transaction.phase = CLASSIC_BOND_COMMITTING;
    if (!persist_bond_transaction(CLASSIC_BOND_COMMITTING)) {
      bond_transaction.phase = CLASSIC_BOND_PREPARED;
      printf("[BOND] could not persist commit intent; previous identity remains intact\r\n");
      return false;
    }
  }
  return finalize_bond_transaction();
}
static bool peer_reserved_for_operation(const bd_addr_t address) {
  if ((hid_connected || hid_cid) &&
      memcmp(address, selected_peer, sizeof(bd_addr_t)) == 0)
    return true;
  if ((attempt_policy.phase == CLASSIC_ATTEMPT_DIRECT ||
       attempt_policy.phase == CLASSIC_ATTEMPT_CANCEL_PENDING) &&
      memcmp(address, selected_peer, sizeof(bd_addr_t)) == 0)
    return true;
  if (bond_transaction_active &&
      (memcmp(address, bond_transaction.candidate, sizeof(bd_addr_t)) == 0 ||
       (bond_transaction.has_victim &&
        memcmp(address, bond_transaction.victim, sizeof(bd_addr_t)) == 0)))
    return true;
  return false;
}
static void list_bonds(void) {
  printf("[BOND-LIST] remembered_count=%u remembered_capacity=%u link_key_count=%u link_key_capacity=%u active_classic_count=%u active_classic_capacity=%u usb_slots_used=%u usb_slots_capacity=%u\r\n",
         known_peer_count, CLASSIC_REMEMBERED_MAX, link_key_count(),
         NVM_NUM_LINK_KEYS, hid_connected ? 1u : 0u, CLASSIC_ACTIVE_CAPACITY,
         input_usb_slots_used(), CLASSIC_USB_SLOTS_CAPACITY);
  for (uint8_t i = 0; i < known_peer_count; ++i)
    printf("[BOND-LIST] lru_index=%u address=%s key_present=%u state=%s\r\n",
           i, bd_addr_to_str(known_peers[i]), peer_has_link_key(known_peers[i]),
           peer_reserved_for_operation(known_peers[i]) ? "active-or-reserved" : "disconnected");
}
static void arm_new_pairing(void) {
  if (hid_connected || bond_transaction_active ||
      input_usb_slots_used() >= CLASSIC_USB_SLOTS_CAPACITY) {
    printf("[BOND-PAIR] denied: Classic transport or USB gamepad slot is busy\r\n");
    print_capacity("pair-arm-denied");
    return;
  }
  if (known_peer_count >= CLASSIC_REMEMBERED_MAX) {
    bd_addr_t victim;
    if (select_replacement_victim(victim) < 0) {
      printf("[BOND-PAIR] denied: every remembered identity is connected or reserved\r\n");
      return;
    }
    if (link_key_count() >= NVM_NUM_LINK_KEYS) {
      printf("[BOND-PAIR] denied: no provisional BTstack link-key slot is available\r\n");
      print_capacity("pair-arm-key-capacity-full");
      return;
    }
    printf("[BOND-PAIR] replacement explicitly authorized; disconnected LRU=%s remains bonded until supported INPUT_READY\r\n",
           bd_addr_to_str(victim));
  } else {
    printf("[BOND-PAIR] new Classic pairing authorized; free identity_count=%u\r\n",
           CLASSIC_REMEMBERED_MAX - known_peer_count);
  }
  pairing_armed = true;
  pairing_deadline_ms = to_ms_since_boot(get_absolute_time()) +
                        CLASSIC_PAIRING_WINDOW_MS;
  btstack_run_loop_remove_timer(&reconnect_timer);
  if (attempt_policy.phase == CLASSIC_ATTEMPT_IDLE && !inquiry_active)
    scan_again(NULL);
  else
    schedule_reconnect(50);
  printf("[BOND-PAIR] Pair window open for %u ms; press the controller's pairing button now\r\n",
         CLASSIC_PAIRING_WINDOW_MS);
}
static void remove_bond(const bd_addr_t address) {
  if (peer_reserved_for_operation(address)) {
    printf("[BOND-REMOVE] denied: identity is active or reserved\r\n");
    return;
  }
  if (bond_transaction_active &&
      (memcmp(address, bond_transaction.candidate, sizeof(bd_addr_t)) == 0 ||
       (bond_transaction.has_victim &&
        memcmp(address, bond_transaction.victim, sizeof(bd_addr_t)) == 0))) {
    printf("[BOND-REMOVE] denied: identity participates in a provisional pairing transaction\r\n");
    return;
  }
  uint8_t next[CLASSIC_REMEMBERED_MAX][6];
  memcpy(next, known_peers, sizeof(next));
  uint8_t next_count = known_peer_count;
  (void)classic_identity_remove(next, &next_count, address);
  if (next_count == known_peer_count && known_peer_index(address) < 0 &&
      !peer_has_link_key(address)) {
    printf("[BOND-REMOVE] not found address=%s\r\n", bd_addr_to_str(address));
    return;
  }
  if (!store_peer_list((const bd_addr_t *)next, next_count)) {
    printf("[BOND-REMOVE] failed to store identity list; link key retained\r\n");
    return;
  }
  memcpy(known_peers, next, sizeof(known_peers));
  known_peer_count = next_count;
  classic_key_db->delete_link_key((uint8_t *)address);
  input_classic_forget_autofire_identity(address);
  classic_peer_rotation_init(&peer_rotation);
  classic_peer_rotation_set_count(&peer_rotation, known_peer_count);
  printf("[BOND-REMOVE] removed address=%s key_removed=%u autofire_cleared=1 other_identities_preserved=1\r\n",
         bd_addr_to_str(address), !peer_has_link_key(address));
  print_capacity("selective-remove");
}
#if !BRIDGE_USB_XINPUT
static void process_diagnostic_command(void) {
  command_buffer[command_length] = 0;
  unsigned capture_count_requested = 24;
  char extra = 0;
  int capture_args = sscanf(command_buffer, "DS4 CAPTURE %u %c",
                            &capture_count_requested, &extra);
  if (strcmp(command_buffer, "BOND LIST") == 0) {
    list_bonds();
  } else if (strcmp(command_buffer, "BOND PAIR") == 0) {
    arm_new_pairing();
  } else if (strcmp(command_buffer, "DS4 CAPTURE") == 0 ||
             (capture_args == 1 && strncmp(command_buffer, "DS4 CAPTURE ", 12) == 0)) {
    if (!hid_connected || !active_is_ds4 || !classic_input_ready || !ds4_input_ready) {
      printf("[DS4-CAPTURE] refused: a DualShock 4 must be connected and INPUT_READY first\r\n");
    } else if (capture_count_requested < 1 || capture_count_requested > 32) {
      printf("[DS4-CAPTURE] sample count must be 1..32\r\n");
    } else {
      capture_read = capture_write = capture_count = 0;
      ds4_capture_sequence = ds4_capture_omitted = 0;
      ds4_capture_last_us = 0;
      ds4_capture_remaining = (uint8_t)capture_count_requested;
      ds4_capture_requested = (uint8_t)capture_count_requested;
      ds4_capture_complete_reported = false;
      ds4_capture_mode = ds4_capture_session = true;
      printf("[DS4-CAPTURE] armed samples=%u interval_us=100000; leave controller untouched\r\n",
             ds4_capture_remaining);
    }
  } else if (strncmp(command_buffer, "BOND REMOVE ", 12) == 0 &&
             strlen(command_buffer) == 29) {
    bd_addr_t address;
    if (sscanf_bd_addr(command_buffer + 12, address)) remove_bond(address);
    else printf("[BOND-REMOVE] invalid address; use BOND REMOVE AA:BB:CC:DD:EE:FF\r\n");
  } else if (command_length) {
    printf("[COMMAND] unknown. Commands: BOND LIST | BOND PAIR | BOND REMOVE AA:BB:CC:DD:EE:FF | DS4 CAPTURE [1..32]\r\n");
  }
  command_length = 0;
}
static void poll_diagnostic_console(void) {
  for (unsigned budget = 0; budget < 64; ++budget) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) break;
    if (c == '\r' || c == '\n') {
      if (command_length) process_diagnostic_command();
    } else if (c == '\b' || c == 0x7f) {
      if (command_length) --command_length;
    } else if (c >= 0x20 && c <= 0x7e) {
      if (command_length + 1 < sizeof(command_buffer))
        command_buffer[command_length++] = (char)c;
      else {
        command_length = 0;
        printf("[COMMAND] line too long; discarded\r\n");
      }
    }
  }
}
#endif
static void reconnect_known(btstack_timer_source_t *ts) {
  (void)ts;
  if (!known_peer_count || hid_connected) return;
  if (attempt_policy.phase == CLASSIC_ATTEMPT_PAGE_SCAN) {
    classic_attempt_page_scan_completed(&attempt_policy);
    printf("[BT-CLASSIC] page-scan window completed; starting directed rotation\r\n");
  }
  if (attempt_policy.phase == CLASSIC_ATTEMPT_DIRECT) {
    if (!classic_attempt_timeout(&attempt_policy, attempt_timer_generation)) return;
    directed_failures++;
    diagnostic_stage = "cancel_pending";
    printf("[BT-CLASSIC] directed timeout gen=%lu cid=%u addr=%s; requesting HCI cancel once\r\n",
           (unsigned long)attempt_policy.generation, hid_cid,
           bd_addr_to_str(selected_peer));
  }
  if (attempt_policy.phase == CLASSIC_ATTEMPT_CANCEL_PENDING) {
    if (attempt_policy.cancel_submitted) return;
    if (!hci_can_send_command_packet_now()) {
      schedule_reconnect(50);
      return;
    }
    uint8_t cancel_status = hci_send_cmd(&hci_create_connection_cancel,
                                         selected_peer);
    if (cancel_status == ERROR_CODE_SUCCESS)
      classic_attempt_cancel_submitted(&attempt_policy);
    printf("[BT-CLASSIC] cancel submitted gen=%lu cid=%u immediate=0x%02x addr=%s\r\n",
           (unsigned long)attempt_policy.generation, hid_cid, cancel_status,
           bd_addr_to_str(selected_peer));
    if (cancel_status != ERROR_CODE_SUCCESS) schedule_reconnect(50);
    return;
  }
  if (attempt_policy.phase != CLASSIC_ATTEMPT_IDLE || hid_cid || inquiry_active) return;
  if (pairing_armed) { scan_again(NULL); return; }
  if (inquiry_active) { gap_inquiry_stop(); inquiry_active = false; }
  int peer_index = classic_peer_rotation_next(&peer_rotation);
  if (peer_index < 0) return;
  bd_addr_t *peer = &known_peers[peer_index];
  memcpy(selected_peer, *peer, sizeof(selected_peer));
  candidate_explicit_8bitdo = false;
  candidate_ds4_hint = false;
  active_is_ds4 = false;
  identity_logged = false;
  uint8_t status = hid_host_connect(*peer, HID_PROTOCOL_MODE_REPORT, &hid_cid);
  directed_attempts++;
  if (status == ERROR_CODE_SUCCESS) {
    attempt_timer_generation = classic_attempt_direct_started(&attempt_policy, hid_cid);
    printf("[BT-CLASSIC] directed requested attempt=%lu gen=%lu cid=%u identity=%d key=%u immediate=0x%02x addr=%s\r\n",
           (unsigned long)directed_attempts, (unsigned long)attempt_timer_generation,
           hid_cid, peer_index,
           peer_has_link_key(*peer),
           status, bd_addr_to_str(*peer));
    schedule_reconnect(8000);
  }
  else {
    directed_failures++; hid_cid = 0;
    printf("[BT-CLASSIC] directed request rejected attempt=%lu immediate=0x%02x addr=%s\r\n",
           (unsigned long)directed_attempts, status, bd_addr_to_str(*peer));
    /* Pairing requires inquiry because a new controller has no remembered address. */
    btstack_run_loop_set_timer(&scan_timer, 500);
    btstack_run_loop_add_timer(&scan_timer);
  }
}
static switch_parser_stats_t parser_stats;
static uint16_t diagnostic_descriptor_len;
static uint8_t descriptor_copy[CLASSIC_DESCRIPTOR_MAX];
static uint16_t descriptor_copy_len;
static bool descriptor_dumped;
static uint32_t id_3f, id_30, id_21, id_other, rejected_short, rejected_unknown;
static uint32_t routed_reports;
static btstack_timer_source_t diagnostic_timer;
enum { CAPTURE_SLOTS = 32, CAPTURE_MAX_BYTES = 1024 };
typedef struct {
  uint64_t timestamp_us;
  uint32_t sequence;
  uint16_t len, copied;
  uint8_t bytes[CAPTURE_MAX_BYTES];
  uint8_t ds4_trace, framing_a1, report_id, parse_ok, state_valid;
  ds4_report_debug_t parser;
  controller_state_t state;
  int16_t xinput_lx, xinput_ly, xinput_rx, xinput_ry;
} capture_t;
static capture_t captures[CAPTURE_SLOTS];
static uint32_t capture_omitted, capture_oversize, capture_total;
static uint32_t last_capture_ms;
static uint8_t last_capture_id;
static uint16_t last_capture_len;
static uint8_t last_button0, last_button1, last_hat;
static uint8_t last_raw[128];
static uint16_t last_raw_len;
static uint32_t init_sent_ms;
static uint8_t init_send_status;
static bool init_pending, init_reply_seen, init_ack_valid, init_timed_out;
static uint8_t init_subcommand, switch_sequence, device_type;
static int8_t player_led_slot = -1;
static bool player_led_pending, player_led_waiting, player_led_ack_valid;
static uint8_t player_led_attempts, player_led_sequence, player_led_send_status;
static uint32_t player_led_sent_ms;
static bool first_state_seen;
static controller_state_t first_state;
static uint32_t last_summary_ms;
static void send_switch_subcommand(uint8_t subcommand, uint8_t value) {
  uint8_t payload[SWITCH_SUBCOMMAND_REPORT_LEN];
  switch_build_subcommand(switch_sequence++, subcommand,
                          subcommand == SWITCH_SUBCMD_REPORT_MODE ? &value : NULL,
                          subcommand == SWITCH_SUBCMD_REPORT_MODE ? 1 : 0, payload);
  init_send_status = hid_host_send_report(hid_cid, 1, payload, sizeof(payload));
  init_sent_ms = to_ms_since_boot(get_absolute_time());
  init_subcommand = subcommand;
  init_pending = init_send_status == ERROR_CODE_SUCCESS;
  printf("[SWITCH-INIT] ms=%lu subcmd=%02x value=%02x status=0x%02x\r\n",
         (unsigned long)init_sent_ms, subcommand, value, init_send_status);
}
static capture_t *capture_report(const uint8_t *data, uint16_t len) {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  uint64_t now_us = time_us_64();
  uint8_t id = len ? data[0] : 0;
  capture_total++;
  bool guided_ds4_sample = active_is_ds4 && ds4_capture_session;
  if (guided_ds4_sample && !ds4_capture_mode) return NULL;
  if (guided_ds4_sample && ds4_capture_remaining &&
      ds4_capture_last_us && now_us - ds4_capture_last_us < 100000u)
    return NULL;
  bool controls_changed = false;
  if (active_is_ds4 && len >= 2 && data[0] == 0xa1) {
    uint8_t report_id = data[1];
    size_t begin = report_id == 0x01 ? 2u : (report_id == 0x11 ? 4u : len);
    size_t end = report_id == 0x01 ? 11u : (report_id == 0x11 ? 13u : len);
    if (end <= len && begin < end) {
      controls_changed = last_raw_len != len ||
          memcmp(data + begin, last_raw + begin, end - begin) != 0;
    }
  } else if (len >= 5 && id == 0xa1 && data[1] == 0x3f) {
    controls_changed = data[2] != last_button0 || data[3] != last_button1 ||
                       data[4] != last_hat;
  }
  if (len <= sizeof(last_raw)) {
    memcpy(last_raw, data, len);
    last_raw_len = len;
  }
  if (!guided_ds4_sample && !controls_changed && id == last_capture_id && len == last_capture_len &&
      (uint32_t)(now - last_capture_ms) < 500) return NULL;
  if (capture_count == CAPTURE_SLOTS) {
    capture_omitted++;
    if (guided_ds4_sample) ds4_capture_omitted++;
    return NULL;
  }
  capture_t *c = &captures[capture_write];
  memset(c, 0, sizeof(*c));
  c->timestamp_us = now_us;
  c->sequence = guided_ds4_sample ? ++ds4_capture_sequence : capture_total;
  c->len = len;
  c->copied = len <= CAPTURE_MAX_BYTES ? len : CAPTURE_MAX_BYTES;
  if (c->copied) memcpy(c->bytes, data, c->copied);
  if (len > CAPTURE_MAX_BYTES) capture_oversize++;
  if (guided_ds4_sample) {
    c->ds4_trace = 1;
    c->framing_a1 = len && data[0] == 0xa1;
    c->report_id = c->framing_a1 && len > 1 ? data[1] : id;
    ds4_capture_last_us = now_us;
    if (--ds4_capture_remaining == 0) ds4_capture_mode = false;
  }
  capture_write = (capture_write + 1) % CAPTURE_SLOTS;
  capture_count++;
  last_capture_ms = now; last_capture_id = id; last_capture_len = len;
  if (len >= 5 && id == 0xa1 && data[1] == 0x3f) {
    last_button0 = data[2]; last_button1 = data[3]; last_hat = data[4];
  }
  return c;
}
static int16_t capture_get_i16le(const uint8_t *p) {
  uint16_t value = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
  return (int16_t)value;
}
static void capture_ds4_analysis(capture_t *c, bool accepted,
                                 const ds4_report_debug_t *debug,
                                 const controller_state_t *state) {
  if (!c || !c->ds4_trace || !debug) return;
  c->report_id = debug->report_id;
  c->parse_ok = accepted;
  c->parser = *debug;
  c->state_valid = accepted && state;
  if (c->state_valid) {
    uint8_t report[XINPUT_REPORT_BYTES];
    c->state = *state;
    xinput_report_encode(state, report);
    c->xinput_lx = capture_get_i16le(report + 6);
    c->xinput_ly = capture_get_i16le(report + 8);
    c->xinput_rx = capture_get_i16le(report + 10);
    c->xinput_ry = capture_get_i16le(report + 12);
  }
}
static void diagnostic_tick(btstack_timer_source_t *ts) {
  (void)ts;
#if !BRIDGE_USB_XINPUT
  if (stdio_usb_connected()) poll_diagnostic_console();
#endif
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (pairing_armed && (int32_t)(now - pairing_deadline_ms) >= 0) {
    pairing_armed = false;
    printf("[BOND-PAIR] authorization expired; no identity was changed\r\n");
  }
  if (bond_transaction_active && bond_transaction.phase == CLASSIC_BOND_COMMITTING &&
      (uint32_t)(now - bond_commit_retry_ms) >= 5000) {
    bond_commit_retry_ms = now;
    (void)finalize_bond_transaction();
  }
  if (bond_transaction_active && hid_connected && bond_validation_deadline_ms &&
      (int32_t)(now - bond_validation_deadline_ms) >= 0) {
    bond_validation_deadline_ms = 0;
    printf("[BOND] supported INPUT_READY timeout; disconnecting provisional peer and preserving previous bond\r\n");
    hid_host_disconnect(hid_cid);
  }
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
  if (ds4_feature_pending && (uint32_t)(now - ds4_feature_sent_ms) > 1500) {
    ds4_feature_pending = false;
    printf("[DS4-INIT] optional feature report timed out; input reports remain enabled\r\n");
  }
  if ((uint32_t)(now - last_summary_ms) >= 5000) {
  last_summary_ms = now;
  printf("[CLASSIC-DIAG] stage=%s target=%s model=%s cid=%u remembered_count=%u remembered_capacity=%u link_key_count=%u link_key_capacity=%u active_classic_count=%u active_classic_capacity=%u usb_slots_used=%u usb_slots_capacity=%u pair_armed=%u tx_pending=%u attempt=%s gen=%lu connected=%u reconnect=%lu failures=%lu incoming=%lu acl=0x%02x auth=0x%02x encrypt=0x%02x/%u descriptor=%u descriptor_len=%u rx=%lu ok=%lu rejected=%lu id3f=%lu id30=%lu id21=%lu other=%lu short=%lu unknown=%lu routed=%lu ds4_rx=%lu ds4_ok=%lu ds4_id01=%lu ds4_id11=%lu ds4_id_other=%lu ds4_lenerr=%lu ds4_iderr=%lu ds4_crcerr=%lu feature_req=%lu feature_resp=%lu feature_pending=%u captured=%lu buffered=%u omitted=%lu oversize=%lu init_status=%u init_reply_seen=%u init_ack_valid=%u init_timeout=%u player=%d led_ack=%u led_attempts=%u\r\n",
         diagnostic_stage, bd_addr_to_str(selected_peer),
         active_is_ds4 ? "ds4-report-profile" : (candidate_explicit_8bitdo ? "8bitdo" : (candidate_ds4_hint ? "sony-unconfirmed" : "switch-profile/unknown")),
         hid_cid, known_peer_count, CLASSIC_REMEMBERED_MAX, link_key_count(),
         NVM_NUM_LINK_KEYS, hid_connected ? 1u : 0u, CLASSIC_ACTIVE_CAPACITY,
         input_usb_slots_used(), CLASSIC_USB_SLOTS_CAPACITY, pairing_armed,
         bond_transaction_active,
         classic_attempt_phase_name(attempt_policy.phase),
         (unsigned long)attempt_policy.generation, hid_connected,
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
         (unsigned long)ds4_stats.received, (unsigned long)ds4_stats.accepted,
         (unsigned long)ds4_id01, (unsigned long)ds4_id11,
         (unsigned long)ds4_id_other,
         (unsigned long)ds4_stats.rejected_length, (unsigned long)ds4_stats.rejected_id,
         (unsigned long)ds4_stats.rejected_crc, (unsigned long)ds4_feature_status,
         (unsigned long)ds4_feature_responses, ds4_feature_pending,
         (unsigned long)capture_total, capture_count,
         (unsigned long)capture_omitted, (unsigned long)capture_oversize,
         init_send_status, init_reply_seen, init_ack_valid, init_timed_out,
         player_led_slot + 1, player_led_ack_valid, player_led_attempts);
  }
  if (descriptor_copy_len && !descriptor_dumped) {
    printf("[CLASSIC-DESCRIPTOR] len=%u bytes=", descriptor_copy_len);
    for (uint16_t i = 0; i < descriptor_copy_len; i++) printf("%02X", descriptor_copy[i]);
    printf("\r\n");
    descriptor_dumped = true;
  }
  if (capture_count) {
    capture_t *c = &captures[capture_read];
    uint8_t report_id = c->len ? c->bytes[0] : 0;
    if (report_id == 0xa1 && c->len > 1) report_id = c->bytes[1];
    if (c->ds4_trace) {
      printf("[DS4-SAMPLE] seq=%lu timestamp_us=%llu framing=%s report_id=%02X len=%u raw=",
             (unsigned long)c->sequence, (unsigned long long)c->timestamp_us,
             c->framing_a1 ? "HIDP-A1" : "report-id-first",
             c->report_id, c->len);
    } else {
      printf("[CLASSIC-RAW] ms=%lu id=%02X len=%u bytes=", (unsigned long)(c->timestamp_us / 1000u),
             report_id, c->len);
    }
    for (unsigned i = 0; i < c->copied; ++i) printf("%02X", c->bytes[i]);
    if (c->ds4_trace) {
      printf("%s", c->len > c->copied ? " TRUNCATED" : "");
      if (c->parser.axes_valid)
        printf(" axes_raw=%u,%u,%u,%u", c->parser.axis_bytes[0],
               c->parser.axis_bytes[1], c->parser.axis_bytes[2],
               c->parser.axis_bytes[3]);
      printf(" parse=%s reject=%u", c->parse_ok ? "accepted" : "rejected",
             c->parser.reject_reason);
      if (c->parser.crc_checked)
        printf(" crc=%s calculated=%08lx reported=%08lx",
               c->parser.crc_valid ? "ok" : "bad",
               (unsigned long)c->parser.crc_calculated,
               (unsigned long)c->parser.crc_reported);
      else printf(" crc=not-applicable");
      if (c->state_valid)
        printf(" ControllerState={buttons:%04x,dpad:%u,lx:%ld,ly:%ld,rx:%ld,ry:%ld,lt:%ld,rt:%ld} XInput={lx:%d,ly:%d,rx:%d,ry:%d}",
               c->state.buttons, c->state.dpad, (long)c->state.left_x,
               (long)c->state.left_y, (long)c->state.right_x,
               (long)c->state.right_y, (long)c->state.left_trigger,
               (long)c->state.right_trigger, c->xinput_lx, c->xinput_ly,
               c->xinput_rx, c->xinput_ry);
      printf("\r\n");
    } else printf("%s\r\n", c->len > c->copied ? " TRUNCATED" : "");
    capture_read = (capture_read + 1) % CAPTURE_SLOTS;
    capture_count--;
  }
  if (ds4_capture_session && !ds4_capture_mode && capture_count == 0 &&
      !ds4_capture_complete_reported) {
    printf("[DS4-CAPTURE] complete requested=%u samples=%lu omitted=%lu\r\n",
           ds4_capture_requested,
           (unsigned long)ds4_capture_sequence,
           (unsigned long)ds4_capture_omitted);
    ds4_capture_complete_reported = true;
    ds4_capture_session = false;
  }
  if (first_state_seen && (uint32_t)(now - last_summary_ms) < 100) {
    printf("[CLASSIC-STATE] buttons=%04x dpad=%u lx=%ld ly=%ld rx=%ld ry=%ld lt=%ld rt=%ld\r\n",
           first_state.buttons, first_state.dpad, (long)first_state.left_x,
           (long)first_state.left_y, (long)first_state.right_x,
           (long)first_state.right_y, (long)first_state.left_trigger,
           (long)first_state.right_trigger);
    first_state_seen = false;
  }
  btstack_run_loop_set_timer(&diagnostic_timer, 100);
  btstack_run_loop_add_timer(&diagnostic_timer);
}
static bd_addr_t name_query_addr;
static uint8_t name_query_page_mode;
static uint16_t name_query_clock_offset;
static bool name_query_pending;

static uint8_t bounded_name_len(const char *name) {
  uint8_t n = 0; while (n < 63 && name[n]) n++; return n;
}
static void scan_again(btstack_timer_source_t *ts) {
  (void)ts;
  if (!hid_cid && !inquiry_active && attempt_policy.phase == CLASSIC_ATTEMPT_IDLE) {
    status_led_set(LED_SCANNING);
    diagnostic_stage = "scanning";
    uint8_t status = gap_inquiry_start(5);
    inquiry_active = status == ERROR_CODE_SUCCESS;
    if (inquiry_active) classic_attempt_inquiry_started(&attempt_policy);
    printf("[BT-CLASSIC] inquiry requested immediate=0x%02x active=%u duration=5\r\n",
           status, inquiry_active);
    if (!inquiry_active) {
      diagnostic_stage = "inquiry_request_failed";
      btstack_run_loop_set_timer(&scan_timer, 1000);
      btstack_run_loop_add_timer(&scan_timer);
    }
  }
}
static void connect_if_supported(bd_addr_t addr, const char *name, uint8_t len) {
  if (hid_cid) {
    printf("[BT-CLASSIC] candidate deferred reason=host-busy name='%.*s'\r\n", len, name);
    return;
  }
  if (!classic_name_is_candidate((const uint8_t *)name, len)) {
    printf("[BT-CLASSIC] candidate rejected reason=unsupported-name addr=%s name='%.*s'\r\n",
           bd_addr_to_str(addr), len, name);
    return;
  }
  bool already_remembered = known_peer_index(addr) >= 0;
  if (!already_remembered) {
    if (input_usb_slots_used() >= CLASSIC_USB_SLOTS_CAPACITY) {
      printf("[BT-CLASSIC] candidate rejected reason=usb-slots-full addr=%s name='%.*s'\r\n",
             bd_addr_to_str(addr), len, name);
      print_capacity("candidate-usb-full");
      return;
    }
    if (known_peer_count >= CLASSIC_REMEMBERED_MAX && !pairing_armed) {
      printf("[BT-CLASSIC] candidate rejected reason=identity-capacity-full; explicit BOND PAIR authorization required addr=%s name='%.*s'\r\n",
             bd_addr_to_str(addr), len, name);
      print_capacity("candidate-identity-full");
      return;
    }
    if (!begin_bond_transaction(addr)) return;
  }
  gap_inquiry_stop(); inquiry_active = false; name_query_pending = false;
  classic_attempt_inquiry_completed(&attempt_policy);
  status_led_set(LED_PAIRING);
  diagnostic_stage = "connecting";
  char lower[64]; uint8_t copy = len < sizeof(lower)-1 ? len : sizeof(lower)-1;
  memcpy(lower, name, copy); lower[copy] = 0;
  for (uint8_t i = 0; i < copy; i++) if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += 32;
  candidate_explicit_8bitdo = strstr(lower, "8bitdo") != NULL;
  candidate_ds4_hint = classic_name_is_ds4_hint((const uint8_t *)name, len);
  active_is_ds4 = false;
  identity_logged = false;
  memcpy(selected_peer, addr, sizeof(selected_peer));
  uint8_t s = hid_host_connect(addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
  printf("[BT-CLASSIC] candidate accepted reason=name-hint name='%.*s' hint=%s addr=%s connect=0x%02x\r\n",
         len, name, candidate_ds4_hint ? "ds4" : (candidate_explicit_8bitdo ? "8bitdo" : "switch-compatible"), bd_addr_to_str(addr), s);
  print_capacity("candidate-accepted");
  if (s == ERROR_CODE_SUCCESS) {
    attempt_timer_generation = classic_attempt_direct_started(&attempt_policy, hid_cid);
    printf("[BT-CLASSIC] candidate connect gen=%lu cid=%u\r\n",
           (unsigned long)attempt_timer_generation, hid_cid);
    schedule_reconnect(8000);
  }
  else {
    hid_cid = 0;
    rollback_bond_transaction("hid-connect-request-failed");
    btstack_run_loop_set_timer(&scan_timer, 1000);
    btstack_run_loop_add_timer(&scan_timer);
  }
}
static void packet_handler(uint8_t type, uint16_t channel, uint8_t *p, uint16_t size) {
  (void)channel; (void)size;
  if (type != HCI_EVENT_PACKET) return;
  uint8_t event = hci_event_packet_get_type(p);
  if (event == HCI_EVENT_USER_CONFIRMATION_REQUEST) {
    bd_addr_t peer;
    hci_event_user_confirmation_request_get_bd_addr(p, peer);
    if ((hid_cid && memcmp(peer, selected_peer, sizeof(peer)) == 0) ||
        known_peer_index(peer) >= 0) {
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
    if (last_acl_status == ERROR_CODE_SUCCESS &&
        (known_peer_index(addr) >= 0 ||
         memcmp(addr, selected_peer, sizeof(addr)) == 0))
      classic_acl_handle = hci_event_connection_complete_get_connection_handle(p);
    printf("[BT-CLASSIC] ACL connection status=0x%02x handle=0x%04x peer=%s known=%u key=%u\r\n",
           last_acl_status,
           hci_event_connection_complete_get_connection_handle(p),
           bd_addr_to_str(addr), known_peer_index(addr) >= 0,
           peer_has_link_key(addr));
  }
  if (event == HCI_EVENT_DISCONNECTION_COMPLETE) {
    hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(p);
    if (handle == classic_acl_handle) {
      last_disconnect_reason = hci_event_disconnection_complete_get_reason(p);
      classic_acl_handle = HCI_CON_HANDLE_INVALID;
      printf("[BT-CLASSIC] ACL disconnected handle=0x%04x reason=0x%02x target=%s\r\n",
             handle, last_disconnect_reason, bd_addr_to_str(selected_peer));
    }
  }
  if (event == HCI_EVENT_LINK_KEY_REQUEST) {
    bd_addr_t addr; hci_event_link_key_request_get_bd_addr(p, addr);
    printf("[BT-CLASSIC] link-key request peer=%s known=%u key_present=%u\r\n",
           bd_addr_to_str(addr), known_peer_index(addr) >= 0,
           peer_has_link_key(addr));
  }
  if (event == HCI_EVENT_COMMAND_COMPLETE &&
      hci_event_command_complete_get_command_opcode(p) == HCI_OPCODE_HCI_CREATE_CONNECTION_CANCEL) {
    const uint8_t *rp = hci_event_command_complete_get_return_parameters(p);
    printf("[BT-CLASSIC] cancel completed gen=%lu status=0x%02x; awaiting HID finalization\r\n",
           (unsigned long)attempt_policy.generation, rp[0]);
  }
  if (event == HCI_EVENT_AUTHENTICATION_COMPLETE) {
    last_auth_status = hci_event_authentication_complete_get_status(p);
    printf("[BT-CLASSIC] authentication handle=0x%04x status=0x%02x target=%s key_present=%u\r\n",
           hci_event_authentication_complete_get_connection_handle(p),
           hci_event_authentication_complete_get_status(p),
           bd_addr_to_str(selected_peer), peer_has_link_key(selected_peer));
    if (hci_event_authentication_complete_get_status(p) != ERROR_CODE_SUCCESS &&
        pending_matches(selected_peer))
      rollback_bond_transaction("authentication-failed");
    if (hci_event_authentication_complete_get_status(p) == ERROR_CODE_SUCCESS &&
        pending_matches(selected_peer))
      printf("[BOND] authentication succeeded for provisional candidate; previous identity is still retained until supported INPUT_READY\r\n");
  }
  if (event == HCI_EVENT_ENCRYPTION_CHANGE) {
    last_encrypt_status = hci_event_encryption_change_get_status(p);
    last_encrypt_enabled = hci_event_encryption_change_get_encryption_enabled(p);
    printf("[BT-CLASSIC] encryption handle=0x%04x status=0x%02x enabled=%u target=%s\r\n",
           hci_event_encryption_change_get_connection_handle(p),
           hci_event_encryption_change_get_status(p),
           hci_event_encryption_change_get_encryption_enabled(p),
           bd_addr_to_str(selected_peer));
  }
  if (event == BTSTACK_EVENT_STATE && btstack_event_state_get_state(p) == HCI_STATE_WORKING) {
    gap_connectable_control(1);
    gap_discoverable_control(0);
    load_peer();
    if (known_peer_count) start_page_scan_window(5000, "startup-known-peers");
    else scan_again(NULL);
    return;
  }
  if (event == GAP_EVENT_INQUIRY_RESULT) {
    bd_addr_t found_addr; gap_event_inquiry_result_get_bd_addr(p, found_addr);
    printf("[BT-CLASSIC] inquiry result addr=%s class=0x%06lx name_available=%u capacity=%s\r\n",
           bd_addr_to_str(found_addr),
           (unsigned long)gap_event_inquiry_result_get_class_of_device(p),
           gap_event_inquiry_result_get_name_available(p),
           known_peer_count < CLASSIC_REMEMBERED_MAX ? "available" : "full");
    print_capacity("inquiry-result");
    if (gap_event_inquiry_result_get_name_available(p)) {
      const uint8_t *name = gap_event_inquiry_result_get_name(p);
      uint8_t len = gap_event_inquiry_result_get_name_len(p);
      printf("[BT-CLASSIC] found %.*s\r\n", len, name);
      bd_addr_t addr; gap_event_inquiry_result_get_bd_addr(p, addr);
      connect_if_supported(addr, (const char *)name, len);
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
      connect_if_supported(addr, name, size >= 72 ? bounded_name_len(name) : (size > 9 ? size-9 : 0));
    } else {
      bd_addr_t addr; hci_event_remote_name_request_complete_get_bd_addr(p, addr);
      printf("[BT-CLASSIC] candidate rejected reason=remote-name-failed addr=%s status=0x%02x\r\n",
             bd_addr_to_str(addr),
             hci_event_remote_name_request_complete_get_status(p));
    }
    return;
  }
  if (event == GAP_EVENT_INQUIRY_COMPLETE) {
    inquiry_active = false;
    classic_attempt_inquiry_completed(&attempt_policy);
    printf("[BT-CLASSIC] inquiry completed status=0x%02x\r\n",
           gap_event_inquiry_complete_get_status(p));
    if (!hid_cid) {
      if (known_peer_count) schedule_reconnect(1000);
      else {
        btstack_run_loop_remove_timer(&scan_timer);
        btstack_run_loop_set_timer(&scan_timer, 1000);
        btstack_run_loop_add_timer(&scan_timer);
      }
    }
    return;
  }
  if (event != HCI_EVENT_HID_META) return;
  switch (hci_event_hid_meta_get_subevent_code(p)) {
    case HID_SUBEVENT_INCOMING_CONNECTION: {
      uint16_t incoming_cid = hid_subevent_incoming_connection_get_hid_cid(p);
      bd_addr_t addr; hid_subevent_incoming_connection_get_address(p, addr);
      uint8_t status = hid_subevent_incoming_connection_get_status(p);
      bool known = known_peer_index(addr) >= 0;
      bool same_outgoing = hid_cid &&
          memcmp(addr, selected_peer, sizeof(addr)) == 0;
      bool host_free = !hid_cid || same_outgoing;
      bool allowed = status == ERROR_CODE_SUCCESS && !hid_connected &&
          host_free && (known || same_outgoing);
      const char *decision = allowed ? "known-or-selected" :
          (!known && !same_outgoing ? "unknown-identity" :
           (!host_free ? "busy-other-identity" : "invalid-state"));
      printf("[BT-CLASSIC] incoming HID peer=%s cid=%u status=0x%02x known=%u key=%u page_scan=%u allowed=%u reason=%s\r\n",
             bd_addr_to_str(addr), incoming_cid, status, known,
             peer_has_link_key(addr),
             attempt_policy.phase == CLASSIC_ATTEMPT_PAGE_SCAN,
             allowed, decision);
      if (status != ERROR_CODE_SUCCESS) break;
      if (!allowed) { hid_host_decline_connection(incoming_cid); break; }
      if (inquiry_active) { gap_inquiry_stop(); inquiry_active = false; }
      btstack_run_loop_remove_timer(&reconnect_timer);
      if (attempt_policy.phase == CLASSIC_ATTEMPT_PAGE_SCAN)
        classic_attempt_page_scan_completed(&attempt_policy);
      else if (same_outgoing)
        classic_attempt_completed(&attempt_policy, attempt_policy.generation,
                                  attempt_policy.cid);
      hid_cid = incoming_cid;
      memcpy(selected_peer, addr, sizeof(selected_peer));
      candidate_explicit_8bitdo = false;
      candidate_ds4_hint = false;
      active_is_ds4 = false;
      identity_logged = false;
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
        classic_attempt_completed(&attempt_policy, attempt_policy.generation, hid_cid);
        hid_cid = hid_subevent_connection_opened_get_hid_cid(p);
        hid_connected = true;
        bd_addr_t opened_addr;
        hid_subevent_connection_opened_get_bd_addr(p, opened_addr);
        memcpy(selected_peer, opened_addr, sizeof(selected_peer));
        classic_input_ready = false;
        if (pending_matches(opened_addr))
          bond_validation_deadline_ms = to_ms_since_boot(get_absolute_time()) +
                                        CLASSIC_UNVERIFIED_LINK_TIMEOUT_MS;
        classic_generation = input_classic_connection_begin(INPUT_CLASSIC_0,
                                                             opened_addr);
        player_led_slot = -1; player_led_pending = false;
        player_led_waiting = false; player_led_ack_valid = false;
        player_led_attempts = 0;
        btstack_run_loop_remove_timer(&reconnect_timer);
        descriptor_ready = false; status_led_set(LED_PAIRING);
        diagnostic_stage = "hid_connected";
        printf("[BT-CLASSIC] HID connected cid=%u\r\n", hid_cid);
        print_capacity("hid-connected");
      } else {
        printf("[BT-CLASSIC] HID open failed status=0x%02x\r\n",
               hid_subevent_connection_opened_get_status(p));
        uint16_t completed_cid = hid_cid;
        rollback_bond_transaction("hid-open-failed");
        bool outgoing_attempt = attempt_policy.phase == CLASSIC_ATTEMPT_DIRECT ||
                                attempt_policy.phase == CLASSIC_ATTEMPT_CANCEL_PENDING;
        if (outgoing_attempt &&
            !classic_attempt_completed(&attempt_policy, attempt_policy.generation,
                                       completed_cid)) {
          printf("[BT-CLASSIC] HID finalization ignored reason=attempt-mismatch cid=%u\r\n",
                 completed_cid);
          break;
        }
        hid_cid = 0;
        hid_connected = false;
        diagnostic_stage = "open_failed";
        status_led_set(LED_RECOVERABLE_ERROR);
        btstack_run_loop_remove_timer(&scan_timer);
        btstack_run_loop_set_timer(&scan_timer, 250);
        btstack_run_loop_add_timer(&scan_timer);
      }
      break;
    case HID_SUBEVENT_DESCRIPTOR_AVAILABLE:
      if (hid_subevent_descriptor_available_get_hid_cid(p) != hid_cid || !hid_connected) break;
      descriptor_ready = hid_subevent_descriptor_available_get_status(p) == ERROR_CODE_SUCCESS;
      diagnostic_stage = descriptor_ready ? "descriptor_ready" : "descriptor_failed";
      if (descriptor_ready) {
        const uint8_t *d = hid_descriptor_storage_get_descriptor_data(hid_cid);
        uint16_t n = hid_descriptor_storage_get_descriptor_len(hid_cid);
        diagnostic_descriptor_len = n;
        descriptor_copy_len = n < sizeof(descriptor_copy) ? n : sizeof(descriptor_copy);
        memcpy(descriptor_copy, d, descriptor_copy_len);
        descriptor_dumped = false;
        init_pending = false; init_reply_seen = false; init_ack_valid = false;
        init_timed_out = false;
        classic_profile_t profile = classic_profile_from_descriptor(d, n);
        active_is_ds4 = profile == CLASSIC_PROFILE_DUALSHOCK4;
        memset(&ds4_stats, 0, sizeof(ds4_stats));
        ds4_feature_responses = 0;
        ds4_id01 = ds4_id11 = ds4_id_other = 0;
        ds4_input_ready = false;
        if (active_is_ds4) {
          diagnostic_stage = "ds4_descriptor_identified";
          init_pending = false;
          ds4_feature_status = hid_host_send_get_report(hid_cid,
              HID_REPORT_TYPE_FEATURE, 0x02);
          ds4_feature_pending = ds4_feature_status == ERROR_CODE_SUCCESS;
          ds4_feature_sent_ms = to_ms_since_boot(get_absolute_time());
          printf("[DS4-INIT] descriptor signature confirmed; optional feature report=02 request=0x%02x; controls do not wait for it\r\n",
                 (unsigned)ds4_feature_status);
        } else if (!candidate_ds4_hint) {
          device_type = SWITCH_TYPE_UNKNOWN; switch_sequence = 0;
          send_switch_subcommand(SWITCH_SUBCMD_DEVICE_INFO, 0);
        } else {
          diagnostic_stage = "ds4_identity_unconfirmed";
          printf("[DS4-ID] name hint only; descriptor signature absent, no DS4 or Nintendo init sent\r\n");
          if (pending_matches(selected_peer)) {
            rollback_bond_transaction("unsupported-ds4-descriptor");
            hid_host_disconnect(hid_cid);
          }
        }
        if (n > 0) { printf("[BT-CLASSIC] descriptor="); for (uint16_t i=0;i<n && i<32;i++) printf("%02X",d[i]); printf("\r\n"); }
      }
      break;
    case HID_SUBEVENT_GET_REPORT_RESPONSE:
      if (hid_subevent_get_report_response_get_hid_cid(p) != hid_cid) break;
      ds4_feature_pending = false;
      ds4_feature_responses++;
      printf("[DS4-INIT] feature response handshake=0x%02x len=%u id=%02x\r\n",
             hid_subevent_get_report_response_get_handshake_status(p),
             hid_subevent_get_report_response_get_report_len(p),
             hid_subevent_get_report_response_get_report_len(p) ?
             hid_subevent_get_report_response_get_report(p)[0] : 0);
      break;
    case HID_SUBEVENT_REPORT: {
      if (hid_subevent_report_get_hid_cid(p) != hid_cid || !hid_connected) break;
      const uint8_t *r = hid_subevent_report_get_report(p);
      uint16_t n = hid_subevent_report_get_report_len(p);
      diagnostic_stage = "report_received";
      capture_t *trace = capture_report(r, n);
      if (n && r[0] == 0xa1) { ++r; --n; }
      if (active_is_ds4) {
        controller_state_t ds4_state;
        ds4_report_debug_t ds4_debug;
        if (!n) ds4_id_other++;
        else if (r[0] == 0x01) ds4_id01++;
        else if (r[0] == 0x11) ds4_id11++;
        else ds4_id_other++;
        bool ds4_ok = ds4_parse_bluetooth_report_debug(
            r, n, &ds4_state, &ds4_stats, &ds4_debug);
        capture_ds4_analysis(trace, ds4_ok, &ds4_debug,
                             ds4_ok ? &ds4_state : NULL);
        if (!ds4_ok) {
          if (ds4_stats.rejected_crc && (ds4_stats.rejected_crc & (ds4_stats.rejected_crc-1u)) == 0)
            printf("[DS4-INPUT] CRC rejection count=%lu id=%02x len=%u\r\n",
                   (unsigned long)ds4_stats.rejected_crc, n ? r[0] : 0, n);
          break;
        }
        if (!classic_input_ready) {
          if (input_usb_slots_used() >= CLASSIC_USB_SLOTS_CAPACITY ||
              !mark_peer_input_ready(selected_peer)) break;
          classic_input_ready = true;
          bond_validation_deadline_ms = 0;
        }
        int ds4_slot = input_report_for(INPUT_CLASSIC_0, classic_generation,
                                        &ds4_state, time_us_64());
        if (ds4_slot < 0) break;
        routed_reports++;
        diagnostic_stage = "ds4_input_ready";
        status_led_set(LED_READY);
        first_state = ds4_state; first_state_seen = true;
        if (!ds4_input_ready) {
          ds4_input_ready = true;
          printf("[DS4-INPUT] INPUT_READY report=%02x len=%u slot=%u\r\n",
                 r[0], n, ds4_slot + 1);
          print_capacity("input-ready");
        }
        break;
      }
      if (candidate_ds4_hint) {
        id_other++; rejected_unknown++;
        break; /* A Sony-like name is not enough to select another parser. */
      }
      if (n && r[0] == 0x21) {
        init_reply_seen = true;
        const uint8_t *reply = NULL; size_t reply_len = 0;
        if (switch_parse_subcommand_ack(r, n, init_subcommand, &reply, &reply_len)) {
          init_pending = false;
          if (init_subcommand == SWITCH_SUBCMD_DEVICE_INFO) {
            if (reply_len >= 3) device_type = reply[2];
            printf("[SWITCH-IDENTITY] device-info ACK type=0x%02x expected-pro=%u\r\n",
                   device_type, device_type == SWITCH_TYPE_PRO);
            send_switch_subcommand(SWITCH_SUBCMD_REPORT_MODE, 0x30);
          } else if (init_subcommand == SWITCH_SUBCMD_REPORT_MODE) {
            init_ack_valid = true;
            printf("[SWITCH-INIT] report-mode 0x30 confirmed by ACK\r\n");
          }
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
      /* A generic "Pro Controller" name is ambiguous. Genuine Switch Pro is
       * considered input-ready only after a valid full 0x30 report. The
       * observed 8BitDo 0x3f fallback is allowed after its explicit name or
       * after initialization has timed out. */
      if (r[0] == 0x3f && !candidate_explicit_8bitdo && !init_timed_out) break;
      if (r[0] == 0x30 && !identity_logged) {
        printf("[SWITCH-IDENTITY] accepted format=0x30 class=Switch-Pro-compatible\r\n");
        identity_logged = true;
      } else if (r[0] == 0x3f && !identity_logged) {
        printf("[SWITCH-IDENTITY] accepted format=0x3f class=Simple-Switch-compatible\r\n");
        identity_logged = true;
      }
      if (!classic_input_ready) {
        if (input_usb_slots_used() >= CLASSIC_USB_SLOTS_CAPACITY ||
            !mark_peer_input_ready(selected_peer)) break;
        classic_input_ready = true;
        bond_validation_deadline_ms = 0;
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
      printf("[BT-CLASSIC] HID disconnected cid=%u identity=%s key=%u acl_reason=0x%02x; neutral and reconnect\r\n",
             hid_cid, bd_addr_to_str(selected_peer),
             peer_has_link_key(selected_peer), last_disconnect_reason);
      rollback_bond_transaction("hid-disconnected-before-or-after-validation");
      input_release_for(INPUT_8BITDO, classic_generation);
      int disconnected_index = known_peer_index(selected_peer);
      if (disconnected_index >= 0)
        classic_peer_rotation_prefer(&peer_rotation,
                                     (uint8_t)disconnected_index);
      hid_cid = 0; hid_connected = false; descriptor_ready = false;
      classic_input_ready = false;
      if (ds4_capture_session) {
        ds4_capture_mode = false;
        ds4_capture_remaining = 0;
      }
      active_is_ds4 = false; ds4_feature_pending = false; ds4_input_ready = false;
      candidate_ds4_hint = false; candidate_explicit_8bitdo = false;
      identity_logged = false;
      player_led_pending = false; player_led_waiting = false; player_led_slot = -1;
      diagnostic_stage = "disconnected";
      if (known_peer_count) start_page_scan_window(12000, "recent-disconnect");
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
  classic_attempt_policy_init(&attempt_policy);
  classic_peer_rotation_init(&peer_rotation);
  btstack_run_loop_set_timer_handler(&diagnostic_timer, diagnostic_tick);
  btstack_run_loop_set_timer(&diagnostic_timer, 100);
  btstack_run_loop_add_timer(&diagnostic_timer);
  hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
}
