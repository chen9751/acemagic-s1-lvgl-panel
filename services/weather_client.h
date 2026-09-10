#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include <stdbool.h>

typedef struct {
    bool valid;
    int temperature_c;
    int rain_probability_percent;
    int weather_code;
} weather_state_t;

void weather_client_init(void);
bool weather_client_refresh(weather_state_t *state);

#endif
