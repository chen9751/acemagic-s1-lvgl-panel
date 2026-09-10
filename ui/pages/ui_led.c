#include "ui_led.h"
#include "../../services/led_client.h"

#include <stdio.h>

#define LED_PAGE_BG          0x080A0F
#define LED_PAGE_BG_2        0x0B1720
#define LED_ROW_BG           0x18374A
#define LED_BLUE             0x18B9D9
#define LED_TEXT             0xE7EDF3
#define LED_SUBTEXT          0x70808C
#define LED_ERROR            0xEF7D72

LV_FONT_DECLARE(s1_led_font_12);

typedef struct {
    led_mode_t mode;
    const char *name;
    const char *subtitle;
    const char *icon;
    lv_obj_t *row;
    lv_obj_t *icon_label;
    lv_obj_t *name_label;
    lv_obj_t *subtitle_label;
    lv_obj_t *status_label;
} led_mode_row_t;

static led_mode_row_t mode_rows[] = {
    { LED_MODE_RAINBOW,     "Rainbow",     "彩虹",     LV_SYMBOL_TINT,     NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_BREATHING,   "Breathing",   "呼吸",     LV_SYMBOL_LOOP,     NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_COLOR_CYCLE, "Color Cycle", "颜色循环", LV_SYMBOL_REFRESH,  NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_AUTOMATIC,   "Automatic",   "自动",     LV_SYMBOL_SETTINGS, NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_OFF,         "Off",         "关闭",     LV_SYMBOL_POWER,    NULL, NULL, NULL, NULL, NULL }
};

#define MODE_COUNT ((int)(sizeof(mode_rows) / sizeof(mode_rows[0])))
#define LED_SELECT_INTENSITY MODE_COUNT
#define LED_SELECT_SPEED     (MODE_COUNT + 1)
#define LED_SELECT_COUNT     (MODE_COUNT + 2)

static lv_obj_t *overlay;
static lv_obj_t *content;
static lv_obj_t *intensity_row;
static lv_obj_t *speed_row;
static lv_obj_t *intensity_value;
static lv_obj_t *speed_value;
static lv_obj_t *hint_label;

static int selected = 1;
static int active_mode = 1;
static int error_mode = -1;
static uint8_t intensity = 3;
static uint8_t speed = 3;

