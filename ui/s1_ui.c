#include "s1_ui.h"
#include "../services/ha_client.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_W 170
#define SCREEN_H 320

#define HA_STUDY_LIGHT "light.yeelink_ceil40_d8b6_light"

#define HA_ICON_POWER   "\xEF\x80\x91"
#define HA_ICON_LIGHT   "\xEF\x83\xAB"
#define HA_ICON_AC      "\xEF\x8B\x9C"
#define HA_ICON_CURTAIN "\xEF\x8B\x90"
#define HA_ICON_HEATER  "\xEF\x96\x93"

#define HA_COLOR_BLUE      0x41BDF5
#define HA_COLOR_SELECTED  0x18374A
#define HA_COLOR_LIGHT_ON  0xF4C54E
#define HA_COLOR_POWER     0xEF7D72

LV_FONT_DECLARE(s1_ui_font_14);


/* =========================================================
 * Page definitions
 * ========================================================= */

typedef enum {
    PAGE_HOME = 0,
    PAGE_HA,
    PAGE_MUSIC,
    PAGE_SETTINGS,
    PAGE_COUNT
} page_id_t;


typedef struct {
    const char *name;
    const char *subtitle;
} page_config_t;


static const page_config_t pages[PAGE_COUNT] = {
    { "HOME",     "Watch face"    },
    { "",         "HomeAssistant" },
    { "MUSIC",    "Now playing"   },
    { "SETTINGS", "System"        }
};


static page_id_t current_page = PAGE_HOME;


/* =========================================================
 * Home Assistant menu
 * ========================================================= */

typedef enum {
    HA_ITEM_ALL_OFF = 0,
    HA_ITEM_LIVING_LIGHT,
    HA_ITEM_STUDY_LIGHT,
    HA_ITEM_BEDROOM_LIGHT,
    HA_ITEM_BEDSIDE_LIGHT,
    HA_ITEM_SMALL_BEDROOM_LIGHT,
    HA_ITEM_BALCONY_LIGHT,
    HA_ITEM_BATHROOM_LIGHT,
    HA_ITEM_AC,
    HA_ITEM_CURTAIN,
    HA_ITEM_HEATER,
    HA_ITEM_COUNT
} ha_item_id_t;


typedef struct {
    const char *name;
    const char *icon;
    const char *entity_id;
    bool is_light;
    bool is_on;
    char state[20];
    lv_obj_t *row;
    lv_obj_t *icon_label;
    lv_obj_t *name_label;
    lv_obj_t *state_label;
} ha_item_t;


static ha_item_t ha_items[HA_ITEM_COUNT] = {
    { "关闭所有灯光", HA_ICON_POWER,   NULL,           false, false, "-- 盏亮", NULL, NULL, NULL, NULL },
    { "客厅灯",       HA_ICON_LIGHT,   NULL,           true,  false, "--",      NULL, NULL, NULL, NULL },
    { "书房灯",       HA_ICON_LIGHT,   HA_STUDY_LIGHT, true,  false, "--",      NULL, NULL, NULL, NULL },
    { "卧室灯",       HA_ICON_LIGHT,   NULL,           true,  false, "--",      NULL, NULL, NULL, NULL },
    { "床头灯",       HA_ICON_LIGHT,   NULL,           true,  false, "--",      NULL, NULL, NULL, NULL },
    { "小卧室灯",     HA_ICON_LIGHT,   NULL,           true,  false, "--",      NULL, NULL, NULL, NULL },
    { "阳台灯",       HA_ICON_LIGHT,   NULL,           true,  false, "--",      NULL, NULL, NULL, NULL },
    { "浴室灯",       HA_ICON_LIGHT,   NULL,           true,  false, "--",      NULL, NULL, NULL, NULL },
    { "空调",         HA_ICON_AC,      NULL,           false, false, "--",      NULL, NULL, NULL, NULL },
    { "窗帘",         HA_ICON_CURTAIN, NULL,           false, false, "--",      NULL, NULL, NULL, NULL },
    { "浴霸",         HA_ICON_HEATER,  NULL,           false, false, "--",      NULL, NULL, NULL, NULL }
};


static int ha_selected = 0;


/* =========================================================
 * UI objects
 * ========================================================= */

