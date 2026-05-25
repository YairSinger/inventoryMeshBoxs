#include "imb_delta.h"
#include <string.h>

static int uid_in_entries(const imb_delta_entry_t *arr, uint16_t count, const char *uid)
{
    for (uint16_t i = 0; i < count; i++)
        if (strncmp(arr[i].item.uid, uid, IMB_UID_LEN) == 0) return 1;
    return 0;
}

uint16_t imb_delta_compute(const imb_session_t *session,
                           imb_registry_t      *registry,
                           imb_delta_entry_t   *out,
                           uint16_t             max)
{
    uint16_t n = 0;

    /* pass 1: walk session present → PRESENT or FOREIGN */
    imb_entry_u present[IMB_REGISTRY_MAX_ITEMS];
    uint16_t    present_count = imb_session_get_present(session, present, IMB_REGISTRY_MAX_ITEMS);

    for (uint16_t i = 0; i < present_count && n < max; i++) {
        imb_delta_entry_t entry;
        strncpy(entry.item.uid, present[i].uid, IMB_UID_LEN - 1);
        entry.item.uid[IMB_UID_LEN - 1] = '\0';

        if (imb_registry_get(registry, present[i].uid, &entry.item) == IMB_REG_OK) {
            entry.status = IMB_DELTA_PRESENT;
        } else {
            entry.item.name[0] = '\0';
            entry.status = IMB_DELTA_FOREIGN;
        }
        out[n++] = entry;
    }

    /* pass 2: walk registry → MISSING if not in session present or ambiguous */
    imb_item_t reg_items[IMB_REGISTRY_MAX_ITEMS];
    uint16_t   reg_count = 0;
    imb_registry_get_all(registry, reg_items, &reg_count);

    imb_entry_u ambiguous[IMB_REGISTRY_MAX_ITEMS];
    uint16_t    ambiguous_count = imb_session_get_ambiguous(session, ambiguous, IMB_REGISTRY_MAX_ITEMS);

    for (uint16_t i = 0; i < reg_count && n < max; i++) {
        int in_present = 0;
        for (uint16_t j = 0; j < present_count; j++) {
            if (strncmp(reg_items[i].uid, present[j].uid, IMB_UID_LEN) == 0) { in_present = 1; break; }
        }
        int in_ambiguous = 0;
        for (uint16_t j = 0; j < ambiguous_count; j++) {
            if (strncmp(reg_items[i].uid, ambiguous[j].uid, IMB_UID_LEN) == 0) { in_ambiguous = 1; break; }
        }
        if (!in_present && !in_ambiguous) {
            imb_delta_entry_t entry;
            entry.item   = reg_items[i];
            entry.status = IMB_DELTA_MISSING;
            out[n++] = entry;
        }
    }

    /* pass 3: walk session ambiguous → AMBIGUOUS; fill name if registered */

    for (uint16_t i = 0; i < ambiguous_count && n < max; i++) {
        if (uid_in_entries(out, n, ambiguous[i].uid)) continue;

        imb_delta_entry_t entry;
        strncpy(entry.item.uid, ambiguous[i].uid, IMB_UID_LEN - 1);
        entry.item.uid[IMB_UID_LEN - 1] = '\0';

        imb_item_t reg_item;
        if (imb_registry_get(registry, ambiguous[i].uid, &reg_item) == IMB_REG_OK)
            entry.item.name[0] = '\0', strncpy(entry.item.name, reg_item.name, IMB_NAME_LEN - 1);
        else
            entry.item.name[0] = '\0';

        entry.status = IMB_DELTA_AMBIGUOUS;
        out[n++] = entry;
    }

    return n;
}
