#pragma once

#include <stdint.h>
#include "imb_types.h"

typedef enum {
    IMB_REG_OK = 0,
    IMB_REG_ERR_NOT_FOUND,  /* uid does not exist in registry */
    IMB_REG_ERR_FULL,       /* registry is at max capacity */
    IMB_REG_ERR_HAL,        /* underlying NVS operation failed */
} imb_reg_err_e;

/* NVS HAL — injected at init so logic layer is testable without ESP-IDF.
   key  — null-terminated NVS key string
   ctx  — opaque pointer supplied at imb_registry_init; passed through unchanged */
typedef struct {
    imb_reg_err_e (*load)(const char *key, imb_item_t *out, void *ctx);
    imb_reg_err_e (*save)(const char *key, const imb_item_t *in, void *ctx);
    imb_reg_err_e (*erase)(const char *key, void *ctx);
    imb_reg_err_e (*load_u16)(const char *key, uint16_t *out, void *ctx);
    imb_reg_err_e (*save_u16)(const char *key, uint16_t val, void *ctx);
    void *ctx;
} imb_nvs_hal_t;

typedef struct {
    imb_nvs_hal_t *hal;
    uint16_t       count;                          /* current number of registered items */
    uint16_t       max;                            /* per-box capacity, persisted in NVS */
    imb_item_t     items[IMB_REGISTRY_MAX_ITEMS];  /* in-memory working set */
} imb_registry_t;

/* hal         — NVS HAL to use; must outlive reg
   default_max — capacity to use on first boot; ignored if max already in NVS */
imb_reg_err_e imb_registry_init(imb_registry_t *reg, imb_nvs_hal_t *hal, uint16_t default_max);

/* item — item to add; if uid already exists the record is overwritten */
imb_reg_err_e imb_registry_add(imb_registry_t *reg, const imb_item_t *item);

/* uid — 14 hex-char null-terminated UID string */
imb_reg_err_e imb_registry_remove(imb_registry_t *reg, const char *uid);

/* out — populated with a copy of the stored item on IMB_REG_OK */
imb_reg_err_e imb_registry_get(imb_registry_t *reg, const char *uid, imb_item_t *out);

/* out       — caller-provided array of at least IMB_REGISTRY_MAX_ITEMS elements
   count_out — set to the number of items written into out */
imb_reg_err_e imb_registry_get_all(imb_registry_t *reg,
                                   imb_item_t out[IMB_REGISTRY_MAX_ITEMS],
                                   uint16_t *count_out);

uint16_t imb_registry_count(const imb_registry_t *reg);
uint16_t imb_registry_max(const imb_registry_t *reg);