static void set_row_selected(lv_obj_t *row, bool is_selected)
{
    lv_obj_set_style_bg_opa(row, is_selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void update_value_label(lv_obj_t *label, uint8_t value)
{
    char text[16];
    snprintf(text, sizeof(text), "%u / 5", (unsigned)value);
    lv_label_set_text(label, text);
}

static void refresh_rows(void)
{
    for(int i = 0; i < MODE_COUNT; i++) {
        bool is_selected = selected == i;
        bool is_active = active_mode == i;
        bool is_error = error_mode == i;

        set_row_selected(mode_rows[i].row, is_selected);
        lv_obj_set_style_text_color(mode_rows[i].icon_label,
                                    lv_color_hex(is_selected ? LED_BLUE : 0x657482), 0);
        lv_obj_set_style_text_color(mode_rows[i].name_label,
                                    lv_color_hex(is_selected ? 0xF7FBFD : LED_TEXT), 0);
        lv_obj_set_style_text_color(mode_rows[i].subtitle_label,
                                    lv_color_hex(is_selected ? 0x9EDDF8 : LED_SUBTEXT), 0);
        lv_label_set_text(mode_rows[i].status_label,
                          is_error ? "!" : (is_active ? LV_SYMBOL_OK : ""));
        lv_obj_set_style_text_color(mode_rows[i].status_label,
                                    lv_color_hex(is_error ? LED_ERROR : LED_BLUE), 0);
    }

    set_row_selected(intensity_row, selected == LED_SELECT_INTENSITY);
    set_row_selected(speed_row, selected == LED_SELECT_SPEED);
    update_value_label(intensity_value, intensity);
    update_value_label(speed_value, speed);

    if(selected == LED_SELECT_INTENSITY) {
        lv_label_set_text(hint_label, "LEFT / RIGHT  Intensity");
    }
    else if(selected == LED_SELECT_SPEED) {
        lv_label_set_text(hint_label, "LEFT / RIGHT  Speed");
    }
    else {
        lv_label_set_text(hint_label, "OK  Apply");
    }
}

static int apply_mode_index(int index)
{
    if(index < 0 || index >= MODE_COUNT) return -1;

    int rc = led_set_state(mode_rows[index].mode, intensity, speed);
    if(rc == 0) {
        active_mode = index;
        error_mode = -1;
    }
    else {
        error_mode = index;
    }
    refresh_rows();
    return rc;
}

static void apply_live_adjustment(void)
{
    if(active_mode < 0 || active_mode >= MODE_COUNT) return;

    if(mode_rows[active_mode].mode == LED_MODE_OFF) {
        refresh_rows();
        return;
    }

    error_mode = led_set_state(mode_rows[active_mode].mode, intensity, speed) == 0
        ? -1 : active_mode;
    refresh_rows();
}

static lv_obj_t *create_mode_row(int index)
{
    led_mode_row_t *item = &mode_rows[index];
    lv_obj_t *row = lv_obj_create(content);

    lv_obj_set_size(row, 146, 29);
    lv_obj_set_pos(row, 0, 4 + index * 31);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(LED_ROW_BG), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    item->row = row;

    item->icon_label = lv_label_create(row);
    lv_label_set_text(item->icon_label, item->icon);
    lv_obj_set_style_text_font(item->icon_label, &lv_font_montserrat_16, 0);
    lv_obj_align(item->icon_label, LV_ALIGN_LEFT_MID, 7, 0);

    item->name_label = lv_label_create(row);
    lv_label_set_text(item->name_label, item->name);
    lv_obj_set_style_text_font(item->name_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(item->name_label, 30, 2);

    item->subtitle_label = lv_label_create(row);
    lv_label_set_text(item->subtitle_label, item->subtitle);
    lv_obj_set_style_text_font(item->subtitle_label, &s1_led_font_12, 0);
    lv_obj_set_pos(item->subtitle_label, 30, 15);

    item->status_label = lv_label_create(row);
    lv_obj_set_style_text_font(item->status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(item->status_label, LV_ALIGN_RIGHT_MID, -8, 0);
    return row;
}

static lv_obj_t *create_setting_row(int32_t y, const char *name, lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_set_size(row, 146, 33);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(LED_ROW_BG), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(LED_TEXT), 0);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 8, 0);

    *value_label = lv_label_create(row);
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(*value_label, lv_color_hex(LED_BLUE), 0);
    lv_obj_align(*value_label, LV_ALIGN_RIGHT_MID, -8, 0);
    return row;
}

void s1_ui_led_init(void)
{
    if(overlay != NULL) return;

    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 170, 292);
    lv_obj_set_pos(overlay, 0, 28);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(LED_PAGE_BG), 0);
    lv_obj_set_style_bg_grad_color(overlay, lv_color_hex(LED_PAGE_BG_2), 0);
    lv_obj_set_style_bg_grad_dir(overlay, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(overlay, 12, 0);

    content = lv_obj_create(overlay);
    lv_obj_set_size(content, 146, 268);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_scrollable(content, false);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content, 0, 0);

    for(int i = 0; i < MODE_COUNT; i++) create_mode_row(i);

    intensity_row = create_setting_row(166, "Intensity", &intensity_value);
    speed_row = create_setting_row(203, "Speed", &speed_value);

    hint_label = lv_label_create(content);
    lv_obj_set_width(hint_label, 146);
    lv_obj_set_pos(hint_label, 0, 247);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x5F6672), 0);

    refresh_rows();
    s1_ui_led_hide();
}

void s1_ui_led_show(void)
{
    if(overlay == NULL) s1_ui_led_init();
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    refresh_rows();
}

void s1_ui_led_hide(void)
{
    if(overlay != NULL) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

void s1_ui_led_key(uint32_t key)
{
    if(key == LV_KEY_UP) {
        selected = (selected + LED_SELECT_COUNT - 1) % LED_SELECT_COUNT;
        error_mode = -1;
        refresh_rows();
        return;
    }
    if(key == LV_KEY_DOWN) {
        selected = (selected + 1) % LED_SELECT_COUNT;
        error_mode = -1;
        refresh_rows();
        return;
    }
    if(key == LV_KEY_ENTER) {
        if(selected < MODE_COUNT) apply_mode_index(selected);
        return;
    }
    if(key != LV_KEY_LEFT && key != LV_KEY_RIGHT) return;

    int delta = key == LV_KEY_RIGHT ? 1 : -1;
    if(selected == LED_SELECT_INTENSITY) {
        int next = (int)intensity + delta;
        if(next < 1) next = 1;
        if(next > 5) next = 5;
        if(next != intensity) {
            intensity = (uint8_t)next;
            apply_live_adjustment();
        }
        return;
    }
    if(selected == LED_SELECT_SPEED) {
        int next = (int)speed + delta;
        if(next < 1) next = 1;
        if(next > 5) next = 5;
        if(next != speed) {
            speed = (uint8_t)next;
            apply_live_adjustment();
        }
    }
}
