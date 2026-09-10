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


/* ============================================================
 * ACEMAGIC S1 native LCD backend
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define S1_LCD_WIDTH       170
#define S1_LCD_HEIGHT      320
#define S1_LCD_FRAME_BYTES (S1_LCD_WIDTH * S1_LCD_HEIGHT * 2)

static FILE * s1_lcd_pipe = NULL;
static uint8_t * s1_lcd_buffer = NULL;

static void s1_lcd_flush_cb(
    lv_display_t * disp,
    const lv_area_t * area,
    uint8_t * px_map)
{
    (void)area;

    if(s1_lcd_pipe) {
        size_t written = fwrite(
            px_map,
            1,
            S1_LCD_FRAME_BYTES,
            s1_lcd_pipe
        );

        if(written != S1_LCD_FRAME_BYTES) {
            fprintf(
                stderr,
                "S1 LCD: short framebuffer write %zu/%d\n",
                written,
                S1_LCD_FRAME_BYTES
            );
        }

        fflush(s1_lcd_pipe);
    }

    lv_display_flush_ready(disp);
}

lv_display_t * s1_hal_init(int32_t w, int32_t h)
{
    if(w != S1_LCD_WIDTH || h != S1_LCD_HEIGHT) {
        fprintf(
            stderr,
            "S1 LCD: invalid resolution %dx%d\n",
            (int)w,
            (int)h
        );
        return NULL;
    }

    lv_group_set_default(
        lv_group_create()
    );

    s1_lcd_pipe = popen(
        "node drivers/s1_lcd_runner.js",
        "w"
    );

    if(!s1_lcd_pipe) {
        perror("S1 LCD: unable to start Node driver");
        return NULL;
    }

    /*
     * Full 170x320 RGB565 framebuffer.
     */
    s1_lcd_buffer = malloc(S1_LCD_FRAME_BYTES);

    if(!s1_lcd_buffer) {
        fprintf(stderr, "S1 LCD: framebuffer allocation failed\n");
        pclose(s1_lcd_pipe);
        s1_lcd_pipe = NULL;
        return NULL;
    }

    lv_display_t * disp =
        lv_display_create(w, h);

    if(!disp) {
        fprintf(stderr, "S1 LCD: lv_display_create failed\n");
        free(s1_lcd_buffer);
        s1_lcd_buffer = NULL;
        pclose(s1_lcd_pipe);
        s1_lcd_pipe = NULL;
        return NULL;
    }

    /*
     * S1 panel native format.
     */
    lv_display_set_color_format(
        disp,
        LV_COLOR_FORMAT_RGB565
    );

    lv_display_set_buffers(
        disp,
        s1_lcd_buffer,
        NULL,
        S1_LCD_FRAME_BYTES,
        LV_DISPLAY_RENDER_MODE_FULL
    );

    lv_display_set_flush_cb(
        disp,
        s1_lcd_flush_cb
    );

    lv_display_set_default(disp);

    printf(
        "S1 LCD HAL initialized: %dx%d RGB565\n",
        S1_LCD_WIDTH,
        S1_LCD_HEIGHT
    );

    return disp;
}
