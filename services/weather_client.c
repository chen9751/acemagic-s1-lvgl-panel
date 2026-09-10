#include "weather_client.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEATHER_URL_DEFAULT "https://api.open-meteo.com/v1/forecast?latitude=25.04&longitude=102.72&current=temperature_2m,weather_code&daily=precipitation_probability_max&forecast_days=1&timezone=auto"

typedef struct {
    char data[4096];
    size_t length;
} weather_buffer_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    weather_buffer_t *buffer = (weather_buffer_t *)userdata;
    size_t bytes = size * nmemb;
    size_t available = sizeof(buffer->data) - buffer->length - 1;
    if(bytes > available) bytes = available;
    if(bytes > 0) {
        memcpy(buffer->data + buffer->length, ptr, bytes);
        buffer->length += bytes;
        buffer->data[buffer->length] = '\0';
    }
    return size * nmemb;
}

static int parse_number_in_section(
    const char *json,
    const char *section,
    const char *key,
    int *value
)
{
    if(json == NULL || section == NULL || key == NULL || value == NULL) return -1;

    const char *p = strstr(json, section);
    if(p == NULL) return -1;

    const char *section_end = strchr(p, '}');
    if(section_end == NULL) return -1;

    p = strstr(p, key);
    if(p == NULL || p >= section_end) return -1;

    p = strchr(p, ':');
    if(p == NULL || p >= section_end) return -1;
    p++;

    while(*p == ' ' || *p == '[') p++;
    if(p >= section_end) return -1;

    *value = (int)strtol(p, NULL, 10);
    return 0;
}

void weather_client_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

bool weather_client_refresh(weather_state_t *state)
{
    if(state == NULL) return false;

    state->valid = false;

    const char *url = getenv("S1_WEATHER_URL");
    if(url == NULL || url[0] == '\0') url = WEATHER_URL_DEFAULT;

    weather_buffer_t buffer = { .data = {0}, .length = 0 };
    CURL *curl = curl_easy_init();
    if(curl == NULL) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3500L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1800L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "acemagic-s1-panel/1.0");

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if(rc != CURLE_OK || status < 200 || status >= 300) {
        fprintf(stderr, "Weather refresh failed: curl=%d http=%ld\n", (int)rc, status);
        return false;
    }

    int temp = 0;
    int code = 0;
    int rain = 0;

    /* Open-Meteo repeats field names inside *_units objects. Parse only from
     * the actual current and daily data objects so strings such as "°C" are
     * never interpreted as numeric weather values. */
    if(parse_number_in_section(buffer.data, "\"current\":", "\"temperature_2m\"", &temp) != 0) {
        fprintf(stderr, "Weather parse failed: current.temperature_2m\n");
        return false;
    }
    if(parse_number_in_section(buffer.data, "\"current\":", "\"weather_code\"", &code) != 0) {
        fprintf(stderr, "Weather parse failed: current.weather_code\n");
        return false;
    }
    if(parse_number_in_section(buffer.data, "\"daily\":", "\"precipitation_probability_max\"", &rain) != 0) {
        fprintf(stderr, "Weather parse failed: daily.precipitation_probability_max\n");
        return false;
    }

    if(rain < 0) rain = 0;
    if(rain > 100) rain = 100;

    state->valid = true;
    state->temperature_c = temp;
    state->weather_code = code;
    state->rain_probability_percent = rain;
    return true;
}
