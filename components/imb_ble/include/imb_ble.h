#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "imb_protocol.h"

/**
 * @brief Callback for commands received from the phone.
 * @param hdr Pointer to the command header (msg_type, msg_id).
 * @param payload Pointer to the command-specific payload (after header).
 */
typedef void (*imb_ble_cmd_cb_t)(const imb_cmd_header_t *hdr, const void *payload);

/**
 * @brief Configuration for the BLE component.
 */
typedef struct {
    char        box_name[IMB_NAME_LEN];
    uint32_t    pin_hash;
    imb_op_mode_e initial_mode;
    
    /* Callbacks */
    imb_ble_cmd_cb_t on_cmd;
} imb_ble_config_t;

/**
 * @brief Initialize the BLE server and start advertising.
 */
esp_err_t imb_ble_init(const imb_ble_config_t *config);

/**
 * @brief Update the operational mode (advertisement + EVENT_MODE notification).
 */
esp_err_t imb_ble_set_mode(imb_op_mode_e mode);

/**
 * @brief Send an EVENT_TAG notification.
 */
esp_err_t imb_ble_notify_event(const imb_pkt_event_tag_t *event);

/**
 * @brief Send an EVENT_ACK to a previously received command.
 */
esp_err_t imb_ble_send_ack(uint8_t acked_msg_id, uint8_t acked_msg_type, imb_ack_status_e status);

/**
 * @brief Send a REPORT_CHUNK notification.
 */
esp_err_t imb_ble_send_report_chunk(const imb_pkt_report_chunk_t *chunk, const imb_pkt_report_entry_t *entries);

/**
 * @brief Disconnect the current client (e.g. on PIN mismatch or HELLO timeout).
 */
void imb_ble_disconnect(void);
