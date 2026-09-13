#include "ui_music_v2.h"
#include "../s1_ui.h"
#include "../ui_router.h"
#include "lvgl/lvgl.h"

#define MUSIC_BLUE       0x20B7F5
#define MUSIC_YELLOW     0xFFD05A
#define MUSIC_GRAY       0x6D7882
#define MUSIC_CARD       0x101C27
#define MUSIC_CARD_INNER 0x0B1B25
#define MUSIC_BORDER     0x263A48
#define MUSIC_TEXT       0xDCEAF3
#define MUSIC_SUBTEXT    0x8193A0
#define MUSIC_DARK       0x071018

#define MUSIC_ICON_PAUSE "\xEF\x81\x8C"

static lv_obj_t *music_panel;
static lv_obj_t *status_card;
static lv_obj_t *status_inner;
static lv_obj_t *status_dot;
static lv_obj_t *state_label;
static lv_obj_t *note_label;
static lv_obj_t *wave_bars[5];
static lv_obj_t *progress_fill;
static lv_obj_t *progress_thumb;
static lv_obj_t *buttons[3];
static lv_obj_t *button_labels[3];
static lv_obj_t *lock_label;
static lv_timer_t *feedback_timer;

static bool media_present;
static bool playing;
static bool locked;
static bool state_initialized;
static int progress_bucket = -1;
static int highlighted = -1;

static uint32_t state_color(void)
{
    if(!media_present) return MUSIC_GRAY;
    return playing ? MUSIC_BLUE : MUSIC_YELLOW;
}

static void style_plain_object(lv_obj_t *obj)
{
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static void restore_button_visual(int index)
{
    if(index < 0 || index > 2 || buttons[index] == NULL) return;

    if(index == 1) {
        uint32_t color = state_color();
        lv_obj_set_style_border_width(buttons[index], 2, 0);
        if(media_present) {
            lv_obj_set_style_bg_color(buttons[index], lv_color_hex(color), 0);
            lv_obj_set_style_bg_opa(buttons[index], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(buttons[index], lv_color_hex(color), 0);
            lv_obj_set_style_text_color(button_labels[index], lv_color_hex(MUSIC_DARK), 0);
        } else {
            lv_obj_set_style_bg_color(buttons[index], lv_color_hex(MUSIC_CARD), 0);
            lv_obj_set_style_bg_opa(buttons[index], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_GRAY), 0);
            lv_obj_set_style_text_color(button_labels[index], lv_color_hex(MUSIC_GRAY), 0);
        }
    } else {
        lv_obj_set_style_border_width(buttons[index], 1, 0);
        lv_obj_set_style_bg_color(buttons[index], lv_color_hex(MUSIC_CARD), 0);
        lv_obj_set_style_bg_opa(buttons[index], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_BORDER), 0);
        lv_obj_set_style_text_color(button_labels[index], lv_color_hex(MUSIC_TEXT), 0);
    }
}

