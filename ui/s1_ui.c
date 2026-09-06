#include "s1_ui.h"
#include "../services/ha_client.h"
#include "../services/led_client.h"

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

#define MUSIC_ICON_PREVIOUS "\xEF\x81\x88"
#define MUSIC_ICON_PLAY     "\xEF\x81\x8B"
#define MUSIC_ICON_PAUSE    "\xEF\x81\x8C"
#define MUSIC_ICON_NEXT     "\xEF\x81\x91"

#define HA_COLOR_BLUE      0x41BDF5
#define HA_COLOR_SELECTED  0x18374A
#define HA_COLOR_LIGHT_ON  0xF4C54E
#define HA_COLOR_POWER     0xEF7D72

#define MUSIC_COLOR_BLUE      0x18B9D9
#define MUSIC_COLOR_BUTTON    0x121A25
#define MUSIC_COLOR_SELECTED  0x0D7088

#define LED_COLOR_BLUE      0x18B9D9
#define LED_COLOR_SELECTED  0x18374A
#define LED_COLOR_ERROR     0xEF7D72

LV_FONT_DECLARE(s1_ui_font_14);
LV_FONT_DECLARE(s1_led_font_12);


/* =========================================================
 * Page definitions
 * ========================================================= */

typedef enum {
    PAGE_HOME = 0,
    PAGE_HA,
    PAGE_MUSIC,
    PAGE_LED,
    PAGE_COUNT
} page_id_t;


typedef struct {
    const char *name;
    const char *subtitle;
} page_config_t;