static lv_obj_t *root;
static lv_obj_t *title_label;
static lv_obj_t *subtitle_label;
static lv_obj_t *page_label;
static lv_obj_t *center_panel;
static lv_obj_t *center_text;
static lv_obj_t *footer_label;
static lv_obj_t *ha_panel;


/* =========================================================
 * Common helpers
 * ========================================================= */

static void set_hidden(
    lv_obj_t *obj,
    bool hidden
)
{
    lv_obj_set_hidden(obj, hidden);
}


static void show_standard_content(bool visible)
{
    set_hidden(title_label, !visible);
    set_hidden(center_panel, !visible);
    set_hidden(footer_label, !visible);
    set_hidden(ha_panel, visible);
}


/* =========================================================
 * Home Assistant UI
 * ========================================================= */

static lv_obj_t *create_ha_category(
    const char *text
)
{
    lv_obj_t *label = lv_label_create(ha_panel);

    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, 18);
    lv_obj_set_style_text_color(label, lv_color_hex(0x607687), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_left(label, 6, 0);
    lv_obj_set_style_pad_top(label, 3, 0);

    return label;
}


static void create_ha_row(
    ha_item_t *item
)
{
    item->row = lv_obj_create(ha_panel);

    lv_obj_set_width(item->row, LV_PCT(100));
    lv_obj_set_height(item->row, 34);
    lv_obj_set_scrollable(item->row, false);
    lv_obj_set_style_radius(item->row, 8, 0);
    lv_obj_set_style_border_width(item->row, 0, 0);
    lv_obj_set_style_bg_opa(item->row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(item->row, 0, 0);

    item->icon_label = lv_label_create(item->row);
    lv_label_set_text(item->icon_label, item->icon);
    lv_obj_set_style_text_font(item->icon_label, &s1_ui_font_14, 0);
    lv_obj_align(item->icon_label, LV_ALIGN_LEFT_MID, 7, 0);

    item->name_label = lv_label_create(item->row);
    lv_label_set_text(item->name_label, item->name);
    lv_obj_set_style_text_font(item->name_label, &s1_ui_font_14, 0);
    lv_obj_set_style_text_color(item->name_label, lv_color_hex(0xDCE4EA), 0);
    lv_obj_align(item->name_label, LV_ALIGN_LEFT_MID, 29, 0);

    item->state_label = lv_label_create(item->row);
    lv_label_set_text(item->state_label, item->state);
    lv_obj_set_style_text_font(item->state_label, &s1_ui_font_14, 0);
    lv_obj_set_style_text_color(item->state_label, lv_color_hex(0x7F8A95), 0);
    lv_obj_align(item->state_label, LV_ALIGN_RIGHT_MID, -7, 0);
}


static void update_ha_rows(void)
{
    for(int i = 0; i < HA_ITEM_COUNT; i++) {
        ha_item_t *item = &ha_items[i];
        bool selected = i == ha_selected;

        lv_label_set_text(item->state_label, item->state);
        lv_obj_set_style_bg_color(item->row, lv_color_hex(HA_COLOR_SELECTED), 0);
        lv_obj_set_style_bg_opa(
            item->row,
            selected ? LV_OPA_COVER : LV_OPA_TRANSP,
            0
        );
        lv_obj_set_style_text_color(
            item->name_label,
            selected ? lv_color_hex(0xF7FBFD) : lv_color_hex(0xDCE4EA),
            0
        );
        lv_obj_set_style_text_color(
            item->state_label,
            selected ? lv_color_hex(0x9EDDF8) : lv_color_hex(0x7F8A95),
            0
        );

        lv_color_t icon_color = lv_color_hex(0x6F7B87);

        if(i == HA_ITEM_ALL_OFF) {
            icon_color = lv_color_hex(HA_COLOR_POWER);
        }
        else if(item->is_light && item->is_on) {
            icon_color = lv_color_hex(HA_COLOR_LIGHT_ON);
        }

        if(selected) {
            icon_color = lv_color_hex(HA_COLOR_BLUE);
        }

        lv_obj_set_style_text_color(item->icon_label, icon_color, 0);
    }

    lv_obj_update_layout(ha_panel);
    lv_obj_scroll_to_view(ha_items[ha_selected].row, LV_ANIM_OFF);
}


static void refresh_ha_states(void)
{
    int count = 0;

    if(ha_count_on_lights(&count) == 0) {
        snprintf(
            ha_items[HA_ITEM_ALL_OFF].state,
            sizeof(ha_items[HA_ITEM_ALL_OFF].state),
            "%d 盏亮",
            count
        );
    }
    else {
        snprintf(
            ha_items[HA_ITEM_ALL_OFF].state,
            sizeof(ha_items[HA_ITEM_ALL_OFF].state),
            "-- 盏亮"
        );
    }

    char state[20];

    if(ha_get_state(HA_STUDY_LIGHT, state, sizeof(state)) == 0) {
        ha_items[HA_ITEM_STUDY_LIGHT].is_on = strcmp(state, "on") == 0;
        snprintf(
            ha_items[HA_ITEM_STUDY_LIGHT].state,
            sizeof(ha_items[HA_ITEM_STUDY_LIGHT].state),
            "%s",
            ha_items[HA_ITEM_STUDY_LIGHT].is_on ? "ON" : "OFF"
        );
    }
    else {
        snprintf(
            ha_items[HA_ITEM_STUDY_LIGHT].state,
            sizeof(ha_items[HA_ITEM_STUDY_LIGHT].state),
            "--"
        );
    }

    update_ha_rows();
}


static void activate_ha_item(void)
{
    if(ha_selected == HA_ITEM_ALL_OFF) {
        if(ha_turn_off_all_lights() == 0) {
            snprintf(
                ha_items[HA_ITEM_ALL_OFF].state,
                sizeof(ha_items[HA_ITEM_ALL_OFF].state),
                "0 盏亮"
            );

            for(int i = 0; i < HA_ITEM_COUNT; i++) {
                if(ha_items[i].is_light) {
                    ha_items[i].is_on = false;

                    if(ha_items[i].entity_id != NULL) {
                        snprintf(
                            ha_items[i].state,
                            sizeof(ha_items[i].state),
                            "OFF"
                        );
                    }
                }
            }
        }

        update_ha_rows();
        return;
    }

    ha_item_t *item = &ha_items[ha_selected];

    if(item->entity_id == NULL) {
        return;
    }

    if(ha_toggle(item->entity_id) != 0) {
        return;
    }

    refresh_ha_states();
}


/* =========================================================
 * Page refresh
 * ========================================================= */

static void update_page(void)
{
    lv_label_set_text(subtitle_label, pages[current_page].subtitle);

    char page_buf[16];

    snprintf(
        page_buf,
        sizeof(page_buf),
        "%d / %d",
        current_page + 1,
        PAGE_COUNT
    );

    lv_label_set_text(page_label, page_buf);

    if(current_page == PAGE_HA) {
        show_standard_content(false);
        lv_obj_set_style_text_color(subtitle_label, lv_color_hex(HA_COLOR_BLUE), 0);
        lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_14, 0);
        refresh_ha_states();
        return;
    }

    show_standard_content(true);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x707785), 0);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(title_label, pages[current_page].name);

    switch(current_page) {
        case PAGE_HOME:
            lv_label_set_text(center_text, "00:00\n\nWatch Face");
            lv_label_set_text(footer_label, "HOME");
            break;

        case PAGE_MUSIC:
            lv_label_set_text(center_text, "Music\n\nNo player");
            lv_label_set_text(footer_label, "<  MUSIC  >");
            break;

        case PAGE_SETTINGS:
            lv_label_set_text(center_text, "Settings\n\nSystem");
            lv_label_set_text(footer_label, "< SETTINGS >");
            break;

        default:
            break;
    }

    lv_obj_set_style_text_align(center_text, LV_TEXT_ALIGN_CENTER, 0);
}


