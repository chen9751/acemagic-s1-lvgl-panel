#include "s1_ui.h"
#include "ui_router.h"
#include "pages/ui_page.h"
#include "pages/ui_light.h"
#include "../services/ha_client.h"
#include "../services/led_client.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define SCREEN_W 170
#define SCREEN_H 320
#define UI_IDLE_TIMEOUT_MS 30000U
#define UI_BLUE 0x1AA8F7
#define UI_BG_TOP 0x07111A
#define UI_BG_BOTTOM 0x02070B
#define UI_CARD 0x101C27
#define UI_CARD_SELECTED 0x11364B
#define UI_TEXT 0xF3F7FA
#define UI_MUTED 0x7D8A96
#define UI_WARM 0xFFD05A
#define UI_OFF 0x6D7882

#define MUSIC_ICON_PREVIOUS "\xEF\x81\x88"
#define MUSIC_ICON_PLAY     "\xEF\x81\x8B"
#define MUSIC_ICON_PAUSE    "\xEF\x81\x8C"
#define MUSIC_ICON_NEXT     "\xEF\x81\x91"

LV_FONT_DECLARE(s1_nunito_extrabold_108);
LV_FONT_DECLARE(s1_led_font_12);

static const char *weekday_names[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

static const char *light_entities[4] = {
    "light.yeelink_ceil40_9771_light",
    "light.yeelink_ceil40_d8b6_light",
    "light.yeelink_ceiling17_b415_light",
    "light.yeelink_ceiling17_40d7_light"
};

static const char *home_light_names[4] = { "LIV", "STU", "BED", "SML" };
static const char *ha_device_names[7] = {
    "LIVING LIGHT", "STUDY LIGHT", "BEDROOM LIGHT", "SMALL LIGHT",
    "AIR CONDITION", "CURTAIN", "BATH HEATER"
};
static const char *ha_device_short[7] = {
    "LIVING", "STUDY", "BEDROOM", "SMALL", "AC", "CURTAIN", "BATH"
};

static const s1_page_id_t light_pages[4] = {
    S1_PAGE_LIGHT_LIVING,
    S1_PAGE_LIGHT_STUDY,
    S1_PAGE_LIGHT_BEDROOM,
    S1_PAGE_LIGHT_SMALL_BEDROOM
};

static lv_obj_t *root;
static lv_obj_t *title_label;
static lv_obj_t *home_panel;
static lv_obj_t *ha_panel;
static lv_obj_t *led_panel;
static lv_obj_t *music_panel;

static lv_obj_t *home_date_label;
static lv_obj_t *home_hour_label;
static lv_obj_t *home_minute_label;
static lv_obj_t *home_cards[4];
static lv_obj_t *home_bulbs[4];
static lv_obj_t *home_bulb_stems[4];
static lv_obj_t *home_bulb_bases[4];
static lv_obj_t *home_state_labels[4];

static lv_obj_t *ha_device_card;
static lv_obj_t *ha_name_label;
static lv_obj_t *ha_short_label;
static lv_obj_t *ha_state_label;
static lv_obj_t *ha_power_box;
static lv_obj_t *ha_power_title;
static lv_obj_t *ha_detail_title;
static lv_obj_t *ha_hint_label;

static lv_obj_t *led_ring;
static lv_obj_t *led_mode_label;
static lv_obj_t *led_index_label;
static lv_obj_t *led_intensity_box;
static lv_obj_t *led_speed_box;
static lv_obj_t *led_intensity_label;
static lv_obj_t *led_speed_label;

static lv_obj_t *music_lock_label;
static lv_obj_t *music_disc;
static lv_obj_t *music_disc_center;
static lv_obj_t *music_state_label;
static lv_obj_t *music_progress_fill;
static lv_obj_t *music_play_label;

static int home_selected;
static int ha_selected;
static int light_on_cache[4] = { -1, -1, -1, -1 };
static int led_selected;
static int led_active;
static int led_adjust_target;
static uint8_t led_intensity = 3;
static uint8_t led_speed = 3;
static bool music_playing;
static bool music_locked;
static int music_progress_bucket = -1;
static uint32_t last_activity_tick;
static s1_music_action_cb_t music_action_callback;
static void *music_action_user_data;

static const led_mode_t led_modes[] = {
    LED_MODE_RAINBOW,
    LED_MODE_BREATHING,
    LED_MODE_COLOR_CYCLE,
    LED_MODE_AUTOMATIC,
    LED_MODE_OFF
};

static const char *led_mode_names[] = {
    "RAINBOW", "BREATHING", "COLOR", "AUTO", "OFF"
};

#define LED_MODE_COUNT 5
#define LED_ADJUST_MODE 0
#define LED_ADJUST_INTENSITY 1
#define LED_ADJUST_SPEED 2

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if(obj == NULL || lv_obj_is_hidden(obj) == hidden) return;
    lv_obj_set_hidden(obj, hidden);
}

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if(label == NULL || text == NULL) return;
    const char *old = lv_label_get_text(label);
    if(old != NULL && strcmp(old, text) == 0) return;
    lv_label_set_text(label, text);
}

