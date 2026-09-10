#ifndef HA_CLIENT_H
#define HA_CLIENT_H

typedef struct {
    int is_on;
    int brightness_percent;
    int color_temperature_kelvin;
    int has_brightness;
    int has_color_temperature;
} ha_light_state_t;

int ha_client_init(void);

int ha_get_state(
    const char *entity_id,
    char *state_buf,
    int state_buf_size
);

int ha_get_light_state(
    const char *entity_id,
    ha_light_state_t *state
);

int ha_set_light_power(const char *entity_id, int on);

int ha_toggle(
    const char *entity_id
);

int ha_set_light_brightness(
    const char *entity_id,
    int brightness_percent
);

int ha_set_light_color_temperature(
    const char *entity_id,
    int color_temperature_kelvin
);

int ha_turn_off_all_lights(void);

int ha_count_on_lights(
    int *count
);

#endif
