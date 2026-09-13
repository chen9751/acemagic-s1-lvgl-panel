#include "ui_music_v2.h"
#include "../s1_ui.h"
#include "../ui_router.h"
#include "lvgl/lvgl.h"

#define MUSIC_BLUE       0x20B7F5
#define MUSIC_BLUE_DIM   0x12638A
#define MUSIC_GRAY       0x6D7882
#define MUSIC_CARD       0x101C27
#define MUSIC_BORDER     0x263A48
#define MUSIC_TEXT       0xDCEAF3
#define MUSIC_SUBTEXT    0x8193A0
#define MUSIC_DARK       0x071018
#define MUSIC_DISC       0x07121A
#define MUSIC_RING       0x142733

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
static lv_obj_t *lock_icon;
static lv_timer_t *feedback_timer;

static bool media_present;
static bool playing;
static bool locked;
static bool state_initialized;
static int progress_bucket = -1;
static int highlighted = -1;

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
        lv_obj_set_style_border_width(buttons[index], 1, 0);
        if(media_present) {
            lv_obj_set_style_bg_color(buttons[index], lv_color_hex(MUSIC_BLUE), 0);
            lv_obj_set_style_bg_opa(buttons[index], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_BLUE), 0);
            lv_obj_set_style_text_color(button_labels[index], lv_color_hex(MUSIC_DARK), 0);
        } else {
            lv_obj_set_style_bg_color(buttons[index], lv_color_hex(MUSIC_CARD), 0);
            lv_obj_set_style_bg_opa(buttons[index], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_BORDER), 0);
            lv_obj_set_style_text_color(button_labels[index], lv_color_hex(MUSIC_GRAY), 0);
        }
    } else {
        lv_obj_set_style_border_width(buttons[index], 0, 0);
        lv_obj_set_style_bg_opa(buttons[index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(button_labels[index],
                                    lv_color_hex(media_present ? MUSIC_TEXT : MUSIC_GRAY), 0);
    }
}

static void apply_state_visual(void)
{
    if(note_label == NULL || progress_fill == NULL) return;

    /* Keep one blue identity. State changes only touch compact dynamic items. */
    lv_obj_set_style_bg_color(status_dot,
                              lv_color_hex(media_present ? MUSIC_BLUE : MUSIC_GRAY), 0);

    if(state_label != NULL) {
        if(!media_present) lv_label_set_text(state_label, "NO MEDIA");
        else lv_label_set_text(state_label, playing ? "PLAYING" : "PAUSED");
        lv_obj_set_style_text_color(state_label,
                                    lv_color_hex(media_present ? MUSIC_BLUE : MUSIC_SUBTEXT), 0);
    }

    if(button_labels[1] != NULL) {
        lv_label_set_text(button_labels[1], playing ? MUSIC_ICON_PAUSE : LV_SYMBOL_PLAY);
    }
    restore_button_visual(0);
    restore_button_visual(1);
    restore_button_visual(2);
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
    lv_obj_set_style_pad_all(button, 0, 0);

    if(index == 1) {
        lv_obj_set_style_bg_color(button, lv_color_hex(MUSIC_BLUE), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(MUSIC_BLUE), 0);
    } else {
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 0, 0);
    }

    lv_obj_t *label = lv_label_create(button);
    button_labels[index] = label;
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(index == 1 ? MUSIC_DARK : MUSIC_TEXT), 0);
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

