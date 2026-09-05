#include "w1_input.h"
#include "../ui/s1_ui.h"

#include "lvgl/lvgl.h"

#include <linux/input.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>


#define W1_VENDOR_ID   0x1915
#define W1_PRODUCT_ID  0x1047

#define W1_EVENT_MIN   0
#define W1_EVENT_MAX   31


static int w1_keyboard_fd = -1;
static int w1_consumer_fd = -1;


/* =========================================================
 * 检查是不是我们的 W1
 * ========================================================= */

static int is_w1_device(int fd)
{
    struct input_id id;

    if(ioctl(fd, EVIOCGID, &id) < 0) {
        return 0;
    }

    return (
        id.vendor  == W1_VENDOR_ID &&
        id.product == W1_PRODUCT_ID
    );
}


/* =========================================================
 * 自动寻找 W1 event 节点
 * ========================================================= */

static void discover_w1_devices(void)
{
    char path[64];
    char name[256];


    for(int i = W1_EVENT_MIN; i <= W1_EVENT_MAX; i++) {

        snprintf(
            path,
            sizeof(path),
            "/dev/input/event%d",
            i
        );


        int fd = open(
            path,
            O_RDONLY | O_NONBLOCK
        );


        if(fd < 0) {
            continue;
        }


        if(!is_w1_device(fd)) {

            close(fd);
            continue;
        }


        memset(
            name,
            0,
            sizeof(name)
        );


        if(ioctl(
            fd,
            EVIOCGNAME(sizeof(name)),
            name
        ) < 0) {

            close(fd);
            continue;
        }


        /*
         * Consumer Control 必须先判断。
         *
         * 因为名称也包含：
         * "ZY.Ltd ZY Control"
         */

        if(
            strstr(
                name,
                "Consumer Control"
            ) != NULL
        ) {

            if(w1_consumer_fd < 0) {

                w1_consumer_fd = fd;

                printf(
                    "W1 Consumer connected: %s (%s)\n",
                    path,
                    name
                );

                continue;
            }
        }


        /*
         * 主 Keyboard
         *
         * 名称必须精确匹配，
         * 避免误识别 Mouse / Consumer / System。
         */

        if(
            strcmp(
                name,
                "ZY.Ltd ZY Control"
            ) == 0
        ) {

            if(w1_keyboard_fd < 0) {

                w1_keyboard_fd = fd;

                printf(
                    "W1 Keyboard connected: %s (%s)\n",
                    path,
                    name
                );

                continue;
            }
        }


        close(fd);
    }
}


/* =========================================================
 * 初始化
 * ========================================================= */

void w1_input_init(void)
{
    discover_w1_devices();


    if(w1_keyboard_fd < 0) {

        printf(
            "W1 Keyboard not found\n"
        );
    }


    if(w1_consumer_fd < 0) {

        printf(
            "W1 Consumer Control not found\n"
        );
    }
}


/* =========================================================
 * 按键映射
 * ========================================================= */

static void handle_key(
    unsigned short code
)
{
    switch(code) {

        /* ---------- 导航 ---------- */

        case KEY_UP:

            s1_ui_key(
                LV_KEY_UP
            );

            break;


        case KEY_DOWN:

            s1_ui_key(
                LV_KEY_DOWN
            );

            break;


        case KEY_LEFT:

            s1_ui_key(
                LV_KEY_LEFT
            );

            break;


        case KEY_RIGHT:

            s1_ui_key(
                LV_KEY_RIGHT
            );

            break;


        case KEY_ENTER:

            s1_ui_key(
                LV_KEY_ENTER
            );

            break;


        /* ---------- BACK ---------- */

        case KEY_ESC:
        case KEY_BACK:

            printf(
                "W1 BACK\n"
            );

            s1_ui_key(
                LV_KEY_ESC
            );

            break;


        /* ---------- HOME ---------- */

        case KEY_HOMEPAGE:
        case KEY_HOME:

            printf(
                "W1 HOME\n"
            );

            s1_ui_key(
                LV_KEY_HOME
            );

            break;


        /* ---------- MENU ---------- */

        case KEY_COMPOSE:
        case KEY_MENU:

            printf(
                "W1 MENU\n"
            );

            s1_ui_key(
                S1_KEY_MENU
            );

            break;


        /* ---------- VOL+ ---------- */

        case KEY_VOLUMEUP:

            printf(
                "W1 VOL+\n"
            );

            s1_ui_key(
                S1_KEY_VOL_UP
            );

            break;


        /* ---------- VOL- ---------- */

        case KEY_VOLUMEDOWN:

            printf(
                "W1 VOL-\n"
            );

            s1_ui_key(
                S1_KEY_VOL_DOWN
            );

            break;


        default:

            break;
    }
}


/* =========================================================
 * 轮询单个 event 节点
 * ========================================================= */

static void poll_device(int fd)
{
    if(fd < 0) {
        return;
    }


    struct input_event ev;


    while(
        read(
            fd,
            &ev,
            sizeof(ev)
        )
        == sizeof(ev)
    ) {

        /*
         * value:
         *
         * 0 = release
         * 1 = press
         * 2 = repeat
         *
         * 当前只处理首次按下。
         */

        if(
            ev.type == EV_KEY &&
            ev.value == 1
        ) {

            handle_key(
                ev.code
            );
        }
    }
}


/* =========================================================
 * 总轮询
 * ========================================================= */

void w1_input_poll(void)
{
    poll_device(
        w1_keyboard_fd
    );


    poll_device(
        w1_consumer_fd
    );
}