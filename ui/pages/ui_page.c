#include "ui_page.h"
static lv_obj_t *panel;
lv_obj_t *s1_ui_page_panel(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, 146, 268);
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}
const char *s1_ui_page_name(s1_page_id_t page)
{
    static const char *names[] = {
        "空调", "浴霸", "窗帘", "", "客厅灯", "书房灯", "卧室灯", "小卧室灯", "LED", "Music"
    };
    return page >= 0 && page < S1_PAGE_COUNT ? names[page] : "";
}
void s1_ui_placeholder_init(lv_obj_t *parent)
{
    panel = s1_ui_page_panel(parent);
    lv_obj_t *label = lv_label_create(panel);
    lv_label_set_text(label, "Coming soon");
    lv_obj_set_style_text_color(label, lv_color_hex(0x83929D), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
    s1_ui_placeholder_hide();
}
void s1_ui_placeholder_show(s1_page_id_t page)
{ lv_obj_set_hidden(panel, page < S1_PAGE_AC || page > S1_PAGE_CURTAIN); }
void s1_ui_placeholder_hide(void) { lv_obj_set_hidden(panel, true); }
