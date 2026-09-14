#include "bluez_media_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLUEZ_SERVICE "org.bluez"
#define BLUEZ_PLAYER_IFACE "org.bluez.MediaPlayer1"
#define BLUEZ_POLL_INTERVAL_MS 1000ULL
#define BLUEZ_PLAYER_PATH_MAX 256
#define BLUEZ_COMMAND_OUTPUT_MAX 8192

static char player_path[BLUEZ_PLAYER_PATH_MAX];
static char control_path[BLUEZ_PLAYER_PATH_MAX];
static bluez_media_state_t cached_state;
static uint64_t next_poll_ms;

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static bool is_safe_object_path(const char *path)
{
    if(path == NULL || strncmp(path, "/org/bluez/", 11) != 0) return false;
    for(const unsigned char *p = (const unsigned char *)path; *p != '\0'; p++) {
        if(!(isalnum(*p) || *p == '_' || *p == '/')) return false;
    }
    return strstr(path, "/player") != NULL;
}

static int run_command(const char *command, char *output, size_t output_size)
{
    if(command == NULL || output == NULL || output_size == 0) return -1;
    output[0] = '\0';
    FILE *pipe = popen(command, "r");
    if(pipe == NULL) return -1;

    size_t used = 0;
    while(used + 1 < output_size) {
        size_t count = fread(output + used, 1, output_size - used - 1, pipe);
        used += count;
        if(count == 0) break;
    }
    output[used] = '\0';
    /* Drain excess output before waiting: a full pipe can block the child. */
    char excess[1024];
    bool truncated = false;
    while(fread(excess, 1, sizeof(excess), pipe) > 0) truncated = true;
    bool read_failed = ferror(pipe) != 0;
    int status = pclose(pipe);
    if(truncated || read_failed) return -1;
    return status == 0 ? 0 : -1;
}

static bool decode_quoted_string(const char *start, char *dst, size_t dst_size);

static int discover_player(void)
{
    char output[BLUEZ_COMMAND_OUTPUT_MAX];
    player_path[0] = control_path[0] = '\0';
    if(run_command("busctl --system --no-pager --list --timeout=1s tree org.bluez",
                   output, sizeof(output)) != 0) return -1;

    /* Resolve the addressed player of a connected AVRCP device, rather than
     * accepting the first path containing /player (which may be a folder). */
    const char *cursor = output;
    while((cursor = strstr(cursor, "/org/bluez/")) != NULL) {
        const char *end = cursor;
        while(*end && !isspace((unsigned char)*end)) end++;
        size_t len = (size_t)(end - cursor);
        char device[BLUEZ_PLAYER_PATH_MAX];
        if(len >= sizeof(device)) { cursor = end; continue; }
        memcpy(device, cursor, len);
        device[len] = '\0';
        cursor = end;
        const char *dev = strstr(device, "/dev_");
        if(dev == NULL || strchr(dev + 1, '/') != NULL) continue;
        bool safe = true;
        for(const unsigned char *c = (const unsigned char *)device; *c; c++)
            if(!(isalnum(*c) || *c == '_' || *c == '/')) safe = false;
        if(!safe) continue;
        char command[640], property[512];
        snprintf(command, sizeof(command),
                 "busctl --system --no-pager --timeout=1s get-property org.bluez %s org.bluez.MediaControl1 Connected 2>/dev/null", device);
        if(run_command(command, property, sizeof(property)) != 0 ||
           strncmp(property, "b true", 6) != 0) continue;
        snprintf(control_path, sizeof(control_path), "%s", device);
        snprintf(command, sizeof(command),
                 "busctl --system --no-pager --timeout=1s get-property org.bluez %s org.bluez.MediaControl1 Player 2>/dev/null", device);
        if(run_command(command, property, sizeof(property)) == 0) {
            const char *quoted = strchr(property, '"');
            char candidate[BLUEZ_PLAYER_PATH_MAX];
            if(decode_quoted_string(quoted, candidate, sizeof(candidate)) &&
               is_safe_object_path(candidate) &&
               strncmp(candidate, device, len) == 0 && candidate[len] == '/')
                snprintf(player_path, sizeof(player_path), "%s", candidate);
        }
        return 0; /* MediaControl works even when optional Player is absent. */
    }
    return -1;
}

static bool decode_quoted_string(const char *start, char *dst, size_t dst_size)
{
    if(start == NULL || dst == NULL || dst_size == 0 || *start != '"') return false;
    size_t out = 0;
    const char *p = start + 1;

    while(*p != '\0' && *p != '"') {
        unsigned char value;
        if(*p != '\\') {
            value = (unsigned char)*p++;
        } else {
            p++;
            if(*p == '\0') break;
            if(*p >= '0' && *p <= '7') {
                int octal = 0;
                int digits = 0;
                while(digits < 3 && *p >= '0' && *p <= '7') {
                    octal = octal * 8 + (*p - '0');
                    p++;
                    digits++;
                }
                value = (unsigned char)octal;
            } else {
                switch(*p) {
                    case 'n': value = '\n'; break;
                    case 'r': value = '\r'; break;
                    case 't': value = '\t'; break;
                    case '"': value = '"'; break;
                    case '\\': value = '\\'; break;
                    default: value = (unsigned char)*p; break;
                }
                p++;
            }
        }
        if(out + 1 < dst_size) dst[out++] = (char)value;
    }
    dst[out] = '\0';
    return *p == '"';
}

