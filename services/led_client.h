#ifndef LED_CLIENT_H
#define LED_CLIENT_H

#include <stdint.h>


typedef enum {
    LED_MODE_RAINBOW = 0x01,
    LED_MODE_BREATHING = 0x02,
    LED_MODE_COLOR_CYCLE = 0x03,
    LED_MODE_OFF = 0x04,
    LED_MODE_AUTOMATIC = 0x05
} led_mode_t;


int led_build_command(
    led_mode_t mode,
    uint8_t intensity,
    uint8_t speed,
    uint8_t command[5]
);
int led_set_mode(led_mode_t mode);


#endif
