#ifndef S1_UI_HOME_OVERLAY_H
#define S1_UI_HOME_OVERLAY_H

#include <stdbool.h>

void s1_ui_home_overlay_init(void);
void s1_ui_home_overlay_show(void);
void s1_ui_home_overlay_hide(void);
void s1_ui_home_overlay_set_weather(int temperature_c, int rain_probability_percent, int weather_code);

#endif
