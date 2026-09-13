#include "ui_second_pass.h"
#include "lvgl/lvgl.h"

#include <string.h>

#define UI_BLUE  0x1AA8F7
#define UI_MUTED 0x7D8A96

static lv_obj_t *music_status_overlay;

static lv_obj_t *find_label_text(lv_obj_t *parent, const char *a, const char *b)
{
    if(parent == NULL) return NULL;

    uint32_t count = lv_obj_get_child_count(parent);
    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);
        if(lv_obj_check_type(child, &lv_label_class)) {
            const char *text = lv_label_get_text(child);
            if(text != NULL && ((a != NULL && strcmp(text, a) == 0) ||
                                (b != NULL && strcmp(text, b) == 0))) {
                return child;
            }
        }

        lv_obj_t *nested = find_label_text(child, a, b);
        if(nested != NULL) return nested;
    }
    return NULL;
}

static void refine_ha_panel(lv_obj_t *root)
{
    /* s1_ui.c creates: title, HOME, HA, LED, MUSIC ... in that order. */
    if(root == NULL || lv_obj_get_child_count(root) < 3) return;
    lv_obj_t *ha_panel = lv_obj_get_child(root, 2);
    if(ha_panel == NULL || lv_obj_get_child_count(ha_panel) < 7) return;

    lv_obj_set_size(ha_panel, 158, 288);
    lv_obj_set_pos(ha_panel, 6, 29);

    for(int i = 0; i < 7; i++) {
        lv_obj_t *row = lv_obj_get_child(ha_panel, i);
        if(row == NULL) continue;

        lv_obj_set_size(row, 158, 40);
        lv_obj_set_pos(row, 0, i * 41);
        lv_obj_set_style_radius(row, 9, 0);

        uint32_t child_count = lv_obj_get_child_count(row);
        for(uint32_t j = 0; j < child_count; j++) {
            lv_obj_t *label = lv_obj_get_child(row, (int32_t)j);
            if(!lv_obj_check_type(label, &lv_label_class)) continue;

            /* Scale the existing glyphs instead of introducing another CJK font. */
            lv_obj_set_style_transform_scale(label, 304, 0);
        }
    }
}

static void refine_music_panel(lv_obj_t *root)
{
    if(root == NULL || lv_obj_get_child_count(root) < 5) return;
    lv_obj_t *music_panel = lv_obj_get_child(root, 4);
    if(music_panel == NULL) return;

    /* The existing 14 px CJK font does not contain these status glyphs on the
     * simulator and renders boxes. Hide that label and use a guaranteed
     * Montserrat English status instead. */
    lv_obj_t *old = find_label_text(music_panel, "已暂停", "正在播放");
    if(old != NULL) lv_obj_add_flag(old, LV_OBJ_FLAG_HIDDEN);

    music_status_overlay = lv_label_create(music_panel);
    lv_label_set_text(music_status_overlay, "PAUSED");
    lv_obj_set_width(music_status_overlay, 150);
    lv_obj_set_pos(music_status_overlay, 10, 154);
    lv_obj_set_style_text_align(music_status_overlay, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(music_status_overlay, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(music_status_overlay, lv_color_hex(UI_MUTED), 0);
}

void s1_ui_second_pass_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    if(screen == NULL || lv_obj_get_child_count(screen) == 0) return;

    lv_obj_t *root = lv_obj_get_child(screen, 0);
    refine_ha_panel(root);
    refine_music_panel(root);
}

void s1_ui_second_pass_music_state(bool playing)
{
    if(music_status_overlay == NULL) return;
    lv_label_set_text(music_status_overlay, playing ? "PLAYING" : "PAUSED");
    lv_obj_set_style_text_color(
        music_status_overlay,
        lv_color_hex(playing ? UI_BLUE : UI_MUTED),
        0
    );
}
