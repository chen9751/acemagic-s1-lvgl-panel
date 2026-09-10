#include "ha_client.h"

#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HA_CONFIG_FILE "config/ha.conf"
#define HA_URL_MAX 256
#define HA_TOKEN_MAX 512

typedef struct {
    char *data;
    size_t size;
} http_buffer_t;

static char ha_url[HA_URL_MAX];
static char ha_token[HA_TOKEN_MAX];

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    http_buffer_t *buffer = (http_buffer_t *)userp;
    char *next = realloc(buffer->data, buffer->size + total + 1);
    if(next == NULL) return 0;
    buffer->data = next;
    memcpy(buffer->data + buffer->size, contents, total);
    buffer->size += total;
    buffer->data[buffer->size] = '\0';
    return total;
}

static void trim_newline(char *text)
{
    size_t n = strlen(text);
    while(n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = '\0';
}

static int load_config(void)
{
    FILE *fp = fopen(HA_CONFIG_FILE, "r");
    if(fp == NULL) {
        perror("HA config open failed");
        return -1;
    }

    ha_url[0] = '\0';
    ha_token[0] = '\0';
    char line[1024];
    while(fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if(strncmp(line, "HA_URL=", 7) == 0)
            snprintf(ha_url, sizeof(ha_url), "%.*s", (int)sizeof(ha_url) - 1, line + 7);
        else if(strncmp(line, "HA_TOKEN=", 9) == 0)
            snprintf(ha_token, sizeof(ha_token), "%.*s", (int)sizeof(ha_token) - 1, line + 9);
    }
    fclose(fp);

    if(ha_url[0] == '\0' || ha_token[0] == '\0') {
        printf("HA config incomplete\n");
        return -1;
    }
    printf("HA config loaded: %s\n", ha_url);
    return 0;
}

int ha_client_init(void)
{
    if(load_config() != 0) return -1;
    if(curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        printf("curl init failed\n");
        return -1;
    }
    return 0;
}

static int request(const char *path, const char *body, char **response_out)
{
    CURL *curl = curl_easy_init();
    if(curl == NULL) return -1;

    char url[768];
    char auth_header[1024];
    snprintf(url, sizeof(url), "%s%s", ha_url, path);
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", ha_token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    http_buffer_t response = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    if(body != NULL) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    CURLcode result = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(result != CURLE_OK || response_code < 200 || response_code >= 300) {
        printf("HA request failed: %s (HTTP %ld)\n", curl_easy_strerror(result), response_code);
        free(response.data);
        return -1;
    }

    if(response_out != NULL) *response_out = response.data;
    else free(response.data);
    return 0;
}

static const char *json_value(const char *json, const char *key)
{
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if(p == NULL) return NULL;
    p += strlen(needle);
    while(*p && isspace((unsigned char)*p)) p++;
    if(*p != ':') return NULL;
    p++;
    while(*p && isspace((unsigned char)*p)) p++;
    return p;
}