static const page_config_t pages[PAGE_COUNT] = {
    { "HOME",     "Watch face"    },
    { "",         "HomeAssistant" },
    { "",         "Music"         },
    { "",         "LED"           }
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
 * Music page
 * ========================================================= */

typedef enum {
    MUSIC_MODE_PAGE = 0,
    MUSIC_MODE_CONTROLS
} music_mode_t;


typedef enum {
    MUSIC_CONTROL_PREVIOUS = 0,
    MUSIC_CONTROL_PLAY,
    MUSIC_CONTROL_NEXT,
    MUSIC_CONTROL_COUNT
} music_control_t;


static music_mode_t music_mode = MUSIC_MODE_PAGE;
static music_control_t music_selected = MUSIC_CONTROL_PLAY;
static bool music_playing = true;
static lv_timer_t *music_flash_timer;
static s1_music_action_cb_t music_action_callback;
static void *music_action_user_data;


/* =========================================================
 * LED mode page
 * ========================================================= */

typedef struct {
    led_mode_t mode;
    const char *name;
    const char *subtitle;
    const char *icon;
    lv_obj_t *row;
    lv_obj_t *icon_label;
    lv_obj_t *name_label;
    lv_obj_t *subtitle_label;
    lv_obj_t *status_label;
} led_item_t;


static led_item_t led_items[] = {
    { LED_MODE_RAINBOW,     "Rainbow",     "彩虹",     LV_SYMBOL_TINT,     NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_BREATHING,   "Breathing",   "呼吸",     LV_SYMBOL_LOOP,     NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_COLOR_CYCLE, "Color Cycle", "颜色循环", LV_SYMBOL_REFRESH,  NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_AUTOMATIC,   "Automatic",   "自动",     LV_SYMBOL_SETTINGS, NULL, NULL, NULL, NULL, NULL },
    { LED_MODE_OFF,         "Off",         "关闭",     LV_SYMBOL_POWER,    NULL, NULL, NULL, NULL, NULL }
};

#define LED_ITEM_COUNT ((int)(sizeof(led_items) / sizeof(led_items[0])))

static int led_selected = 1;
static int led_active = 1;
static int led_error = -1;


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
static lv_obj_t *music_panel;
static lv_obj_t *music_info_box;
static lv_obj_t *music_info_content;
static lv_obj_t *music_left_arrow;
static lv_obj_t *music_right_arrow;
static lv_obj_t *music_song_label;
static lv_obj_t *music_accent_line;
static lv_obj_t *music_artist_caption;
static lv_obj_t *music_artist_label;
static lv_obj_t *music_album_caption;
static lv_obj_t *music_album_label;
static lv_obj_t *music_progress_fill;
static lv_obj_t *music_elapsed_label;
static lv_obj_t *music_duration_label;
static lv_obj_t *music_control_buttons[MUSIC_CONTROL_COUNT];
static lv_obj_t *music_play_icon;
static lv_obj_t *led_panel;


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
    set_hidden(music_panel, true);
    set_hidden(led_panel, true);
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
 * Music UI and interaction
 * ========================================================= */

static void update_music_controls(void)
{
    for(int i = 0; i < MUSIC_CONTROL_COUNT; i++) {
        bool selected =
            music_mode == MUSIC_MODE_CONTROLS &&
            (music_control_t)i == music_selected;

        lv_obj_set_style_bg_color(
            music_control_buttons[i],
            lv_color_hex(
                selected
                ? MUSIC_COLOR_SELECTED
                : MUSIC_COLOR_BUTTON
            ),
            0
        );
        lv_obj_set_style_text_color(
            lv_obj_get_child(music_control_buttons[i], 0),
            lv_color_hex(selected ? 0xFFFFFF : 0x7E8998),
            0
        );
        lv_obj_set_style_transform_scale(
            music_control_buttons[i],
            selected ? 272 : 256,
            0
        );
    }
}


static void music_mode_anim_cb(
    void *var,
    int32_t value
)
{
    (void)var;

    int32_t scale = 256 + (36 * value / 100);
    int32_t line_width = 30 + (22 * value / 100);
    int32_t line_x = (104 - line_width) * (100 - value) / 200;
    lv_opa_t navigation_opa = (lv_opa_t)(LV_OPA_COVER * (100 - value) / 100);

    lv_obj_set_style_translate_x(music_info_content, -4 * value / 100, 0);
    lv_obj_set_style_translate_y(music_info_content, -4 * value / 100, 0);
    lv_obj_set_style_transform_scale(music_info_content, scale, 0);
    lv_obj_set_style_opa(music_left_arrow, navigation_opa, 0);
    lv_obj_set_style_opa(music_right_arrow, navigation_opa, 0);
    lv_obj_set_style_border_opa(music_info_box, navigation_opa, 0);
    lv_obj_set_style_bg_opa(
        music_info_box,
        (lv_opa_t)(8 * (100 - value) / 100),
        0
    );
    lv_obj_set_width(music_accent_line, line_width);
    lv_obj_set_x(music_accent_line, line_x);
}


static void animate_music_mode(void)
{
    bool control_mode = music_mode == MUSIC_MODE_CONTROLS;
    lv_text_align_t align =
        control_mode
        ? LV_TEXT_ALIGN_LEFT
        : LV_TEXT_ALIGN_CENTER;

    lv_obj_set_style_text_align(music_song_label, align, 0);
    lv_obj_set_style_text_align(music_artist_caption, align, 0);
    lv_obj_set_style_text_align(music_artist_label, align, 0);
    lv_obj_set_style_text_align(music_album_caption, align, 0);
    lv_obj_set_style_text_align(music_album_label, align, 0);

    int32_t current_scale =
        lv_obj_get_style_transform_scale_x(music_info_content, 0);
    int32_t start = (current_scale - 256) * 100 / 36;

    if(start < 0) {
        start = 0;
    }
    else if(start > 100) {
        start = 100;
    }

    lv_anim_delete(music_info_content, music_mode_anim_cb);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, music_info_content);
    lv_anim_set_values(&animation, start, control_mode ? 100 : 0);
    lv_anim_set_duration(&animation, 300);
    lv_anim_set_exec_cb(&animation, music_mode_anim_cb);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}


static void set_music_mode(
    music_mode_t mode,
    bool animated
)
{
    if(music_flash_timer != NULL) {
        lv_timer_delete(music_flash_timer);
        music_flash_timer = NULL;
    }

    music_mode = mode;
    music_selected = MUSIC_CONTROL_PLAY;
    update_music_controls();

    if(animated) {
        animate_music_mode();
    }
    else {
        lv_text_align_t align =
            mode == MUSIC_MODE_CONTROLS
            ? LV_TEXT_ALIGN_LEFT
            : LV_TEXT_ALIGN_CENTER;

        lv_obj_set_style_text_align(music_song_label, align, 0);
        lv_obj_set_style_text_align(music_artist_caption, align, 0);
        lv_obj_set_style_text_align(music_artist_label, align, 0);
        lv_obj_set_style_text_align(music_album_caption, align, 0);
        lv_obj_set_style_text_align(music_album_label, align, 0);
        music_mode_anim_cb(
            music_info_content,
            mode == MUSIC_MODE_CONTROLS ? 100 : 0
        );
    }
}


