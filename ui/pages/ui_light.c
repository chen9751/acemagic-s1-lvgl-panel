#include "ui_light.h"
#include "ui_page.h"
#include "../../services/ha_client.h"
#include <stdio.h>

LV_FONT_DECLARE(s1_ui_font_14);
typedef enum { LIGHT_OFF, LIGHT_BRIGHTNESS, LIGHT_TEMPERATURE } light_mode_t;
typedef struct {
    const char *entity;
    light_mode_t mode;
    int brightness;
    int temperature; /* Existing 0..100 scale maps to 2700..6500 K. */
} light_t;
static light_t lights[] = {
    {"light.yeelink_ceil40_9771_light", LIGHT_OFF, 50, 50},
    {"light.yeelink_ceil40_d8b6_light", LIGHT_OFF, 50, 50},
    {"light.yeelink_ceiling17_b415_light", LIGHT_OFF, 50, 50},
    {"light.yeelink_ceiling17_40d7_light", LIGHT_OFF, 50, 50}
};
static int selected;
static lv_obj_t *panel, *mode_label, *value_label, *bar, *error_label;
static void render(void)
{
    light_t *light = &lights[selected];
    bool off = light->mode == LIGHT_OFF;
    bool temperature = light->mode == LIGHT_TEMPERATURE;
    lv_label_set_text(mode_label, off ? "OFF" : temperature ? "色温" : "亮度");
    lv_obj_set_hidden(value_label, off);
    lv_obj_set_hidden(bar, off);
    lv_obj_align(mode_label, LV_ALIGN_CENTER, 0, off ? -10 : -65);
    if(!off) {
        char value[16];
        snprintf(value, sizeof(value), temperature ? "%dK" : "%d%%",
                 temperature ? 2700 + light->temperature * 38 : light->brightness);
        lv_label_set_text(value_label, value);
        lv_obj_set_style_bg_color(bar, lv_color_hex(temperature ? 0xF2B15C : 0x41BDF5), LV_PART_INDICATOR);
        lv_bar_set_value(bar, temperature ? light->temperature : light->brightness, LV_ANIM_OFF);
    }
}
void s1_ui_light_init(lv_obj_t *parent)
{
    panel = s1_ui_page_panel(parent);
    mode_label = lv_label_create(panel);
    lv_obj_set_style_text_font(mode_label, &s1_ui_font_14, 0);
    lv_obj_set_style_text_color(mode_label, lv_color_hex(0xE8EBF0), 0);
    value_label = lv_label_create(panel);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xF4F6FA), 0);
    lv_obj_align(value_label, LV_ALIGN_CENTER, 0, -20);
    bar = lv_bar_create(panel);
    lv_obj_set_size(bar, 132, 14);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 25);
    lv_bar_set_range(bar, 0, 100);
    error_label = lv_label_create(panel);
    lv_label_set_text(error_label, "Command failed");
    lv_obj_set_style_text_font(error_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(error_label, lv_color_hex(0xEF7D72), 0);
    lv_obj_align(error_label, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_hidden(error_label, true);
    s1_ui_light_hide();
}
void s1_ui_light_show(s1_page_id_t page)
{
    if(!s1_ui_router_is_light_page(page)) return;
    selected = page - S1_PAGE_LIGHT_LIVING;
    lv_obj_set_hidden(panel, false);
    lv_obj_set_hidden(error_label, true);
    render();
}
void s1_ui_light_hide(void) { lv_obj_set_hidden(panel, true); }
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
        light->mode = light->mode == LIGHT_BRIGHTNESS ? LIGHT_TEMPERATURE : LIGHT_BRIGHTNESS;
    } else if((key == LV_KEY_UP || key == LV_KEY_DOWN) && light->mode != LIGHT_OFF) {
        bool temperature = light->mode == LIGHT_TEMPERATURE;
        int *value = temperature ? &light->temperature : &light->brightness;
        int next = *value + (key == LV_KEY_UP ? 10 : -10);
        int minimum = temperature ? 0 : 10;
        if(next < minimum) next = minimum;
        if(next > 100) next = 100;
        if(next == *value) return;
        result = temperature ? ha_set_light_color_temperature(light->entity, 2700 + next * 38)
                             : ha_set_light_brightness(light->entity, next);
        if(result == 0) *value = next;
    } else return; /* OFF arrows/MENU and volume keys send no services. */
    lv_obj_set_hidden(error_label, result == 0);
    render();
}
