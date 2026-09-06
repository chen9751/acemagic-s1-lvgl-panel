#ifndef S1_UI_H
#define S1_UI_H

#include "lvgl/lvgl.h"


#define S1_KEY_MENU       0x1001
#define S1_KEY_VOL_UP     0x1002
#define S1_KEY_VOL_DOWN   0x1003


typedef enum {
    S1_MUSIC_ACTION_PREVIOUS = 0,
    S1_MUSIC_ACTION_PLAY_PAUSE,
    S1_MUSIC_ACTION_NEXT
} s1_music_action_t;


typedef void (*s1_music_action_cb_t)(
    s1_music_action_t action,
    void *user_data
);


void s1_ui_init(void);
void s1_ui_key(uint32_t key);
void s1_ui_home_set_weather(
    int temperature_c,
    int rain_probability_percent
);
void s1_ui_music_set_action_cb(
    s1_music_action_cb_t callback,
    void *user_data
);
void s1_ui_music_set_metadata(
    const char *song,
    const char *artist,
    const char *album,
    bool playing
);
void s1_ui_music_set_progress(
    uint32_t elapsed_seconds,
    uint32_t duration_seconds
);


#endif