/* =========================================================
 * Unified keyboard / W1 input entry
 * ========================================================= */

void s1_ui_key(uint32_t key)
{
    if(key == LV_KEY_HOME || key == LV_KEY_ESC) {
        current_page = PAGE_HOME;
        update_page();
        return;
    }

    if(key == S1_KEY_MENU) {
        if(current_page != PAGE_HA) {
            lv_label_set_text(footer_label, "MENU");
        }
        return;
    }

    if(key == S1_KEY_VOL_UP) {
        if(current_page != PAGE_HA) {
            lv_label_set_text(footer_label, "VOL +");
        }
        return;
    }

    if(key == S1_KEY_VOL_DOWN) {
        if(current_page != PAGE_HA) {
            lv_label_set_text(footer_label, "VOL -");
        }
        return;
    }

    if(current_page == PAGE_HA) {
        if(key == LV_KEY_UP) {
            ha_selected =
                (ha_selected + HA_ITEM_COUNT - 1)
                % HA_ITEM_COUNT;
            update_ha_rows();
            return;
        }

        if(key == LV_KEY_DOWN) {
            ha_selected =
                (ha_selected + 1)
                % HA_ITEM_COUNT;
            update_ha_rows();
            return;
        }

        if(key == LV_KEY_ENTER) {
            activate_ha_item();
            return;
        }
    }

    if(key == LV_KEY_RIGHT) {
        current_page =
            (current_page + 1)
            % PAGE_COUNT;
        update_page();
        return;
    }

    if(key == LV_KEY_LEFT) {
        current_page =
            current_page == PAGE_HOME
            ? PAGE_COUNT - 1
            : current_page - 1;
        update_page();
    }
}