static int json_string(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p = json_value(json, key);
    if(p == NULL || *p != '"' || out_size == 0) return -1;
    p++;
    const char *end = strchr(p, '"');
    if(end == NULL) return -1;
    size_t n = (size_t)(end - p);
    if(n >= out_size) n = out_size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int json_int(const char *json, const char *key, int *value)
{
    const char *p = json_value(json, key);
    if(p == NULL || value == NULL || !(*p == '-' || isdigit((unsigned char)*p))) return -1;
    char *end = NULL;
    long parsed = strtol(p, &end, 10);
    if(end == p) return -1;
    *value = (int)parsed;
    return 0;
}

int ha_get_state(const char *entity_id, char *state_buf, int state_buf_size)
{
    if(entity_id == NULL || state_buf == NULL || state_buf_size <= 0) return -1;
    char path[512];
    snprintf(path, sizeof(path), "/api/states/%s", entity_id);
    char *json = NULL;
    if(request(path, NULL, &json) != 0) return -1;
    int result = json_string(json, "state", state_buf, (size_t)state_buf_size);
    free(json);
    if(result == 0) printf("HA state: %s = %s\n", entity_id, state_buf);
    return result;
}

int ha_get_light_state(const char *entity_id, ha_light_state_t *state)
{
    if(entity_id == NULL || state == NULL) return -1;

    char path[512];
    snprintf(path, sizeof(path), "/api/states/%s", entity_id);
    char *json = NULL;
    if(request(path, NULL, &json) != 0) return -1;

    char power[16];
    if(json_string(json, "state", power, sizeof(power)) != 0) {
        free(json);
        return -1;
    }

    ha_light_state_t next = *state;
    next.is_on = strcmp(power, "on") == 0;
    next.has_brightness = 0;
    next.has_color_temperature = 0;

    int raw = 0;
    if(json_int(json, "brightness", &raw) == 0) {
        if(raw < 0) raw = 0;
        if(raw > 255) raw = 255;
        next.brightness_percent = (raw * 100 + 127) / 255;
        if(next.is_on && next.brightness_percent < 1) next.brightness_percent = 1;
        next.has_brightness = 1;
    }

    int kelvin = 0;
    if(json_int(json, "color_temp_kelvin", &kelvin) == 0 && kelvin > 0) {
        next.color_temperature_kelvin = kelvin;
        next.has_color_temperature = 1;
    } else {
        int mireds = 0;
        if((json_int(json, "color_temp_mireds", &mireds) == 0 ||
            json_int(json, "color_temp", &mireds) == 0) && mireds > 0) {
            next.color_temperature_kelvin = 1000000 / mireds;
            next.has_color_temperature = 1;
        }
    }

    *state = next;
    printf("HA light sync: %s state=%s brightness=%d%% kelvin=%d\n",
           entity_id, next.is_on ? "on" : "off", next.brightness_percent,
           next.color_temperature_kelvin);
    free(json);
    return 0;
}

static int post_light_service(const char *service, const char *body)
{
    char path[256];
    snprintf(path, sizeof(path), "/api/services/light/%s", service);
    return request(path, body, NULL);
}

int ha_set_light_power(const char *entity_id, int on)
{
    char body[512];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity_id);
    return post_light_service(on ? "turn_on" : "turn_off", body);
}

int ha_toggle(const char *entity_id)
{
    char body[512];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity_id);
    return post_light_service("toggle", body);
}

int ha_set_light_brightness(const char *entity_id, int brightness_percent)
{
    if(brightness_percent < 0) brightness_percent = 0;
    if(brightness_percent > 100) brightness_percent = 100;
    char body[512];
    snprintf(body, sizeof(body),
             "{\"entity_id\":\"%s\",\"brightness_pct\":%d}",
             entity_id, brightness_percent);
    return post_light_service("turn_on", body);
}

int ha_set_light_color_temperature(const char *entity_id, int color_temperature_kelvin)
{
    char body[512];
    snprintf(body, sizeof(body),
             "{\"entity_id\":\"%s\",\"color_temp_kelvin\":%d}",
             entity_id, color_temperature_kelvin);
    return post_light_service("turn_on", body);
}

int ha_turn_off_all_lights(void)
{
    return post_light_service("turn_off", "{\"entity_id\":\"all\"}");
}

int ha_count_on_lights(int *count)
{
    if(count == NULL) return -1;
    char *json = NULL;
    if(request("/api/states", NULL, &json) != 0) return -1;

    int on_count = 0;
    const char *cursor = json;
    while((cursor = strstr(cursor, "\"entity_id\"")) != NULL) {
        const char *id = strchr(cursor, ':');
        if(id == NULL) break;
        while(*id && (*id == ':' || isspace((unsigned char)*id))) id++;
        if(*id == '"') id++;
        if(strncmp(id, "light.", 6) == 0) {
            const char *next = strstr(cursor + 1, "\"entity_id\"");
            const char *state_on = strstr(cursor, "\"state\":\"on\"");
            if(state_on == NULL) state_on = strstr(cursor, "\"state\": \"on\"");
            if(state_on != NULL && (next == NULL || state_on < next)) on_count++;
        }
        cursor += 11;
    }
    free(json);
    *count = on_count;
    printf("HA lights on: %d\n", on_count);
    return 0;
}
