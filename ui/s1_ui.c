#include "s1_ui.h"
#include "../services/ha_client.h"

#include <stdio.h>

#define SCREEN_W 170
#define SCREEN_H 320

#define HA_STUDY_LIGHT "light.yeelink_ceil40_d8b6_light"


/* =========================================================
 * 页面定义
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


static page_config_t pages[PAGE_COUNT] = {
    { "HOME",     "Watch face"   },
    { "HA",       "Home control" },
    { "MUSIC",    "Now playing"  },
    { "SETTINGS", "System"       }
};


static page_id_t current_page = PAGE_HOME;


/* =========================================================
 * HA 临时列表
 * ========================================================= */

static const char *ha_items[] = {
    "All Lights",
    "Living Light",
    "Desk Light",
    "Bedroom Light",
    "Environment"
};


#define HA_ITEM_COUNT \
    (sizeof(ha_items) / sizeof(ha_items[0]))


static int ha_selected = 0;


/* =========================================================
 * UI 对象
 * ========================================================= */

static lv_obj_t *root;

static lv_obj_t *title_label;
static lv_obj_t *subtitle_label;
static lv_obj_t *page_label;

static lv_obj_t *center_panel;
static lv_obj_t *center_text;

static lv_obj_t *footer_label;


/* =========================================================
 * HA 页面刷新
 * ========================================================= */

static void update_ha_text(void)
{
    static char buf[256];

    int offset = 0;


    for(int i = 0; i < HA_ITEM_COUNT; i++) {

        if(i == ha_selected) {

            offset += snprintf(
                buf + offset,
                sizeof(buf) - offset,
                "> %s\n",
                ha_items[i]
            );
        }
        else {

            offset += snprintf(
                buf + offset,
                sizeof(buf) - offset,
                "  %s\n",
                ha_items[i]
            );
        }


        if(offset >= (int)sizeof(buf)) {
            break;
        }
    }


    lv_label_set_text(
        center_text,
        buf
    );
}


/* =========================================================
 * 页面刷新
 * ========================================================= */

static void update_page(void)
{
    lv_label_set_text(
        title_label,
        pages[current_page].name
    );


    lv_label_set_text(
        subtitle_label,
        pages[current_page].subtitle
    );


    char page_buf[32];

    snprintf(
        page_buf,
        sizeof(page_buf),
        "%d / %d",
        current_page + 1,
        PAGE_COUNT
    );


    lv_label_set_text(
        page_label,
        page_buf
    );


    switch(current_page) {

        /* ---------------- HOME ---------------- */

        case PAGE_HOME:

            lv_label_set_text(
                center_text,
                "00:00\n\nWatch Face"
            );


            lv_obj_set_style_text_align(
                center_text,
                LV_TEXT_ALIGN_CENTER,
                0
            );


            lv_label_set_text(
                footer_label,
                "HOME"
            );

            break;


        /* ---------------- HA ---------------- */

        case PAGE_HA:

            lv_obj_set_style_text_align(
                center_text,
                LV_TEXT_ALIGN_LEFT,
                0
            );


            update_ha_text();


            lv_label_set_text(
                footer_label,
                "UP/DOWN  OK"
            );

            break;


        /* ---------------- MUSIC ---------------- */

        case PAGE_MUSIC:

            lv_label_set_text(
                center_text,
                "Music\n\nNo player"
            );


            lv_obj_set_style_text_align(
                center_text,
                LV_TEXT_ALIGN_CENTER,
                0
            );


            lv_label_set_text(
                footer_label,
                "<  MUSIC  >"
            );

            break;


        /* ---------------- SETTINGS ---------------- */

        case PAGE_SETTINGS:

            lv_label_set_text(
                center_text,
                "Settings\n\nSystem"
            );


            lv_obj_set_style_text_align(
                center_text,
                LV_TEXT_ALIGN_CENTER,
                0
            );


            lv_label_set_text(
                footer_label,
                "< SETTINGS >"
            );

            break;


        default:
            break;
    }
}


/* =========================================================
 * 统一输入入口
 *
 * PC SDL Keyboard
 *      ↓
 * s1_ui_key()
 *      ↑
 * W1 Input Manager
 * ========================================================= */

