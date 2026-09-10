#ifndef S1_UI_PAGE_H
#define S1_UI_PAGE_H
#include "lvgl/lvgl.h"
#include "../ui_router.h"
lv_obj_t *s1_ui_page_panel(lv_obj_t *parent);
const char *s1_ui_page_name(s1_page_id_t page);
void s1_ui_placeholder_init(lv_obj_t *parent);
void s1_ui_placeholder_show(s1_page_id_t page);
void s1_ui_placeholder_hide(void);
#endif
