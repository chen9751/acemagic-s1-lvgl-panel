/**
 * @file main.c
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>

#ifdef _MSC_VER
#include <Windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

#include "lvgl/lvgl.h"
#include "../ui/s1_ui.h"
#include "../ui/pages/ui_home_overlay.h"
#include "../ui/ui_theme.h"
#include "../input/w1_input.h"
#include "../services/ha_client.h"
#include "../services/bluez_media_client.h"
#include "../services/weather_client.h"

/* Keep new standalone service/UI modules linked while the root CMake source
 * list remains explicit. */
#include "../services/bluez_media_client.c"
#include "../services/weather_client.c"
#include "../ui/pages/ui_home_overlay.c"
#include "../ui/ui_theme.c"

#include "lvgl/examples/lv_examples.h"
#include "lvgl/demos/lv_demos.h"
#include <SDL.h>
#include "hal/hal.h"

static uint32_t s1_tick_get(void)
{
    return (uint32_t)SDL_GetTicks();
}

static void music_action_cb(s1_music_action_t action, void *user_data)
{
    (void)user_data;
    bluez_media_action_t bluez_action = BLUEZ_MEDIA_PLAY_PAUSE;
    if(action == S1_MUSIC_ACTION_PREVIOUS) bluez_action = BLUEZ_MEDIA_PREVIOUS;
    else if(action == S1_MUSIC_ACTION_NEXT) bluez_action = BLUEZ_MEDIA_NEXT;
    if(bluez_media_control(bluez_action) != 0) printf("BlueZ media action failed\n");
}

static void sync_music_from_bluez(void)
{
    bluez_media_state_t state;
    if(!bluez_media_poll(&state)) return;
    if(!state.connected) {
        s1_ui_music_set_metadata(NULL, NULL, NULL, false);
        s1_ui_music_set_progress(0, 0);
        return;
    }
    s1_ui_music_set_metadata(state.title, state.artist, state.album, state.playing);
    s1_ui_music_set_progress(state.position_ms / 1000U, state.duration_ms / 1000U);
}

static void refresh_weather(void)
{
    weather_state_t state = {0};
    if(weather_client_refresh(&state) && state.valid) {
        s1_ui_home_overlay_set_weather(
            state.temperature_c,
            state.rain_probability_percent,
            state.weather_code
        );
    }
}

#if LV_USE_OS != LV_OS_FREERTOS
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lv_init();
    lv_tick_set_cb(s1_tick_get);
    sdl_hal_init(170, 320);

    s1_ui_init();
    s1_ui_home_overlay_init();
    s1_ui_apply_gradient_theme();

    w1_input_init();

    if(ha_client_init() != 0) printf("HA init failed\n");
    else printf("HA init OK\n");

    bluez_media_init();
    s1_ui_music_set_action_cb(music_action_cb, NULL);
    bluez_media_force_refresh();
    sync_music_from_bluez();

    weather_client_init();
    refresh_weather();
    uint32_t next_weather_refresh = SDL_GetTicks() + 600000U;

    while(1) {
        w1_input_poll();
        sync_music_from_bluez();

        uint32_t now = SDL_GetTicks();
        if((int32_t)(now - next_weather_refresh) >= 0) {
            refresh_weather();
            next_weather_refresh = now + 600000U;
        }

        uint32_t sleep_time_ms = lv_timer_handler();
        if(sleep_time_ms == LV_NO_TIMER_READY) sleep_time_ms = LV_DEF_REFR_PERIOD;

#ifdef _MSC_VER
        Sleep(sleep_time_ms);
#else
        usleep(sleep_time_ms * 1000);
#endif
    }
    return 0;
}
#endif