void s1_ui_key(uint32_t key)
{
    /*
     * -----------------------------------------------------
     * 全局按键
     * -----------------------------------------------------
     */


    /* HOME */

    if(key == LV_KEY_HOME) {

        current_page = PAGE_HOME;

        update_page();

        return;
    }


    /* BACK */

    if(key == LV_KEY_ESC) {

        /*
         * 当前还没有真正的二级菜单。
         *
         * 所以现阶段：
         *
         * BACK → HOME
         *
         * 后续改成：
         *
         * 详情页 → 上一级
         * 一级页面 → HOME
         */

        current_page = PAGE_HOME;

        update_page();

        return;
    }


    /* MENU */

    if(key == S1_KEY_MENU) {

        /*
         * MENU 输入已经接入。
         * 当前暂时只显示确认信息。
         */

        lv_label_set_text(
            footer_label,
            "MENU"
        );

        return;
    }


    /* VOL + */

    if(key == S1_KEY_VOL_UP) {

        lv_label_set_text(
            footer_label,
            "VOL +"
        );

        return;
    }


    /* VOL - */

    if(key == S1_KEY_VOL_DOWN) {

        lv_label_set_text(
            footer_label,
            "VOL -"
        );

        return;
    }


    /*
     * -----------------------------------------------------
     * HA 页面内部操作
     * -----------------------------------------------------
     */

    if(current_page == PAGE_HA) {

        /* UP */

        if(key == LV_KEY_UP) {

            if(ha_selected == 0) {

                ha_selected =
                    HA_ITEM_COUNT - 1;
            }
            else {

                ha_selected--;
            }


            update_ha_text();

            return;
        }


        /* DOWN */

        if(key == LV_KEY_DOWN) {

            ha_selected =
                (ha_selected + 1)
                % HA_ITEM_COUNT;


            update_ha_text();

            return;
        }


        /* OK */

        if(key == LV_KEY_ENTER) {

            /*
             * 当前只把 Desk Light
             * 绑定到真实 Home Assistant 书房主灯。
             *
             * 索引：
             *
             * 0 All Lights
             * 1 Living Light
             * 2 Desk Light
             * 3 Bedroom Light
             * 4 Environment
             */

            if(ha_selected == 2) {

                lv_label_set_text(
                    footer_label,
                    "Desk Light..."
                );


                /*
                 * 先切换真实灯。
                 */

                if(
                    ha_toggle(
                        HA_STUDY_LIGHT
                    ) != 0
                ) {

                    lv_label_set_text(
                        footer_label,
                        "HA toggle failed"
                    );

                    return;
                }


                /*
                 * 再读取 Home Assistant
                 * 当前真实状态。
                 */

                char state[32];


                if(
                    ha_get_state(
                        HA_STUDY_LIGHT,
                        state,
                        sizeof(state)
                    ) != 0
                ) {

                    lv_label_set_text(
                        footer_label,
                        "HA state failed"
                    );

                    return;
                }


                /*
                 * 把最终状态显示到 UI。
                 */

                static char state_buf[64];


                snprintf(
                    state_buf,
                    sizeof(state_buf),
                    "Desk Light: %s",
                    state
                );


                lv_label_set_text(
                    footer_label,
                    state_buf
                );


                return;
            }


            /*
             * 其他 HA 项目现在仍然只是 UI 占位。
             */

            static char selected_buf[64];


            snprintf(
                selected_buf,
                sizeof(selected_buf),
                "Selected: %s",
                ha_items[ha_selected]
            );


            lv_label_set_text(
                footer_label,
                selected_buf
            );


            return;
        }
    }


    /*
     * -----------------------------------------------------
     * 一级页面导航
     * -----------------------------------------------------
     */


    /* RIGHT */

    if(key == LV_KEY_RIGHT) {

        current_page =
            (current_page + 1)
            % PAGE_COUNT;


        update_page();

        return;
    }


    /* LEFT */

    if(key == LV_KEY_LEFT) {

        if(current_page == PAGE_HOME) {

            current_page =
                PAGE_COUNT - 1;
        }
        else {

            current_page--;
        }


        update_page();

        return;
    }
}


/* =========================================================
 * UI 初始化
 * ========================================================= */

