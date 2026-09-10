#include "ui_router.h"
/* Enum order defines the eight-page horizontal ring. Branches never join it. */
static s1_page_id_t current_page = S1_PAGE_HOME;
void s1_ui_router_init(void) { current_page = S1_PAGE_HOME; }
s1_page_id_t s1_ui_router_current(void) { return current_page; }
bool s1_ui_router_is_horizontal_page(s1_page_id_t page)
{ return page >= S1_PAGE_AC && page <= S1_PAGE_LIGHT_SMALL_BEDROOM; }
bool s1_ui_router_is_light_page(s1_page_id_t page)
{ return page >= S1_PAGE_LIGHT_LIVING && page <= S1_PAGE_LIGHT_SMALL_BEDROOM; }
static bool horizontal(int delta)
{
    if(!s1_ui_router_is_horizontal_page(current_page)) return false;
    current_page = (s1_page_id_t)((current_page + delta + 8) % 8);
    return true;
}
bool s1_ui_router_left(void) { return horizontal(-1); }
bool s1_ui_router_right(void) { return horizontal(1); }
static bool branch(s1_page_id_t page)
{
    if(current_page != S1_PAGE_HOME) return false;
    current_page = page;
    return true;
}
bool s1_ui_router_up(void) { return branch(S1_PAGE_LED); }
bool s1_ui_router_down(void) { return branch(S1_PAGE_MUSIC); }
void s1_ui_router_home(void) { current_page = S1_PAGE_HOME; }
