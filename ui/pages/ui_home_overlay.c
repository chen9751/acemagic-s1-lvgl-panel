#include "ui_home_overlay.h"
#include "../ui_router.h"
#include "lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HOME_BG_TOP       0x070A10
#define HOME_BG_BOTTOM    0x102A38
#define HOME_HOUR_COLOR   0xA9E9F8
#define HOME_MIN_COLOR    0x18B9D9
#define HOME_TEXT_MUTED   0x71818D
#define HOME_TEXT_MAIN    0xDCE7ED

LV_FONT_DECLARE(s1_home_info_font_14);
LV_FONT_DECLARE(s1_nunito_extrabold_108);

static lv_obj_t *overlay;
static lv_obj_t *small_time_label;
static lv_obj_t *weather_icon_label;
static lv_obj_t *temperature_label;
static lv_obj_t *rain_label;
static lv_obj_t *hour_label;
static lv_obj_t *minute_label;
static lv_obj_t *date_label;

static const char *weekday_names[] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
};

static void set_hidden_if_changed(lv_obj_t *obj, bool hidden)
{
    if(obj == NULL || lv_obj_is_hidden(obj) == hidden) return;
    lv_obj_set_hidden(obj, hidden);
}

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if(label == NULL || text == NULL) return;

    const char *current = lv_label_get_text(label);
    if(current != NULL && strcmp(current, text) == 0) return;

    lv_label_set_text(label, text);
}

static void update_clock_text(void)
{
    if(overlay == NULL) return;

    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if(local == NULL) return;

    int hour12 = local->tm_hour % 12;
    if(hour12 == 0) hour12 = 12;

    char small_time[8];
    char hour[4];
    char minute[4];
    char date[40];

    snprintf(small_time, sizeof(small_time), "%02d:%02d", local->tm_hour, local->tm_min);
    snprintf(hour, sizeof(hour), "%02d", hour12);
    snprintf(minute, sizeof(minute), "%02d", local->tm_min);
    snprintf(date, sizeof(date), "%s  %d月%d日", weekday_names[local->tm_wday], local->tm_mon + 1, local->tm_mday);

    set_label_text_if_changed(small_time_label, small_time);
    set_label_text_if_changed(hour_label, hour);
    set_label_text_if_changed(minute_label, minute);
    set_label_text_if_changed(date_label, date);
}

static void refresh_home_overlay(lv_timer_t *timer)
{
    (void)timer;
    if(overlay == NULL) return;

    const bool on_home = s1_ui_router_current() == S1_PAGE_HOME;

    /*
     * Keep the 50 ms visibility safety sync so page transitions stay
     * responsive, but do not invalidate LVGL when visibility/text is already
     * correct. The previous code rewrote four labels 20 times per second.
     */
    set_hidden_if_changed(overlay, !on_home);
    if(on_home) update_clock_text();
}

void s1_ui_home_overlay_set_weather(int temperature_c, int rain_probability_percent, int weather_code)
{
    (void)weather_code;
    if(overlay == NULL) return;

    if(rain_probability_percent < 0) rain_probability_percent = 0;
    if(rain_probability_percent > 100) rain_probability_percent = 100;

    char temperature[16];
    char rain[24];
    snprintf(temperature, sizeof(temperature), "%d°", temperature_c);
    snprintf(rain, sizeof(rain), "降雨 %d%%", rain_probability_percent);

    /* A drop is used as the neutral weather glyph. The previous code mapped
     * clear weather to LV_SYMBOL_OK, which looked like a status check mark. */
    set_label_text_if_changed(weather_icon_label, LV_SYMBOL_TINT);
    set_label_text_if_changed(temperature_label, temperature);
    set_label_text_if_changed(rain_label, rain);
}

void s1_ui_home_overlay_show(void)
{
    if(overlay == NULL) s1_ui_home_overlay_init();
    set_hidden_if_changed(overlay, false);
    lv_obj_move_foreground(overlay);
    update_clock_text();
}

void s1_ui_home_overlay_hide(void)
{
    set_hidden_if_changed(overlay, true);
}

void s1_ui_home_overlay_init(void)
{
    if(overlay != NULL) return;

    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 170, 320);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_overflow_visible(overlay, true);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(HOME_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(overlay, lv_color_hex(HOME_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(overlay, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(overlay, 0, 0);
    lv_obj_set_style_bg_grad_stop(overlay, 255, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);

    small_time_label = lv_label_create(overlay);
    lv_label_set_text(small_time_label, "00:00");
    lv_obj_set_style_text_font(small_time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(small_time_label, lv_color_hex(0x818998), 0);
    lv_obj_align(small_time_label, LV_ALIGN_TOP_RIGHT, -12, 12);

    weather_icon_label = lv_label_create(overlay);
    lv_label_set_text(weather_icon_label, LV_SYMBOL_TINT);
    lv_obj_set_style_text_font(weather_icon_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(weather_icon_label, lv_color_hex(HOME_MIN_COLOR), 0);
    lv_obj_set_pos(weather_icon_label, 14, 34);

    temperature_label = lv_label_create(overlay);
    lv_label_set_text(temperature_label, "--°");
    lv_obj_set_style_text_font(temperature_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(temperature_label, lv_color_hex(HOME_TEXT_MAIN), 0);
    lv_obj_set_pos(temperature_label, 38, 32);

    rain_label = lv_label_create(overlay);
    lv_label_set_text(rain_label, "降雨 --%");
    lv_obj_set_style_text_font(rain_label, &s1_home_info_font_14, 0);
    lv_obj_set_style_text_color(rain_label, lv_color_hex(HOME_MIN_COLOR), 0);
    lv_obj_set_pos(rain_label, 14, 55);

    /* Full-screen labels center the native digit advances without transforms. */
    hour_label = lv_label_create(overlay);
    lv_label_set_text(hour_label, "12");
    lv_obj_set_size(hour_label, 170, LV_SIZE_CONTENT);
    lv_obj_set_pos(hour_label, 0, 101);
    lv_obj_set_style_text_align(hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hour_label, &s1_nunito_extrabold_108, 0);
    lv_obj_set_style_text_letter_space(hour_label, 0, 0);
    lv_obj_set_style_text_color(hour_label, lv_color_hex(HOME_HOUR_COLOR), 0);

    minute_label = lv_label_create(overlay);
    lv_label_set_text(minute_label, "00");
    lv_obj_set_size(minute_label, 170, LV_SIZE_CONTENT);
    lv_obj_set_pos(minute_label, 0, 204);
    lv_obj_set_style_text_align(minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(minute_label, &s1_nunito_extrabold_108, 0);
    lv_obj_set_style_text_letter_space(minute_label, 0, 0);
    lv_obj_set_style_text_color(minute_label, lv_color_hex(HOME_MIN_COLOR), 0);

    date_label = lv_label_create(overlay);
    lv_label_set_text(date_label, "星期--  --月--日");
    lv_obj_set_width(date_label, 146);
    lv_obj_set_pos(date_label, 12, 291);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(date_label, &s1_home_info_font_14, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(HOME_TEXT_MUTED), 0);

    lv_timer_create(refresh_home_overlay, 50, NULL);
    s1_ui_home_overlay_show();
}
