#ifndef S1_UI_ROUTER_H
#define S1_UI_ROUTER_H
#include <stdbool.h>
typedef enum {
    S1_PAGE_AC, S1_PAGE_BATH, S1_PAGE_CURTAIN, S1_PAGE_HOME,
    S1_PAGE_LIGHT_LIVING, S1_PAGE_LIGHT_STUDY,
    S1_PAGE_LIGHT_BEDROOM, S1_PAGE_LIGHT_SMALL_BEDROOM,
    S1_PAGE_LED, S1_PAGE_MUSIC, S1_PAGE_COUNT
} s1_page_id_t;
void s1_ui_router_init(void);
s1_page_id_t s1_ui_router_current(void);
bool s1_ui_router_is_horizontal_page(s1_page_id_t page);
bool s1_ui_router_is_light_page(s1_page_id_t page);
bool s1_ui_router_left(void);
bool s1_ui_router_right(void);
bool s1_ui_router_up(void);
bool s1_ui_router_down(void);
void s1_ui_router_home(void);
#endif
