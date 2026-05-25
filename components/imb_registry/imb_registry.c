#include "imb_registry.h"
#include <string.h>
#include <stdio.h>

#define KEY_COUNT "item_count"
#define KEY_MAX   "item_max"

static void slot_key(char *buf, size_t len, uint16_t idx)
{
    snprintf(buf, len, "slot_%u", (unsigned)idx);
}

imb_reg_err_e imb_registry_init(imb_registry_t *reg, imb_nvs_hal_t *hal, uint16_t default_max)
{
    reg->hal   = hal;
    reg->count = 0;
    reg->max   = default_max;

    uint16_t stored;
    if (hal->load_u16(KEY_MAX, &stored, hal->ctx) == IMB_REG_OK)
        reg->max = stored;
    else
        hal->save_u16(KEY_MAX, default_max, hal->ctx);

    if (reg->max > IMB_REGISTRY_MAX_ITEMS)
        reg->max = IMB_REGISTRY_MAX_ITEMS;

    if (hal->load_u16(KEY_COUNT, &stored, hal->ctx) == IMB_REG_OK)
        reg->count = stored;

    /* load persisted items into memory */
    for (uint16_t i = 0; i < reg->count; i++) {
        char key[16];
        slot_key(key, sizeof(key), i);
        hal->load(key, &reg->items[i], hal->ctx);
    }

    return IMB_REG_OK;
}

imb_reg_err_e imb_registry_add(imb_registry_t *reg, const imb_item_t *item)
{
    /* check for existing uid → overwrite */
    for (uint16_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].uid, item->uid) == 0) {
            reg->items[i] = *item;
            char key[16];
            slot_key(key, sizeof(key), i);
            return reg->hal->save(key, item, reg->hal->ctx);
        }
    }

    if (reg->count >= reg->max)
        return IMB_REG_ERR_FULL;

    uint16_t idx = reg->count;
    reg->items[idx] = *item;
    reg->count++;

    char key[16];
    slot_key(key, sizeof(key), idx);
    imb_reg_err_e err = reg->hal->save(key, item, reg->hal->ctx);
    if (err != IMB_REG_OK) {
        reg->count--;
        return err;
    }
    reg->hal->save_u16(KEY_COUNT, reg->count, reg->hal->ctx);
    return IMB_REG_OK;
}

imb_reg_err_e imb_registry_get(imb_registry_t *reg, const char *uid, imb_item_t *out)
{
    for (uint16_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].uid, uid) == 0) {
            *out = reg->items[i];
            return IMB_REG_OK;
        }
    }
    return IMB_REG_ERR_NOT_FOUND;
}

imb_reg_err_e imb_registry_remove(imb_registry_t *reg, const char *uid)
{
    for (uint16_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].uid, uid) == 0) {
            uint16_t last = reg->count - 1;
            if (i != last) {
                /* swap with last slot in NVS */
                reg->items[i] = reg->items[last];
                char key[16];
                slot_key(key, sizeof(key), i);
                reg->hal->save(key, &reg->items[i], reg->hal->ctx);
            }
            char last_key[16];
            slot_key(last_key, sizeof(last_key), last);
            reg->hal->erase(last_key, reg->hal->ctx);
            reg->count--;
            reg->hal->save_u16(KEY_COUNT, reg->count, reg->hal->ctx);
            return IMB_REG_OK;
        }
    }
    return IMB_REG_ERR_NOT_FOUND;
}

imb_reg_err_e imb_registry_get_all(imb_registry_t *reg,
                                   imb_item_t out[IMB_REGISTRY_MAX_ITEMS],
                                   uint16_t *count_out)
{
    for (uint16_t i = 0; i < reg->count; i++)
        out[i] = reg->items[i];
    *count_out = reg->count;
    return IMB_REG_OK;
}

uint16_t imb_registry_count(const imb_registry_t *reg) { return reg->count; }
uint16_t imb_registry_max(const imb_registry_t *reg)   { return reg->max;   }
