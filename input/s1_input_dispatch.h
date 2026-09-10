#ifndef S1_INPUT_DISPATCH_H
#define S1_INPUT_DISPATCH_H

#include "../ui/s1_ui.h"
#include "../ui/ui_router.h"

#include <string.h>


/*
 * Music is a branch page below HOME. The legacy UI still contains a
 * two-stage "page -> controls" transition. The dispatcher collapses that
 * transition so every physical/SDL input source sees Music as one direct
 * control page.
 */

static inline lv_obj_t *s1_input_find_label(
    lv_obj_t *parent,
    const char *text
)
{
    if(parent == NULL || text == NULL) {
        return NULL;
    }

    uint32_t count = lv_obj_get_child_count(parent);
    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);

        if(lv_obj_check_type(child, &lv_label_class)) {
            const char *value = lv_label_get_text(child);
            if(value != NULL && strcmp(value, text) == 0) {
                return child;
            }
        }

        lv_obj_t *nested = s1_input_find_label(child, text);
        if(nested != NULL) {
            return nested;
        }
    }

    return NULL;
}


static inline void s1_input_force_music_control_visual(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *artist_caption = s1_input_find_label(screen, "ARTIST");

    if(artist_caption == NULL) {
        return;
    }

    lv_obj_t *content = lv_obj_get_parent(artist_caption);
    lv_obj_t *box = content != NULL ? lv_obj_get_parent(content) : NULL;
    lv_obj_t *panel = box != NULL ? lv_obj_get_parent(box) : NULL;

    if(content == NULL || box == NULL || panel == NULL) {
        return;
    }

    /* Cancel the old 300 ms frame-removal animation and apply its end state. */
    lv_anim_delete(content, NULL);

    lv_obj_set_style_translate_x(content, -4, 0);
    lv_obj_set_style_translate_y(content, -4, 0);
    lv_obj_set_style_transform_scale(content, 292, 0);
    lv_obj_set_style_border_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);

    uint32_t child_count = lv_obj_get_child_count(content);
    for(uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(content, (int32_t)i);
        if(lv_obj_check_type(child, &lv_label_class)) {
            lv_obj_set_style_text_align(child, LV_TEXT_ALIGN_LEFT, 0);
        }
    }

    if(child_count > 1) {
        lv_obj_t *accent = lv_obj_get_child(content, 1);
        lv_obj_set_width(accent, 52);
        lv_obj_set_x(accent, 0);
    }

    lv_obj_t *left_arrow = s1_input_find_label(panel, "<");
    lv_obj_t *right_arrow = s1_input_find_label(panel, ">");
    if(left_arrow != NULL) {
        lv_obj_add_flag(left_arrow, LV_OBJ_FLAG_HIDDEN);
    }
    if(right_arrow != NULL) {
        lv_obj_add_flag(right_arrow, LV_OBJ_FLAG_HIDDEN);
    }
}


static inline void s1_input_dispatch_key(uint32_t key)
{
    s1_page_id_t before = s1_ui_router_current();

    if(before == S1_PAGE_MUSIC) {
        /* UP, BACK and HOME all leave the player immediately. */
        if(key == LV_KEY_UP || key == LV_KEY_ESC || key == LV_KEY_HOME) {
            s1_ui_key(LV_KEY_HOME);
            return;
        }

        /* Music is already in control mode: LEFT/RIGHT/OK act immediately. */
        s1_ui_key(key);
        return;
    }

    s1_ui_key(key);

    /* Entering Music from HOME automatically activates its control layer. */
    if(before != S1_PAGE_MUSIC && s1_ui_router_current() == S1_PAGE_MUSIC) {
        s1_ui_key(LV_KEY_ENTER);
        s1_input_force_music_control_visual();
    }
}


#endif