static lv_obj_t *make_lock_icon(lv_obj_t *parent)
{
    /* The legacy music page still toggles its old LOCK text when locking.
     * Use this small opaque patch as the v2 lock container so that old text is
     * fully covered, while the visible indicator remains icon-only. */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 42, 22);
    lv_obj_set_pos(root, 4, 0);
    style_plain_object(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(MUSIC_DARK), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    lv_obj_t *shackle = lv_obj_create(root);
    lv_obj_set_size(shackle, 10, 9);
    lv_obj_set_pos(shackle, 7, 1);
    style_plain_object(shackle);
    lv_obj_set_style_radius(shackle, 5, 0);
    lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shackle, 2, 0);
    lv_obj_set_style_border_color(shackle, lv_color_hex(MUSIC_TEXT), 0);

    lv_obj_t *body = lv_obj_create(root);
    lv_obj_set_size(body, 14, 10);
    lv_obj_set_pos(body, 5, 8);
    style_plain_object(body);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(MUSIC_TEXT), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);

    lv_obj_t *keyhole = lv_obj_create(body);
    lv_obj_set_size(keyhole, 2, 4);
    lv_obj_align(keyhole, LV_ALIGN_CENTER, 0, 1);
    style_plain_object(keyhole);
    lv_obj_set_style_radius(keyhole, 1, 0);
    lv_obj_set_style_bg_color(keyhole, lv_color_hex(MUSIC_DARK), 0);
    lv_obj_set_style_bg_opa(keyhole, LV_OPA_COVER, 0);

    return root;
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

    /* Preserve legacy objects because s1_ui.c still owns their pointers. */
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

    lock_icon = make_lock_icon(music_panel);
    lv_obj_add_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);

    /* The compact player references we checked all give the artwork one clear
     * focal point and generous breathing room. Here the record is slightly
     * smaller and lower than the previous version so it does not crowd the
     * global MUSIC/time header. Every object in this block stays static. */
    status_card = lv_obj_create(music_panel);
    lv_obj_set_size(status_card, 108, 108);
    lv_obj_set_pos(status_card, 31, 29);
    style_plain_object(status_card);
    lv_obj_set_style_radius(status_card, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(MUSIC_DISC), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(MUSIC_BLUE_DIM), 0);

    status_inner = lv_obj_create(status_card);
    lv_obj_set_size(status_inner, 86, 86);
    lv_obj_center(status_inner);
    style_plain_object(status_inner);
    lv_obj_set_style_radius(status_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_inner, 1, 0);
    lv_obj_set_style_border_color(status_inner, lv_color_hex(MUSIC_RING), 0);

    lv_obj_t *groove = lv_obj_create(status_card);
    lv_obj_set_size(groove, 66, 66);
    lv_obj_center(groove);
    style_plain_object(groove);
    lv_obj_set_style_radius(groove, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(groove, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(groove, 1, 0);
    lv_obj_set_style_border_color(groove, lv_color_hex(0x10212B), 0);

    lv_obj_t *center_disc = lv_obj_create(status_card);
    lv_obj_set_size(center_disc, 44, 44);
    lv_obj_center(center_disc);
    style_plain_object(center_disc);
    lv_obj_set_style_radius(center_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_disc, lv_color_hex(MUSIC_BLUE), 0);
    lv_obj_set_style_bg_opa(center_disc, LV_OPA_COVER, 0);

    note_label = lv_label_create(center_disc);
    lv_label_set_text(note_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(note_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(note_label, lv_color_hex(MUSIC_DARK), 0);
    lv_obj_center(note_label);

    /* A tiny static equalizer detail gives the disc a music identity without
     * any animation or recurring invalidation. */
    const int heights[5] = { 4, 8, 11, 7, 4 };
    for(int i = 0; i < 5; i++) {
        wave_bars[i] = lv_obj_create(status_card);
        lv_obj_set_size(wave_bars[i], 2, heights[i]);
        lv_obj_set_pos(wave_bars[i], 41 + i * 6, 86 - heights[i] / 2);
        style_plain_object(wave_bars[i]);
        lv_obj_set_style_radius(wave_bars[i], 1, 0);
        lv_obj_set_style_bg_color(wave_bars[i], lv_color_hex(MUSIC_BLUE), 0);
        lv_obj_set_style_bg_opa(wave_bars[i], LV_OPA_60, 0);
    }

    /* Small status dot sits on the disc edge rather than floating outside it. */
    status_dot = lv_obj_create(status_card);
    lv_obj_set_size(status_dot, 6, 6);
    lv_obj_set_pos(status_dot, 89, 13);
    style_plain_object(status_dot);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);

    state_label = lv_label_create(music_panel);
    lv_obj_set_width(state_label, 150);
    lv_obj_set_pos(state_label, 10, 148);
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(state_label, 2, 0);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_CLIP);

    lv_obj_t *progress_track = lv_obj_create(music_panel);
    lv_obj_set_size(progress_track, 126, 4);
    lv_obj_set_pos(progress_track, 22, 174);
    style_plain_object(progress_track);
    lv_obj_set_style_radius(progress_track, 3, 0);
    lv_obj_set_style_bg_color(progress_track, lv_color_hex(0x2A3B47), 0);
    lv_obj_set_style_bg_opa(progress_track, LV_OPA_COVER, 0);

    progress_fill = lv_obj_create(progress_track);
    lv_obj_set_size(progress_fill, 0, 4);
    lv_obj_set_pos(progress_fill, 0, 0);
    style_plain_object(progress_fill);
    lv_obj_set_style_radius(progress_fill, 3, 0);
    lv_obj_set_style_bg_color(progress_fill, lv_color_hex(MUSIC_BLUE), 0);

    progress_thumb = lv_obj_create(progress_track);
    lv_obj_set_size(progress_thumb, 7, 7);
    lv_obj_set_pos(progress_thumb, 0, -2);
    style_plain_object(progress_thumb);
    lv_obj_set_style_radius(progress_thumb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(progress_thumb, lv_color_hex(MUSIC_BLUE), 0);

    /* Side controls remain icon-only and move farther from the center button. */
    buttons[0] = make_button(0, 14, 222, 34, LV_SYMBOL_PREV);
    buttons[1] = make_button(1, 60, 212, 50, LV_SYMBOL_PLAY);
    buttons[2] = make_button(2, 122, 222, 34, LV_SYMBOL_NEXT);

    feedback_timer = lv_timer_create(clear_feedback, 180, NULL);
    lv_timer_pause(feedback_timer);

    apply_state_visual();
    state_initialized = true;
}

void s1_ui_music_v2_set_state(bool has_media, bool is_playing)
{
    bool new_playing = has_media && is_playing;

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

    /* The only continuous media redraw remains this narrow progress region. */
    if(bucket == progress_bucket) return;
    progress_bucket = bucket;

    int fill_width = (126 * bucket) / 100;
    lv_obj_set_width(progress_fill, fill_width);

    int thumb_x = fill_width - 3;
    if(thumb_x < 0) thumb_x = 0;
    if(thumb_x > 119) thumb_x = 119;
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
        if(locked) lv_obj_remove_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if(index < 0) return;

    if(highlighted >= 0 && highlighted < 3 && highlighted != index) {
        restore_button_visual(highlighted);
    }

    highlighted = index;
    if(index == 1) {
        lv_obj_set_style_border_color(buttons[index], lv_color_hex(MUSIC_TEXT), 0);
        lv_obj_set_style_border_width(buttons[index], 2, 0);
    } else {
        lv_obj_set_style_bg_color(buttons[index], lv_color_hex(0x163A4E), 0);
        lv_obj_set_style_bg_opa(buttons[index], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(button_labels[index], lv_color_hex(MUSIC_BLUE), 0);
    }

    lv_timer_reset(feedback_timer);
    lv_timer_resume(feedback_timer);
}
