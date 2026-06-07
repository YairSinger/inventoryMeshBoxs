#include "imb_protocol.h"
#include <string.h>

/* ── pack helpers ──────────────────────────────────────────────────────── */

#define PACK(msg, buf, max) \
    do { if ((max) < sizeof(*(msg))) return 0; \
         memcpy((buf), (msg), sizeof(*(msg))); \
         return sizeof(*(msg)); } while (0)

size_t imb_proto_pack_event_tag(const imb_pkt_event_tag_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_event_mode(const imb_pkt_event_mode_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_event_ack(const imb_pkt_event_ack_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_cmd_hello(const imb_pkt_cmd_hello_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_cmd_mode(const imb_pkt_cmd_mode_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_cmd_name(const imb_pkt_cmd_name_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_cmd_accept(const imb_pkt_cmd_accept_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_report_chunk(const imb_pkt_report_chunk_t *hdr,
                                   const imb_pkt_report_entry_t *entries,
                                   uint8_t *buf, size_t max)
{
    size_t hdr_sz     = sizeof(imb_pkt_report_chunk_t);
    size_t entries_sz = hdr->entries_in_chunk * sizeof(imb_pkt_report_entry_t);
    size_t total      = hdr_sz + entries_sz;
    if (max < total) return 0;
    memcpy(buf, hdr, hdr_sz);
    memcpy(buf + hdr_sz, entries, entries_sz);
    return total;
}

/* ── unpack helpers ────────────────────────────────────────────────────── */

#define UNPACK(type, expected_type, buf, len, out) \
    do { if ((len) < sizeof(type) || (buf)[0] != (expected_type)) return -1; \
         memcpy((out), (buf), sizeof(type)); return 0; } while (0)

int imb_proto_unpack_event_tag(const uint8_t *buf, size_t len, imb_pkt_event_tag_t *out)
    { UNPACK(imb_pkt_event_tag_t, IMB_MSG_EVENT_TAG, buf, len, out); }

int imb_proto_unpack_event_mode(const uint8_t *buf, size_t len, imb_pkt_event_mode_t *out)
    { UNPACK(imb_pkt_event_mode_t, IMB_MSG_EVENT_MODE, buf, len, out); }

int imb_proto_unpack_event_ack(const uint8_t *buf, size_t len, imb_pkt_event_ack_t *out)
    { UNPACK(imb_pkt_event_ack_t, IMB_MSG_EVENT_ACK, buf, len, out); }

int imb_proto_unpack_cmd_hello(const uint8_t *buf, size_t len, imb_pkt_cmd_hello_t *out)
    { UNPACK(imb_pkt_cmd_hello_t, IMB_MSG_CMD_HELLO, buf, len, out); }

int imb_proto_unpack_report_chunk(const uint8_t *buf, size_t len,
                                  imb_pkt_report_chunk_t *hdr_out,
                                  imb_pkt_report_entry_t *entries_out)
{
    size_t hdr_sz = sizeof(imb_pkt_report_chunk_t);
    if (len < hdr_sz || buf[0] != IMB_MSG_REPORT_CHUNK) return -1;
    memcpy(hdr_out, buf, hdr_sz);
    size_t entries_sz = hdr_out->entries_in_chunk * sizeof(imb_pkt_report_entry_t);
    if (len < hdr_sz + entries_sz) return -1;
    memcpy(entries_out, buf + hdr_sz, entries_sz);
    return 0;
}

int imb_proto_unpack_cmd(const uint8_t *buf, size_t len, void *out_struct)
{
    if (len < 1) return -1;
    switch (buf[0]) {
        case IMB_MSG_CMD_MODE:
            UNPACK(imb_pkt_cmd_mode_t,   IMB_MSG_CMD_MODE,   buf, len, (imb_pkt_cmd_mode_t *)out_struct);
        case IMB_MSG_CMD_NAME:
            UNPACK(imb_pkt_cmd_name_t,   IMB_MSG_CMD_NAME,   buf, len, (imb_pkt_cmd_name_t *)out_struct);
        case IMB_MSG_CMD_ACCEPT:
            UNPACK(imb_pkt_cmd_accept_t, IMB_MSG_CMD_ACCEPT, buf, len, (imb_pkt_cmd_accept_t *)out_struct);
        case IMB_MSG_CMD_HELLO:
            UNPACK(imb_pkt_cmd_hello_t,  IMB_MSG_CMD_HELLO,  buf, len, (imb_pkt_cmd_hello_t *)out_struct);
        default:
            return -1;
    }
}
