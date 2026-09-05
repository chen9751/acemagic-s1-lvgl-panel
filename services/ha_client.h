#ifndef HA_CLIENT_H
#define HA_CLIENT_H

int ha_client_init(void);

int ha_get_state(
    const char *entity_id,
    char *state_buf,
    int state_buf_size
);

int ha_toggle(
    const char *entity_id
);

#endif
