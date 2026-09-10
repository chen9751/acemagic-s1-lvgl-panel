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
#include <string.h>
#include <pthread.h>

#define S1_LCD_WIDTH       170
#define S1_LCD_HEIGHT      320
#define S1_LCD_FRAME_BYTES (S1_LCD_WIDTH * S1_LCD_HEIGHT * 2)

static FILE * s1_lcd_pipe = NULL;
static uint8_t * s1_lcd_buffer = NULL;
static uint8_t * s1_lcd_pending_buffer = NULL;
static uint8_t * s1_lcd_tx_buffer = NULL;

static pthread_t s1_lcd_writer_thread;
static pthread_mutex_t s1_lcd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s1_lcd_cond = PTHREAD_COND_INITIALIZER;
static int s1_lcd_frame_pending = 0;
static int s1_lcd_writer_started = 0;

static void * s1_lcd_writer_main(void * arg)
{
    (void)arg;

    for(;;) {
        pthread_mutex_lock(&s1_lcd_mutex);

        while(!s1_lcd_frame_pending) {
            pthread_cond_wait(
                &s1_lcd_cond,
                &s1_lcd_mutex
            );
        }

        /*
         * Copy the newest queued framebuffer into a private transmit
         * buffer, then release the lock before touching the pipe.
         * If LVGL produces more frames while fwrite() is blocked, its
         * flush callback simply overwrites s1_lcd_pending_buffer.
         */
        memcpy(
            s1_lcd_tx_buffer,
            s1_lcd_pending_buffer,
            S1_LCD_FRAME_BYTES
        );
        s1_lcd_frame_pending = 0;

        pthread_mutex_unlock(&s1_lcd_mutex);

        if(s1_lcd_pipe) {
            size_t written = fwrite(
                s1_lcd_tx_buffer,
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
                clearerr(s1_lcd_pipe);
            }

            fflush(s1_lcd_pipe);
        }
    }

    return NULL;
}

static void s1_lcd_flush_cb(
    lv_display_t * disp,
    const lv_area_t * area,
    uint8_t * px_map)
{
    (void)area;

    if(s1_lcd_writer_started && s1_lcd_pending_buffer) {
        pthread_mutex_lock(&s1_lcd_mutex);

        /*
         * Latest-frame-wins queue. There is intentionally only one
         * pending framebuffer: intermediate UI frames are discarded
         * whenever the LCD/USB path is slower than LVGL rendering.
         */
        memcpy(
            s1_lcd_pending_buffer,
            px_map,
            S1_LCD_FRAME_BYTES
        );
        s1_lcd_frame_pending = 1;

        pthread_cond_signal(&s1_lcd_cond);
        pthread_mutex_unlock(&s1_lcd_mutex);
    }

    /*
     * The LVGL main loop never waits for pipe/HID transmission.
     */
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
     * Three full-frame buffers:
     *  - LVGL render buffer
     *  - newest frame waiting for the writer thread
     *  - writer thread's private pipe transmit buffer
     */
    s1_lcd_buffer = malloc(S1_LCD_FRAME_BYTES);
    s1_lcd_pending_buffer = malloc(S1_LCD_FRAME_BYTES);
    s1_lcd_tx_buffer = malloc(S1_LCD_FRAME_BYTES);

    if(!s1_lcd_buffer ||
       !s1_lcd_pending_buffer ||
       !s1_lcd_tx_buffer) {
        fprintf(stderr, "S1 LCD: framebuffer allocation failed\n");
        free(s1_lcd_buffer);
        free(s1_lcd_pending_buffer);
        free(s1_lcd_tx_buffer);
        s1_lcd_buffer = NULL;
        s1_lcd_pending_buffer = NULL;
        s1_lcd_tx_buffer = NULL;
        pclose(s1_lcd_pipe);
        s1_lcd_pipe = NULL;
        return NULL;
    }

    if(pthread_create(
        &s1_lcd_writer_thread,
        NULL,
        s1_lcd_writer_main,
        NULL
    ) != 0) {
        fprintf(stderr, "S1 LCD: unable to start writer thread\n");
        free(s1_lcd_buffer);
        free(s1_lcd_pending_buffer);
        free(s1_lcd_tx_buffer);
        s1_lcd_buffer = NULL;
        s1_lcd_pending_buffer = NULL;
        s1_lcd_tx_buffer = NULL;
        pclose(s1_lcd_pipe);
        s1_lcd_pipe = NULL;
        return NULL;
    }

    s1_lcd_writer_started = 1;

    lv_display_t * disp =
        lv_display_create(w, h);

    if(!disp) {
        fprintf(stderr, "S1 LCD: lv_display_create failed\n");
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
        "S1 LCD HAL initialized: %dx%d RGB565 async writer\n",
        S1_LCD_WIDTH,
        S1_LCD_HEIGHT
    );

    return disp;
}
