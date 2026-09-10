/**
 * @file main.c
 */

/*********************
 *      INCLUDES
 *********************/

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* needed for usleep() */
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
#include "../input/w1_input.h"
#include "../services/ha_client.h"
#include "../services/bluez_media_client.h"

/*
 * bluez_media_client is kept as its own service module. The current root
 * CMake file still enumerates sources manually, so include its implementation
 * here until the source list is converted to the new services layout.
 */
#include "../services/bluez_media_client.c"

#include "lvgl/examples/lv_examples.h"
#include "lvgl/demos/lv_demos.h"

#include <SDL.h>

#include "hal/hal.h"

/*********************
 *  STATIC FUNCTIONS
 *********************/

static uint32_t s1_tick_get(void)
{
    /*
     * LVGL timers need a monotonically increasing millisecond time source.
     * The SDL build previously called lv_timer_handler() without advancing
     * LVGL's tick, so long-running timers (for example the 3 s Home Assistant
     * light-state refresh timer) did not reliably become due while the page
     * remained open. SDL_GetTicks() supplies that runtime clock directly.
     */
    return (uint32_t)SDL_GetTicks();
}


static void music_action_cb(
    s1_music_action_t action,
    void *user_data
)
{
    (void)user_data;

    bluez_media_action_t bluez_action = BLUEZ_MEDIA_PLAY_PAUSE;

    if(action == S1_MUSIC_ACTION_PREVIOUS) {
        bluez_action = BLUEZ_MEDIA_PREVIOUS;
    }
    else if(action == S1_MUSIC_ACTION_NEXT) {
        bluez_action = BLUEZ_MEDIA_NEXT;
    }

    if(bluez_media_control(bluez_action) != 0) {
        printf("BlueZ media action failed\n");
    }
}


static void sync_music_from_bluez(void)
{
    bluez_media_state_t state;

    if(!bluez_media_poll(&state)) {
        return;
    }

    if(!state.connected) {
        s1_ui_music_set_metadata(NULL, NULL, NULL, false);
        s1_ui_music_set_progress(0, 0);
        return;
    }

    s1_ui_music_set_metadata(
        state.title,
        state.artist,
        state.album,
        state.playing
    );

    s1_ui_music_set_progress(
        state.position_ms / 1000U,
        state.duration_ms / 1000U
    );
}

/*********************
 *  GLOBAL FUNCTIONS
 *********************/

#if LV_USE_OS != LV_OS_FREERTOS

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Initialize LVGL */
    lv_init();

    /*
     * Give LVGL a real millisecond clock before creating any UI timers.
     * This is required by the periodic Home Assistant synchronization timer.
     */
    lv_tick_set_cb(s1_tick_get);

    /* Initialize SDL display/input */
    sdl_hal_init(170, 320);

    /* Initialize S1 UI */
    s1_ui_init();

    /* Initialize W1 remote input */
    w1_input_init();

    /* Initialize Home Assistant client */
    if(ha_client_init() != 0) {
        printf("HA init failed\n");
    }
    else {
        printf("HA init OK\n");
    }

    /*
     * BlueZ exposes an AVRCP player for an iPad/iPhone A2DP source as
     * org.bluez.MediaPlayer1. It supplies title/artist/album/status/position
     * and Previous/Play/Pause/Next methods over the system D-Bus.
     */
    bluez_media_init();
    s1_ui_music_set_action_cb(music_action_cb, NULL);
    bluez_media_force_refresh();
    sync_music_from_bluez();

    while(1) {

        /*
         * Read W1 evdev events and forward them
         * to the same UI input entry used by SDL keyboard.
         */
        w1_input_poll();

        /* Keep Music metadata, playback state and progress synchronized. */
        sync_music_from_bluez();

        /*
         * Periodically call the LVGL timer handler. Timer deadlines are
         * calculated from the SDL tick callback registered above.
         */
        uint32_t sleep_time_ms = lv_timer_handler();

        if(sleep_time_ms == LV_NO_TIMER_READY) {
            sleep_time_ms = LV_DEF_REFR_PERIOD;
        }

#ifdef _MSC_VER
        Sleep(sleep_time_ms);
#else
        usleep(sleep_time_ms * 1000);
#endif
    }

    return 0;
}

#endif
