#include "ui_theme.h"
#include "lvgl/lvgl.h"

#define THEME_BG_TOP    0x080A0F
#define THEME_BG_BOTTOM 0x0B1822

static void apply_to_large_top_level(lv_obj_t *obj)
{
    if(obj == NULL) return;
    if(lv_obj_get_width(obj) >= 160 && lv_obj_get_height(obj) >= 260) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(THEME_BG_TOP), 0);
        lv_obj_set_style_bg_grad_color(obj, lv_color_hex(THEME_BG_BOTTOM), 0);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    }
}

void s1_ui_apply_gradient_theme(void)
{
    lv_obj_t *screen = lv_screen_active();
    uint32_t count = lv_obj_get_child_count(screen);
    for(uint32_t i = 0; i < count; i++) {
        apply_to_large_top_level(lv_obj_get_child(screen, (int32_t)i));
    }
}
