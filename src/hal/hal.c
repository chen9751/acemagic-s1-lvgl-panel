#include "hal.h"
#include "../../input/s1_input_dispatch.h"


static void keyboard_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code != LV_EVENT_KEY) {
        return;
    }

    lv_indev_t * indev = lv_event_get_target(e);

    /*
     * 只处理按下状态，
     * 防止一次按键跳两页。
     */
    if(lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
        return;
    }

    uint32_t key = lv_indev_get_key(indev);

    s1_input_dispatch_key(key);
}


lv_display_t * sdl_hal_init(int32_t w, int32_t h)
{
    lv_group_set_default(
        lv_group_create()
    );


    /* Display */
    lv_display_t * disp =
        lv_sdl_window_create(w, h);

    lv_display_set_default(disp);


    /* Mouse */
    lv_indev_t * mouse =
        lv_sdl_mouse_create();

    lv_indev_set_group(
        mouse,
        lv_group_get_default()
    );

    lv_indev_set_display(
        mouse,
        disp
    );


    /* Mouse cursor */
    LV_IMAGE_DECLARE(mouse_cursor_icon);

    lv_obj_t * cursor_obj =
        lv_image_create(lv_screen_active());

    lv_image_set_src(
        cursor_obj,
        &mouse_cursor_icon
    );

    lv_indev_set_cursor(
        mouse,
        cursor_obj
    );


    /* Mouse wheel */
    lv_indev_t * mousewheel =
        lv_sdl_mousewheel_create();

    lv_indev_set_display(
        mousewheel,
        disp
    );

    lv_indev_set_group(
        mousewheel,
        lv_group_get_default()
    );


    /* Keyboard */
    lv_indev_t * kb =
        lv_sdl_keyboard_create();

    lv_indev_set_display(
        kb,
        disp
    );

    lv_indev_add_event_cb(
        kb,
        keyboard_event_cb,
        LV_EVENT_KEY,
        NULL
    );


    return disp;
}
