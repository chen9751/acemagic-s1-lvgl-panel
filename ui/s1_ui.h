#ifndef S1_UI_H
#define S1_UI_H

#include "lvgl/lvgl.h"


#define S1_KEY_MENU       0x1001
#define S1_KEY_VOL_UP     0x1002
#define S1_KEY_VOL_DOWN   0x1003


void s1_ui_init(void);
void s1_ui_key(uint32_t key);


#endif