#include "ui_second_pass.h"
#include "lvgl/lvgl.h"

#include <string.h>

#define UI_BLUE  0x1AA8F7
#define UI_MUTED 0x7D8A96
#define UI_WARM  0xFFD05A
#define UI_TEXT  0xF3F7FA

#define HA_ICON_BULB "\xEF\x83\xAB"

LV_FONT_DECLARE(s1_ui_font_14);

static lv_obj_t *music_status_overlay;
static lv_obj_t *ha_card;
static lv_obj_t *ha_visible_name;
static lv_obj_t *ha_source_name;
static lv_obj_t *ha_state;
static lv_obj_t *ha_icon;
static int ha_device_index = -1;

static const char *ha_names_cn[7] = {
    "客厅灯", "书房灯", "卧室灯", "小卧室灯",
    "空调", "窗帘", "浴霸"
};

static const char *ha_source_names[7] = {
    "LIVING LIGHT", "STUDY LIGHT", "BEDROOM LIGHT", "SMALL LIGHT",
    "AIR CONDITION", "CURTAIN", "BATH HEATER"
};

static const char *ha_icons[7] = {
    HA_ICON_BULB, HA_ICON_BULB, HA_ICON_BULB, HA_ICON_BULB,
    "AC", "||", "HOT"
};

static lv_obj_t *find_label_text(lv_obj_t *parent, const char *a, const char *b)
{
    if(parent == NULL) return NULL;

    uint32_t count = lv_obj_get_child_count(parent);
    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);
        if(lv_obj_check_type(child, &lv_label_class)) {
            const char *text = lv_label_get_text(child);
            if(text != NULL && ((a != NULL && strcmp(text, a) == 0) ||
                                (b != NULL && strcmp(text, b) == 0))) {
                return child;
            }
        }

        lv_obj_t *nested = find_label_text(child, a, b);
        if(nested != NULL) return nested;
    }
    return NULL;
}

static int detect_ha_device(void)
{
    if(ha_source_name == NULL) return ha_device_index;
    const char *text = lv_label_get_text(ha_source_name);
    if(text == NULL) return ha_device_index;

    for(int i = 0; i < 7; i++) {
        if(strcmp(text, ha_source_names[i]) == 0) return i;
    }
    return ha_device_index;
}

static void render_ha_device(void)
{
    if(ha_visible_name == NULL || ha_icon == NULL || ha_state == NULL) return;

    int next = detect_ha_device();
    if(next >= 0 && next < 7) ha_device_index = next;
    if(ha_device_index < 0 || ha_device_index >= 7) return;

    lv_label_set_text(ha_visible_name, ha_names_cn[ha_device_index]);
    lv_label_set_text(ha_icon, ha_icons[ha_device_index]);

    const char *state = lv_label_get_text(ha_state);
    bool on = state != NULL && strcmp(state, "ON") == 0;

    uint32_t icon_color;
    if(ha_device_index < 4) icon_color = on ? UI_WARM : UI_MUTED;
    else icon_color = UI_BLUE;

    lv_obj_set_style_text_color(ha_icon, lv_color_hex(icon_color), 0);
    lv_obj_set_style_text_color(
        ha_state,
        lv_color_hex(on ? UI_BLUE : UI_MUTED),
        0
    );
}

static void ha_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if(ha_card == NULL || lv_obj_is_hidden(ha_card)) return;
    render_ha_device();
}

