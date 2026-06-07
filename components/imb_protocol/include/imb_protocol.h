#pragma once

#include <stdint.h>
#include <stddef.h>
#include "imb_types.h"
#include "imb_delta.h"

/* ── Message type byte (first byte of every BLE payload) ──────────────── */

typedef enum {
    IMB_MSG_EVENT_TAG        = 0x01,  /* box→phone: real-time tag scan event */
    IMB_MSG_EVENT_MODE       = 0x02,  /* box→phone: operational mode changed */
    IMB_MSG_REPORT_CHUNK     = 0x03,  /* box→phone: fragmented report chunk */
    IMB_MSG_EVENT_ACK        = 0x04,  /* box→phone: application-level ACK */
    IMB_MSG_EVENT_DROPPED    = 0x05,  /* box→phone: events dropped during window */
    IMB_MSG_EVENT_LOG_CHUNK  = 0x06,  /* box→phone: transaction log chunk [NEW] */

    IMB_MSG_CMD_MODE         = 0x10,  /* phone→box: set operational mode */
    IMB_MSG_CMD_NAME         = 0x11,  /* phone→box: assign name to UID */
    IMB_MSG_CMD_ACCEPT       = 0x12,  /* phone→box: accept/reject foreign */
    IMB_MSG_CMD_HELLO        = 0x13,  /* phone→box: mandatory first message */
    IMB_MSG_CMD_SET_PIN      = 0x14,  /* phone→box: provision PIN (SETUP mode) */
    IMB_MSG_CMD_REPORT_ACK   = 0x15,  /* phone→box: ack report chunk */
    IMB_MSG_CMD_REPORT_NACK  = 0x16,  /* phone→box: nack report chunk */
    IMB_MSG_CMD_UNBOND       = 0x17,  /* phone→box: erase bond */
    IMB_MSG_CMD_GET_LOG      = 0x18,  /* phone→box: pull transaction log [NEW] */
    IMB_MSG_CMD_MESH_STATUS  = 0x19,  /* phone→box: request peer status [NEW] */
} imb_msg_type_e;

typedef enum {
    IMB_ACK_OK                       = 0,
    IMB_ACK_PIN_MISMATCH             = 1,
    IMB_ACK_REGISTRY_FULL            = 2,
    IMB_ACK_NDEF_WRITE_FAILED        = 3,
    IMB_ACK_INVALID_MODE             = 4,
    IMB_ACK_UNKNOWN_UID              = 5,
    IMB_ACK_NOT_AUTHED               = 6,
    IMB_ACK_REGISTRATION_INCOMPLETE  = 7,
    IMB_ACK_LOG_OVERFLOW             = 8,  /* Gap fill failed; log wrapped [NEW] */
} imb_ack_status_e;

/* ── Packed wire structs ───────────────────────────────────────────────── */

/* Common header for all commands */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t msg_id;
} imb_cmd_header_t;

/* CMD_HELLO — mandatory first message */
typedef struct __attribute__((packed)) {
    uint8_t  msg_type;  /* IMB_MSG_CMD_HELLO */
    uint8_t  msg_id;
    uint32_t pin_hash;
} imb_pkt_cmd_hello_t;

/* EVENT_ACK — universal command reply */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;        /* IMB_MSG_EVENT_ACK */
    uint8_t acked_msg_id;
    uint8_t acked_msg_type;
    uint8_t status;          /* imb_ack_status_e */
} imb_pkt_event_ack_t;

/* EVENT_NOTIFY — tag scan */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t direction;
    char    uid[IMB_UID_LEN];
    char    name[IMB_NAME_LEN];
} imb_pkt_event_tag_t;

/* EVENT_NOTIFY — mode changed */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t mode;
} imb_pkt_event_mode_t;

/* REPORT_CHUNK — fragmented report */
typedef struct __attribute__((packed)) {
    uint8_t  msg_type;
    uint16_t report_id;
    uint16_t chunk_index;
    uint16_t chunk_total;
    uint16_t entries_in_chunk;
    /* entries[] follow in payload */
} imb_pkt_report_chunk_t;

/* REPORT_ENTRY */
typedef struct __attribute__((packed)) {
    uint16_t box_id;
    uint8_t  status;
    char     uid[IMB_UID_LEN];
    char     name[IMB_NAME_LEN];
} imb_pkt_report_entry_t;