static void style_panel(lv_obj_t *obj)
{
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static void update_title(void)
{
    s1_page_id_t page = s1_ui_router_current();
    const char *title = "";
    if(page == S1_PAGE_HA) title = "HOME ASSISTANT";
    else if(page == S1_PAGE_LED) title = "LED";
    else if(page == S1_PAGE_MUSIC) title = "MUSIC";
    else if(page == S1_PAGE_AC) title = "AC";
    else if(page == S1_PAGE_BATH) title = "BATH";
    else if(page == S1_PAGE_CURTAIN) title = "CURTAIN";
    set_label_text_if_changed(title_label, title);
    set_hidden(title_label, page == S1_PAGE_HOME || s1_ui_router_is_light_page(page));
}

static void update_home_clock(void)
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if(local == NULL) return;

    char hour[4];
    char minute[4];
    char date[24];
    int h = local->tm_hour % 12;
    if(h == 0) h = 12;

    snprintf(hour, sizeof(hour), "%02d", h);
    snprintf(minute, sizeof(minute), "%02d", local->tm_min);
    snprintf(date, sizeof(date), "%02d/%02d  %s", local->tm_mon + 1, local->tm_mday, weekday_names[local->tm_wday]);

    set_label_text_if_changed(home_hour_label, hour);
    set_label_text_if_changed(home_minute_label, minute);
    set_label_text_if_changed(home_date_label, date);
}

static void update_home_card_visual(int index)
{
    bool selected = index == home_selected;
    bool on = light_on_cache[index] == 1;
    uint32_t lamp_color = on ? UI_WARM : UI_OFF;

    lv_obj_set_style_bg_color(home_cards[index], lv_color_hex(selected ? UI_CARD_SELECTED : UI_CARD), 0);
    lv_obj_set_style_border_width(home_cards[index], selected ? 1 : 0, 0);
    lv_obj_set_style_border_color(home_cards[index], lv_color_hex(UI_BLUE), 0);
    lv_obj_set_style_bg_color(home_bulbs[index], lv_color_hex(lamp_color), 0);
    lv_obj_set_style_bg_color(home_bulb_stems[index], lv_color_hex(lamp_color), 0);
    lv_obj_set_style_bg_color(home_bulb_bases[index], lv_color_hex(lamp_color), 0);
    set_label_text_if_changed(home_state_labels[index], on ? "ON" : "OFF");
    lv_obj_set_style_text_color(home_state_labels[index], lv_color_hex(on ? UI_WARM : UI_OFF), 0);
}

static void update_home_selection(int next)
{
    int old = home_selected;
    home_selected = next;
    if(home_selected < 0) home_selected = 3;
    if(home_selected > 3) home_selected = 0;
    update_home_card_visual(old);
    update_home_card_visual(home_selected);
}

static void update_ha_visual(void)
{
    set_label_text_if_changed(ha_name_label, ha_device_names[ha_selected]);
    set_label_text_if_changed(ha_short_label, ha_device_short[ha_selected]);

    if(ha_selected < 4) {
        bool on = light_on_cache[ha_selected] == 1;
        set_label_text_if_changed(ha_state_label, on ? "ON" : "OFF");
        lv_obj_set_style_text_color(ha_state_label, lv_color_hex(on ? UI_BLUE : UI_MUTED), 0);
        lv_obj_set_style_bg_opa(ha_power_box, LV_OPA_COVER, 0);
        set_label_text_if_changed(ha_power_title, "OK  POWER");
        set_label_text_if_changed(ha_hint_label, "LEFT/RIGHT DEVICE");
    } else {
        set_label_text_if_changed(ha_state_label, "--");
        lv_obj_set_style_text_color(ha_state_label, lv_color_hex(UI_MUTED), 0);
        lv_obj_set_style_bg_opa(ha_power_box, LV_OPA_30, 0);
        set_label_text_if_changed(ha_power_title, "OK  --");
        set_label_text_if_changed(ha_hint_label, "MENU FOR DETAIL");
    }
    lv_obj_send_event(ha_panel, LV_EVENT_VALUE_CHANGED, NULL);
}