static void refine_ha_panel(lv_obj_t *root)
{
    /* s1_ui.c creates: title, HOME, HA, LED, MUSIC ... in that order. */
    if(root == NULL || lv_obj_get_child_count(root) < 3) return;

    lv_obj_t *title = lv_obj_get_child(root, 0);
    lv_obj_t *ha_panel = lv_obj_get_child(root, 2);
    if(ha_panel == NULL || lv_obj_get_child_count(ha_panel) < 6) return;

    /* Keep the global clock clear: compact the page title to a small HA mark. */
    if(title != NULL && lv_obj_check_type(title, &lv_label_class)) {
        lv_label_set_text(title, "HA");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xC8D4DD), 0);
        lv_obj_set_pos(title, 8, 7);
    }

    lv_obj_set_size(ha_panel, 170, 292);
    lv_obj_set_pos(ha_panel, 0, 26);

    lv_obj_t *left = lv_obj_get_child(ha_panel, 0);
    lv_obj_t *right = lv_obj_get_child(ha_panel, 1);
    ha_card = lv_obj_get_child(ha_panel, 2);
    lv_obj_t *power_box = lv_obj_get_child(ha_panel, 3);
    lv_obj_t *detail_box = lv_obj_get_child(ha_panel, 4);
    lv_obj_t *hint = lv_obj_get_child(ha_panel, 5);

    if(left != NULL) lv_obj_set_pos(left, 5, 106);
    if(right != NULL) lv_obj_set_pos(right, 149, 106);

    if(ha_card == NULL || lv_obj_get_child_count(ha_card) < 3) return;
    lv_obj_set_size(ha_card, 140, 214);
    lv_obj_set_pos(ha_card, 15, 16);
    lv_obj_set_style_radius(ha_card, 18, 0);
    lv_obj_set_style_border_width(ha_card, 2, 0);
    lv_obj_set_style_border_color(ha_card, lv_color_hex(UI_BLUE), 0);

    ha_visible_name = lv_obj_get_child(ha_card, 0);
    ha_source_name = lv_obj_get_child(ha_card, 1);
    ha_state = lv_obj_get_child(ha_card, 2);

    if(ha_visible_name != NULL) {
        lv_obj_set_width(ha_visible_name, 130);
        lv_obj_set_pos(ha_visible_name, 5, 18);
        lv_obj_set_style_text_align(ha_visible_name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(ha_visible_name, &s1_ui_font_14, 0);
        lv_obj_set_style_text_color(ha_visible_name, lv_color_hex(UI_TEXT), 0);
        lv_obj_set_style_transform_scale(ha_visible_name, 330, 0);
    }

    /* The old second English line is only used internally to identify selection. */
    if(ha_source_name != NULL) lv_obj_add_flag(ha_source_name, LV_OBJ_FLAG_HIDDEN);

    if(ha_state != NULL) {
        lv_obj_set_width(ha_state, 130);
        lv_obj_set_pos(ha_state, 5, 151);
        lv_obj_set_style_text_align(ha_state, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(ha_state, &lv_font_montserrat_20, 0);
        lv_obj_set_style_transform_scale(ha_state, 320, 0);
    }

    ha_icon = lv_label_create(ha_card);
    lv_obj_set_width(ha_icon, 130);
    lv_obj_set_pos(ha_icon, 5, 76);
    lv_obj_set_style_text_align(ha_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ha_icon, &s1_ui_font_14, 0);
    lv_obj_set_style_transform_scale(ha_icon, 440, 0);

    /* Remove the two bottom buttons; MENU is now the only detail-entry action. */
    if(power_box != NULL) lv_obj_add_flag(power_box, LV_OBJ_FLAG_HIDDEN);
    if(detail_box != NULL) lv_obj_add_flag(detail_box, LV_OBJ_FLAG_HIDDEN);

    if(hint != NULL && lv_obj_check_type(hint, &lv_label_class)) {
        lv_label_set_text(hint, "MENU  ENTER DEVICE");
        lv_obj_set_width(hint, 160);
        lv_obj_set_pos(hint, 5, 258);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(UI_MUTED), 0);
    }

    render_ha_device();
    lv_timer_create(ha_refresh_timer_cb, 100, NULL);
}

static void refine_music_panel(lv_obj_t *root)
{
    if(root == NULL || lv_obj_get_child_count(root) < 5) return;
    lv_obj_t *music_panel = lv_obj_get_child(root, 4);
    if(music_panel == NULL) return;

    /* The existing 14 px CJK font does not contain these status glyphs on the
     * simulator and renders boxes. Hide that label and use a guaranteed
     * Montserrat English status instead. */
    lv_obj_t *old = find_label_text(music_panel, "已暂停", "正在播放");
    if(old != NULL) lv_obj_add_flag(old, LV_OBJ_FLAG_HIDDEN);

    music_status_overlay = lv_label_create(music_panel);
    lv_label_set_text(music_status_overlay, "PAUSED");
    lv_obj_set_width(music_status_overlay, 150);
    lv_obj_set_pos(music_status_overlay, 10, 154);
    lv_obj_set_style_text_align(music_status_overlay, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(music_status_overlay, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(music_status_overlay, lv_color_hex(UI_MUTED), 0);
}

void s1_ui_second_pass_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    if(screen == NULL || lv_obj_get_child_count(screen) == 0) return;

    lv_obj_t *root = lv_obj_get_child(screen, 0);
    refine_ha_panel(root);
    refine_music_panel(root);
}

void s1_ui_second_pass_music_state(bool playing)
{
    if(music_status_overlay == NULL) return;
    lv_label_set_text(music_status_overlay, playing ? "PLAYING" : "PAUSED");
    lv_obj_set_style_text_color(
        music_status_overlay,
        lv_color_hex(playing ? UI_BLUE : UI_MUTED),
        0
    );
}