/* =========================================================
 * UI initialization
 * ========================================================= */

void s1_ui_init(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080A0F), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    root = lv_obj_create(screen);
    lv_obj_set_size(root, SCREEN_W, SCREEN_H);
    lv_obj_center(root);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x080A0F), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 12, 0);

    subtitle_label = lv_label_create(root);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x707785), 0);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_10, 0);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_LEFT, 0, 2);

    title_label = lv_label_create(root);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xF4F6FA), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 20);

    page_label = lv_label_create(root);
    lv_obj_set_style_text_color(page_label, lv_color_hex(0x5F6672), 0);
    lv_obj_set_style_text_font(page_label, &lv_font_montserrat_10, 0);
    lv_obj_align(page_label, LV_ALIGN_TOP_RIGHT, 0, 5);

    center_panel = lv_obj_create(root);
    lv_obj_set_size(center_panel, 146, 180);
    lv_obj_align(center_panel, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_scrollable(center_panel, false);
    lv_obj_set_style_radius(center_panel, 14, 0);
    lv_obj_set_style_bg_color(center_panel, lv_color_hex(0x11151C), 0);
    lv_obj_set_style_bg_opa(center_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_panel, 1, 0);
    lv_obj_set_style_border_color(center_panel, lv_color_hex(0x252A33), 0);

    center_text = lv_label_create(center_panel);
    lv_obj_set_width(center_text, 120);
    lv_obj_set_style_text_color(center_text, lv_color_hex(0xE8EBF0), 0);
    lv_obj_set_style_text_font(center_text, &lv_font_montserrat_14, 0);
    lv_obj_center(center_text);

    footer_label = lv_label_create(root);
    lv_obj_set_width(footer_label, 145);
    lv_obj_set_style_text_align(footer_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(footer_label, lv_color_hex(0x818998), 0);
    lv_obj_set_style_text_font(footer_label, &lv_font_montserrat_10, 0);
    lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 0, -2);

    ha_panel = lv_obj_create(root);
    lv_obj_set_size(ha_panel, 146, 268);
    lv_obj_align(ha_panel, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_flex_flow(ha_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ha_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ha_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(ha_panel, 10, 0);
    lv_obj_set_style_bg_color(ha_panel, lv_color_hex(0x0E141C), 0);
    lv_obj_set_style_bg_opa(ha_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ha_panel, 0, 0);
    lv_obj_set_style_pad_all(ha_panel, 4, 0);
    lv_obj_set_style_pad_row(ha_panel, 2, 0);

    create_ha_category("LIGHTS");

    for(int i = 0; i <= HA_ITEM_BATHROOM_LIGHT; i++) {
        create_ha_row(&ha_items[i]);
    }

    create_ha_category("HOME DEVICES");

    for(int i = HA_ITEM_AC; i < HA_ITEM_COUNT; i++) {
        create_ha_row(&ha_items[i]);
    }

    update_page();
}
