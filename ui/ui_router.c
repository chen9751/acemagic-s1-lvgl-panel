#include "ui_router.h"

static s1_page_id_t current_page = S1_PAGE_HOME;

void s1_ui_router_init(void)
{
    current_page = S1_PAGE_HOME;
}

s1_page_id_t s1_ui_router_current(void)
{
    return current_page;
}

bool s1_ui_router_is_primary_page(s1_page_id_t page)
{
    return page >= S1_PAGE_HOME && page <= S1_PAGE_MUSIC;
}

/* Kept for compatibility with older page code. Primary pages are now vertical. */
bool s1_ui_router_is_horizontal_page(s1_page_id_t page)
{
    return s1_ui_router_is_primary_page(page);
}

bool s1_ui_router_is_light_page(s1_page_id_t page)
{
    return page >= S1_PAGE_LIGHT_LIVING && page <= S1_PAGE_LIGHT_SMALL_BEDROOM;
}

static bool primary_move(int delta)
{
    if(!s1_ui_router_is_primary_page(current_page)) return false;

    int index = (int)current_page + delta;
    while(index < (int)S1_PAGE_HOME) index += 4;
    while(index > (int)S1_PAGE_MUSIC) index -= 4;
    current_page = (s1_page_id_t)index;
    return true;
}

bool s1_ui_router_left(void)
{
    return false;
}

bool s1_ui_router_right(void)
{
    return false;
}

bool s1_ui_router_up(void)
{
    return primary_move(-1);
}

bool s1_ui_router_down(void)
{
    return primary_move(1);
}

bool s1_ui_router_open(s1_page_id_t page)
{
    if(page < S1_PAGE_HOME || page >= S1_PAGE_COUNT) return false;
    current_page = page;
    return true;
}

void s1_ui_router_home(void)
{
    current_page = S1_PAGE_HOME;
}
