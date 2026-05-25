#pragma once

#include <stdint.h>
#include <stddef.h>
#include "imb_types.h"
#include "imb_delta.h"

/* ── Message type byte (first byte of every BLE payload) ──────────────── */

typedef enum {
    IMB_MSG_EVENT_TAG    = 0x01,  /* box→phone: real-time tag scan event (insert/extract/ambiguous)
                                     name field is empty string for unregistered/foreign tags */
    IMB_MSG_EVENT_MODE   = 0x02,  /* box→phone: operational mode changed */
    IMB_MSG_REPORT       = 0x03,  /* box→phone: consolidated delta report on lid close
                                     phase 1: single-box entries only
                                     phase 2: mesh-wide, after cross-box resolution */
    IMB_MSG_CMD_MODE     = 0x10,  /* phone→box: set operational mode (e.g. START/END_REGISTRATION) */
    IMB_MSG_CMD_NAME     = 0x11,  /* phone→box: assign human-readable name to a scanned uid */
    IMB_MSG_CMD_ACCEPT   = 0x12,  /* phone→box: accept (1) or reject (0) a foreign tag */
} imb_msg_type_e;

/* ── Packed wire structs ───────────────────────────────────────────────── */

/* EVENT_NOTIFY — tag scan; name empty when tag is unregistered */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;           /* IMB_MSG_EVENT_TAG */
    uint8_t direction;          /* imb_direction_e */
    char    uid[IMB_UID_LEN];
    char    name[IMB_NAME_LEN];
} imb_pkt_event_tag_t;

/* EVENT_NOTIFY — operational mode changed */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;  /* IMB_MSG_EVENT_MODE */
    uint8_t mode;      /* imb_op_mode_e */
} imb_pkt_event_mode_t;

/* REPORT_NOTIFY — one entry per item in the consolidated delta report
   box_id = 0 in phase 1; populated per-source-box in phase 2 mesh report */
typedef struct __attribute__((packed)) {
    uint16_t box_id;            /* source box (last 2 bytes of MAC address) */
    uint8_t  status;            /* imb_delta_status_e */
    char     uid[IMB_UID_LEN];
    char     name[IMB_NAME_LEN];
} imb_pkt_report_entry_t;

/* REPORT_NOTIFY — full consolidated report
   BLE fragmentation across MTU boundaries is handled by the driver layer */
typedef struct __attribute__((packed)) {
    uint8_t                msg_type;  /* IMB_MSG_REPORT */
    uint16_t               count;
    imb_pkt_report_entry_t entries[IMB_REPORT_MAX_ENTRIES];
} imb_pkt_report_t;

/* COMMAND_WRITE — phone→box: set operational mode */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;  /* IMB_MSG_CMD_MODE */
    uint8_t mode;      /* imb_op_mode_e */
} imb_pkt_cmd_mode_t;

/* COMMAND_WRITE — phone→box: assign name to a scanned uid (registration flow) */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;           /* IMB_MSG_CMD_NAME */
    char    uid[IMB_UID_LEN];
    char    name[IMB_NAME_LEN];
} imb_pkt_cmd_name_t;

/* COMMAND_WRITE — phone→box: accept or reject a foreign tag */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;           /* IMB_MSG_CMD_ACCEPT */
    char    uid[IMB_UID_LEN];
    uint8_t accepted;           /* 1 = accepted, 0 = rejected */
} imb_pkt_cmd_accept_t;

/* Tagged union for unpack_cmd — msg_type is at offset 0 in all three members */
typedef union {
    uint8_t              msg_type;  /* read first to determine which member is valid */
    imb_pkt_cmd_mode_t   mode;
    imb_pkt_cmd_name_t   name;
    imb_pkt_cmd_accept_t accept;
} imb_cmd_u;

/* Largest possible wire message; use for stack-allocated BLE TX/RX buffers */
#define IMB_PROTO_BUF_MAX sizeof(imb_pkt_report_t)

/* ── Pack functions ────────────────────────────────────────────────────── */
/* All return bytes written into buf, or 0 if buf is too small (max < sizeof msg). */

size_t imb_proto_pack_event_tag (const imb_pkt_event_tag_t  *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_event_mode(const imb_pkt_event_mode_t *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_report    (const imb_pkt_report_t     *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_cmd_mode  (const imb_pkt_cmd_mode_t   *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_cmd_name  (const imb_pkt_cmd_name_t   *msg, uint8_t *buf, size_t max);
size_t imb_proto_pack_cmd_accept(const imb_pkt_cmd_accept_t *msg, uint8_t *buf, size_t max);

/* ── Unpack functions ──────────────────────────────────────────────────── */
/* All return 0 on success, -1 on wrong msg_type or truncated buffer. */

int imb_proto_unpack_event_tag (const uint8_t *buf, size_t len, imb_pkt_event_tag_t  *out);
int imb_proto_unpack_event_mode(const uint8_t *buf, size_t len, imb_pkt_event_mode_t *out);
int imb_proto_unpack_report    (const uint8_t *buf, size_t len, imb_pkt_report_t     *out);

/* Dispatches on buf[0] (msg_type); populates the matching union member.
   Returns -1 if msg_type is not a known CMD type or buffer is truncated. */
int imb_proto_unpack_cmd(const uint8_t *buf, size_t len, imb_cmd_u *out);
