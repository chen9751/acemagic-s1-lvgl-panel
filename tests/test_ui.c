#include "ui/s1_ui.h"
#include "ui/ui_router.h"
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
int led_set_mode(led_mode_t mode) { (void)mode; led_calls++; return 0; }
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
    for(int i = 0; i < 8 && s1_ui_router_current() != page; i++) s1_ui_key(LV_KEY_RIGHT);
    assert(s1_ui_router_current() == page);
}
static unsigned char pixels[170 * 320 * 3];
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
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
int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(170, 320);
    static unsigned char buffer[170 * 320 * 3];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    s1_ui_init();
    s1_ui_music_set_action_cb(music, NULL);
    snapshot("home.ppm");
    const s1_page_id_t ring[] = { S1_PAGE_AC, S1_PAGE_BATH, S1_PAGE_CURTAIN, S1_PAGE_HOME,
        S1_PAGE_LIGHT_LIVING, S1_PAGE_LIGHT_STUDY, S1_PAGE_LIGHT_BEDROOM, S1_PAGE_LIGHT_SMALL_BEDROOM };
    go(S1_PAGE_AC);
    for(int round = 0; round < 2; round++) for(int i = 0; i < 8; i++) {
        assert(s1_ui_router_current() == ring[i]); s1_ui_key(LV_KEY_RIGHT);
    }
    for(int i = 7; i >= 0; i--) { s1_ui_key(LV_KEY_LEFT); assert(s1_ui_router_current() == ring[i]); }
    for(int i = 0; i < 8; i++) if(ring[i] != S1_PAGE_HOME) {
        go(ring[i]); int before = calls;
        s1_ui_key(LV_KEY_UP); s1_ui_key(LV_KEY_DOWN);
        assert(s1_ui_router_current() == ring[i] && calls == before);
    }
    go(S1_PAGE_AC); visible("空调"); visible("Coming soon"); snapshot("placeholder.ppm");
    const char *entities[] = { "light.yeelink_ceil40_9771_light", "light.yeelink_ceil40_d8b6_light",
        "light.yeelink_ceiling17_b415_light", "light.yeelink_ceiling17_40d7_light" };
    for(int i = 0; i < 4; i++) {
        s1_page_id_t page = (s1_page_id_t)(S1_PAGE_LIGHT_LIVING + i);
        go(page); visible("OFF"); assert(bars(lv_screen_active()) == 0);
        int before = calls;
        s1_ui_key(S1_KEY_MENU); s1_ui_key(S1_KEY_VOL_UP); s1_ui_key(S1_KEY_VOL_DOWN);
        assert(calls == before);
        if(i == 0) snapshot("light-off.ppm");
        fail_service = 1; s1_ui_key(LV_KEY_ENTER); visible("OFF"); visible("Command failed");
        fail_service = 0; s1_ui_key(LV_KEY_ENTER); visible("亮度"); visible("50%");
        assert(strcmp(last_entity, entities[i]) == 0 && strcmp(last_service, "brightness") == 0);
        assert(bars(lv_screen_active()) == 1);
        if(i == 0) snapshot("light-brightness.ppm");
        before = calls; s1_ui_key(S1_KEY_VOL_UP); s1_ui_key(S1_KEY_VOL_DOWN); assert(calls == before);
        fail_service = 1; s1_ui_key(LV_KEY_UP); visible("50%");
        fail_service = 0;
        for(int j = 0; j < 12; j++) s1_ui_key(LV_KEY_DOWN);
        visible("10%"); assert(last_value == 10);
        for(int j = 0; j < 12; j++) s1_ui_key(LV_KEY_UP);
        visible("100%"); assert(last_value == 100);
        s1_ui_key(S1_KEY_MENU); visible("色温"); visible("4600K");
        if(i == 0) snapshot("light-temperature.ppm");
        for(int j = 0; j < 12; j++) s1_ui_key(LV_KEY_DOWN);
        visible("2700K"); assert(last_value == 2700 && strcmp(last_service, "temperature") == 0);
        for(int j = 0; j < 12; j++) s1_ui_key(LV_KEY_UP);
        visible("6500K"); assert(last_value == 6500);
        before = calls;
        s1_ui_key(LV_KEY_RIGHT); s1_ui_key(LV_KEY_LEFT);
        assert(s1_ui_router_current() == page); visible("亮度"); visible("100%"); assert(calls == before);
        s1_ui_key(S1_KEY_MENU); visible("色温"); visible("6500K");
        fail_service = 1; s1_ui_key(LV_KEY_ENTER); visible("6500K");
        fail_service = 0; s1_ui_key(LV_KEY_ENTER); visible("OFF");
        assert(strcmp(last_service, "power") == 0 && last_value == 0);
        s1_ui_key(LV_KEY_ENTER); visible("亮度"); visible("100%");
        s1_ui_key(LV_KEY_ENTER); visible("OFF");
    }
    go(S1_PAGE_HOME); s1_ui_key(LV_KEY_UP); assert(s1_ui_router_current() == S1_PAGE_LED);
    s1_ui_key(LV_KEY_LEFT); s1_ui_key(LV_KEY_RIGHT); assert(s1_ui_router_current() == S1_PAGE_LED);
    s1_ui_key(LV_KEY_DOWN); s1_ui_key(LV_KEY_ENTER); assert(led_calls == 1);
    snapshot("led.ppm");
    s1_ui_key(LV_KEY_ESC); assert(s1_ui_router_current() == S1_PAGE_HOME);
    s1_ui_key(LV_KEY_DOWN); assert(s1_ui_router_current() == S1_PAGE_MUSIC);
    snapshot("music.ppm");
    s1_ui_key(LV_KEY_ENTER); s1_ui_key(LV_KEY_LEFT); assert(last_action == S1_MUSIC_ACTION_PREVIOUS);
    s1_ui_key(LV_KEY_RIGHT); assert(last_action == S1_MUSIC_ACTION_NEXT);
    s1_ui_key(LV_KEY_ENTER); assert(last_action == S1_MUSIC_ACTION_PLAY_PAUSE && music_calls == 3);
    s1_ui_key(LV_KEY_ESC); assert(s1_ui_router_current() == S1_PAGE_MUSIC);
    s1_ui_key(LV_KEY_ESC); assert(s1_ui_router_current() == S1_PAGE_HOME);
    s1_ui_key(LV_KEY_DOWN); s1_ui_key(LV_KEY_ENTER); s1_ui_key(LV_KEY_HOME);
    assert(s1_ui_router_current() == S1_PAGE_HOME);
    for(int p = 0; p < 8; p++) { go(ring[p]); s1_ui_key(LV_KEY_HOME); assert(s1_ui_router_current() == S1_PAGE_HOME); }
    puts("PASS: ring, branch isolation, four lights, failures, limits, volume, HOME/BACK, LED and MUSIC");
    lv_deinit();
    return 0;
}
