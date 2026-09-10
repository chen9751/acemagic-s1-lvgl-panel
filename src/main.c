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

    while(1) {

        /*
         * Read W1 evdev events and forward them
         * to the same UI input entry used by SDL keyboard.
         */
        w1_input_poll();

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
