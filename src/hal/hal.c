#include "hal.h"
#include "../../input/s1_input_dispatch.h"


static void keyboard_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code != LV_EVENT_KEY) {
        return;
    }

    lv_indev_t * indev = lv_event_get_target(e);

    /* Only handle key-down so one press cannot advance twice. */
    if(lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
        return;
    }

    uint32_t key = lv_indev_get_key(indev);
    s1_input_dispatch_key(key);
}


lv_display_t * sdl_hal_init(int32_t w, int32_t h)
{
    lv_group_set_default(lv_group_create());

    lv_display_t * disp = lv_sdl_window_create(w, h);
    lv_display_set_default(disp);

    lv_indev_t * mouse = lv_sdl_mouse_create();
    lv_indev_set_group(mouse, lv_group_get_default());
    lv_indev_set_display(mouse, disp);

    LV_IMAGE_DECLARE(mouse_cursor_icon);
    lv_obj_t * cursor_obj = lv_image_create(lv_screen_active());
    lv_image_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(mouse, cursor_obj);

    lv_indev_t * mousewheel = lv_sdl_mousewheel_create();
    lv_indev_set_display(mousewheel, disp);
    lv_indev_set_group(mousewheel, lv_group_get_default());

    lv_indev_t * kb = lv_sdl_keyboard_create();
    lv_indev_set_display(kb, disp);
    lv_indev_add_event_cb(kb, keyboard_event_cb, LV_EVENT_KEY, NULL);

    return disp;
}


/* ============================================================
 * ACEMAGIC S1 native LCD backend
 *
 * LVGL now renders in PARTIAL mode. The flush callback receives LVGL's
 * native invalid rectangles instead of forcing a complete framebuffer on
 * every refresh. We keep a full shadow framebuffer only so multiple LVGL
 * invalid areas can safely be coalesced while the pipe/USB side is busy.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define S1_LCD_WIDTH        170
#define S1_LCD_HEIGHT       320
#define S1_LCD_BPP          2
#define S1_LCD_FRAME_BYTES  (S1_LCD_WIDTH * S1_LCD_HEIGHT * S1_LCD_BPP)

#define S1_PIPE_MAGIC_0     'S'
#define S1_PIPE_MAGIC_1     '1'
#define S1_PIPE_MAGIC_2     'U'
#define S1_PIPE_MAGIC_3     'P'
#define S1_PIPE_VERSION     1
#define S1_PIPE_PARTIAL     1
#define S1_PIPE_FULL        2
#define S1_PIPE_HEADER_SIZE 18

/* A very short debounce lets LVGL finish emitting adjacent invalid areas. */
#define S1_DIRTY_DEBOUNCE_NS (2L * 1000L * 1000L)

static FILE * s1_lcd_pipe = NULL;
static uint8_t * s1_lcd_draw_buffer = NULL;
static uint8_t * s1_lcd_shadow = NULL;
static uint8_t * s1_lcd_tx_buffer = NULL;

static pthread_t s1_lcd_writer_thread;
static pthread_mutex_t s1_lcd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s1_lcd_cond = PTHREAD_COND_INITIALIZER;
static int s1_lcd_writer_started = 0;

static int s1_dirty_valid = 0;
static int s1_dirty_full = 0;
static int s1_dirty_x1 = 0;
static int s1_dirty_y1 = 0;
static int s1_dirty_x2 = 0;
static int s1_dirty_y2 = 0;

