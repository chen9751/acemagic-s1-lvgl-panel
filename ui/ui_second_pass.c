#include "ui_second_pass.h"
#include "lvgl/lvgl.h"

#include <string.h>

#define UI_BLUE  0x1AA8F7
#define UI_MUTED 0x7D8A96
#define UI_WARM  0xFFD05A
#define UI_TEXT  0xF3F7FA

#define HA_ICON_BULB "\xEF\x83\xAB"
#define HA_FOOTER_TEXT "MENU  ENTER DEVICE"

LV_FONT_DECLARE(s1_ui_font_14);

static lv_obj_t *ha_panel;
static lv_obj_t *ha_title;
static lv_obj_t *ha_card;
static lv_obj_t *ha_legacy_short;
static lv_obj_t *ha_source_name;
static lv_obj_t *ha_state;
static lv_obj_t *ha_name_overlay;
static lv_obj_t *ha_icon;
static lv_obj_t *ha_hint;
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

static void set_label_if_needed(lv_obj_t *label, const char *text)
{
    if(label == NULL || text == NULL || !lv_obj_check_type(label, &lv_label_class)) return;
    const char *old = lv_label_get_text(label);
    if(old != NULL && strcmp(old, text) == 0) return;
    lv_label_set_text(label, text);
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
    if(ha_name_overlay == NULL || ha_icon == NULL || ha_state == NULL) return;

    int next = detect_ha_device();
    if(next >= 0 && next < 7) ha_device_index = next;
    if(ha_device_index < 0 || ha_device_index >= 7) return;

    set_label_if_needed(ha_name_overlay, ha_names_cn[ha_device_index]);
    set_label_if_needed(ha_icon, ha_icons[ha_device_index]);

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

static void enforce_ha_chrome(void)
{
    if(ha_title != NULL && ha_panel != NULL && !lv_obj_is_hidden(ha_panel)) {
        set_label_if_needed(ha_title, "HOME ASSISTANT");
        lv_obj_set_style_text_font(ha_title, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ha_title, lv_color_hex(0xC8D4DD), 0);
        lv_obj_set_pos(ha_title, 8, 7);
    }

    if(ha_hint != NULL) {
        set_label_if_needed(ha_hint, HA_FOOTER_TEXT);
        lv_obj_set_width(ha_hint, 160);
        lv_obj_set_pos(ha_hint, 5, 258);
        lv_obj_set_style_text_align(ha_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(ha_hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ha_hint, lv_color_hex(UI_MUTED), 0);
    }
}

static void ha_value_changed_cb(lv_event_t *event)
{
    (void)event;
    /* Update synchronously after model changes, before LVGL starts drawing. */
    enforce_ha_chrome();
    render_ha_device();
}

static void refine_ha_panel(lv_obj_t *root)
{
    /* s1_ui.c creates: title, HOME, HA, LED, MUSIC ... in that order. */
    if(root == NULL || lv_obj_get_child_count(root) < 3) return;

    ha_title = lv_obj_get_child(root, 0);
    ha_panel = lv_obj_get_child(root, 2);
    if(ha_panel == NULL || lv_obj_get_child_count(ha_panel) < 6) return;

    lv_obj_set_size(ha_panel, 170, 292);
    lv_obj_set_pos(ha_panel, 0, 26);

    lv_obj_t *left = lv_obj_get_child(ha_panel, 0);
    lv_obj_t *right = lv_obj_get_child(ha_panel, 1);
    ha_card = lv_obj_get_child(ha_panel, 2);
    lv_obj_t *power_box = lv_obj_get_child(ha_panel, 3);
    lv_obj_t *detail_box = lv_obj_get_child(ha_panel, 4);
    ha_hint = lv_obj_get_child(ha_panel, 5);

    if(left != NULL) lv_obj_set_pos(left, 5, 106);
    if(right != NULL) lv_obj_set_pos(right, 149, 106);

    if(ha_card == NULL || lv_obj_get_child_count(ha_card) < 3) return;
    lv_obj_set_size(ha_card, 140, 214);
    lv_obj_set_pos(ha_card, 15, 16);
    lv_obj_set_style_radius(ha_card, 18, 0);
    lv_obj_set_style_border_width(ha_card, 2, 0);
    lv_obj_set_style_border_color(ha_card, lv_color_hex(UI_BLUE), 0);

    ha_legacy_short = lv_obj_get_child(ha_card, 0);
    ha_source_name = lv_obj_get_child(ha_card, 1);
    ha_state = lv_obj_get_child(ha_card, 2);

    /* Keep the legacy selector labels permanently hidden. They are still used
     * as the internal source of the selected-device id. */
    if(ha_legacy_short != NULL) lv_obj_set_hidden(ha_legacy_short, true);
    if(ha_source_name != NULL) lv_obj_set_hidden(ha_source_name, true);

    ha_name_overlay = lv_label_create(ha_card);
    lv_obj_set_size(ha_name_overlay, 140, 22);
    lv_obj_set_pos(ha_name_overlay, 0, 31);
    lv_obj_set_style_text_align(ha_name_overlay, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ha_name_overlay, &s1_ui_font_14, 0);
    lv_obj_set_style_text_color(ha_name_overlay, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_transform_pivot_x(ha_name_overlay, 70, 0);
    lv_obj_set_style_transform_pivot_y(ha_name_overlay, 11, 0);
    lv_obj_set_style_transform_scale(ha_name_overlay, 390, 0);

    ha_icon = lv_label_create(ha_card);
    lv_obj_set_size(ha_icon, 140, 24);
    lv_obj_set_pos(ha_icon, 0, 91);
    lv_obj_set_style_text_align(ha_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ha_icon, &s1_ui_font_14, 0);
    lv_obj_set_style_transform_pivot_x(ha_icon, 70, 0);
    lv_obj_set_style_transform_pivot_y(ha_icon, 12, 0);
    lv_obj_set_style_transform_scale(ha_icon, 520, 0);

    if(ha_state != NULL) {
        lv_obj_set_size(ha_state, 140, 28);
        lv_obj_set_pos(ha_state, 0, 158);
        lv_obj_set_style_text_align(ha_state, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(ha_state, &lv_font_montserrat_20, 0);
        lv_obj_set_style_transform_pivot_x(ha_state, 70, 0);
        lv_obj_set_style_transform_pivot_y(ha_state, 14, 0);
        lv_obj_set_style_transform_scale(ha_state, 360, 0);
    }

    /* The final HA page has one large card and one consistent MENU hint. */
    if(power_box != NULL) lv_obj_set_hidden(power_box, true);
    if(detail_box != NULL) lv_obj_set_hidden(detail_box, true);

    enforce_ha_chrome();
    render_ha_device();

    /* Model notifications also cover HA polling and page entry. */
    lv_obj_add_event_cb(ha_panel, ha_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void s1_ui_second_pass_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    if(screen == NULL || lv_obj_get_child_count(screen) == 0) return;

    lv_obj_t *root = lv_obj_get_child(screen, 0);
    refine_ha_panel(root);
}

void s1_ui_second_pass_music_state(bool playing)
{
    /* Music v2 owns playback state; do not recreate a duplicate status label. */
    (void)playing;
}
