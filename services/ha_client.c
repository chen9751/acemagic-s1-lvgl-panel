#include "ha_client.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define HA_CONFIG_FILE "config/ha.conf"

#define HA_URL_MAX   256
#define HA_TOKEN_MAX 512


static char ha_url[HA_URL_MAX];
static char ha_token[HA_TOKEN_MAX];


/* =========================================================
 * HTTP 接收缓存
 * ========================================================= */

typedef struct {
    char *data;
    size_t size;
} http_buffer_t;


static size_t write_callback(
    void *contents,
    size_t size,
    size_t nmemb,
    void *userp
)
{
    size_t total = size * nmemb;

    http_buffer_t *buffer =
        (http_buffer_t *)userp;


    char *new_data =
        realloc(
            buffer->data,
            buffer->size + total + 1
        );


    if(new_data == NULL) {
        return 0;
    }


    buffer->data = new_data;


    memcpy(
        buffer->data + buffer->size,
        contents,
        total
    );


    buffer->size += total;
    buffer->data[buffer->size] = '\0';


    return total;
}


/* =========================================================
 * 去掉换行
 * ========================================================= */

static void trim_newline(char *str)
{
    size_t len = strlen(str);

    while(
        len > 0 &&
        (
            str[len - 1] == '\n' ||
            str[len - 1] == '\r'
        )
    ) {

        str[len - 1] = '\0';
        len--;
    }
}


/* =========================================================
 * 读取 config/ha.conf
 * ========================================================= */

static int load_config(void)
{
    FILE *fp =
        fopen(
            HA_CONFIG_FILE,
            "r"
        );


    if(fp == NULL) {

        perror(
            "HA config open failed"
        );

        return -1;
    }


    char line[1024];


    while(
        fgets(
            line,
            sizeof(line),
            fp
        )
    ) {

        trim_newline(line);


        if(
            strncmp(
                line,
                "HA_URL=",
                7
            ) == 0
        ) {

            snprintf(
                ha_url,
                sizeof(ha_url),
                "%s",
                line + 7
            );
        }


        else if(
            strncmp(
                line,
                "HA_TOKEN=",
                9
            ) == 0
        ) {

            snprintf(
                ha_token,
                sizeof(ha_token),
                "%s",
                line + 9
            );
        }
    }


    fclose(fp);


    if(
        ha_url[0] == '\0' ||
        ha_token[0] == '\0'
    ) {

        printf(
            "HA config incomplete\n"
        );

        return -1;
    }


    printf(
        "HA config loaded: %s\n",
        ha_url
    );


    return 0;
}


/* =========================================================
 * 初始化
 * ========================================================= */

int ha_client_init(void)
{
    if(load_config() != 0) {
        return -1;
    }


    CURLcode result =
        curl_global_init(
            CURL_GLOBAL_DEFAULT
        );


    if(result != CURLE_OK) {

        printf(
            "curl init failed\n"
        );

        return -1;
    }


    return 0;
}


/* =========================================================
 * GET 状态
 * ========================================================= */

