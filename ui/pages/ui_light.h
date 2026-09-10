#ifndef S1_UI_LIGHT_H
#define S1_UI_LIGHT_H
#include "../s1_ui.h"
#include "../ui_router.h"
void s1_ui_light_init(lv_obj_t *parent);
void s1_ui_light_show(s1_page_id_t page);
void s1_ui_light_hide(void);
void s1_ui_light_key(uint32_t key);
#endif
