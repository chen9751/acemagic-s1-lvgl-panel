#ifndef BLUEZ_MEDIA_CLIENT_H
#define BLUEZ_MEDIA_CLIENT_H

#include <stdbool.h>
#include <stdint.h>


typedef enum {
    BLUEZ_MEDIA_PREVIOUS = 0,
    BLUEZ_MEDIA_PLAY_PAUSE,
    BLUEZ_MEDIA_NEXT
} bluez_media_action_t;


typedef struct {
    bool connected;
    bool playing;
    uint32_t position_ms;
    uint32_t duration_ms;
    char title[192];
    char artist[192];
    char album[192];
} bluez_media_state_t;


int bluez_media_init(void);
bool bluez_media_poll(bluez_media_state_t *state);
int bluez_media_control(bluez_media_action_t action);
void bluez_media_force_refresh(void);


#endif