void s1_ui_init(void)
{
    lv_obj_t *screen =
        lv_screen_active();


    /* -----------------------------------------------------
     * Screen
     * ----------------------------------------------------- */

    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(0x080A0F),
        0
    );


    lv_obj_set_style_bg_opa(
        screen,
        LV_OPA_COVER,
        0
    );


    /* -----------------------------------------------------
     * Root
     * ----------------------------------------------------- */

    root =
        lv_obj_create(screen);


    lv_obj_set_size(
        root,
        SCREEN_W,
        SCREEN_H
    );


    lv_obj_center(root);


    lv_obj_clear_flag(
        root,
        LV_OBJ_FLAG_SCROLLABLE
    );


    lv_obj_set_style_radius(
        root,
        0,
        0
    );


    lv_obj_set_style_bg_color(
        root,
        lv_color_hex(0x080A0F),
        0
    );


    lv_obj_set_style_bg_opa(
        root,
        LV_OPA_COVER,
        0
    );


    lv_obj_set_style_border_width(
        root,
        0,
        0
    );


    lv_obj_set_style_pad_all(
        root,
        12,
        0
    );


    /* -----------------------------------------------------
     * Subtitle
     * ----------------------------------------------------- */

    subtitle_label =
        lv_label_create(root);


    lv_obj_set_style_text_color(
        subtitle_label,
        lv_color_hex(0x707785),
        0
    );


    lv_obj_set_style_text_font(
        subtitle_label,
        &lv_font_montserrat_10,
        0
    );


    lv_obj_align(
        subtitle_label,
        LV_ALIGN_TOP_LEFT,
        0,
        2
    );


    /* -----------------------------------------------------
     * Title
     * ----------------------------------------------------- */

    title_label =
        lv_label_create(root);


    lv_obj_set_style_text_color(
        title_label,
        lv_color_hex(0xF4F6FA),
        0
    );


    lv_obj_set_style_text_font(
        title_label,
        &lv_font_montserrat_22,
        0
    );


    lv_obj_align(
        title_label,
        LV_ALIGN_TOP_LEFT,
        0,
        20
    );


    /* -----------------------------------------------------
     * Page Number
     * ----------------------------------------------------- */

    page_label =
        lv_label_create(root);


    lv_obj_set_style_text_color(
        page_label,
        lv_color_hex(0x5F6672),
        0
    );


    lv_obj_set_style_text_font(
        page_label,
        &lv_font_montserrat_10,
        0
    );


    lv_obj_align(
        page_label,
        LV_ALIGN_TOP_RIGHT,
        0,
        5
    );


    /* -----------------------------------------------------
     * Center Panel
     * ----------------------------------------------------- */

    center_panel =
        lv_obj_create(root);


    lv_obj_set_size(
        center_panel,
        146,
        180
    );


    lv_obj_align(
        center_panel,
        LV_ALIGN_CENTER,
        0,
        10
    );


    lv_obj_clear_flag(
        center_panel,
        LV_OBJ_FLAG_SCROLLABLE
    );


    lv_obj_set_style_radius(
        center_panel,
        14,
        0
    );


    lv_obj_set_style_bg_color(
        center_panel,
        lv_color_hex(0x11151C),
        0
    );


    lv_obj_set_style_bg_opa(
        center_panel,
        LV_OPA_COVER,
        0
    );


    lv_obj_set_style_border_width(
        center_panel,
        1,
        0
    );


    lv_obj_set_style_border_color(
        center_panel,
        lv_color_hex(0x252A33),
        0
    );


    /* -----------------------------------------------------
     * Center Text
     * ----------------------------------------------------- */

    center_text =
        lv_label_create(center_panel);


    lv_obj_set_width(
        center_text,
        120
    );


    lv_obj_set_style_text_color(
        center_text,
        lv_color_hex(0xE8EBF0),
        0
    );


    lv_obj_set_style_text_font(
        center_text,
        &lv_font_montserrat_14,
        0
    );


    lv_obj_center(
        center_text
    );


    /* -----------------------------------------------------
     * Footer
     * ----------------------------------------------------- */

    footer_label =
        lv_label_create(root);


    lv_obj_set_width(
        footer_label,
        145
    );


    lv_obj_set_style_text_align(
        footer_label,
        LV_TEXT_ALIGN_CENTER,
        0
    );


    lv_obj_set_style_text_color(
        footer_label,
        lv_color_hex(0x818998),
        0
    );


    lv_obj_set_style_text_font(
        footer_label,
        &lv_font_montserrat_10,
        0
    );


    lv_obj_align(
        footer_label,
        LV_ALIGN_BOTTOM_MID,
        0,
        -2
    );


    /* -----------------------------------------------------
     * First Draw
     * ----------------------------------------------------- */

    update_page();
}