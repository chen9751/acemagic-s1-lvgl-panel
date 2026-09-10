#include "ui_light.h"
#include "ui_page.h"
#include "../../services/ha_client.h"
#include <stdio.h>

LV_FONT_DECLARE(s1_ui_font_14);

typedef enum {
    LIGHT_OFF,
    LIGHT_BRIGHTNESS,
    LIGHT_TEMPERATURE
} light_mode_t;

typedef struct {
    const char *entity;
    const char *name;
    const char *subtitle;
    light_mode_t mode;
    int brightness;
    int temperature; /* Existing 0..100 scale maps to 2700..6500 K. */
} light_t;

static light_t lights[] = {
    {"light.yeelink_ceil40_9771_light",    "客厅灯",   "LIVING ROOM", LIGHT_OFF, 50, 50},
    {"light.yeelink_ceil40_d8b6_light",    "书房灯",   "STUDY",       LIGHT_OFF, 50, 50},
    {"light.yeelink_ceiling17_b415_light", "卧室灯",   "BEDROOM",     LIGHT_OFF, 50, 50},
    {"light.yeelink_ceiling17_40d7_light", "小卧室灯", "SMALL ROOM",  LIGHT_OFF, 50, 50}
};

static int selected;
static lv_obj_t *panel;
static lv_obj_t *room_label;
static lv_obj_t *subtitle_label;
static lv_obj_t *status_chip;
static lv_obj_t *status_label;
static lv_obj_t *power_circle;
static lv_obj_t *power_icon;
static lv_obj_t *off_label;
static lv_obj_t *bar;
static lv_obj_t *info_caption;
static lv_obj_t *value_label;
static lv_obj_t *hint_label;
static lv_obj_t *error_label;

#define COLOR_TEXT          0xF5F6F7
#define COLOR_MUTED         0x73777D
#define COLOR_WARM          0xFFD05A
#define COLOR_WARM_DEEP     0xF1A93B
#define COLOR_TRACK         0x292B2F
#define COLOR_TRACK_BORDER  0x4A4D52
#define COLOR_OFF_CIRCLE    0x17191C
#define COLOR_OFF_BORDER    0x36393D
#define COLOR_ERROR         0xEF7D72

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    lv_obj_set_hidden(obj, hidden);
}