static void update_ha_selection(int next)
{
    ha_selected = next;
    if(ha_selected < 0) ha_selected = 6;
    if(ha_selected > 6) ha_selected = 0;
    update_ha_visual();
}

static void refresh_light_states(bool force)
{
    for(int i = 0; i < 4; i++) {
        ha_light_state_t state = {0};
        if(ha_get_light_state(light_entities[i], &state) != 0) continue;
        int on = state.is_on ? 1 : 0;
        if(!force && light_on_cache[i] == on) continue;
        light_on_cache[i] = on;
        update_home_card_visual(i);
        if(ha_selected == i) update_ha_visual();
    }
}

static void update_led_visual(void)
{
    static int previous_mode = -1, previous_active = -1, previous_target = -1;
    static int previous_intensity = -1, previous_speed = -1;
    if(previous_mode == led_selected && previous_active == led_active &&
       previous_target == led_adjust_target && previous_intensity == led_intensity &&
       previous_speed == led_speed) return;
    bool focus_changed = previous_target != led_adjust_target;
    bool mode_changed = previous_mode != led_selected || previous_active != led_active;
    previous_mode = led_selected; previous_active = led_active;
    previous_target = led_adjust_target;
    previous_intensity = led_intensity; previous_speed = led_speed;
    bool off = led_modes[led_selected] == LED_MODE_OFF;
    set_label_text_if_changed(led_mode_label, led_mode_names[led_selected]);
    char text[24];
    snprintf(text, sizeof(text), "%d / %d", led_selected + 1, LED_MODE_COUNT);
    set_label_text_if_changed(led_index_label, text);

    uint32_t ring_color = off ? UI_OFF : (led_selected == led_active ? UI_BLUE : 0x875CFF);
    if(mode_changed) lv_obj_set_style_border_color(led_ring, lv_color_hex(ring_color), 0);

    snprintf(text, sizeof(text), "亮度：%u/5", (unsigned)led_intensity);
    set_label_text_if_changed(led_intensity_label, text);
    snprintf(text, sizeof(text), "速度：%u/5", (unsigned)led_speed);
    set_label_text_if_changed(led_speed_label, text);

    if(!focus_changed) return;
    bool intensity_selected = led_adjust_target == LED_ADJUST_INTENSITY;
    bool speed_selected = led_adjust_target == LED_ADJUST_SPEED;
    lv_obj_set_style_border_width(led_intensity_box, 1, 0);
    lv_obj_set_style_border_color(led_intensity_box, lv_color_hex(intensity_selected ? UI_BLUE : 0x23435A), 0);
    lv_obj_set_style_border_width(led_speed_box, 1, 0);
    lv_obj_set_style_border_color(led_speed_box, lv_color_hex(speed_selected ? UI_BLUE : 0x23435A), 0);
    lv_obj_set_style_text_color(led_intensity_label, lv_color_hex(intensity_selected ? UI_BLUE : UI_TEXT), 0);
    lv_obj_set_style_text_color(led_speed_label, lv_color_hex(speed_selected ? UI_BLUE : UI_TEXT), 0);
}

static void activate_led_mode(void)
{
    if(led_set_state(led_modes[led_selected], led_intensity, led_speed) == 0) {
        led_active = led_selected;
        update_led_visual();
    }
}

static void adjust_led_value(uint32_t key)
{
    int delta = key == LV_KEY_RIGHT ? 1 : -1;
    if(led_adjust_target == LED_ADJUST_INTENSITY) {
        int value = (int)led_intensity + delta;
        if(value < 1) value = 1;
        if(value > 5) value = 5;
        if(value != led_intensity) {
            if(led_set_state(led_modes[led_active], (uint8_t)value, led_speed) == 0)
                led_intensity = (uint8_t)value;
            update_led_visual();
        }
        return;
    }

    if(led_adjust_target == LED_ADJUST_SPEED) {
        int value = (int)led_speed + delta;
        if(value < 1) value = 1;
        if(value > 5) value = 5;
        if(value != led_speed) {
            if(led_set_state(led_modes[led_active], led_intensity, (uint8_t)value) == 0)
                led_speed = (uint8_t)value;
            update_led_visual();
        }
    }
}

static void dispatch_music_action(s1_music_action_t action)
{
    if(music_action_callback != NULL) music_action_callback(action, music_action_user_data);
}

