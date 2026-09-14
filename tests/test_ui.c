#include "ui/s1_ui.h"
#include "ui/ui_router.h"
#include "ui/pages/ui_home_overlay.h"
#include "ui/pages/ui_home_v3.h"
#include "ui/pages/ui_music_v2.h"
#include "ui/ui_second_pass.h"
#include "input/s1_input_dispatch.h"
LV_FONT_DECLARE(s1_nunito_extrabold_108);
LV_FONT_DECLARE(s1_led_font_12);
#include "services/ha_client.h"
#include "services/led_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int calls, fail_service, last_value, led_calls, music_calls;
static const char *last_entity, *last_service;
static s1_music_action_t last_action;
static int service(const char *entity, const char *name, int value)
{ calls++; last_entity = entity; last_service = name; last_value = value; return fail_service ? -1 : 0; }
int ha_set_light_brightness(const char *entity, int value) { return service(entity, "brightness", value); }
int ha_set_light_color_temperature(const char *entity, int value) { return service(entity, "temperature", value); }
int ha_set_light_power(const char *entity, int on) { return service(entity, "power", on); }
int ha_get_light_state(const char *entity, ha_light_state_t *state) { (void)entity; (void)state; return -1; }
static led_mode_t last_mode;
static int last_intensity, last_speed;
int led_set_state(led_mode_t mode, uint8_t intensity, uint8_t speed)
{ led_calls++; last_mode = mode; last_intensity = intensity; last_speed = speed; return fail_service ? -1 : 0; }
int ha_toggle(const char *entity) { return service(entity, "toggle", 0); }
static void music(s1_music_action_t action, void *data)
{ (void)data; music_calls++; last_action = action; }
static lv_obj_t *label(lv_obj_t *obj, const char *text)
{
    if(lv_obj_is_hidden(obj)) return NULL;
    if(lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0) return obj;
    for(uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *found = label(lv_obj_get_child(obj, i), text);
        if(found) return found;
    }
    return NULL;
}
static int bars(lv_obj_t *obj)
{
    if(lv_obj_is_hidden(obj)) return 0;
    int count = lv_obj_check_type(obj, &lv_bar_class);
    for(uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) count += bars(lv_obj_get_child(obj, i));
    return count;
}
static void visible(const char *text) { assert(label(lv_screen_active(), text)); }
static void go(s1_page_id_t page)
{
    s1_ui_key(LV_KEY_HOME);
    for(int i = 0; i < 8 && s1_ui_router_current() != page; i++) s1_ui_key(LV_KEY_DOWN);
    assert(s1_ui_router_current() == page);
}
static unsigned char pixels[170 * 320 * 3];
static int dirty_pixels, dirty_top = 320;
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    dirty_pixels += (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    if(area->y1 < dirty_top) dirty_top = area->y1;
    int stride = (area->x2 - area->x1 + 1) * 3;
    for(int y = area->y1; y <= area->y2; y++) {
        for(int x = area->x1; x <= area->x2; x++) {
            int src = (y - area->y1) * stride + (x - area->x1) * 3;
            int dst = (y * 170 + x) * 3;
            pixels[dst] = data[src + 2]; pixels[dst + 1] = data[src + 1]; pixels[dst + 2] = data[src];
        }
    }
    lv_display_flush_ready(display);
}
static void snapshot(const char *name)
{
    lv_tick_inc(400); lv_timer_handler(); lv_refr_now(NULL);
    FILE *f = fopen(name, "wb"); assert(f);
    fprintf(f, "P6\n170 320\n255\n");
    assert(fwrite(pixels, 1, sizeof(pixels), f) == sizeof(pixels)); fclose(f);
}
/* Check real LVGL layout for every two-digit value, including narrow 11. */
static void test_home_clock(void)
{
    s1_ui_home_overlay_init();
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *overlay = lv_obj_get_child(screen, -1);
    lv_obj_t *clocks[2];
    unsigned count = 0;
    for(uint32_t i = 0; i < lv_obj_get_child_count(overlay); i++) {
        lv_obj_t *child = lv_obj_get_child(overlay, i);
        if(lv_obj_get_style_text_font(child, 0) == &s1_nunito_extrabold_108) {
            assert(count < 2);
            clocks[count++] = child;
        }
    }
    assert(count == 2);
    lv_timer_enable(false);
    for(int value = 0; value < 100; value++) {
        char text[4];
        snprintf(text, sizeof(text), "%02d", value);
        for(unsigned i = 0; i < 2; i++) {
            lv_label_set_text(clocks[i], text);
            lv_obj_update_layout(clocks[i]);
            lv_area_t area;
            lv_obj_get_coords(clocks[i], &area);
            assert(area.x1 == 0 && area.x2 == 169);
            assert(lv_obj_get_style_text_align(clocks[i], 0) == LV_TEXT_ALIGN_CENTER);
            assert(lv_obj_get_style_transform_scale_x(clocks[i], 0) == 256);
            assert(lv_obj_get_style_transform_scale_y(clocks[i], 0) == 256);
            lv_point_t size;
            lv_text_get_size(&size, text, &s1_nunito_extrabold_108, 0, 0, 170, LV_TEXT_FLAG_NONE);
            assert(size.x <= 170 && size.y == s1_nunito_extrabold_108.line_height);
            assert(area.y1 >= 75 && area.y2 < 291);
        }
    }
    lv_label_set_text(clocks[0], "04");
    lv_label_set_text(clocks[1], "57");
    snapshot("home-nunito-0457.ppm");
    lv_label_set_text(clocks[0], "11");
    lv_label_set_text(clocks[1], "11");
    snapshot("home-nunito-1111.ppm");
    lv_timer_enable(true);
    puts("PASS: Nunito Home 00-99, screen centering, no scaling or clipping");
}
int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(170, 320);
    static unsigned char buffer[170 * 320 * 3];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    s1_ui_init();
    s1_ui_music_set_action_cb(music, NULL);
    snapshot("home.ppm");
    s1_ui_music_v2_init();
    s1_ui_second_pass_init();
    for(int round = 0; round < 2; round++) {
        for(int i = 1; i <= 4; i++) {
            s1_input_dispatch_key(LV_KEY_DOWN);
            assert(s1_ui_router_current() == (s1_page_id_t)(i % 4));
        }
    }
    go(S1_PAGE_HA);
    const char *names[] = { "客厅灯", "书房灯", "卧室灯", "小卧室灯", "空调", "窗帘", "浴霸" };
    for(int i = 0; i < 7; i++) {
        visible(names[i]); visible("MENU  ENTER DEVICE");
        snapshot("ha.ppm");
        s1_input_dispatch_key(LV_KEY_RIGHT);
    }
    for(int i = 0; i < 4; i++) {
        s1_input_dispatch_key(S1_KEY_MENU);
        assert(s1_ui_router_is_light_page(s1_ui_router_current()));
        visible("OFF"); assert(bars(lv_screen_active()) == 0);
        fail_service = 1;
        s1_input_dispatch_key(LV_KEY_ENTER); visible("OFF");
        fail_service = 0;
        go(S1_PAGE_HA); s1_input_dispatch_key(LV_KEY_RIGHT);
    }
    const uint32_t glyphs[] = {0x4eae, 0x5ea6, 0x901f, 0xff1a, '1', '5', '/'};
    for(unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++) {
        lv_font_glyph_dsc_t glyph;
        assert(lv_font_get_glyph_dsc(&s1_led_font_12, &glyph, glyphs[i], 0));
        assert(!glyph.is_placeholder);
    }
    go(S1_PAGE_LED);
    lv_tick_inc(3000); lv_timer_handler(); visible("LED");
    visible("亮度：3/5"); visible("速度：3/5");
    s1_input_dispatch_key(LV_KEY_RIGHT);
    s1_input_dispatch_key(LV_KEY_RIGHT); visible("COLOR");
    lv_obj_update_layout(lv_screen_active());
    lv_obj_t *mode = label(lv_screen_active(), "COLOR");
    assert(lv_obj_get_height(mode) == lv_font_montserrat_14.line_height);
    snapshot("led-color.ppm");
    s1_input_dispatch_key(LV_KEY_RIGHT); visible("AUTO");
    snapshot("led-auto.ppm");
    int before = led_calls;
    s1_input_dispatch_key(S1_KEY_MENU);
    assert(led_calls == before);
    s1_input_dispatch_key(LV_KEY_RIGHT);
    assert(last_mode == LED_MODE_RAINBOW && last_intensity == 4);
    visible("亮度：4/5");
    fail_service = 1;
    s1_input_dispatch_key(LV_KEY_RIGHT); visible("亮度：4/5");
    fail_service = 0;
    for(int i = 0; i < 8; i++) s1_input_dispatch_key(LV_KEY_LEFT);
    visible("亮度：1/5");
    for(int i = 0; i < 8; i++) s1_input_dispatch_key(LV_KEY_RIGHT);
    visible("亮度：5/5");
    s1_input_dispatch_key(S1_KEY_MENU);
    s1_input_dispatch_key(LV_KEY_LEFT);
    assert(last_speed == 2); visible("速度：2/5");
    snapshot("led-speed.ppm");
    s1_input_dispatch_key(S1_KEY_MENU);
    s1_input_dispatch_key(LV_KEY_ENTER);
    assert(last_mode == LED_MODE_AUTOMATIC);
    s1_input_dispatch_key(LV_KEY_RIGHT); visible("OFF");
    s1_input_dispatch_key(LV_KEY_RIGHT); visible("RAINBOW");
    go(S1_PAGE_MUSIC);
    assert(!label(lv_screen_active(), "PAUSED"));
    s1_ui_music_v2_set_state(true, false);
    visible("PAUSED");
    snapshot("music-paused.ppm");
    s1_ui_music_v2_set_state(true, true);
    visible("PLAYING");
    assert(!label(lv_screen_active(), "PAUSED"));
    s1_input_dispatch_key(LV_KEY_LEFT); assert(last_action == S1_MUSIC_ACTION_PREVIOUS);
    s1_input_dispatch_key(LV_KEY_RIGHT); assert(last_action == S1_MUSIC_ACTION_NEXT);
    s1_input_dispatch_key(LV_KEY_ENTER); assert(last_action == S1_MUSIC_ACTION_PLAY_PAUSE);
    assert(music_calls == 3);
    lv_obj_t *next_icon = label(lv_screen_active(), LV_SYMBOL_NEXT);
    assert(next_icon);
    lv_obj_send_event(lv_obj_get_parent(next_icon), LV_EVENT_CLICKED, NULL);
    assert(music_calls == 4 && last_action == S1_MUSIC_ACTION_NEXT);
    s1_ui_music_v2_control_result(false); visible("CONTROL FAILED");
    s1_ui_music_v2_control_result(true); visible("PLAYING");
    s1_ui_music_v2_set_progress(50, 100);
    snapshot("music-playing.ppm");
    go(S1_PAGE_HOME);
    puts("PASS: vertical navigation, HA sync, LED focus/limits/failures, Music state and dispatch");
    test_home_clock();
    s1_ui_home_v3_init();
    snapshot("home-v3.ppm");
    /* A stationary Home tick must not redraw the full overlay. */
    dirty_pixels = 0;
    lv_tick_inc(160); lv_timer_handler(); lv_refr_now(NULL);
    assert(dirty_pixels < 170 * 320);
    go(S1_PAGE_LED);
    lv_tick_inc(200); lv_timer_handler(); lv_refr_now(NULL);
    s1_input_dispatch_key(S1_KEY_MENU);
    lv_refr_now(NULL);
    dirty_pixels = 0; dirty_top = 320;
    s1_input_dispatch_key(LV_KEY_LEFT);
    lv_refr_now(NULL);
    assert(dirty_pixels > 0 && dirty_pixels < 170 * 100 && dirty_top >= 180);
    printf("PASS: LED adjustment flushed %d pixels, top=%d (no circle redraw)\n", dirty_pixels, dirty_top);
    lv_deinit();
    return 0;
}
