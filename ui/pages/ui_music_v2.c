#include "ui_music_v2.h"
#include "../s1_ui.h"
#include "../ui_router.h"
#include "lvgl/lvgl.h"

#define MUSIC_BLUE   0x1AA8F7
#define MUSIC_YELLOW 0xFFD05A
#define MUSIC_GRAY   0x6D7882
#define MUSIC_CARD   0x101C27
#define MUSIC_BG_TOP 0x07111A
#define MUSIC_BG_BOT 0x02070B
#define MUSIC_TEXT   0xD7E9F5

static lv_obj_t *overlay;
static lv_obj_t *status_card;
static lv_obj_t *note_label;
static lv_obj_t *progress_track;
static lv_obj_t *progress_fill;
static lv_obj_t *buttons[3];
static lv_obj_t *lock_label;
static lv_timer_t *feedback_timer;

static bool connected;
static bool playing;
static bool locked;
static int progress_bucket = -1;
static int highlighted = -1;

static uint32_t state_color(void)
{
    if(!connected) return MUSIC_GRAY;
    return playing ? MUSIC_BLUE : MUSIC_YELLOW;
}

static void music_set_hidden_if_changed(lv_obj_t *obj, bool hidden)
{
    if(obj == NULL || lv_obj_is_hidden(obj) == hidden) return;
    lv_obj_set_hidden(obj, hidden);
}

static void apply_state_visual(void)
{
    if(status_card == NULL || note_label == NULL || progress_fill == NULL) return;
    uint32_t color = state_color();
    lv_obj_set_style_border_color(status_card, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(note_label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(progress_fill, lv_color_hex(color), 0);
}

static void clear_feedback(lv_timer_t *timer)
{
    (void)timer;
    if(highlighted >= 0 && highlighted < 3) {
        lv_obj_set_style_bg_color(buttons[highlighted], lv_color_hex(MUSIC_CARD), 0);
        lv_obj_set_style_border_color(buttons[highlighted], lv_color_hex(0x315064), 0);
    }
    highlighted = -1;
    if(feedback_timer != NULL) lv_timer_pause(feedback_timer);
}

static lv_obj_t *make_button(int x, const char *symbol, bool center)
{
    int size = center ? 48 : 40;
    lv_obj_t *button = lv_obj_create(overlay);
    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, center ? 243 : 247);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(MUSIC_CARD), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x315064), 0);
    lv_obj_set_style_pad_all(button, 0, 0);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(MUSIC_TEXT), 0);
    lv_obj_center(label);
    return button;
}

void s1_ui_music_v2_sync_visibility(void)
{
    if(overlay == NULL) return;
    bool show = s1_ui_router_current() == S1_PAGE_MUSIC;
    music_set_hidden_if_changed(overlay, !show);
    if(show) lv_obj_move_foreground(overlay);
}

static void refresh_visibility(lv_timer_t *timer)
{
    (void)timer;
    s1_ui_music_v2_sync_visibility();
}

void s1_ui_music_v2_init(void)
{
    if(overlay != NULL) return;

    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 170, 320);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(MUSIC_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(overlay, lv_color_hex(MUSIC_BG_BOT), 0);
    lv_obj_set_style_bg_grad_dir(overlay, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(overlay);
    lv_label_set_text(title, "MUSIC");
    lv_obj_set_pos(title, 10, 9);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(MUSIC_TEXT), 0);

    lock_label = lv_label_create(overlay);
    lv_label_set_text(lock_label, "LOCK");
    lv_obj_set_pos(lock_label, 10, 29);
    lv_obj_set_style_text_font(lock_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lock_label, lv_color_hex(MUSIC_YELLOW), 0);
    lv_obj_set_hidden(lock_label, true);

    status_card = lv_obj_create(overlay);
    lv_obj_set_size(status_card, 132, 112);
    lv_obj_set_pos(status_card, 19, 61);
    lv_obj_set_scrollable(status_card, false);
    lv_obj_set_style_radius(status_card, 20, 0);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0x09141D), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_card, 3, 0);
    lv_obj_set_style_pad_all(status_card, 0, 0);

    note_label = lv_label_create(status_card);
    lv_label_set_text(note_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(note_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_transform_scale(note_label, 384, 0);
    lv_obj_center(note_label);

    progress_track = lv_obj_create(overlay);
    lv_obj_set_size(progress_track, 142, 8);
    lv_obj_set_pos(progress_track, 14, 199);
    lv_obj_set_scrollable(progress_track, false);
    lv_obj_set_style_radius(progress_track, 4, 0);
    lv_obj_set_style_border_width(progress_track, 0, 0);
    lv_obj_set_style_pad_all(progress_track, 0, 0);
    lv_obj_set_style_bg_color(progress_track, lv_color_hex(0x374754), 0);

    progress_fill = lv_obj_create(progress_track);
    lv_obj_set_size(progress_fill, 0, 8);
    lv_obj_set_pos(progress_fill, 0, 0);
    lv_obj_set_scrollable(progress_fill, false);
    lv_obj_set_style_radius(progress_fill, 4, 0);
    lv_obj_set_style_border_width(progress_fill, 0, 0);
    lv_obj_set_style_pad_all(progress_fill, 0, 0);

    buttons[0] = make_button(20, LV_SYMBOL_PREV, false);
    buttons[1] = make_button(61, LV_SYMBOL_PLAY, true);
    buttons[2] = make_button(113, LV_SYMBOL_NEXT, false);

    feedback_timer = lv_timer_create(clear_feedback, 180, NULL);
    lv_timer_pause(feedback_timer);
    lv_timer_create(refresh_visibility, 120, NULL);

    apply_state_visual();
    s1_ui_music_v2_sync_visibility();
}

void s1_ui_music_v2_set_state(bool is_connected, bool is_playing)
{
    connected = is_connected;
    playing = is_connected && is_playing;
    apply_state_visual();
}

void s1_ui_music_v2_set_progress(uint32_t elapsed_seconds, uint32_t duration_seconds)
{
    int bucket = 0;
    if(connected && duration_seconds > 0) {
        if(elapsed_seconds > duration_seconds) elapsed_seconds = duration_seconds;
        int percent = (int)((uint64_t)elapsed_seconds * 100U / duration_seconds);
        bucket = (percent / 5) * 5;
        if(bucket > 100) bucket = 100;
    }
    if(bucket == progress_bucket) return;
    progress_bucket = bucket;
    lv_obj_set_width(progress_fill, (142 * bucket) / 100);
}

void s1_ui_music_v2_key_feedback(uint32_t key)
{
    if(s1_ui_router_current() != S1_PAGE_MUSIC) return;

    int index = -1;
    if(key == LV_KEY_LEFT) index = 0;
    else if(key == LV_KEY_ENTER) index = 1;
    else if(key == LV_KEY_RIGHT) index = 2;
    else if(key == S1_KEY_MENU) {
        locked = !locked;
        music_set_hidden_if_changed(lock_label, !locked);
        return;
    }
    if(index < 0) return;

    if(highlighted >= 0 && highlighted < 3 && highlighted != index) {
        lv_obj_set_style_bg_color(buttons[highlighted], lv_color_hex(MUSIC_CARD), 0);
        lv_obj_set_style_border_color(buttons[highlighted], lv_color_hex(0x315064), 0);
    }

    highlighted = index;
    lv_obj_set_style_bg_color(buttons[index], lv_color_hex(0x174B66), 0);
    lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_BLUE), 0);
    lv_timer_reset(feedback_timer);
    lv_timer_resume(feedback_timer);
}