static void update_music_visual(void)
{
    uint32_t color = music_playing ? UI_BLUE : UI_OFF;
    lv_obj_set_style_border_color(music_disc, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(music_disc_center, lv_color_hex(color), 0);
    set_label_text_if_changed(music_state_label, music_playing ? "PLAYING" : "PAUSED");
    lv_obj_set_style_text_color(music_state_label, lv_color_hex(color), 0);
    set_label_text_if_changed(music_play_label, music_playing ? MUSIC_ICON_PAUSE : MUSIC_ICON_PLAY);
    set_hidden(music_lock_label, !music_locked);
}

static void update_page(void)
{
    s1_page_id_t page = s1_ui_router_current();
    set_hidden(home_panel, true);
    set_hidden(ha_panel, true);
    set_hidden(led_panel, true);
    set_hidden(music_panel, true);
    s1_ui_light_hide();
    s1_ui_placeholder_hide();
    update_title();

    if(page == S1_PAGE_HOME) {
        set_hidden(home_panel, false);
        update_home_clock();
        refresh_light_states(true);
    } else if(page == S1_PAGE_HA) {
        set_hidden(ha_panel, false);
        refresh_light_states(true);
        update_ha_visual();
    } else if(page == S1_PAGE_LED) {
        led_adjust_target = LED_ADJUST_MODE;
        set_hidden(led_panel, false);
        update_led_visual();
    } else if(page == S1_PAGE_MUSIC) {
        set_hidden(music_panel, false);
        update_music_visual();
    } else if(s1_ui_router_is_light_page(page)) {
        s1_ui_light_show(page);
    } else {
        s1_ui_placeholder_show(page);
    }
}

static void periodic_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_home_clock();
    static int refresh_divider;
    if(++refresh_divider >= 3) {
        refresh_divider = 0;
        refresh_light_states(false);
    }

    s1_page_id_t page = s1_ui_router_current();
    if(page == S1_PAGE_HOME) return;
    if(page == S1_PAGE_MUSIC && music_locked) return;
    if((uint32_t)(lv_tick_get() - last_activity_tick) >= UI_IDLE_TIMEOUT_MS) {
        s1_ui_router_home();
        update_page();
        last_activity_tick = lv_tick_get();
    }
}

