#ifndef S1_INPUT_DISPATCH_H
#define S1_INPUT_DISPATCH_H

#include "../ui/s1_ui.h"
#include "../ui/pages/ui_music_v2.h"

/*
 * W1 input has one control owner: s1_ui_key(). The Music visual layer only
 * receives non-owning notifications for button feedback and visibility.
 */
static inline void s1_input_dispatch_key(uint32_t key)
{
    s1_ui_music_v2_key_feedback(key);
    s1_ui_key(key);
    /* Page changes happen inside s1_ui_key(), so synchronize the overlay only
     * after routing the key. This makes the redesigned Music page visible
     * immediately when UP/DOWN lands on S1_PAGE_MUSIC instead of relying on
     * the periodic timer to bring it to the foreground. */
    s1_ui_music_v2_sync_visibility();
}

#endif
