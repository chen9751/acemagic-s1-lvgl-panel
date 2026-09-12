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
 * LVGL renders in PARTIAL mode. Each native invalid area is preserved as
 * its own queued rectangle instead of being collapsed immediately into a
 * single screen-spanning bounding box. The shadow framebuffer always holds
 * the newest logical pixels, so queued regions can safely be coalesced when
 * they truly overlap without losing newer content.
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

/* Let one LVGL refresh pass finish before the writer snapshots a region. */
#define S1_DIRTY_DEBOUNCE_NS (2L * 1000L * 1000L)
#define S1_DIRTY_QUEUE_CAPACITY 32

typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
    int full;
} s1_dirty_rect_t;

static FILE * s1_lcd_pipe = NULL;
static uint8_t * s1_lcd_draw_buffer = NULL;
static uint8_t * s1_lcd_shadow = NULL;
static uint8_t * s1_lcd_tx_buffer = NULL;

static pthread_t s1_lcd_writer_thread;
static pthread_mutex_t s1_lcd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s1_lcd_cond = PTHREAD_COND_INITIALIZER;
static int s1_lcd_writer_started = 0;

static s1_dirty_rect_t s1_dirty_queue[S1_DIRTY_QUEUE_CAPACITY];
static int s1_dirty_count = 0;

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

static int s1_rect_contains(
    const s1_dirty_rect_t * outer,
    const s1_dirty_rect_t * inner)
{
    return outer->x1 <= inner->x1 &&
           outer->y1 <= inner->y1 &&
           outer->x2 >= inner->x2 &&
           outer->y2 >= inner->y2;
}

static int s1_rects_touch_or_overlap(
    const s1_dirty_rect_t * a,
    const s1_dirty_rect_t * b)
{
    return !(a->x2 + 1 < b->x1 ||
             b->x2 + 1 < a->x1 ||
             a->y2 + 1 < b->y1 ||
             b->y2 + 1 < a->y1);
}

static int s1_rect_area(const s1_dirty_rect_t * rect)
{
    return (rect->x2 - rect->x1 + 1) *
           (rect->y2 - rect->y1 + 1);
}

static s1_dirty_rect_t s1_rect_union(
    const s1_dirty_rect_t * a,
    const s1_dirty_rect_t * b)
{
    s1_dirty_rect_t out;

    out.x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    out.y1 = a->y1 < b->y1 ? a->y1 : b->y1;
    out.x2 = a->x2 > b->x2 ? a->x2 : b->x2;
    out.y2 = a->y2 > b->y2 ? a->y2 : b->y2;
    out.full = a->full || b->full;

    return out;
}

static void s1_remove_dirty_locked(int index)
{
    if(index < 0 || index >= s1_dirty_count) {
        return;
    }

    if(index + 1 < s1_dirty_count) {
        memmove(
            &s1_dirty_queue[index],
            &s1_dirty_queue[index + 1],
            (size_t)(s1_dirty_count - index - 1) * sizeof(s1_dirty_rect_t)
        );
    }

    s1_dirty_count--;
}