static void apply_state_visual(void)
{
    if(status_inner == NULL || note_label == NULL || progress_fill == NULL) return;

    uint32_t color = state_color();

    /* Keep the large outer card static.  Only these smaller objects change
     * with playback state so LVGL's dirty area stays compact. */
    lv_obj_set_style_border_color(status_inner, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(note_label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(progress_fill, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(progress_thumb, lv_color_hex(color), 0);

    if(state_label != NULL) {
        if(!media_present) lv_label_set_text(state_label, "NO MEDIA");
        else lv_label_set_text(state_label, playing ? "PLAYING" : "PAUSED");
        lv_obj_set_style_text_color(state_label,
                                    lv_color_hex(media_present ? color : MUSIC_SUBTEXT), 0);
    }

    for(int i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(wave_bars[i], lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(wave_bars[i], media_present ? LV_OPA_70 : LV_OPA_30, 0);
    }

    if(button_labels[1] != NULL) {
        /* Playing -> show pause; paused/stopped -> show play. */
        lv_label_set_text(button_labels[1], playing ? MUSIC_ICON_PAUSE : LV_SYMBOL_PLAY);
    }
    restore_button_visual(1);
}

static void clear_feedback(lv_timer_t *timer)
{
    (void)timer;
    if(highlighted >= 0 && highlighted < 3) restore_button_visual(highlighted);
    highlighted = -1;
    if(feedback_timer != NULL) lv_timer_pause(feedback_timer);
}

static lv_obj_t *make_button(int index, int x, int y, int size, const char *symbol)
{
    lv_obj_t *button = lv_obj_create(music_panel);
    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(MUSIC_CARD), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, index == 1 ? 2 : 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(MUSIC_BORDER), 0);
    lv_obj_set_style_pad_all(button, 0, 0);

    lv_obj_t *label = lv_label_create(button);
    button_labels[index] = label;
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(MUSIC_TEXT), 0);
    lv_obj_center(label);

    restore_button_visual(index);
    return button;
}

static lv_obj_t *find_routed_music_panel(void)
{
    lv_obj_t *screen = lv_screen_active();
    if(screen == NULL || lv_obj_get_child_count(screen) == 0) return NULL;

    lv_obj_t *root = lv_obj_get_child(screen, 0);
    if(root == NULL || lv_obj_get_child_count(root) < 5) return NULL;
    return lv_obj_get_child(root, 4);
}

void s1_ui_music_v2_sync_visibility(void)
{
    /* The routed panel is shown/hidden by s1_ui.c. */
}

void s1_ui_music_v2_init(void)
{
    if(music_panel != NULL) return;

    music_panel = find_routed_music_panel();
    if(music_panel == NULL) return;

    /* Preserve the old objects because s1_ui.c still owns pointers to them.
     * Hiding them keeps those pointers valid while this layer supplies the new
     * presentation inside the same routed panel. */
    uint32_t old_count = lv_obj_get_child_count(music_panel);
    for(uint32_t i = 0; i < old_count; i++) {
        lv_obj_add_flag(lv_obj_get_child(music_panel, (int32_t)i), LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_size(music_panel, 170, 290);
    lv_obj_set_pos(music_panel, 0, 26);
    lv_obj_set_scrollable(music_panel, false);
    lv_obj_set_style_radius(music_panel, 0, 0);
    lv_obj_set_style_border_width(music_panel, 0, 0);
    lv_obj_set_style_pad_all(music_panel, 0, 0);
    lv_obj_set_style_bg_opa(music_panel, LV_OPA_TRANSP, 0);

    lock_label = lv_label_create(music_panel);
    lv_label_set_text(lock_label, "LOCK");
    lv_obj_set_pos(lock_label, 8, 2);
    lv_obj_set_style_text_font(lock_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lock_label, lv_color_hex(MUSIC_YELLOW), 0);
    lv_obj_add_flag(lock_label, LV_OBJ_FLAG_HIDDEN);

    /* A compact album-art style card.  It is deliberately static: playback
     * changes only recolor the inner border/icon/dot instead of invalidating
     * the whole 126x126 block every refresh cycle. */
    status_card = lv_obj_create(music_panel);
    lv_obj_set_size(status_card, 126, 126);
    lv_obj_set_pos(status_card, 22, 17);
    style_plain_object(status_card);
    lv_obj_set_style_radius(status_card, 24, 0);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0x09141C), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(MUSIC_BORDER), 0);

    status_inner = lv_obj_create(status_card);
    lv_obj_set_size(status_inner, 98, 94);
    lv_obj_set_pos(status_inner, 14, 14);
    style_plain_object(status_inner);
    lv_obj_set_style_radius(status_inner, 19, 0);
    lv_obj_set_style_bg_color(status_inner, lv_color_hex(MUSIC_CARD_INNER), 0);
    lv_obj_set_style_bg_opa(status_inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_inner, 1, 0);

    note_label = lv_label_create(status_inner);
    lv_label_set_text(note_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(note_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_transform_scale(note_label, 300, 0);
    lv_obj_align(note_label, LV_ALIGN_CENTER, 0, -7);

    const int heights[5] = { 6, 12, 17, 11, 7 };
    for(int i = 0; i < 5; i++) {
        wave_bars[i] = lv_obj_create(status_inner);
        lv_obj_set_size(wave_bars[i], 3, heights[i]);
        lv_obj_set_pos(wave_bars[i], 33 + i * 7, 73 - heights[i] / 2);
        style_plain_object(wave_bars[i]);
        lv_obj_set_style_radius(wave_bars[i], 2, 0);
    }

    status_dot = lv_obj_create(status_card);
    lv_obj_set_size(status_dot, 7, 7);
    lv_obj_set_pos(status_dot, 106, 12);
    style_plain_object(status_dot);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);

    state_label = lv_label_create(music_panel);
    lv_obj_set_width(state_label, 150);
    lv_obj_set_pos(state_label, 10, 149);
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(state_label, 2, 0);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_CLIP);

    lv_obj_t *progress_track = lv_obj_create(music_panel);
    lv_obj_set_size(progress_track, 136, 4);
    lv_obj_set_pos(progress_track, 17, 171);
    style_plain_object(progress_track);
    lv_obj_set_style_radius(progress_track, 3, 0);
    lv_obj_set_style_bg_color(progress_track, lv_color_hex(0x2A3B47), 0);
    lv_obj_set_style_bg_opa(progress_track, LV_OPA_COVER, 0);

    progress_fill = lv_obj_create(progress_track);
    lv_obj_set_size(progress_fill, 0, 4);
    lv_obj_set_pos(progress_fill, 0, 0);
    style_plain_object(progress_fill);
    lv_obj_set_style_radius(progress_fill, 3, 0);

    progress_thumb = lv_obj_create(progress_track);
    lv_obj_set_size(progress_thumb, 8, 8);
    lv_obj_set_pos(progress_thumb, 0, -2);
    style_plain_object(progress_thumb);
    lv_obj_set_style_radius(progress_thumb, LV_RADIUS_CIRCLE, 0);

    /* Exact horizontal symmetry around x=85 keeps the control row visually
     * centered on the 170 px panel. */
    buttons[0] = make_button(0, 16, 220, 40, LV_SYMBOL_PREV);
    buttons[1] = make_button(1, 56, 211, 58, LV_SYMBOL_PLAY);
    buttons[2] = make_button(2, 114, 220, 40, LV_SYMBOL_NEXT);

    feedback_timer = lv_timer_create(clear_feedback, 180, NULL);
    lv_timer_pause(feedback_timer);

    apply_state_visual();
    state_initialized = true;
}

void s1_ui_music_v2_set_state(bool has_media, bool is_playing)
{
    bool new_playing = has_media && is_playing;

    /* HA/media polling may call this repeatedly with identical data.  Avoid
     * touching LVGL styles when nothing actually changed; that prevents a
     * periodic redraw of the artwork area and center button. */
    if(state_initialized && media_present == has_media && playing == new_playing) return;

    media_present = has_media;
    playing = new_playing;
    state_initialized = true;
    apply_state_visual();
}

void s1_ui_music_v2_set_progress(uint32_t elapsed_seconds, uint32_t duration_seconds)
{
    if(progress_fill == NULL || progress_thumb == NULL) return;

    int bucket = 0;
    if(media_present && duration_seconds > 0) {
        if(elapsed_seconds > duration_seconds) elapsed_seconds = duration_seconds;
        int percent = (int)((uint64_t)elapsed_seconds * 100U / duration_seconds);
        bucket = (percent / 5) * 5;
        if(bucket > 100) bucket = 100;
    }

    /* Keep progress updates at 5% steps: only the 136x8 bar region changes,
     * not the whole page. */
    if(bucket == progress_bucket) return;
    progress_bucket = bucket;

    int fill_width = (136 * bucket) / 100;
    lv_obj_set_width(progress_fill, fill_width);

    int thumb_x = fill_width - 4;
    if(thumb_x < 0) thumb_x = 0;
    if(thumb_x > 128) thumb_x = 128;
    lv_obj_set_x(progress_thumb, thumb_x);
}

void s1_ui_music_v2_key_feedback(uint32_t key)
{
    if(s1_ui_router_current() != S1_PAGE_MUSIC || music_panel == NULL) return;

    int index = -1;
    if(key == LV_KEY_LEFT) index = 0;
    else if(key == LV_KEY_ENTER) index = 1;
    else if(key == LV_KEY_RIGHT) index = 2;
    else if(key == S1_KEY_MENU) {
        locked = !locked;
        if(locked) lv_obj_remove_flag(lock_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(lock_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if(index < 0) return;

    if(highlighted >= 0 && highlighted < 3 && highlighted != index) {
        restore_button_visual(highlighted);
    }

    highlighted = index;
    if(index == 1) {
        lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_TEXT), 0);
        lv_obj_set_style_border_width(buttons[index], 3, 0);
    } else {
        lv_obj_set_style_bg_color(buttons[index], lv_color_hex(0x174B66), 0);
        lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_BLUE), 0);
    }

    lv_timer_reset(feedback_timer);
    lv_timer_resume(feedback_timer);
}