static void music_flash_reset_cb(lv_timer_t *timer)
{
    (void)timer;

    music_flash_timer = NULL;
    music_selected = MUSIC_CONTROL_PLAY;
    update_music_controls();
}


static void flash_music_control(music_control_t control)
{
    if(music_flash_timer != NULL) {
        lv_timer_delete(music_flash_timer);
    }

    music_selected = control;
    update_music_controls();

    music_flash_timer = lv_timer_create(
        music_flash_reset_cb,
        300,
        NULL
    );
    lv_timer_set_repeat_count(music_flash_timer, 1);
}


static void dispatch_music_action(s1_music_action_t action)
{
    if(music_action_callback != NULL) {
        music_action_callback(action, music_action_user_data);
        return;
    }

    const char *name = "play-pause";

    if(action == S1_MUSIC_ACTION_PREVIOUS) {
        name = "previous";
    }
    else if(action == S1_MUSIC_ACTION_NEXT) {
        name = "next";
    }

    printf("MUSIC %s (no action handler)\n", name);
}


static void activate_music_control(music_control_t control)
{
    switch(control) {
        case MUSIC_CONTROL_PREVIOUS:
            dispatch_music_action(S1_MUSIC_ACTION_PREVIOUS);
            flash_music_control(MUSIC_CONTROL_PREVIOUS);
            break;

        case MUSIC_CONTROL_PLAY:
            music_playing = !music_playing;
            lv_label_set_text(
                music_play_icon,
                music_playing
                ? MUSIC_ICON_PAUSE
                : MUSIC_ICON_PLAY
            );
            dispatch_music_action(S1_MUSIC_ACTION_PLAY_PAUSE);
            music_selected = MUSIC_CONTROL_PLAY;
            update_music_controls();
            break;

        case MUSIC_CONTROL_NEXT:
            dispatch_music_action(S1_MUSIC_ACTION_NEXT);
            flash_music_control(MUSIC_CONTROL_NEXT);
            break;

        default:
            break;
    }
}


void s1_ui_music_set_action_cb(
    s1_music_action_cb_t callback,
    void *user_data
)
{
    music_action_callback = callback;
    music_action_user_data = user_data;
}


void s1_ui_music_set_metadata(
    const char *song,
    const char *artist,
    const char *album,
    bool playing
)
{
    if(music_panel == NULL) {
        return;
    }

    lv_label_set_text(
        music_song_label,
        song != NULL && song[0] != '\0' ? song : "No track"
    );
    lv_label_set_text(
        music_artist_label,
        artist != NULL && artist[0] != '\0' ? artist : "--"
    );
    lv_label_set_text(
        music_album_label,
        album != NULL && album[0] != '\0' ? album : "--"
    );

    music_playing = playing;
    lv_label_set_text(
        music_play_icon,
        music_playing ? MUSIC_ICON_PAUSE : MUSIC_ICON_PLAY
    );
}


void s1_ui_music_set_progress(
    uint32_t elapsed_seconds,
    uint32_t duration_seconds
)
{
    if(music_panel == NULL) {
        return;
    }

    if(duration_seconds > 0 && elapsed_seconds > duration_seconds) {
        elapsed_seconds = duration_seconds;
    }

    int32_t percent =
        duration_seconds > 0
        ? (int32_t)(
            (uint64_t)elapsed_seconds * 100
            / duration_seconds
        )
        : 0;
    char elapsed[16];
    char duration[16];

    snprintf(
        elapsed,
        sizeof(elapsed),
        "%u:%02u",
        elapsed_seconds / 60,
        elapsed_seconds % 60
    );
    snprintf(
        duration,
        sizeof(duration),
        "%u:%02u",
        duration_seconds / 60,
        duration_seconds % 60
    );

    lv_obj_set_width(music_progress_fill, LV_PCT(percent));
    lv_label_set_text(music_elapsed_label, elapsed);
    lv_label_set_text(music_duration_label, duration);
}


static lv_obj_t *create_music_info_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    uint32_t color,
    int32_t y
)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, 104, LV_SIZE_CONTENT);
    lv_obj_set_pos(label, 0, y);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);

    return label;
}


static lv_obj_t *create_music_control_button(
    lv_obj_t *parent,
    const char *icon,
    int32_t size
)
{
    lv_obj_t *button = lv_obj_create(parent);

    lv_obj_set_size(button, size, size);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(MUSIC_COLOR_BUTTON), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(button, 0, 0);

    lv_obj_t *icon_label = lv_label_create(button);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0x7E8998), 0);
    lv_obj_center(icon_label);

    return button;
}