int ha_get_state(
    const char *entity_id,
    char *state_buf,
    int state_buf_size
)
{
    CURL *curl =
        curl_easy_init();


    if(curl == NULL) {
        return -1;
    }


    char url[512];


    snprintf(
        url,
        sizeof(url),
        "%s/api/states/%s",
        ha_url,
        entity_id
    );


    char auth_header[1024];


    snprintf(
        auth_header,
        sizeof(auth_header),
        "Authorization: Bearer %s",
        ha_token
    );


    struct curl_slist *headers = NULL;


    headers =
        curl_slist_append(
            headers,
            auth_header
        );


    headers =
        curl_slist_append(
            headers,
            "Content-Type: application/json"
        );


    http_buffer_t response = {
        .data = NULL,
        .size = 0
    };


    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url
    );


    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );


    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        write_callback
    );


    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );


    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT,
        3L
    );


    CURLcode result =
        curl_easy_perform(curl);


    if(
        result != CURLE_OK ||
        response.data == NULL
    ) {

        printf(
            "HA GET failed: %s\n",
            curl_easy_strerror(result)
        );


        free(response.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return -1;
    }


    /*
     * 这里只做最小解析。
     * 查找 JSON 中：
     *
     * "state":"on"
     * 或
     * "state":"off"
     */

    char *state =
        strstr(
            response.data,
            "\"state\":\""
        );


    if(state == NULL) {

        free(response.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return -1;
    }


    state += strlen(
        "\"state\":\""
    );


    char *end =
        strchr(
            state,
            '"'
        );


    if(end == NULL) {

        free(response.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return -1;
    }


    int len =
        (int)(end - state);


    if(len >= state_buf_size) {
        len = state_buf_size - 1;
    }


    memcpy(
        state_buf,
        state,
        len
    );


    state_buf[len] = '\0';


    printf(
        "HA state: %s = %s\n",
        entity_id,
        state_buf
    );


    free(response.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);


    return 0;
}


/* =========================================================
 * Toggle
 * ========================================================= */

int ha_toggle(
    const char *entity_id
)
{
    CURL *curl =
        curl_easy_init();


    if(curl == NULL) {
        return -1;
    }


    char url[512];


    snprintf(
        url,
        sizeof(url),
        "%s/api/services/light/toggle",
        ha_url
    );


    char auth_header[1024];


    snprintf(
        auth_header,
        sizeof(auth_header),
        "Authorization: Bearer %s",
        ha_token
    );


    struct curl_slist *headers = NULL;


    headers =
        curl_slist_append(
            headers,
            auth_header
        );


    headers =
        curl_slist_append(
            headers,
            "Content-Type: application/json"
        );


    char body[512];


    snprintf(
        body,
        sizeof(body),
        "{\"entity_id\":\"%s\"}",
        entity_id
    );


    http_buffer_t response = {
        .data = NULL,
        .size = 0
    };


    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url
    );


    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );


    curl_easy_setopt(
        curl,
        CURLOPT_POST,
        1L
    );


    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        body
    );


    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        write_callback
    );


    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );


    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT,
        3L
    );


    CURLcode result =
        curl_easy_perform(curl);


    if(result != CURLE_OK) {

        printf(
            "HA toggle failed: %s\n",
            curl_easy_strerror(result)
        );


        free(response.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return -1;
    }


    printf(
        "HA toggle: %s\n",
        entity_id
    );


    free(response.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);


    return 0;
}


/* =========================================================
 * Turn off every Home Assistant light entity
 * ========================================================= */

int ha_turn_off_all_lights(void)
{
    CURL *curl = curl_easy_init();

    if(curl == NULL) {
        return -1;
    }

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "%s/api/services/light/turn_off",
        ha_url
    );

    char auth_header[1024];

    snprintf(
        auth_header,
        sizeof(auth_header),
        "Authorization: Bearer %s",
        ha_token
    );

    struct curl_slist *headers = NULL;

    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    http_buffer_t response = {
        .data = NULL,
        .size = 0
    };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "{\"entity_id\":\"all\"}");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);

    CURLcode result = curl_easy_perform(curl);

    long response_code = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &response_code
    );

    free(response.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(
        result != CURLE_OK ||
        response_code < 200 ||
        response_code >= 300
    ) {
        printf(
            "HA turn off all lights failed: %s (HTTP %ld)\n",
            curl_easy_strerror(result),
            response_code
        );

        return -1;
    }

    printf("HA turn off all lights\n");

    return 0;
}


/* =========================================================
 * Count light.* entities whose current state is "on"
 * ========================================================= */

int ha_count_on_lights(
    int *count
)
{
    if(count == NULL) {
        return -1;
    }

    CURL *curl = curl_easy_init();

    if(curl == NULL) {
        return -1;
    }

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "%s/api/states",
        ha_url
    );

    char auth_header[1024];

    snprintf(
        auth_header,
        sizeof(auth_header),
        "Authorization: Bearer %s",
        ha_token
    );

    struct curl_slist *headers = NULL;

    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    http_buffer_t response = {
        .data = NULL,
        .size = 0
    };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);

    CURLcode result = curl_easy_perform(curl);

    long response_code = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &response_code
    );

    if(
        result != CURLE_OK ||
        response.data == NULL ||
        response_code < 200 ||
        response_code >= 300
    ) {
        printf(
            "HA light count failed: %s (HTTP %ld)\n",
            curl_easy_strerror(result),
            response_code
        );

        free(response.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return -1;
    }

    int on_count = 0;
    char *cursor = response.data;

    while(
        (
            cursor = strstr(
                cursor,
                "\"entity_id\":\"light."
            )
        ) != NULL
    ) {
        char *next_entity = strstr(
            cursor + 1,
            "\"entity_id\":"
        );

        char *state = strstr(
            cursor,
            "\"state\":\"on\""
        );

        if(
            state != NULL &&
            (
                next_entity == NULL ||
                state < next_entity
            )
        ) {
            on_count++;
        }

        cursor += strlen(
            "\"entity_id\":\"light."
        );
    }

    *count = on_count;

    printf(
        "HA lights on: %d\n",
        on_count
    );

    free(response.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return 0;
}
