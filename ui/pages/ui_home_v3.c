#include "ui_home_v3.h"
#include "../ui_router.h"
#include "lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HOME_BLUE      0x1AA8F7
#define HOME_TEXT      0xF3F7FA
#define HOME_MUTED     0x73818C
#define HOME_WARM      0xFFD05A
#define HOME_CARD      0x101C27
#define HOME_SELECTED  0x123247
#define HOME_BG_TOP    0x07111A
#define HOME_BG_BOTTOM 0x02070B

LV_FONT_DECLARE(s1_home_info_font_14);
LV_FONT_DECLARE(s1_nunito_extrabold_108);

static lv_obj_t *overlay;
static lv_obj_t *date_label;
static lv_obj_t *hour_label;
static lv_obj_t *minute_label;
static lv_obj_t *cards[4];
static lv_obj_t *lamp_glass[4];
static lv_obj_t *lamp_cord[4];
static lv_obj_t *lamp_base[4];
static lv_obj_t *state_labels[4];

static char last_date[40];
static char last_hour[4];
static char last_minute[4];
static int last_selected = -1;
static int last_on[4] = { -1, -1, -1, -1 };

static const char *weekday_cn[] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
};

static void set_hidden_if_changed(lv_obj_t *obj, bool hidden)
{
    if(obj == NULL || lv_obj_is_hidden(obj) == hidden) return;
    lv_obj_set_hidden(obj, hidden);
}

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

static void create_sofa_icon(lv_obj_t *card)
{
    block(card, 8, 40, 22, 9, 0x92A0AB, 3);
    block(card, 6, 48, 26, 6, 0x92A0AB, 2);
    block(card, 6, 43, 4, 10, 0x92A0AB, 2);
    block(card, 28, 43, 4, 10, 0x92A0AB, 2);
}

static void create_computer_icon(lv_obj_t *card)
{
    lv_obj_t *screen = block(card, 7, 37, 24, 15, 0x92A0AB, 2);
    lv_obj_set_style_border_width(screen, 1, 0);
    lv_obj_set_style_border_color(screen, lv_color_hex(0xBAC5CD), 0);
    block(card, 17, 52, 4, 5, 0x92A0AB, 1);
    block(card, 11, 57, 16, 3, 0x92A0AB, 1);
}

static void create_bed_icon(lv_obj_t *card)
{
    block(card, 7, 43, 24, 10, 0x92A0AB, 2);
    block(card, 5, 39, 4, 18, 0x92A0AB, 1);
    block(card, 7, 41, 8, 5, 0xBAC5CD, 2);
    block(card, 7, 54, 24, 3, 0x92A0AB, 1);
}

static void create_bunk_icon(lv_obj_t *card)
{
    block(card, 7, 38, 24, 5, 0x92A0AB, 1);
    block(card, 7, 50, 24, 5, 0x92A0AB, 1);
    block(card, 5, 35, 3, 23, 0x92A0AB, 1);
    block(card, 30, 35, 3, 23, 0x92A0AB, 1);
    block(card, 23, 43, 3, 7, 0x92A0AB, 1);
}

static void create_room_icon(lv_obj_t *card, int index)
{
    if(index == 0) create_sofa_icon(card);
    else if(index == 1) create_computer_icon(card);
    else if(index == 2) create_bed_icon(card);
    else create_bunk_icon(card);
}

static void create_card(int index, int x)
{
    cards[index] = lv_obj_create(overlay);
    lv_obj_set_size(cards[index], 38, 78);
    lv_obj_set_pos(cards[index], x, 239);
    lv_obj_set_scrollable(cards[index], false);
    lv_obj_set_style_radius(cards[index], 11, 0);
    lv_obj_set_style_pad_all(cards[index], 0, 0);
    lv_obj_set_style_bg_color(cards[index], lv_color_hex(HOME_CARD), 0);
    lv_obj_set_style_bg_opa(cards[index], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cards[index], 0, 0);

    lamp_cord[index] = block(cards[index], 18, 5, 2, 7, HOME_MUTED, 1);
    lamp_glass[index] = block(cards[index], 11, 11, 16, 13, HOME_MUTED, 7);
    lamp_base[index] = block(cards[index], 14, 23, 10, 3, HOME_MUTED, 1);

    create_room_icon(cards[index], index);

    state_labels[index] = lv_label_create(cards[index]);
    lv_label_set_text(state_labels[index], "OFF");
    lv_obj_set_width(state_labels[index], 36);
    lv_obj_set_pos(state_labels[index], 1, 61);
    lv_obj_set_style_text_align(state_labels[index], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state_labels[index], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(state_labels[index], lv_color_hex(HOME_MUTED), 0);
}

static void update_clock(void)
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if(local == NULL) return;

    int h = local->tm_hour % 12;
    if(h == 0) h = 12;

    char date[40];
    char hour[4];
    char minute[4];
    snprintf(date, sizeof(date), "%d月%d日  %s", local->tm_mon + 1, local->tm_mday, weekday_cn[local->tm_wday]);
    snprintf(hour, sizeof(hour), "%02d", h);
    snprintf(minute, sizeof(minute), "%02d", local->tm_min);

    if(strcmp(last_date, date) != 0) {
        lv_label_set_text(date_label, date);
        snprintf(last_date, sizeof(last_date), "%s", date);
    }
    if(strcmp(last_hour, hour) != 0) {
        lv_label_set_text(hour_label, hour);
        snprintf(last_hour, sizeof(last_hour), "%s", hour);
    }
    if(strcmp(last_minute, minute) != 0) {
        lv_label_set_text(minute_label, minute);
        snprintf(last_minute, sizeof(last_minute), "%s", minute);
    }
}