static void s1_enqueue_dirty_locked(
    int x1,
    int y1,
    int x2,
    int y2,
    int force_full)
{
    s1_dirty_rect_t incoming = {
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .full = force_full ? 1 : 0
    };

    if(force_full) {
        incoming.x1 = 0;
        incoming.y1 = 0;
        incoming.x2 = S1_LCD_WIDTH - 1;
        incoming.y2 = S1_LCD_HEIGHT - 1;
        s1_dirty_queue[0] = incoming;
        s1_dirty_count = 1;
        return;
    }

    for(int i = 0; i < s1_dirty_count; i++) {
        if(s1_dirty_queue[i].full ||
           s1_rect_contains(&s1_dirty_queue[i], &incoming)) {
            return;
        }
    }

    for(int i = 0; i < s1_dirty_count;) {
        if(s1_rect_contains(&incoming, &s1_dirty_queue[i])) {
            s1_remove_dirty_locked(i);
            continue;
        }
        i++;
    }

    /*
     * Merge only when the union does not cover more pixels than sending the
     * two rectangles separately. This keeps nearby LVGL invalid areas useful
     * without recreating the old giant bounding-box behavior.
     */
    for(int i = 0; i < s1_dirty_count; i++) {
        if(!s1_rects_touch_or_overlap(&s1_dirty_queue[i], &incoming)) {
            continue;
        }

        s1_dirty_rect_t combined =
            s1_rect_union(&s1_dirty_queue[i], &incoming);
        const int separate_area =
            s1_rect_area(&s1_dirty_queue[i]) + s1_rect_area(&incoming);

        if(s1_rect_area(&combined) <= separate_area) {
            s1_dirty_queue[i] = combined;
            return;
        }
    }

    if(s1_dirty_count < S1_DIRTY_QUEUE_CAPACITY) {
        s1_dirty_queue[s1_dirty_count++] = incoming;
        return;
    }

    /*
     * Queue overflow is rare. Never drop pixels: merge into the existing
     * rectangle that introduces the least extra area.
     */
    int best_index = 0;
    int best_growth = -1;

    for(int i = 0; i < s1_dirty_count; i++) {
        const s1_dirty_rect_t combined =
            s1_rect_union(&s1_dirty_queue[i], &incoming);
        const int growth =
            s1_rect_area(&combined) - s1_rect_area(&s1_dirty_queue[i]);

        if(best_growth < 0 || growth < best_growth) {
            best_growth = growth;
            best_index = i;
        }
    }

    s1_dirty_queue[best_index] =
        s1_rect_union(&s1_dirty_queue[best_index], &incoming);
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
        s1_dirty_rect_t dirty;

        pthread_mutex_lock(&s1_lcd_mutex);

        while(s1_dirty_count == 0) {
            pthread_cond_wait(&s1_lcd_cond, &s1_lcd_mutex);
        }

        pthread_mutex_unlock(&s1_lcd_mutex);

        struct timespec ts = { 0, S1_DIRTY_DEBOUNCE_NS };
        nanosleep(&ts, NULL);

        pthread_mutex_lock(&s1_lcd_mutex);

        if(s1_dirty_count == 0) {
            pthread_mutex_unlock(&s1_lcd_mutex);
            continue;
        }

        /* Prefer smaller regions so key feedback reaches the pipe first. */
        int best_index = 0;
        int best_area = s1_rect_area(&s1_dirty_queue[0]);

        for(int i = 1; i < s1_dirty_count; i++) {
            const int area = s1_rect_area(&s1_dirty_queue[i]);
            if(area < best_area) {
                best_area = area;
                best_index = i;
            }
        }

        dirty = s1_dirty_queue[best_index];
        s1_remove_dirty_locked(best_index);

        const int width = dirty.x2 - dirty.x1 + 1;
        const int height = dirty.y2 - dirty.y1 + 1;
        const size_t row_bytes = (size_t)width * S1_LCD_BPP;
        const size_t payload_bytes = row_bytes * (size_t)height;

        for(int row = 0; row < height; row++) {
            const size_t src_offset =
                ((size_t)(dirty.y1 + row) * S1_LCD_WIDTH +
                 (size_t)dirty.x1) * S1_LCD_BPP;
            const size_t dst_offset = (size_t)row * row_bytes;

            memcpy(
                s1_lcd_tx_buffer + dst_offset,
                s1_lcd_shadow + src_offset,
                row_bytes
            );
        }

        pthread_mutex_unlock(&s1_lcd_mutex);

        if(s1_lcd_pipe) {
            uint8_t header[S1_PIPE_HEADER_SIZE];
            const uint8_t type =
                dirty.full ? S1_PIPE_FULL : S1_PIPE_PARTIAL;

            s1_build_pipe_header(
                header,
                type,
                (uint16_t)dirty.x1,
                (uint16_t)dirty.y1,
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

                pthread_mutex_lock(&s1_lcd_mutex);
                s1_enqueue_dirty_locked(
                    dirty.x1,
                    dirty.y1,
                    dirty.x2,
                    dirty.y2,
                    dirty.full
                );
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

        s1_enqueue_dirty_locked(x1, y1, x2, y2, force_full);
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
        "S1 LCD HAL initialized: %dx%d RGB565 preserved dirty-area mode\n",
        S1_LCD_WIDTH,
        S1_LCD_HEIGHT
    );

    return disp;
}