static void put_u16_le(uint8_t * p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_u32_le(uint8_t * p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void s1_merge_dirty_locked(
    int x1,
    int y1,
    int x2,
    int y2,
    int force_full)
{
    if(force_full) {
        x1 = 0;
        y1 = 0;
        x2 = S1_LCD_WIDTH - 1;
        y2 = S1_LCD_HEIGHT - 1;
        s1_dirty_full = 1;
    }

    if(!s1_dirty_valid) {
        s1_dirty_x1 = x1;
        s1_dirty_y1 = y1;
        s1_dirty_x2 = x2;
        s1_dirty_y2 = y2;
        s1_dirty_valid = 1;
    } else {
        if(x1 < s1_dirty_x1) s1_dirty_x1 = x1;
        if(y1 < s1_dirty_y1) s1_dirty_y1 = y1;
        if(x2 > s1_dirty_x2) s1_dirty_x2 = x2;
        if(y2 > s1_dirty_y2) s1_dirty_y2 = y2;
    }

    if(s1_dirty_x1 == 0 &&
       s1_dirty_y1 == 0 &&
       s1_dirty_x2 == S1_LCD_WIDTH - 1 &&
       s1_dirty_y2 == S1_LCD_HEIGHT - 1) {
        s1_dirty_full = 1;
    }
}

static void s1_build_pipe_header(
    uint8_t * header,
    uint8_t type,
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint32_t payload_bytes)
{
    header[0] = S1_PIPE_MAGIC_0;
    header[1] = S1_PIPE_MAGIC_1;
    header[2] = S1_PIPE_MAGIC_2;
    header[3] = S1_PIPE_MAGIC_3;
    header[4] = S1_PIPE_VERSION;
    header[5] = type;
    put_u16_le(header + 6, x);
    put_u16_le(header + 8, y);
    put_u16_le(header + 10, w);
    put_u16_le(header + 12, h);
    put_u32_le(header + 14, payload_bytes);
}

static void * s1_lcd_writer_main(void * arg)
{
    (void)arg;

    for(;;) {
        int x1;
        int y1;
        int x2;
        int y2;
        int full;

        pthread_mutex_lock(&s1_lcd_mutex);

        while(!s1_dirty_valid) {
            pthread_cond_wait(&s1_lcd_cond, &s1_lcd_mutex);
        }

        pthread_mutex_unlock(&s1_lcd_mutex);

        /*
         * LVGL can emit several adjacent invalid areas in one refresh pass.
         * Give those callbacks a tiny window to merge before snapshotting.
         */
        struct timespec ts = { 0, S1_DIRTY_DEBOUNCE_NS };
        nanosleep(&ts, NULL);

        pthread_mutex_lock(&s1_lcd_mutex);

        if(!s1_dirty_valid) {
            pthread_mutex_unlock(&s1_lcd_mutex);
            continue;
        }

        x1 = s1_dirty_x1;
        y1 = s1_dirty_y1;
        x2 = s1_dirty_x2;
        y2 = s1_dirty_y2;
        full = s1_dirty_full;

        const int width = x2 - x1 + 1;
        const int height = y2 - y1 + 1;
        const size_t row_bytes = (size_t)width * S1_LCD_BPP;
        const size_t payload_bytes = row_bytes * (size_t)height;

        for(int row = 0; row < height; row++) {
            const size_t src_offset =
                ((size_t)(y1 + row) * S1_LCD_WIDTH + (size_t)x1) * S1_LCD_BPP;
            const size_t dst_offset = (size_t)row * row_bytes;

            memcpy(
                s1_lcd_tx_buffer + dst_offset,
                s1_lcd_shadow + src_offset,
                row_bytes
            );
        }

        s1_dirty_valid = 0;
        s1_dirty_full = 0;

        pthread_mutex_unlock(&s1_lcd_mutex);

        if(s1_lcd_pipe) {
            uint8_t header[S1_PIPE_HEADER_SIZE];
            const uint8_t type = full ? S1_PIPE_FULL : S1_PIPE_PARTIAL;

            s1_build_pipe_header(
                header,
                type,
                (uint16_t)x1,
                (uint16_t)y1,
                (uint16_t)width,
                (uint16_t)height,
                (uint32_t)payload_bytes
            );

            const size_t header_written = fwrite(
                header,
                1,
                sizeof(header),
                s1_lcd_pipe
            );
            const size_t payload_written = fwrite(
                s1_lcd_tx_buffer,
                1,
                payload_bytes,
                s1_lcd_pipe
            );
            const int flush_rc = fflush(s1_lcd_pipe);

            if(header_written != sizeof(header) ||
               payload_written != payload_bytes ||
               flush_rc != 0) {
                fprintf(
                    stderr,
                    "S1 LCD: short pipe update header=%zu/%zu payload=%zu/%zu\n",
                    header_written,
                    sizeof(header),
                    payload_written,
                    payload_bytes
                );
                clearerr(s1_lcd_pipe);

                /* Requeue this dirty area; merge it with anything newer. */
                pthread_mutex_lock(&s1_lcd_mutex);
                s1_merge_dirty_locked(x1, y1, x2, y2, full);
                pthread_cond_signal(&s1_lcd_cond);
                pthread_mutex_unlock(&s1_lcd_mutex);
            }
        }
    }

    return NULL;
}

static void s1_lcd_flush_cb(
    lv_display_t * disp,
    const lv_area_t * area,
    uint8_t * px_map)
{
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    if(x1 < 0) x1 = 0;
    if(y1 < 0) y1 = 0;
    if(x2 >= S1_LCD_WIDTH) x2 = S1_LCD_WIDTH - 1;
    if(y2 >= S1_LCD_HEIGHT) y2 = S1_LCD_HEIGHT - 1;

    if(x1 <= x2 && y1 <= y2 &&
       s1_lcd_writer_started && s1_lcd_shadow) {
        const int width = x2 - x1 + 1;
        const int height = y2 - y1 + 1;
        const size_t row_bytes = (size_t)width * S1_LCD_BPP;
        const int force_full =
            x1 == 0 && y1 == 0 &&
            x2 == S1_LCD_WIDTH - 1 &&
            y2 == S1_LCD_HEIGHT - 1;

        pthread_mutex_lock(&s1_lcd_mutex);

        /*
         * In PARTIAL mode px_map contains the rendered invalid rectangle.
         * Copy it into a full logical shadow so updates can be coalesced
         * without losing pixels while the LCD path is slower than LVGL.
         */
        for(int row = 0; row < height; row++) {
            const size_t dst_offset =
                ((size_t)(y1 + row) * S1_LCD_WIDTH + (size_t)x1) * S1_LCD_BPP;
            const size_t src_offset = (size_t)row * row_bytes;

            memcpy(
                s1_lcd_shadow + dst_offset,
                px_map + src_offset,
                row_bytes
            );
        }

        s1_merge_dirty_locked(x1, y1, x2, y2, force_full);
        pthread_cond_signal(&s1_lcd_cond);
        pthread_mutex_unlock(&s1_lcd_mutex);
    }

    /* Never make LVGL wait for pipe/HID I/O. */
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

    lv_group_set_default(lv_group_create());

    s1_lcd_pipe = popen("node drivers/s1_lcd_runner.js", "w");

    if(!s1_lcd_pipe) {
        perror("S1 LCD: unable to start Node driver");
        return NULL;
    }

    /*
     * A screen-sized LVGL draw buffer avoids LVGL having to split a large
     * invalid rectangle merely because the draw buffer is too small.
     * s1_lcd_shadow keeps the latest logical 170x320 screen image.
     */
    s1_lcd_draw_buffer = malloc(S1_LCD_FRAME_BYTES);
    s1_lcd_shadow = calloc(1, S1_LCD_FRAME_BYTES);
    s1_lcd_tx_buffer = malloc(S1_LCD_FRAME_BYTES);

    if(!s1_lcd_draw_buffer || !s1_lcd_shadow || !s1_lcd_tx_buffer) {
        fprintf(stderr, "S1 LCD: framebuffer allocation failed\n");
        free(s1_lcd_draw_buffer);
        free(s1_lcd_shadow);
        free(s1_lcd_tx_buffer);
        s1_lcd_draw_buffer = NULL;
        s1_lcd_shadow = NULL;
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
        free(s1_lcd_draw_buffer);
        free(s1_lcd_shadow);
        free(s1_lcd_tx_buffer);
        s1_lcd_draw_buffer = NULL;
        s1_lcd_shadow = NULL;
        s1_lcd_tx_buffer = NULL;
        pclose(s1_lcd_pipe);
        s1_lcd_pipe = NULL;
        return NULL;
    }

    s1_lcd_writer_started = 1;

    lv_display_t * disp = lv_display_create(w, h);

    if(!disp) {
        fprintf(stderr, "S1 LCD: lv_display_create failed\n");
        return NULL;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    lv_display_set_buffers(
        disp,
        s1_lcd_draw_buffer,
        NULL,
        S1_LCD_FRAME_BYTES,
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    lv_display_set_flush_cb(disp, s1_lcd_flush_cb);
    lv_display_set_default(disp);

    printf(
        "S1 LCD HAL initialized: %dx%d RGB565 LVGL partial invalid-area mode\n",
        S1_LCD_WIDTH,
        S1_LCD_HEIGHT
    );

    return disp;
}
