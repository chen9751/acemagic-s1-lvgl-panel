#ifndef S1_INPUT_DISPATCH_H
#define S1_INPUT_DISPATCH_H

#include "../ui/s1_ui.h"
#include "../ui/ui_router.h"
#include "../ui/pages/ui_led.h"


/*
 * Central input routing shared by the W1 remote. Music and LED are HOME branch
 * pages: they consume navigation locally and only BACK/HOME leave the branch.
 */
static inline void s1_input_dispatch_key(uint32_t key)
{
    s1_page_id_t before = s1_ui_router_current();

    if(before == S1_PAGE_MUSIC) {
        if(key == LV_KEY_UP || key == LV_KEY_ESC || key == LV_KEY_HOME) {
            s1_ui_key(LV_KEY_HOME);
            return;
        }

        s1_ui_key(key);
        return;
    }

    if(before == S1_PAGE_LED) {
        if(key == LV_KEY_ESC || key == LV_KEY_HOME) {
            s1_ui_led_hide();
            s1_ui_key(LV_KEY_HOME);
            return;
        }

        s1_ui_led_key(key);
        return;
    }

    s1_ui_key(key);

    if(s1_ui_router_current() == S1_PAGE_LED) {
        s1_ui_led_show();
    }
    else {
        s1_ui_led_hide();
    }
}

#endif