/* =========================================================
 * LED UI and interaction
 * ========================================================= */

static void create_led_row(
    led_item_t *item,
    int index
)
{
    item->row = lv_obj_create(led_panel);
    lv_obj_set_size(item->row, 146, 44);
    lv_obj_set_pos(item->row, 0, 24 + index * 48);
    lv_obj_set_scrollable(item->row, false);
    lv_obj_set_style_radius(item->row, 9, 0);
    lv_obj_set_style_border_width(item->row, 0, 0);
    lv_obj_set_style_bg_color(item->row, lv_color_hex(LED_COLOR_SELECTED), 0);
    lv_obj_set_style_bg_opa(item->row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(item->row, 0, 0);

    item->icon_label = lv_label_create(item->row);
    lv_label_set_text(item->icon_label, item->icon);
    lv_obj_set_style_text_font(item->icon_label, &lv_font_montserrat_18, 0);
    lv_obj_align(item->icon_label, LV_ALIGN_LEFT_MID, 8, 0);

    item->name_label = lv_label_create(item->row);
    lv_label_set_text(item->name_label, item->name);
    lv_obj_set_style_text_font(item->name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(item->name_label, 36, 4);

    item->subtitle_label = lv_label_create(item->row);
    lv_label_set_text(item->subtitle_label, item->subtitle);
    lv_obj_set_style_text_font(item->subtitle_label, &s1_led_font_12, 0);
    lv_obj_set_pos(item->subtitle_label, 36, 24);

    item->status_label = lv_label_create(item->row);
    lv_obj_set_style_text_font(item->status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(item->status_label, LV_ALIGN_RIGHT_MID, -9, 0);
}


static void update_led_rows(void)
{
    for(int index = 0; index < LED_ITEM_COUNT; index++) {
        led_item_t *item = &led_items[index];
        bool selected = index == led_selected;
        bool active = index == led_active;
        bool error = index == led_error;

        lv_obj_set_style_bg_opa(
            item->row,
            selected ? LV_OPA_COVER : LV_OPA_TRANSP,
            0
        );
        lv_obj_set_style_text_color(
            item->icon_label,
            lv_color_hex(selected ? LED_COLOR_BLUE : 0x657482),
            0
        );
        lv_obj_set_style_text_color(
            item->name_label,
            lv_color_hex(selected ? 0xF7FBFD : 0xDCE4EA),
            0
        );
        lv_obj_set_style_text_color(
            item->subtitle_label,
            lv_color_hex(selected ? 0x9EDDF8 : 0x667886),
            0
        );

        lv_label_set_text(
            item->status_label,
            error ? "!" : (active ? LV_SYMBOL_OK : "")
        );
        lv_obj_set_style_text_color(
            item->status_label,
            lv_color_hex(error ? LED_COLOR_ERROR : LED_COLOR_BLUE),
            0
        );
    }
}


static void activate_led_item(void)
{
    if(led_set_mode(led_items[led_selected].mode) == 0) {
        led_active = led_selected;
        led_error = -1;
    }
    else {
        led_error = led_selected;
    }

    update_led_rows();
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

    if(current_page == PAGE_MUSIC) {
        set_hidden(title_label, true);
        set_hidden(center_panel, true);
        set_hidden(footer_label, true);
        set_hidden(ha_panel, true);
        set_hidden(music_panel, false);
        lv_obj_set_style_text_color(
            subtitle_label,
            lv_color_hex(MUSIC_COLOR_BLUE),
            0
        );
        lv_obj_set_style_text_font(
            subtitle_label,
            &lv_font_montserrat_14,
            0
        );
        set_music_mode(MUSIC_MODE_PAGE, false);
        return;
    }

    if(current_page == PAGE_LED) {
        set_hidden(title_label, true);
        set_hidden(center_panel, true);
        set_hidden(footer_label, true);
        set_hidden(ha_panel, true);
        set_hidden(music_panel, true);
        set_hidden(led_panel, false);
        lv_obj_set_style_text_color(
            subtitle_label,
            lv_color_hex(LED_COLOR_BLUE),
            0
        );
        lv_obj_set_style_text_font(
            subtitle_label,
            &lv_font_montserrat_14,
            0
        );
        update_led_rows();
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
    if(
        key == LV_KEY_ESC &&
        current_page == PAGE_MUSIC &&
        music_mode == MUSIC_MODE_CONTROLS
    ) {
        set_music_mode(MUSIC_MODE_PAGE, true);
        return;
    }

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

    if(current_page == PAGE_MUSIC) {
        if(music_mode == MUSIC_MODE_CONTROLS) {
            if(key == LV_KEY_UP) {
                set_music_mode(MUSIC_MODE_PAGE, true);
                return;
            }

            if(key == LV_KEY_LEFT) {
                activate_music_control(MUSIC_CONTROL_PREVIOUS);
                return;
            }

            if(key == LV_KEY_RIGHT) {
                activate_music_control(MUSIC_CONTROL_NEXT);
                return;
            }

            if(key == LV_KEY_ENTER) {
                activate_music_control(MUSIC_CONTROL_PLAY);
                return;
            }

            return;
        }

        if(key == LV_KEY_ENTER) {
            set_music_mode(MUSIC_MODE_CONTROLS, true);
            return;
        }
    }

    if(current_page == PAGE_LED) {
        if(key == LV_KEY_UP) {
            led_selected =
                (led_selected + LED_ITEM_COUNT - 1)
                % LED_ITEM_COUNT;
            led_error = -1;
            update_led_rows();
            return;
        }

        if(key == LV_KEY_DOWN) {
            led_selected =
                (led_selected + 1)
                % LED_ITEM_COUNT;
            led_error = -1;
            update_led_rows();
            return;
        }

        if(key == LV_KEY_ENTER) {
            activate_led_item();
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

    music_panel = lv_obj_create(root);
    lv_obj_set_size(music_panel, 146, 268);
    lv_obj_align(music_panel, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_scrollable(music_panel, false);
    lv_obj_set_style_bg_opa(music_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_panel, 0, 0);
    lv_obj_set_style_pad_all(music_panel, 0, 0);

    music_left_arrow = lv_label_create(music_panel);
    lv_label_set_text(music_left_arrow, "<");
    lv_obj_set_style_text_font(music_left_arrow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(
        music_left_arrow,
        lv_color_hex(MUSIC_COLOR_BLUE),
        0
    );
    lv_obj_align(music_left_arrow, LV_ALIGN_TOP_LEFT, 0, 50);

    music_right_arrow = lv_label_create(music_panel);
    lv_label_set_text(music_right_arrow, ">");
    lv_obj_set_style_text_font(music_right_arrow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(
        music_right_arrow,
        lv_color_hex(MUSIC_COLOR_BLUE),
        0
    );
    lv_obj_align(music_right_arrow, LV_ALIGN_TOP_RIGHT, 0, 50);

    music_info_box = lv_obj_create(music_panel);
    lv_obj_set_size(music_info_box, 116, 116);
    lv_obj_align(music_info_box, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_scrollable(music_info_box, false);
    lv_obj_set_overflow_visible(music_info_box, true);
    lv_obj_set_style_radius(music_info_box, 10, 0);
    lv_obj_set_style_border_width(music_info_box, 1, 0);
    lv_obj_set_style_border_color(
        music_info_box,
        lv_color_hex(MUSIC_COLOR_BLUE),
        0
    );
    lv_obj_set_style_border_opa(music_info_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(
        music_info_box,
        lv_color_hex(MUSIC_COLOR_BLUE),
        0
    );
    lv_obj_set_style_bg_opa(music_info_box, 8, 0);
    lv_obj_set_style_pad_all(music_info_box, 0, 0);

    music_info_content = lv_obj_create(music_info_box);
    lv_obj_set_size(music_info_content, 104, 108);
    lv_obj_center(music_info_content);
    lv_obj_set_scrollable(music_info_content, false);
    lv_obj_set_overflow_visible(music_info_content, true);
    lv_obj_set_style_bg_opa(music_info_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_info_content, 0, 0);
    lv_obj_set_style_pad_all(music_info_content, 0, 0);

    music_song_label = create_music_info_label(
        music_info_content,
        "No track",
        &lv_font_montserrat_22,
        0xFFFFFF,
        0
    );

    music_accent_line = lv_obj_create(music_info_content);
    lv_obj_set_size(music_accent_line, 30, 2);
    lv_obj_set_pos(music_accent_line, 37, 28);
    lv_obj_set_style_radius(music_accent_line, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(music_accent_line, 0, 0);
    lv_obj_set_style_bg_color(
        music_accent_line,
        lv_color_hex(MUSIC_COLOR_BLUE),
        0
    );
    lv_obj_set_style_bg_opa(music_accent_line, LV_OPA_COVER, 0);

    music_artist_caption = create_music_info_label(
        music_info_content,
        "ARTIST",
        &lv_font_montserrat_10,
        0x5F7E8C,
        38
    );
    music_artist_label = create_music_info_label(
        music_info_content,
        "--",
        &lv_font_montserrat_14,
        0xD8DEE7,
        50
    );
    music_album_caption = create_music_info_label(
        music_info_content,
        "ALBUM",
        &lv_font_montserrat_10,
        0x5F7E8C,
        73
    );
    music_album_label = create_music_info_label(
        music_info_content,
        "--",
        &lv_font_montserrat_12,
        0xB9C3D0,
        85
    );

    lv_obj_t *progress_track = lv_obj_create(music_panel);
    lv_obj_set_size(progress_track, 136, 4);
    lv_obj_set_pos(progress_track, 5, 143);
    lv_obj_set_scrollable(progress_track, false);
    lv_obj_set_style_radius(progress_track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(progress_track, 0, 0);
    lv_obj_set_style_bg_color(progress_track, lv_color_hex(0x202A38), 0);
    lv_obj_set_style_bg_opa(progress_track, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(progress_track, 0, 0);

    music_progress_fill = lv_obj_create(progress_track);
    lv_obj_set_size(music_progress_fill, LV_PCT(0), 4);
    lv_obj_align(music_progress_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(music_progress_fill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(music_progress_fill, 0, 0);
    lv_obj_set_style_bg_color(
        music_progress_fill,
        lv_color_hex(MUSIC_COLOR_BLUE),
        0
    );
    lv_obj_set_style_bg_opa(music_progress_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(music_progress_fill, 0, 0);

    music_elapsed_label = lv_label_create(music_panel);
    lv_label_set_text(music_elapsed_label, "0:00");
    lv_obj_set_style_text_font(music_elapsed_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(music_elapsed_label, lv_color_hex(0x6F7C8E), 0);
    lv_obj_set_pos(music_elapsed_label, 5, 151);

    music_duration_label = lv_label_create(music_panel);
    lv_label_set_text(music_duration_label, "0:00");
    lv_obj_set_style_text_font(music_duration_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(music_duration_label, lv_color_hex(0x6F7C8E), 0);
    lv_obj_align(music_duration_label, LV_ALIGN_TOP_RIGHT, -5, 151);

    lv_obj_t *controls = lv_obj_create(music_panel);
    lv_obj_set_size(controls, 146, 54);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_set_scrollable(controls, false);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);

    music_control_buttons[MUSIC_CONTROL_PREVIOUS] =
        create_music_control_button(controls, MUSIC_ICON_PREVIOUS, 40);
    lv_obj_align(
        music_control_buttons[MUSIC_CONTROL_PREVIOUS],
        LV_ALIGN_LEFT_MID,
        0,
        0
    );

    music_control_buttons[MUSIC_CONTROL_PLAY] =
        create_music_control_button(controls, MUSIC_ICON_PAUSE, 46);
    lv_obj_center(music_control_buttons[MUSIC_CONTROL_PLAY]);
    music_play_icon = lv_obj_get_child(
        music_control_buttons[MUSIC_CONTROL_PLAY],
        0
    );

    music_control_buttons[MUSIC_CONTROL_NEXT] =
        create_music_control_button(controls, MUSIC_ICON_NEXT, 40);
    lv_obj_align(
        music_control_buttons[MUSIC_CONTROL_NEXT],
        LV_ALIGN_RIGHT_MID,
        0,
        0
    );

    led_panel = lv_obj_create(root);
    lv_obj_set_size(led_panel, 146, 268);
    lv_obj_align(led_panel, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_scrollable(led_panel, false);
    lv_obj_set_style_bg_opa(led_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(led_panel, 0, 0);
    lv_obj_set_style_pad_all(led_panel, 0, 0);

    lv_obj_t *led_category = lv_label_create(led_panel);
    lv_label_set_text(led_category, "LIGHT MODE");
    lv_obj_set_style_text_color(led_category, lv_color_hex(0x607687), 0);
    lv_obj_set_style_text_font(led_category, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(led_category, 6, 3);

    for(int index = 0; index < LED_ITEM_COUNT; index++) {
        create_led_row(&led_items[index], index);
    }

    update_page();
}
