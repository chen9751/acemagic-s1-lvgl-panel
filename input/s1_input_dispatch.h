#ifndef S1_INPUT_DISPATCH_H
#define S1_INPUT_DISPATCH_H

#include "../ui/s1_ui.h"

/*
 * W1 input must have exactly one owner: s1_ui_key().
 *
 * Older revisions special-cased Music/LED here and also showed separate
 * Home/LED overlays. That bypassed the new four-page router, so UP/DOWN could
 * not cycle HOME -> HA -> LED -> MUSIC and the old Home/LED layouts covered
 * the redesigned panels. Keep this dispatcher deliberately thin.
 */
static inline void s1_input_dispatch_key(uint32_t key)
{
    s1_ui_key(key);
}

#endif