/* LOG_ENTRY [NEW] */
typedef struct __attribute__((packed)) {
    uint16_t seq_id;
    uint16_t box_id;
    uint8_t  event_type; /* 0=INSERT, 1=EXTRACT, 2=AMBIGUOUS */
    char     uid[IMB_UID_LEN];
} imb_pkt_log_entry_t;

/* CMD_GET_LOG [NEW] */
typedef struct __attribute__((packed)) {
    uint8_t  msg_type;
    uint8_t  msg_id;
    uint16_t last_seen_id;
} imb_pkt_cmd_get_log_t;

/* EVENT_LOG_CHUNK [NEW] */
typedef struct __attribute__((packed)) {
    uint8_t  msg_type;
    uint16_t chunk_index;
    uint16_t chunk_total;
    uint8_t  entries_in_chunk;
    /* imb_pkt_log_entry_t entries[] follow */
} imb_pkt_event_log_chunk_t;

/* CMD_MODE */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t msg_id;
    uint8_t mode;
} imb_pkt_cmd_mode_t;

/* CMD_NAME */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t msg_id;
    char    uid[IMB_UID_LEN];
    char    name[IMB_NAME_LEN];
} imb_pkt_cmd_name_t;

/* CMD_ACCEPT */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t msg_id;
    char    uid[IMB_UID_LEN];
    uint8_t accepted;
} imb_pkt_cmd_accept_t;

#define IMB_PROTO_BUF_MAX 256

/* Advertisement Manufacturer Data (8 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t company_id;   /* 0xFFFF for now */
    uint32_t pin_hash;     /* CRC32 of PIN */
    uint8_t  op_mode;      /* imb_op_mode_e */
    uint8_t  mesh_epoch;   /* bit 7: unread report; bit 6: reg paused; bit 5-0: epoch counter */
} imb_pkt_adv_t;

/* 
 * DESIGN NOTE: For large-scale / commercial inventories (e.g. 5000+ items):
 * 1. Switch from "Full Replication" to "Query-on-Demand".
 * 2. Use "Light Sleep" with DTIM (WiFi radio stays listening) to allow 
 *    instant mesh-wide wakeups/interactivity at the cost of battery life.
 */

/* BLE UUIDs (Locked 2026-05-27) */
#define IMB_SERVICE_UUID        "e5d50000-01d0-47e0-afc5-01e466d9298e"
#define IMB_CHAR_EVENT_NOTIFY   "e5d50001-01d0-47e0-afc5-01e466d9298e"
#define IMB_CHAR_REPORT_NOTIFY  "e5d50002-01d0-47e0-afc5-01e466d9298e"
#define IMB_CHAR_COMMAND_WRITE  "e5d50003-01d0-47e0-afc5-01e466d9298e"

/* ── Pack functions ────────────────────────────────────────────────────── */
/* All return bytes written into buf, or 0 if buf is too small (max < sizeof msg). */

size_t imb_proto_pack_event_tag    (const imb_pkt_event_tag_t  *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_event_mode   (const imb_pkt_event_mode_t *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_event_ack    (const imb_pkt_event_ack_t  *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_report_chunk (const imb_pkt_report_chunk_t *msg, const imb_pkt_report_entry_t *entries, uint8_t *buf, size_t max);
size_t imb_proto_pack_cmd_mode     (const imb_pkt_cmd_mode_t   *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_cmd_name     (const imb_pkt_cmd_name_t   *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_cmd_accept   (const imb_pkt_cmd_accept_t *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_cmd_hello    (const imb_pkt_cmd_hello_t  *msg, uint8_t *buf, size_t max);

/* ── Unpack functions ──────────────────────────────────────────────────── */
/* All return 0 on success, -1 on wrong msg_type or truncated buffer. */

int imb_proto_unpack_event_tag    (const uint8_t *buf, size_t len, imb_pkt_event_tag_t  *out);
int imb_proto_unpack_event_mode   (const uint8_t *buf, size_t len, imb_pkt_event_mode_t *out);
int imb_proto_unpack_event_ack    (const uint8_t *buf, size_t len, imb_pkt_event_ack_t  *out);
int imb_proto_unpack_cmd_hello    (const uint8_t *buf, size_t len, imb_pkt_cmd_hello_t  *out);
int imb_proto_unpack_report_chunk (const uint8_t *buf, size_t len, imb_pkt_report_chunk_t *out, imb_pkt_report_entry_t *entries_out);

/* Dispatches on buf[0] (msg_type); populates the matching union member.
   Returns -1 if msg_type is not a known CMD type or buffer is truncated. */
int imb_proto_unpack_cmd(const uint8_t *buf, size_t len, void *out_struct);
