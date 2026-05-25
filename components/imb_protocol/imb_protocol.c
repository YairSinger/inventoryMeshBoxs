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

size_t imb_proto_pack_report(const imb_pkt_report_t *msg, uint8_t *buf, size_t max)
{
    /* only pack the live portion: header + count entries */
    size_t used = sizeof(msg->msg_type) + sizeof(msg->count) +
                  msg->count * sizeof(imb_pkt_report_entry_t);
    if (max < used) return 0;
    memcpy(buf, msg, used);
    return used;
}

size_t imb_proto_pack_cmd_mode(const imb_pkt_cmd_mode_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_cmd_name(const imb_pkt_cmd_name_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

size_t imb_proto_pack_cmd_accept(const imb_pkt_cmd_accept_t *msg, uint8_t *buf, size_t max)
    { PACK(msg, buf, max); }

/* ── unpack helpers ────────────────────────────────────────────────────── */

#define UNPACK(type, expected_type, buf, len, out) \
    do { if ((len) < sizeof(type) || (buf)[0] != (expected_type)) return -1; \
         memcpy((out), (buf), sizeof(type)); return 0; } while (0)

int imb_proto_unpack_event_tag(const uint8_t *buf, size_t len, imb_pkt_event_tag_t *out)
    { UNPACK(imb_pkt_event_tag_t, IMB_MSG_EVENT_TAG, buf, len, out); }

int imb_proto_unpack_event_mode(const uint8_t *buf, size_t len, imb_pkt_event_mode_t *out)
    { UNPACK(imb_pkt_event_mode_t, IMB_MSG_EVENT_MODE, buf, len, out); }

int imb_proto_unpack_report(const uint8_t *buf, size_t len, imb_pkt_report_t *out)
{
    size_t header = sizeof(out->msg_type) + sizeof(out->count);
    if (len < header || buf[0] != IMB_MSG_REPORT) return -1;
    memcpy(out, buf, header);
    size_t expected = header + out->count * sizeof(imb_pkt_report_entry_t);
    if (len < expected || out->count > IMB_REPORT_MAX_ENTRIES) return -1;
    memcpy(out, buf, expected);
    return 0;
}

int imb_proto_unpack_cmd(const uint8_t *buf, size_t len, imb_cmd_u *out)
{
    if (len < 1) return -1;
    switch (buf[0]) {
        case IMB_MSG_CMD_MODE:
            UNPACK(imb_pkt_cmd_mode_t, IMB_MSG_CMD_MODE, buf, len, &out->mode);
        case IMB_MSG_CMD_NAME:
            UNPACK(imb_pkt_cmd_name_t, IMB_MSG_CMD_NAME, buf, len, &out->name);
        case IMB_MSG_CMD_ACCEPT:
            UNPACK(imb_pkt_cmd_accept_t, IMB_MSG_CMD_ACCEPT, buf, len, &out->accept);
        default:
            return -1;
    }
}