static void read_underlying_home(int *selected, int on[4])
{
    *selected = 0;
    for(int i = 0; i < 4; i++) on[i] = 0;

    lv_obj_t *screen = lv_screen_active();
    if(screen == NULL || lv_obj_get_child_count(screen) == 0) return;

    lv_obj_t *root = lv_obj_get_child(screen, 0);
    if(root == NULL || lv_obj_get_child_count(root) < 2) return;

    lv_obj_t *home = lv_obj_get_child(root, 1);
    if(home == NULL || lv_obj_get_child_count(home) < 7) return;

    for(int i = 0; i < 4; i++) {
        lv_obj_t *card = lv_obj_get_child(home, 3 + i);
        if(card == NULL) continue;

        if(lv_obj_get_style_border_width(card, 0) > 0) *selected = i;

        uint32_t count = lv_obj_get_child_count(card);
        for(uint32_t j = 0; j < count; j++) {
            lv_obj_t *child = lv_obj_get_child(card, (int32_t)j);
            if(!lv_obj_check_type(child, &lv_label_class)) continue;
            const char *text = lv_label_get_text(child);
            if(text != NULL && strcmp(text, "ON") == 0) on[i] = 1;
        }
    }
}

static void update_cards(void)
{
    int selected;
    int on[4];
    read_underlying_home(&selected, on);

    if(selected != last_selected) {
        for(int i = 0; i < 4; i++) {
            if(last_selected >= 0 && i != selected && i != last_selected) continue;
            bool active = i == selected;
            lv_obj_set_style_bg_color(cards[i], lv_color_hex(active ? HOME_SELECTED : HOME_CARD), 0);
            lv_obj_set_style_border_width(cards[i], 1, 0);
            lv_obj_set_style_border_color(cards[i], lv_color_hex(active ? HOME_BLUE : 0x21313F), 0);
        }
        last_selected = selected;
    }

    for(int i = 0; i < 4; i++) {
        if(on[i] == last_on[i]) continue;
        uint32_t lamp = on[i] ? HOME_WARM : HOME_MUTED;
        lv_obj_set_style_bg_color(lamp_cord[i], lv_color_hex(lamp), 0);
        lv_obj_set_style_bg_color(lamp_glass[i], lv_color_hex(lamp), 0);
        lv_obj_set_style_bg_color(lamp_base[i], lv_color_hex(lamp), 0);
        lv_label_set_text(state_labels[i], on[i] ? "ON" : "OFF");
        lv_obj_set_style_text_color(state_labels[i], lv_color_hex(on[i] ? HOME_WARM : HOME_MUTED), 0);
        last_on[i] = on[i];
    }
}

static void refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    bool home = s1_ui_router_current() == S1_PAGE_HOME;
    set_hidden_if_changed(overlay, !home);
    if(!home) return;

    /* Reordering a visible full-screen overlay invalidates the entire page. */
    if(lv_obj_get_child(lv_screen_active(), -1) != overlay)
        lv_obj_move_foreground(overlay);
    update_clock();
    update_cards();
}

void s1_ui_home_v3_init(void)
{
    if(overlay != NULL) return;

    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 170, 320);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(HOME_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(overlay, lv_color_hex(HOME_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(overlay, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);

    date_label = lv_label_create(overlay);
    lv_label_set_text(date_label, "9月14日  星期一");
    lv_obj_set_width(date_label, 170);
    lv_obj_set_pos(date_label, 0, 13);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(date_label, &s1_home_info_font_14, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xD7E1E8), 0);

    hour_label = lv_label_create(overlay);
    lv_label_set_text(hour_label, "12");
    lv_obj_set_size(hour_label, 170, 108);
    lv_obj_set_pos(hour_label, 0, 36);
    lv_obj_set_style_text_align(hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hour_label, &s1_nunito_extrabold_108, 0);
    lv_obj_set_style_text_color(hour_label, lv_color_hex(HOME_TEXT), 0);
    lv_obj_set_style_transform_scale(hour_label, 248, 0);

    minute_label = lv_label_create(overlay);
    lv_label_set_text(minute_label, "00");
    lv_obj_set_size(minute_label, 170, 108);
    lv_obj_set_pos(minute_label, 0, 128);
    lv_obj_set_style_text_align(minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(minute_label, &s1_nunito_extrabold_108, 0);
    lv_obj_set_style_text_color(minute_label, lv_color_hex(HOME_BLUE), 0);
    lv_obj_set_style_transform_scale(minute_label, 248, 0);

    create_card(0, 6);
    create_card(1, 46);
    create_card(2, 86);
    create_card(3, 126);

    update_clock();
    update_cards();
    lv_timer_create(refresh_cb, 150, NULL);
    refresh_cb(NULL);
}