static void create_home_card(int index, int x)
{
    lv_obj_t *card = lv_obj_create(home_panel);
    home_cards[index] = card;
    lv_obj_set_size(card, 38, 102);
    lv_obj_set_pos(card, x, 206);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_style_radius(card, 11, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);

    lv_obj_t *name = make_label(card, home_light_names[index], &lv_font_montserrat_10, UI_TEXT);
    lv_obj_set_width(name, 36);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name, 1, 6);

    home_bulbs[index] = lv_obj_create(card);
    lv_obj_set_size(home_bulbs[index], 16, 16);
    lv_obj_set_pos(home_bulbs[index], 11, 26);
    lv_obj_set_scrollable(home_bulbs[index], false);
    lv_obj_set_style_radius(home_bulbs[index], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(home_bulbs[index], 0, 0);
    lv_obj_set_style_pad_all(home_bulbs[index], 0, 0);

    home_bulb_stems[index] = lv_obj_create(card);
    lv_obj_set_size(home_bulb_stems[index], 5, 6);
    lv_obj_set_pos(home_bulb_stems[index], 16, 39);
    lv_obj_set_style_border_width(home_bulb_stems[index], 0, 0);
    lv_obj_set_style_pad_all(home_bulb_stems[index], 0, 0);

    home_bulb_bases[index] = lv_obj_create(card);
    lv_obj_set_size(home_bulb_bases[index], 10, 3);
    lv_obj_set_pos(home_bulb_bases[index], 14, 45);
    lv_obj_set_style_border_width(home_bulb_bases[index], 0, 0);
    lv_obj_set_style_pad_all(home_bulb_bases[index], 0, 0);

    lv_obj_t *sofa_back = lv_obj_create(card);
    lv_obj_set_size(sofa_back, 22, 9);
    lv_obj_set_pos(sofa_back, 8, 56);
    lv_obj_set_style_radius(sofa_back, 3, 0);
    lv_obj_set_style_border_width(sofa_back, 0, 0);
    lv_obj_set_style_bg_color(sofa_back, lv_color_hex(0x92A0AB), 0);
    lv_obj_set_style_pad_all(sofa_back, 0, 0);

    lv_obj_t *sofa_seat = lv_obj_create(card);
    lv_obj_set_size(sofa_seat, 26, 6);
    lv_obj_set_pos(sofa_seat, 6, 64);
    lv_obj_set_style_radius(sofa_seat, 2, 0);
    lv_obj_set_style_border_width(sofa_seat, 0, 0);
    lv_obj_set_style_bg_color(sofa_seat, lv_color_hex(0x92A0AB), 0);
    lv_obj_set_style_pad_all(sofa_seat, 0, 0);

    home_state_labels[index] = make_label(card, "OFF", &lv_font_montserrat_12, UI_OFF);
    lv_obj_set_width(home_state_labels[index], 36);
    lv_obj_set_style_text_align(home_state_labels[index], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(home_state_labels[index], 1, 79);
    update_home_card_visual(index);
}

static void create_home_ui(void)
{
    home_panel = lv_obj_create(root);
    lv_obj_set_size(home_panel, SCREEN_W, SCREEN_H);
    style_panel(home_panel);

    home_date_label = make_label(home_panel, "09/14  SUN", &lv_font_montserrat_14, 0xD7E1E8);
    lv_obj_set_pos(home_date_label, 8, 8);

    home_hour_label = make_label(home_panel, "12", &s1_nunito_extrabold_108, UI_TEXT);
    lv_obj_set_size(home_hour_label, 160, 108);
    lv_obj_set_style_text_align(home_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(home_hour_label, 5, 29);
    lv_obj_set_style_transform_scale(home_hour_label, 176, 0);

    home_minute_label = make_label(home_panel, "00", &s1_nunito_extrabold_108, UI_BLUE);
    lv_obj_set_size(home_minute_label, 160, 108);
    lv_obj_set_style_text_align(home_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(home_minute_label, 5, 101);
    lv_obj_set_style_transform_scale(home_minute_label, 176, 0);

    create_home_card(0, 6);
    create_home_card(1, 46);
    create_home_card(2, 86);
    create_home_card(3, 126);
}

static void create_ha_ui(void)
{
    ha_panel = lv_obj_create(root);
    lv_obj_set_size(ha_panel, SCREEN_W, 286);
    lv_obj_set_pos(ha_panel, 0, 30);
    style_panel(ha_panel);

    lv_obj_t *left = make_label(ha_panel, "<", &lv_font_montserrat_24, 0xBED7E8);
    lv_obj_set_pos(left, 10, 86);
    lv_obj_t *right = make_label(ha_panel, ">", &lv_font_montserrat_24, 0xBED7E8);
    lv_obj_set_pos(right, 147, 86);

    ha_device_card = lv_obj_create(ha_panel);
    lv_obj_set_size(ha_device_card, 118, 132);
    lv_obj_set_pos(ha_device_card, 26, 28);
    lv_obj_set_scrollable(ha_device_card, false);
    lv_obj_set_style_radius(ha_device_card, 16, 0);
    lv_obj_set_style_bg_color(ha_device_card, lv_color_hex(UI_CARD_SELECTED), 0);
    lv_obj_set_style_border_width(ha_device_card, 2, 0);
    lv_obj_set_style_border_color(ha_device_card, lv_color_hex(UI_BLUE), 0);
    lv_obj_set_style_pad_all(ha_device_card, 0, 0);

    ha_short_label = make_label(ha_device_card, "LIVING", &lv_font_montserrat_20, UI_BLUE);
    lv_obj_set_width(ha_short_label, 108);
    lv_obj_set_style_text_align(ha_short_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ha_short_label, 5, 24);

    ha_name_label = make_label(ha_device_card, "LIVING LIGHT", &lv_font_montserrat_12, UI_TEXT);
    lv_obj_set_width(ha_name_label, 108);
    lv_obj_set_style_text_align(ha_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ha_name_label, 5, 57);

    ha_state_label = make_label(ha_device_card, "OFF", &lv_font_montserrat_20, UI_MUTED);
    lv_obj_set_width(ha_state_label, 108);
    lv_obj_set_style_text_align(ha_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ha_state_label, 5, 88);

    ha_power_box = lv_obj_create(ha_panel);
    lv_obj_set_size(ha_power_box, 72, 58);
    lv_obj_set_pos(ha_power_box, 9, 181);
    lv_obj_set_scrollable(ha_power_box, false);
    lv_obj_set_style_radius(ha_power_box, 12, 0);
    lv_obj_set_style_bg_color(ha_power_box, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_border_width(ha_power_box, 1, 0);
    lv_obj_set_style_border_color(ha_power_box, lv_color_hex(0x23435A), 0);
    lv_obj_set_style_pad_all(ha_power_box, 0, 0);
    ha_power_title = make_label(ha_power_box, "OK  POWER", &lv_font_montserrat_12, UI_TEXT);
    lv_obj_center(ha_power_title);

    lv_obj_t *detail_box = lv_obj_create(ha_panel);
    lv_obj_set_size(detail_box, 72, 58);
    lv_obj_set_pos(detail_box, 89, 181);
    lv_obj_set_scrollable(detail_box, false);
    lv_obj_set_style_radius(detail_box, 12, 0);
    lv_obj_set_style_bg_color(detail_box, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_border_width(detail_box, 1, 0);
    lv_obj_set_style_border_color(detail_box, lv_color_hex(0x23435A), 0);
    lv_obj_set_style_pad_all(detail_box, 0, 0);
    ha_detail_title = make_label(detail_box, "MENU\nDETAIL", &lv_font_montserrat_12, UI_TEXT);
    lv_obj_set_style_text_align(ha_detail_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(ha_detail_title);

    ha_hint_label = make_label(ha_panel, "LEFT/RIGHT DEVICE", &lv_font_montserrat_10, UI_MUTED);
    lv_obj_set_width(ha_hint_label, 160);
    lv_obj_set_style_text_align(ha_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ha_hint_label, 5, 257);
    update_ha_visual();
}

static void create_led_ui(void)
{
    led_panel = lv_obj_create(root);
    lv_obj_set_size(led_panel, SCREEN_W, 286);
    lv_obj_set_pos(led_panel, 0, 30);
    style_panel(led_panel);

    led_ring = lv_obj_create(led_panel);
    lv_obj_set_size(led_ring, 112, 112);
    lv_obj_align(led_ring, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_scrollable(led_ring, false);
    lv_obj_set_style_radius(led_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(led_ring, 5, 0);
    lv_obj_set_style_bg_color(led_ring, lv_color_hex(0x08121A), 0);
    lv_obj_set_style_bg_opa(led_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(led_ring, 0, 0);

    led_mode_label = make_label(led_ring, "RAINBOW", &lv_font_montserrat_14, UI_TEXT);
    lv_obj_set_width(led_mode_label, 100);
    lv_obj_set_style_text_align(led_mode_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(led_mode_label, LV_LABEL_LONG_CLIP);
    lv_obj_center(led_mode_label);

    led_index_label = make_label(led_panel, "1 / 5", &lv_font_montserrat_12, UI_MUTED);
    lv_obj_align(led_index_label, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t *left = make_label(led_panel, "<", &lv_font_montserrat_24, 0xBED7E8);
    lv_obj_set_pos(left, 12, 68);
    lv_obj_t *right = make_label(led_panel, ">", &lv_font_montserrat_24, 0xBED7E8);
    lv_obj_set_pos(right, 145, 68);

    led_intensity_box = lv_obj_create(led_panel);
    lv_obj_set_size(led_intensity_box, 71, 67);
    lv_obj_set_pos(led_intensity_box, 10, 183);
    lv_obj_set_scrollable(led_intensity_box, false);
    lv_obj_set_style_radius(led_intensity_box, 12, 0);
    lv_obj_set_style_bg_color(led_intensity_box, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_border_width(led_intensity_box, 1, 0);
    lv_obj_set_style_border_color(led_intensity_box, lv_color_hex(0x23435A), 0);
    lv_obj_set_style_pad_all(led_intensity_box, 0, 0);
    led_intensity_label = make_label(led_intensity_box, "亮度：3/5", &s1_led_font_12, UI_TEXT);
    lv_obj_center(led_intensity_label);

    led_speed_box = lv_obj_create(led_panel);
    lv_obj_set_size(led_speed_box, 71, 67);
    lv_obj_set_pos(led_speed_box, 89, 183);
    lv_obj_set_scrollable(led_speed_box, false);
    lv_obj_set_style_radius(led_speed_box, 12, 0);
    lv_obj_set_style_bg_color(led_speed_box, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_border_width(led_speed_box, 1, 0);
    lv_obj_set_style_border_color(led_speed_box, lv_color_hex(0x23435A), 0);
    lv_obj_set_style_pad_all(led_speed_box, 0, 0);
    led_speed_label = make_label(led_speed_box, "速度：3/5", &s1_led_font_12, UI_TEXT);
    lv_obj_center(led_speed_label);
}

static lv_obj_t *create_round_music_button(lv_obj_t *parent, int x, const char *symbol, bool center)
{
    int size = center ? 48 : 38;
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, center ? 222 : 227);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(center ? 0x7D9CB4 : 0x254054), 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t *label = make_label(button, symbol, &lv_font_montserrat_24, 0xD7E9F5);
    lv_obj_center(label);
    if(center) music_play_label = label;
    return button;
}

static void create_music_ui(void)
{
    music_panel = lv_obj_create(root);
    lv_obj_set_size(music_panel, SCREEN_W, 290);
    lv_obj_set_pos(music_panel, 0, 26);
    style_panel(music_panel);

    music_lock_label = make_label(music_panel, "LOCK", &lv_font_montserrat_10, 0xD5E9F7);
    lv_obj_set_pos(music_lock_label, 7, 4);

    music_disc = lv_obj_create(music_panel);
    lv_obj_set_size(music_disc, 126, 126);
    lv_obj_align(music_disc, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_scrollable(music_disc, false);
    lv_obj_set_style_radius(music_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(music_disc, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(music_disc, 3, 0);
    lv_obj_set_style_pad_all(music_disc, 0, 0);

    lv_obj_t *disc_inner = lv_obj_create(music_disc);
    lv_obj_set_size(disc_inner, 92, 92);
    lv_obj_center(disc_inner);
    lv_obj_set_scrollable(disc_inner, false);
    lv_obj_set_style_radius(disc_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc_inner, lv_color_hex(0x0B151E), 0);
    lv_obj_set_style_border_width(disc_inner, 1, 0);
    lv_obj_set_style_border_color(disc_inner, lv_color_hex(0x16364B), 0);
    lv_obj_set_style_pad_all(disc_inner, 0, 0);

    music_disc_center = lv_obj_create(disc_inner);
    lv_obj_set_size(music_disc_center, 38, 38);
    lv_obj_center(music_disc_center);
    lv_obj_set_scrollable(music_disc_center, false);
    lv_obj_set_style_radius(music_disc_center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(music_disc_center, 0, 0);
    lv_obj_set_style_pad_all(music_disc_center, 0, 0);

    lv_obj_t *spindle = lv_obj_create(music_disc_center);
    lv_obj_set_size(spindle, 7, 7);
    lv_obj_center(spindle);
    lv_obj_set_scrollable(spindle, false);
    lv_obj_set_style_radius(spindle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(spindle, lv_color_hex(0x081018), 0);
    lv_obj_set_style_border_width(spindle, 0, 0);
    lv_obj_set_style_pad_all(spindle, 0, 0);

    music_state_label = make_label(music_panel, "PAUSED", &lv_font_montserrat_14, UI_OFF);
    lv_obj_set_width(music_state_label, 150);
    lv_obj_set_style_text_align(music_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(music_state_label, 10, 154);

    lv_obj_t *progress_track = lv_obj_create(music_panel);
    lv_obj_set_size(progress_track, 142, 7);
    lv_obj_set_pos(progress_track, 14, 184);
    lv_obj_set_scrollable(progress_track, false);
    lv_obj_set_style_radius(progress_track, 4, 0);
    lv_obj_set_style_bg_color(progress_track, lv_color_hex(0x3A4957), 0);
    lv_obj_set_style_border_width(progress_track, 0, 0);
    lv_obj_set_style_pad_all(progress_track, 0, 0);

    music_progress_fill = lv_obj_create(progress_track);
    lv_obj_set_size(music_progress_fill, 0, 7);
    lv_obj_set_pos(music_progress_fill, 0, 0);
    lv_obj_set_scrollable(music_progress_fill, false);
    lv_obj_set_style_radius(music_progress_fill, 4, 0);
    lv_obj_set_style_bg_color(music_progress_fill, lv_color_hex(UI_BLUE), 0);
    lv_obj_set_style_border_width(music_progress_fill, 0, 0);
    lv_obj_set_style_pad_all(music_progress_fill, 0, 0);

    create_round_music_button(music_panel, 18, MUSIC_ICON_PREVIOUS, false);
    create_round_music_button(music_panel, 61, MUSIC_ICON_PLAY, true);
    create_round_music_button(music_panel, 114, MUSIC_ICON_NEXT, false);
    update_music_visual();
}

void s1_ui_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_BG_TOP), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    root = lv_obj_create(screen);
    lv_obj_set_size(root, SCREEN_W, SCREEN_H);
    lv_obj_center(root);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(UI_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(UI_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    title_label = make_label(root, "", &lv_font_montserrat_14, UI_TEXT);
    lv_obj_set_pos(title_label, 8, 6);

    create_home_ui();
    create_ha_ui();
    create_led_ui();
    create_music_ui();
    s1_ui_light_init(root);
    s1_ui_placeholder_init(root);
    s1_ui_router_init();
    last_activity_tick = lv_tick_get();
    lv_timer_create(periodic_timer_cb, 1000, NULL);
    update_page();
}

void s1_ui_key(uint32_t key)
{
    s1_page_id_t page = s1_ui_router_current();
    last_activity_tick = lv_tick_get();

    if(key == LV_KEY_HOME) {
        s1_ui_router_home();
        update_page();
        return;
    }
    if(key == LV_KEY_ESC) {
        if(page != S1_PAGE_HOME) {
            s1_ui_router_home();
            update_page();
        }
        return;
    }
    if(s1_ui_router_is_light_page(page)) {
        s1_ui_light_key(key);
        return;
    }
    if(page == S1_PAGE_AC || page == S1_PAGE_BATH || page == S1_PAGE_CURTAIN) return;

    /* UP/DOWN are reserved globally for the four primary pages. */
    if(key == LV_KEY_UP) {
        if(s1_ui_router_up()) update_page();
        return;
    }
    if(key == LV_KEY_DOWN) {
        if(s1_ui_router_down()) update_page();
        return;
    }

    if(page == S1_PAGE_HOME) {
        if(key == LV_KEY_LEFT) update_home_selection(home_selected - 1);
        else if(key == LV_KEY_RIGHT) update_home_selection(home_selected + 1);
        else if(key == LV_KEY_ENTER) {
            if(ha_toggle(light_entities[home_selected]) == 0) refresh_light_states(true);
        } else if(key == S1_KEY_MENU) {
            s1_ui_router_open(light_pages[home_selected]);
            update_page();
        }
        return;
    }

    if(page == S1_PAGE_HA) {
        if(key == LV_KEY_LEFT) update_ha_selection(ha_selected - 1);
        else if(key == LV_KEY_RIGHT) update_ha_selection(ha_selected + 1);
        else if(key == LV_KEY_ENTER && ha_selected < 4) {
            if(ha_toggle(light_entities[ha_selected]) == 0) refresh_light_states(true);
        } else if(key == S1_KEY_MENU) {
            if(ha_selected < 4) s1_ui_router_open(light_pages[ha_selected]);
            else if(ha_selected == 4) s1_ui_router_open(S1_PAGE_AC);
            else if(ha_selected == 5) s1_ui_router_open(S1_PAGE_CURTAIN);
            else s1_ui_router_open(S1_PAGE_BATH);
            update_page();
        }
        return;
    }

    if(page == S1_PAGE_LED) {
        if(key == S1_KEY_MENU) {
            led_adjust_target = (led_adjust_target + 1) % 3;
            update_led_visual();
            return;
        }

        if(key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            if(led_adjust_target == LED_ADJUST_MODE) {
                if(key == LV_KEY_LEFT) led_selected = (led_selected + LED_MODE_COUNT - 1) % LED_MODE_COUNT;
                else led_selected = (led_selected + 1) % LED_MODE_COUNT;
                update_led_visual();
            } else {
                adjust_led_value(key);
            }
            return;
        }

        if(key == LV_KEY_ENTER && led_adjust_target == LED_ADJUST_MODE) activate_led_mode();
        return;
    }

    if(page == S1_PAGE_MUSIC) {
        if(key == LV_KEY_LEFT) dispatch_music_action(S1_MUSIC_ACTION_PREVIOUS);
        else if(key == LV_KEY_RIGHT) dispatch_music_action(S1_MUSIC_ACTION_NEXT);
        else if(key == LV_KEY_ENTER) {
            music_playing = !music_playing;
            update_music_visual();
            dispatch_music_action(S1_MUSIC_ACTION_PLAY_PAUSE);
        } else if(key == S1_KEY_MENU) {
            music_locked = !music_locked;
            update_music_visual();
        }
    }
}

void s1_ui_home_set_weather(int temperature_c, int rain_probability_percent)
{
    (void)temperature_c;
    (void)rain_probability_percent;
}

void s1_ui_music_set_action_cb(s1_music_action_cb_t callback, void *user_data)
{
    music_action_callback = callback;
    music_action_user_data = user_data;
}

void s1_ui_music_set_metadata(const char *song, const char *artist, const char *album, bool playing)
{
    (void)song;
    (void)artist;
    (void)album;
    if(music_playing == playing) return;
    music_playing = playing;
    update_music_visual();
}

void s1_ui_music_set_progress(uint32_t elapsed_seconds, uint32_t duration_seconds)
{
    if(duration_seconds == 0) {
        music_progress_bucket = 0;
        lv_obj_set_width(music_progress_fill, 0);
        return;
    }
    if(elapsed_seconds > duration_seconds) elapsed_seconds = duration_seconds;
    int raw_percent = (int)((uint64_t)elapsed_seconds * 100U / duration_seconds);
    int bucket = (raw_percent / 5) * 5;
    if(bucket > 100) bucket = 100;
    if(bucket == music_progress_bucket) return;
    music_progress_bucket = bucket;
    lv_obj_set_width(music_progress_fill, (142 * bucket) / 100);
}