static bool extract_string_field(const char *output, const char *key, char *dst, size_t dst_size)
{
    char marker[80];
    snprintf(marker, sizeof(marker), "\"%s\"", key);
    const char *hit = strstr(output, marker);
    if(hit == NULL) return false;

    const char *p = hit + strlen(marker);
    while(*p != '\0' && isspace((unsigned char)*p)) p++;
    if(*p != 's') return false;
    p++;
    while(*p != '\0' && isspace((unsigned char)*p)) p++;
    return decode_quoted_string(p, dst, dst_size);
}

static bool extract_uint_field(const char *output, const char *key, uint32_t *value)
{
    char marker[80];
    snprintf(marker, sizeof(marker), "\"%s\"", key);
    const char *hit = strstr(output, marker);
    if(hit == NULL) return false;

    const char *p = hit + strlen(marker);
    while(*p != '\0' && isspace((unsigned char)*p)) p++;
    if(*p != 'u') return false;
    p++;
    while(*p != '\0' && isspace((unsigned char)*p)) p++;

    char *end = NULL;
    unsigned long parsed = strtoul(p, &end, 10);
    if(end == p) return false;
    *value = (uint32_t)parsed;
    return true;
}

static void clear_state(void)
{
    memset(&cached_state, 0, sizeof(cached_state));
}

static int refresh_state(void)
{
    if(control_path[0] == '\0' && discover_player() != 0) {
        clear_state();
        return -1;
    }

    if(player_path[0] == '\0') {
        cached_state.connected = true;
        /* Try again on the next poll: the phone may register its player later. */
        control_path[0] = '\0';
        return 0;
    }
    char command[640];
    char output[BLUEZ_COMMAND_OUTPUT_MAX];
    snprintf(command, sizeof(command),
             "busctl --system --no-pager --timeout=1s call %s %s org.freedesktop.DBus.Properties GetAll s %s 2>/dev/null",
             BLUEZ_SERVICE, player_path, BLUEZ_PLAYER_IFACE);

    if(run_command(command, output, sizeof(output)) != 0) {
        player_path[0] = control_path[0] = '\0';
        clear_state();
        return -1;
    }

    bluez_media_state_t next;
    memset(&next, 0, sizeof(next));
    next.connected = true;

    char status[32] = {0};
    if(extract_string_field(output, "Status", status, sizeof(status))) {
        next.playing = strcmp(status, "playing") == 0;
        next.paused = strcmp(status, "paused") == 0;
    }

    extract_uint_field(output, "Position", &next.position_ms);
    extract_uint_field(output, "Duration", &next.duration_ms);
    extract_string_field(output, "Title", next.title, sizeof(next.title));
    extract_string_field(output, "Artist", next.artist, sizeof(next.artist));
    extract_string_field(output, "Album", next.album, sizeof(next.album));

    cached_state = next;
    return 0;
}

int bluez_media_init(void)
{
    player_path[0] = control_path[0] = '\0';
    clear_state();
    next_poll_ms = 0;
    return 0;
}

bool bluez_media_poll(bluez_media_state_t *state)
{
    if(state == NULL) return false;
    uint64_t now = monotonic_ms();
    if(next_poll_ms != 0 && now < next_poll_ms) return false;
    next_poll_ms = now + BLUEZ_POLL_INTERVAL_MS;
    refresh_state();
    *state = cached_state;
    return true;
}

void bluez_media_force_refresh(void)
{
    next_poll_ms = 0;
}

int bluez_media_control(bluez_media_action_t action)
{
    if(action < BLUEZ_MEDIA_PREVIOUS || action > BLUEZ_MEDIA_NEXT) return -1;
    /* Re-resolve on an action: reconnection can change the addressed player. */
    if(discover_player() != 0) {
        fprintf(stderr, "Music: no connected Bluetooth media controller\n");
        return -1;
    }
    if(player_path[0] != '\0') refresh_state();
    if(control_path[0] == '\0' && discover_player() != 0) return -1;
    const char *method = action == BLUEZ_MEDIA_PREVIOUS ? "Previous" :
                         action == BLUEZ_MEDIA_NEXT ? "Next" :
                         cached_state.playing ? "Pause" : "Play";
    char command[640], output[1024];
    bool use_player = player_path[0] != '\0';
    snprintf(command, sizeof(command),
             "busctl --system --no-pager --timeout=1s call %s %s %s %s 2>&1",
             BLUEZ_SERVICE, use_player ? player_path : control_path,
             use_player ? BLUEZ_PLAYER_IFACE : "org.bluez.MediaControl1", method);
    int rc = run_command(command, output, sizeof(output));
    /* Only retry explicit unsupported-method failures: never duplicate Next
     * after a timeout, since the phone may already have executed it. */
    if(rc != 0 && use_player &&
       (strstr(output, "NotSupported") || strstr(output, "Not supported") ||
        strstr(output, "UnknownMethod") || strstr(output, "UnknownInterface") ||
        strstr(output, "not provided") || strstr(output, "No such interface"))) {
        snprintf(command, sizeof(command),
                 "busctl --system --no-pager --timeout=1s call %s %s org.bluez.MediaControl1 %s 2>&1",
                 BLUEZ_SERVICE, control_path, method);
        rc = run_command(command, output, sizeof(output));
    }
    if(rc == 0 && action == BLUEZ_MEDIA_PLAY_PAUSE) {
        cached_state.playing = strcmp(method, "Play") == 0;
        cached_state.paused = !cached_state.playing;
    }
    if(rc != 0) fprintf(stderr, "Music %s failed: %s\n", method, output);
    bluez_media_force_refresh();
    return rc;
}
