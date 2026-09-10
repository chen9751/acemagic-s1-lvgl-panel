#include "led_client.h"

#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LED_DEVICE_STABLE  "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
#define LED_DEVICE_FALLBACK "/dev/ttyUSB0"
#define LED_BAUD_RATE 10000U
#define LED_BYTE_DELAY_NS 5000000L
#define LED_LEVEL_DEFAULT 3U


int led_build_command(
    led_mode_t mode,
    uint8_t intensity,
    uint8_t speed,
    uint8_t command[5]
)
{
    if(
        command == NULL ||
        mode < LED_MODE_RAINBOW ||
        mode > LED_MODE_AUTOMATIC ||
        intensity < 1 || intensity > 5 ||
        speed < 1 || speed > 5
    ) {
        errno = EINVAL;
        return -1;
    }

    command[0] = 0xFA;
    command[1] = (uint8_t)mode;
    command[2] = intensity;
    command[3] = speed;
    command[4] = (uint8_t)(
        command[0] + command[1] + command[2] + command[3]
    );

    return 0;
}


static int configure_port(int fd)
{
    struct termios2 config;

    if(ioctl(fd, TCGETS2, &config) != 0) {
        return -1;
    }

    config.c_iflag = 0;
    config.c_oflag = 0;
    config.c_lflag = 0;
    config.c_cflag &= ~(CBAUD | CSIZE | CSTOPB | PARENB | CRTSCTS);
    config.c_cflag |= BOTHER | CS8 | CLOCAL | CREAD;
    config.c_ispeed = LED_BAUD_RATE;
    config.c_ospeed = LED_BAUD_RATE;
    config.c_cc[VMIN] = 0;
    config.c_cc[VTIME] = 0;

    return ioctl(fd, TCSETS2, &config);
}


static int write_command(int fd, const uint8_t command[5])
{
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = LED_BYTE_DELAY_NS
    };

    for(size_t index = 0; index < 5; index++) {
        ssize_t written;

        do {
            written = write(fd, &command[index], 1);
        } while(written < 0 && errno == EINTR);

        if(written != 1) {
            return -1;
        }

        struct timespec remaining = delay;

        while(nanosleep(&remaining, &remaining) != 0) {
            if(errno != EINTR) {
                return -1;
            }
        }
    }

    return 0;
}


static int open_led_device(void)
{
    const char *device = getenv("S1_LED_DEVICE");

    if(device != NULL && device[0] != '\0') {
        return open(device, O_RDWR | O_NOCTTY | O_CLOEXEC);
    }

    int fd = open(LED_DEVICE_STABLE, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if(fd >= 0) {
        return fd;
    }

    return open(LED_DEVICE_FALLBACK, O_RDWR | O_NOCTTY | O_CLOEXEC);
}


int led_set_state(
    led_mode_t mode,
    uint8_t intensity,
    uint8_t speed
)
{
    uint8_t command[5];

    if(led_build_command(mode, intensity, speed, command) != 0) {
        return -1;
    }

    int fd = open_led_device();

    if(fd < 0) {
        perror("LED: unable to open serial device");
        return -1;
    }

    if(configure_port(fd) != 0) {
        perror("LED: unable to configure serial device");
        close(fd);
        return -1;
    }

    int result = write_command(fd, command);

    if(result != 0) {
        perror("LED: command write failed");
    }

    close(fd);
    return result;
}


int led_set_mode(led_mode_t mode)
{
    return led_set_state(
        mode,
        LED_LEVEL_DEFAULT,
        LED_LEVEL_DEFAULT
    );
}