static void style_label(lv_obj_t *obj, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

static void set_on_widgets_hidden(bool hidden)
{
    set_hidden(bar, hidden);
    set_hidden(info_caption, hidden);
    set_hidden(value_label, hidden);
    set_hidden(status_chip, hidden);
}

static void hide_global_page_counter(lv_obj_t *parent)
{
    /* s1_ui_init creates subtitle_label then page_label before page modules.
     * Hide the legacy page counter once here; its hidden state persists on all pages.
     */
    if(lv_obj_get_child_count(parent) > 1) {
        lv_obj_t *candidate = lv_obj_get_child(parent, 1);
        if(lv_obj_check_type(candidate, &lv_label_class)) {
            lv_obj_set_hidden(candidate, true);
        }
    }
}

static void render(void)
{
    light_t *light = &lights[selected];
    bool off = light->mode == LIGHT_OFF;
    bool temperature = light->mode == LIGHT_TEMPERATURE;

    lv_label_set_text(room_label, light->name);
    lv_label_set_text(subtitle_label, light->subtitle);

    set_hidden(power_circle, !off);
    set_hidden(off_label, !off);
    set_on_widgets_hidden(off);

    if(off) {
        lv_label_set_text(off_label, "OFF");
        lv_label_set_text(hint_label, "OK  ON");
        return;
    }

    lv_label_set_text(status_label, "ON");
    lv_label_set_text(info_caption, temperature ? "色温" : "亮度");
    lv_label_set_text(hint_label, temperature ? "MENU  亮度" : "MENU  色温");

    char value[16];
    snprintf(value,
             sizeof(value),
             temperature ? "%dK" : "%d%%",
             temperature ? 2700 + light->temperature * 38 : light->brightness);
    lv_label_set_text(value_label, value);

    if(temperature) {
        /* Full cold-to-warm field; current Kelvin value is shown beside it. */
        lv_bar_set_value(bar, 100, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFB94F), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0xEEF7FF), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    } else {
        lv_bar_set_value(bar, light->brightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_WARM_DEEP), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(bar, lv_color_hex(COLOR_WARM), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    }
}

void s1_ui_light_init(lv_obj_t *parent)
{
    hide_global_page_counter(parent);
    panel = s1_ui_page_panel(parent);

    room_label = lv_label_create(panel);
    style_label(room_label, &s1_ui_font_14, COLOR_TEXT);
    lv_obj_set_pos(room_label, 2, 4);

    subtitle_label = lv_label_create(panel);
    lv_label_set_text(subtitle_label, "LIVING ROOM");
    style_label(subtitle_label, &lv_font_montserrat_10, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(subtitle_label, 2, 0);
    lv_obj_set_pos(subtitle_label, 2, 25);

    status_chip = lv_obj_create(panel);
    lv_obj_set_size(status_chip, 37, 22);
    lv_obj_set_pos(status_chip, 107, 4);
    lv_obj_set_scrollable(status_chip, false);
    lv_obj_set_style_radius(status_chip, 11, 0);
    lv_obj_set_style_border_width(status_chip, 0, 0);
    lv_obj_set_style_pad_all(status_chip, 0, 0);
    lv_obj_set_style_bg_color(status_chip, lv_color_hex(COLOR_WARM), 0);
    lv_obj_set_style_bg_opa(status_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(status_chip, lv_color_hex(COLOR_WARM), 0);
    lv_obj_set_style_shadow_width(status_chip, 12, 0);
    lv_obj_set_style_shadow_opa(status_chip, LV_OPA_30, 0);

    status_label = lv_label_create(status_chip);
    lv_label_set_text(status_label, "ON");
    style_label(status_label, &lv_font_montserrat_14, 0x101114);
    lv_obj_center(status_label);

    power_circle = lv_obj_create(panel);
    lv_obj_set_size(power_circle, 86, 86);
    lv_obj_align(power_circle, LV_ALIGN_CENTER, 0, -15);
    lv_obj_set_scrollable(power_circle, false);
    lv_obj_set_style_radius(power_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(power_circle, 1, 0);
    lv_obj_set_style_border_color(power_circle, lv_color_hex(COLOR_OFF_BORDER), 0);
    lv_obj_set_style_bg_color(power_circle, lv_color_hex(COLOR_OFF_CIRCLE), 0);
    lv_obj_set_style_bg_opa(power_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(power_circle, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_width(power_circle, 14, 0);
    lv_obj_set_style_shadow_opa(power_circle, LV_OPA_40, 0);

    power_icon = lv_label_create(power_circle);
    lv_label_set_text(power_icon, LV_SYMBOL_POWER);
    style_label(power_icon, &lv_font_montserrat_22, 0xBFC2C6);
    lv_obj_center(power_icon);

    off_label = lv_label_create(panel);
    lv_label_set_text(off_label, "OFF");
    style_label(off_label, &lv_font_montserrat_22, 0xD7D9DC);
    lv_obj_align(off_label, LV_ALIGN_CENTER, 0, 48);

    bar = lv_bar_create(panel);
    lv_obj_set_size(bar, 52, 144);
    lv_obj_set_pos(bar, 17, 62);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 26, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 26, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(COLOR_TRACK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_shadow_color(bar, lv_color_hex(COLOR_WARM), 0);
    lv_obj_set_style_shadow_width(bar, 12, 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_20, 0);

    info_caption = lv_label_create(panel);
    lv_label_set_text(info_caption, "亮度");
    style_label(info_caption, &s1_ui_font_14, COLOR_WARM);
    lv_obj_set_pos(info_caption, 81, 92);

    value_label = lv_label_create(panel);
    lv_label_set_text(value_label, "50%");
    style_label(value_label, &lv_font_montserrat_20, COLOR_TEXT);
    lv_obj_set_width(value_label, 65);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(value_label, 79, 116);

    hint_label = lv_label_create(panel);
    lv_label_set_text(hint_label, "OK  ON");
    style_label(hint_label, &s1_ui_font_14, 0x777B81);
    lv_obj_set_width(hint_label, 146);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -7);

    error_label = lv_label_create(panel);
    lv_label_set_text(error_label, "Command failed");
    style_label(error_label, &lv_font_montserrat_10, COLOR_ERROR);
    lv_obj_set_width(error_label, 146);
    lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(error_label, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_hidden(error_label, true);

    s1_ui_light_hide();
}

void s1_ui_light_show(s1_page_id_t page)
{
    if(!s1_ui_router_is_light_page(page)) return;

    selected = page - S1_PAGE_LIGHT_LIVING;

    /* A revisit always opens an active light at brightness control.
     * Power state and remembered brightness/temperature values are preserved,
     * and this UI-only mode reset sends no Home Assistant request.
     */
    if(lights[selected].mode != LIGHT_OFF) {
        lights[selected].mode = LIGHT_BRIGHTNESS;
    }

    lv_obj_set_hidden(panel, false);
    lv_obj_set_hidden(error_label, true);
    render();
}

void s1_ui_light_hide(void)
{
    lv_obj_set_hidden(panel, true);
}

void s1_ui_light_key(uint32_t key)
{
    light_t *light = &lights[selected];
    int result = 0;

    if(key == LV_KEY_ENTER) {
        if(light->mode == LIGHT_OFF) {
            result = ha_set_light_brightness(light->entity, light->brightness);
            if(result == 0) light->mode = LIGHT_BRIGHTNESS;
        } else {
            result = ha_set_light_power(light->entity, 0);
            if(result == 0) light->mode = LIGHT_OFF;
        }
    } else if(key == S1_KEY_MENU && light->mode != LIGHT_OFF) {
        light->mode = light->mode == LIGHT_BRIGHTNESS
            ? LIGHT_TEMPERATURE
            : LIGHT_BRIGHTNESS;
    } else if((key == LV_KEY_UP || key == LV_KEY_DOWN) && light->mode != LIGHT_OFF) {
        bool temperature = light->mode == LIGHT_TEMPERATURE;
        int *value = temperature ? &light->temperature : &light->brightness;
        int next = *value + (key == LV_KEY_UP ? 10 : -10);
        int minimum = temperature ? 0 : 10;

        if(next < minimum) next = minimum;
        if(next > 100) next = 100;
        if(next == *value) return;

        result = temperature
            ? ha_set_light_color_temperature(light->entity, 2700 + next * 38)
            : ha_set_light_brightness(light->entity, next);

        if(result == 0) *value = next;
    } else {
        return; /* OFF arrows/MENU and volume keys send no services. */
    }

    lv_obj_set_hidden(error_label, result == 0);
    render();
}
