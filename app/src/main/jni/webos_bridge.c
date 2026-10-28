#include "webos_bridge.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define TAG "MainstreamBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* Time */
int ftime(struct timeb *tp) {
    if (!tp) return -1;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tp->time = tv.tv_sec;
    tp->millitm = (unsigned short)(tv.tv_usec / 1000);
    tp->timezone = 0;
    tp->dstflag = 0;
    return 0;
}

/* Base64 encoding table */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *g_base64_encode(const unsigned char *data, size_t len) {
    if (!data || len == 0) return strdup("");
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;

    size_t i, j = 0;
    for (i = 0; i < len;) {
        uint32_t oct_a = i < len ? data[i++] : 0;
        uint32_t oct_b = i < len ? data[i++] : 0;
        uint32_t oct_c = i < len ? data[i++] : 0;

        uint32_t triple = (oct_a << 16) | (oct_b << 8) | oct_c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64_table[triple & 0x3F];
    }
    out[j] = '\0';
    return out;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

unsigned char *g_base64_decode(const char *text, size_t *out_len) {
    if (!text) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    size_t len = strlen(text);
    if (len % 4 != 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    size_t alloc_len = (len / 4) * 3;
    if (len > 0 && text[len - 1] == '=') alloc_len--;
    if (len > 1 && text[len - 2] == '=') alloc_len--;

    unsigned char *out = (unsigned char *)malloc(alloc_len + 1);
    if (!out) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    size_t i, j = 0;
    for (i = 0; i < len;) {
        int v0 = b64_val(text[i++]);
        int v1 = b64_val(text[i++]);
        int v2 = (i < len && text[i] != '=') ? b64_val(text[i]) : 0; i++;
        int v3 = (i < len && text[i] != '=') ? b64_val(text[i]) : 0; i++;

        if (v0 < 0 || v1 < 0) break;
        uint32_t triple = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;

        if (j < alloc_len) out[j++] = (triple >> 16) & 0xFF;
        if (j < alloc_len) out[j++] = (triple >> 8) & 0xFF;
        if (j < alloc_len) out[j++] = triple & 0xFF;
    }
    out[j] = '\0';
    if (out_len) *out_len = j;
    return out;
}

void g_free(void *ptr) {
    free(ptr);
}

/* NDL */
int NDL_ServiceInit(int timeout) {
    LOGI("NDL_ServiceInit(%d) -> OK", timeout);
    return 0;
}

int NDL_ServiceQuit(int reason) {
    LOGI("NDL_ServiceQuit(%d) -> OK", reason);
    return 0;
}

int NDL_ServiceCall(const char *service, const char *params) {
    LOGI("NDL_ServiceCall: %s params: %s", service ? service : "", params ? params : "");
    return 0;
}

struct NDL_Response {
    const char *json_str;
};

static char s_savedata_buf[65536] = {0};
static char s_response_json[65536] = {0};

int NDL_ServiceCallWithCallback(const char *service, const char *params, NDL_Callback cb, void *user_data, int async) {
    LOGI("NDL_ServiceCallWithCallback: %s, params: %s", service ? service : "", params ? params : "");

    struct NDL_Response resp;
    resp.json_str = "{\"returnValue\": true}";

    if (service && strstr(service, "com.webos.mediadb")) {
        if (strstr(service, "find")) {
            /* Try to read savedata from disk */
            FILE *f = fopen("save_game.dat", "rb");
            s_savedata_buf[0] = '\0';
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (sz > 0 && sz < (long)sizeof(s_savedata_buf) - 1) {
                    fread(s_savedata_buf, 1, sz, f);
                    s_savedata_buf[sz] = '\0';
                }
                fclose(f);
            }

            if (s_savedata_buf[0]) {
                snprintf(s_response_json, sizeof(s_response_json),
                         "{\"returnValue\": true, \"results\": [{\"path\": \"mainstream.save_data\", \"content\": \"%s\"}]}",
                         s_savedata_buf);
            } else {
                snprintf(s_response_json, sizeof(s_response_json),
                         "{\"returnValue\": true, \"results\": []}");
            }
            resp.json_str = s_response_json;
        } else if (strstr(service, "put") || strstr(service, "merge")) {
            /* Extract content from params and write to disk */
            if (params) {
                cJSON *root = cJSON_Parse(params);
                if (root) {
                    const char *b64_content = NULL;
                    /* Check "objects" array (used by put) */
                    cJSON *objects = cJSON_GetObjectItem(root, "objects");
                    if (objects && cJSON_GetArraySize(objects) > 0) {
                        cJSON *obj0 = cJSON_GetArrayItem(objects, 0);
                        cJSON *content_item = cJSON_GetObjectItem(obj0, "content");
                        if (content_item && content_item->valuestring) {
                            b64_content = content_item->valuestring;
                        }
                    }
                    /* Check "props" (used by merge) */
                    if (!b64_content) {
                        cJSON *props = cJSON_GetObjectItem(root, "props");
                        if (props) {
                            cJSON *content_item = cJSON_GetObjectItem(props, "content");
                            if (content_item && content_item->valuestring) {
                                b64_content = content_item->valuestring;
                            }
                        }
                    }

                    if (b64_content) {
                        strncpy(s_savedata_buf, b64_content, sizeof(s_savedata_buf) - 1);
                        s_savedata_buf[sizeof(s_savedata_buf) - 1] = '\0';
                        FILE *f = fopen("save_game.dat", "wb");
                        if (f) {
                            fwrite(s_savedata_buf, 1, strlen(s_savedata_buf), f);
                            fclose(f);
                            LOGI("Game successfully saved to save_game.dat (%zu bytes)", strlen(s_savedata_buf));
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            resp.json_str = "{\"returnValue\": true}";
        } else if (strstr(service, "putKind")) {
            resp.json_str = "{\"returnValue\": true}";
        }
    }

    if (cb) {
        LOGI("Invoking NDL callback with json: %s", resp.json_str);
        cb(&resp, user_data);
    }
    return 0;
}

const char *NDL_ServiceGetError(void) {
    return "";
}

int NDL_UnregisterServiceCallback(NDL_Callback cb) {
    return 0;
}

/* LG HAL */
const char *lg_hal_m3g_OpenGLES_getHint(const char *hint) {
    if (hint && strcmp(hint, "aa_samples_preferred") == 0) return "2";
    return NULL;
}

int lg_hal_m3g_stereo3d_isUserAllowed(void) {
    return 0;
}

int lg_hal_m3g_stereo3d_getModeCount(void) {
    return 0;
}

/* Curl Stubs */
void *curl_easy_init(void) {
    return (void *)0x1000;
}

int curl_easy_setopt(void *curl, int option, ...) {
    return 0; /* CURLE_OK */
}

int curl_easy_perform(void *curl) {
    return 0; /* CURLE_OK */
}

void curl_easy_cleanup(void *curl) {}

char *curl_easy_escape(void *curl, const char *string, int length) {
    return strdup(string ? string : "");
}

void curl_free(void *p) {
    free(p);
}

void *curl_slist_append(void *list, const char *string) {
    return (void *)0x2000;
}

void curl_slist_free_all(void *list) {}

int curl_global_init(long flags) {
    return 0;
}

/* JSON-C API */
void *json_tokener_parse(const char *str) {
    if (!str) return NULL;
    return (void *)cJSON_Parse(str);
}

void *json_object_object_get(void *jso, const char *key) {
    if (!jso || !key) return NULL;
    return (void *)cJSON_GetObjectItem((cJSON *)jso, key);
}

void *json_object_array_get_idx(void *jso, int idx) {
    if (!jso) return NULL;
    return (void *)cJSON_GetArrayItem((cJSON *)jso, idx);
}

int json_object_get_boolean(void *jso) {
    if (!jso) return 0;
    return cJSON_IsTrue((cJSON *)jso) ? 1 : 0;
}

int json_object_put(void *jso) {
    if (jso) {
        cJSON_Delete((cJSON *)jso);
    }
    return 1;
}

int json_object_get_int(void *jso) {
    if (!jso) return 0;
    return ((cJSON *)jso)->valueint;
}

const char *json_object_get_string(void *jso) {
    if (!jso) return "";
    cJSON *item = (cJSON *)jso;
    return item->valuestring ? item->valuestring : "";
}

int json_object_array_length(void *jso) {
    if (!jso) return 0;
    return cJSON_GetArraySize((cJSON *)jso);
}

double json_object_get_double(void *jso) {
    if (!jso) return 0.0;
    return ((cJSON *)jso)->valuedouble;
}

/* json_type enum values:
   json_type_null = 0, json_type_boolean = 1, json_type_double = 2,
   json_type_int = 3, json_type_object = 4, json_type_array = 5, json_type_string = 6 */
int json_object_get_type(void *jso) {
    if (!jso) return 0;
    cJSON *item = (cJSON *)jso;
    if (cJSON_IsBool(item)) return 1;
    if (cJSON_IsNumber(item)) return 2;
    if (cJSON_IsObject(item)) return 4;
    if (cJSON_IsArray(item)) return 5;
    if (cJSON_IsString(item)) return 6;
    return 0;
}
