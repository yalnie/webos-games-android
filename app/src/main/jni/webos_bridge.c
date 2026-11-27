#include "webos_bridge.h"
#include "cJSON.h"
#include "SDL.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define TAG "MainstreamBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* Magic Remote / Sensor State */
static float s_gyro[3] = {0.0f, 0.0f, 0.0f};
static float s_accel[3] = {0.0f, 0.0f, 9.81f};
static float s_quat[4] = {1.0f, 0.0f, 0.0f, 0.0f};
static int s_ptr_x = 0;
static int s_ptr_y = 0;

void webos_update_sensors(float gx, float gy, float gz, float ax, float ay, float az, float q0, float q1, float q2, float q3) {
    s_gyro[0] = gx; s_gyro[1] = gy; s_gyro[2] = gz;
    s_accel[0] = ax; s_accel[1] = ay; s_accel[2] = az;
    s_quat[0] = q0; s_quat[1] = q1; s_quat[2] = q2; s_quat[3] = q3;
}

void webos_update_pointer(int x, int y) {
    s_ptr_x = x;
    s_ptr_y = y;
}

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

#define SAVE_STORE_FILE "save_store.json"
static char s_response_json[262144] = {0};

static cJSON *load_save_store(void) {
    FILE *f = fopen(SAVE_STORE_FILE, "rb");
    if (!f) {
        return cJSON_CreateObject();
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return cJSON_CreateObject();
    }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return cJSON_CreateObject();
    }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root ? root : cJSON_CreateObject();
}

static void persist_save_store(cJSON *root) {
    if (!root) return;
    char *printed = cJSON_PrintUnformatted(root);
    if (printed) {
        FILE *f = fopen(SAVE_STORE_FILE, "wb");
        if (f) {
            fwrite(printed, 1, strlen(printed), f);
            fclose(f);
            LOGI("Persisted save store (%zu bytes): %s", strlen(printed), printed);
        }
        free(printed);
    }
}

static void save_store_set(const char *key, const char *val) {
    if (!key || !val) return;
    cJSON *root = load_save_store();
    cJSON_DeleteItemFromObject(root, key);
    cJSON_AddStringToObject(root, key, val);
    persist_save_store(root);
    cJSON_Delete(root);
}

static char *save_store_get(const char *key) {
    if (!key) return NULL;
    cJSON *root = load_save_store();
    cJSON *item = cJSON_GetObjectItem(root, key);
    char *val = NULL;
    if (item && item->valuestring) {
        val = strdup(item->valuestring);
    }
    cJSON_Delete(root);
    return val;
}

int NDL_ServiceCallWithCallback(const char *service, const char *params, NDL_Callback cb, void *user_data, int async) {
    LOGI("NDL_ServiceCallWithCallback: %s, params: %s", service ? service : "", params ? params : "");

    struct NDL_Response resp;
    resp.json_str = "{\"returnValue\": true}";

    if (service && strstr(service, "com.webos.mediadb")) {
        if (strstr(service, "find")) {
            const char *key = NULL;
            cJSON *query_root = params ? cJSON_Parse(params) : NULL;
            if (query_root) {
                cJSON *query = cJSON_GetObjectItem(query_root, "query");
                if (query) {
                    cJSON *where = cJSON_GetObjectItem(query, "where");
                    if (where && cJSON_GetArraySize(where) > 0) {
                        cJSON *w0 = cJSON_GetArrayItem(where, 0);
                        cJSON *val = cJSON_GetObjectItem(w0, "val");
                        if (val && val->valuestring) {
                            key = val->valuestring;
                        }
                    }
                }
            }

            char *stored_val = key ? save_store_get(key) : NULL;
            if (stored_val) {
                snprintf(s_response_json, sizeof(s_response_json),
                         "{\"returnValue\": true, \"results\": [{\"path\": \"%s\", \"content\": \"%s\"}]}",
                         key, stored_val);
                free(stored_val);
            } else {
                snprintf(s_response_json, sizeof(s_response_json),
                         "{\"returnValue\": true, \"results\": []}");
            }
            if (query_root) cJSON_Delete(query_root);
            resp.json_str = s_response_json;
        } else if (strstr(service, "put")) {
            cJSON *root = params ? cJSON_Parse(params) : NULL;
            if (root) {
                cJSON *objects = cJSON_GetObjectItem(root, "objects");
                if (objects && cJSON_GetArraySize(objects) > 0) {
                    cJSON *obj0 = cJSON_GetArrayItem(objects, 0);
                    cJSON *path_item = cJSON_GetObjectItem(obj0, "path");
                    cJSON *content_item = cJSON_GetObjectItem(obj0, "content");
                    if (path_item && path_item->valuestring && content_item && content_item->valuestring) {
                        save_store_set(path_item->valuestring, content_item->valuestring);
                        LOGI("Saved key [%s] -> %zu bytes", path_item->valuestring, strlen(content_item->valuestring));
                    }
                }
                cJSON_Delete(root);
            }
            resp.json_str = "{\"returnValue\": true}";
        } else if (strstr(service, "merge")) {
            cJSON *root = params ? cJSON_Parse(params) : NULL;
            if (root) {
                const char *key = NULL;
                cJSON *query = cJSON_GetObjectItem(root, "query");
                if (query) {
                    cJSON *where = cJSON_GetObjectItem(query, "where");
                    if (where && cJSON_GetArraySize(where) > 0) {
                        cJSON *w0 = cJSON_GetArrayItem(where, 0);
                        cJSON *val = cJSON_GetObjectItem(w0, "val");
                        if (val && val->valuestring) {
                            key = val->valuestring;
                        }
                    }
                }
                cJSON *props = cJSON_GetObjectItem(root, "props");
                cJSON *content_item = props ? cJSON_GetObjectItem(props, "content") : NULL;
                if (key && content_item && content_item->valuestring) {
                    save_store_set(key, content_item->valuestring);
                    LOGI("Merged key [%s] -> %zu bytes", key, strlen(content_item->valuestring));
                }
                cJSON_Delete(root);
            }
            resp.json_str = "{\"returnValue\": true}";
        } else if (strstr(service, "putKind")) {
            resp.json_str = "{\"returnValue\": true}";
        }
    } else if (service && strstr(service, "com.webos.service.mrcu")) {
        if (strstr(service, "getSensorData")) {
            snprintf(s_response_json, sizeof(s_response_json),
                     "{\"returnValue\": true, \"deviceId\": 0, "
                     "\"coordinate\": {\"x\": %d, \"y\": %d}, "
                     "\"gyroscope\": {\"x\": %.6f, \"y\": %.6f, \"z\": %.6f}, "
                     "\"acceleration\": {\"x\": %.6f, \"y\": %.6f, \"z\": %.6f}, "
                     "\"quaternion\": {\"q0\": %.6f, \"q1\": %.6f, \"q2\": %.6f, \"q3\": %.6f}}",
                     s_ptr_x, s_ptr_y,
                     s_gyro[0], s_gyro[1], s_gyro[2],
                     s_accel[0], s_accel[1], s_accel[2],
                     s_quat[0], s_quat[1], s_quat[2], s_quat[3]);
            resp.json_str = s_response_json;
        } else {
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

/* NDL Media (backed by Android MediaPlayer via JNI to avoid SDL_OpenAudio conflict) */
static jclass find_main_activity_class(JNIEnv *env) {
    if (!env) return NULL;
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!act) return NULL;
    jclass cls = (*env)->GetObjectClass(env, act);
    (*env)->DeleteLocalRef(env, act);
    return cls;
}

int NDL_MediaInit(const char *appId) { return 0; }

int NDL_MediaQuit(void) {
    return NDL_MediaUnload();
}

int NDL_MediaLoad(const char *mediaInfo, void *cb) {
    if (!mediaInfo) return 0;
    char path_buf[512] = {0};
    if (mediaInfo[0] == '{') {
        cJSON *root = cJSON_Parse(mediaInfo);
        if (root) {
            cJSON *src = cJSON_GetObjectItem(root, "source");
            if (!src) src = cJSON_GetObjectItem(root, "uri");
            if (src && src->valuestring) {
                strncpy(path_buf, src->valuestring, sizeof(path_buf) - 1);
            }
            cJSON_Delete(root);
        }
    } else {
        strncpy(path_buf, mediaInfo, sizeof(path_buf) - 1);
    }

    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jclass cls = find_main_activity_class(env);
    int res = 1;
    if (env && cls) {
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "ndlMediaLoad", "(Ljava/lang/String;)I");
        if (mid) {
            jstring jpath = (*env)->NewStringUTF(env, path_buf);
            res = (*env)->CallStaticIntMethod(env, cls, mid, jpath);
            (*env)->DeleteLocalRef(env, jpath);
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    return res;
}

int NDL_MediaUnload(void) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jclass cls = find_main_activity_class(env);
    if (env && cls) {
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "ndlMediaUnload", "()V");
        if (mid) (*env)->CallStaticVoidMethod(env, cls, mid);
        (*env)->DeleteLocalRef(env, cls);
    }
    return 0;
}

int NDL_MediaPlay(void) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jclass cls = find_main_activity_class(env);
    if (env && cls) {
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "ndlMediaPlay", "()V");
        if (mid) (*env)->CallStaticVoidMethod(env, cls, mid);
        (*env)->DeleteLocalRef(env, cls);
    }
    return 0;
}

int NDL_MediaPause(void) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jclass cls = find_main_activity_class(env);
    if (env && cls) {
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "ndlMediaPause", "()V");
        if (mid) (*env)->CallStaticVoidMethod(env, cls, mid);
        (*env)->DeleteLocalRef(env, cls);
    }
    return 0;
}

int NDL_MediaSeekTo(int posMs) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jclass cls = find_main_activity_class(env);
    if (env && cls) {
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "ndlMediaSeekTo", "(I)V");
        if (mid) (*env)->CallStaticVoidMethod(env, cls, mid, (jint)posMs);
        (*env)->DeleteLocalRef(env, cls);
    }
    return 0;
}

int NDL_MediaSubscribeEvents(void *cb, void *userData) { return 0; }
const char *NDL_MediaGetError(void) { return ""; }

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

/* LGNC (NetCast) */
int LGNC_SYSTEM_Initialize(int argc, char **argv, void *cb) { return 0; }
int LGNC_SYSTEM_Finalize(void) { return 0; }
const char *LGNC_SYSTEM_GetLanguage(void) { return "en"; }
const char *LGNC_SYSTEM_GetSaveDirectory(void) { return "."; }

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

const char *curl_easy_strerror(int code) {
    return "No error";
}

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

void curl_global_cleanup(void) {}

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
    static char s_json_str_buf[4096];
    if (!jso) return "";
    cJSON *item = (cJSON *)jso;
    if (item->valuestring) return item->valuestring;
    char *printed = cJSON_PrintUnformatted(item);
    if (printed) {
        strncpy(s_json_str_buf, printed, sizeof(s_json_str_buf) - 1);
        s_json_str_buf[sizeof(s_json_str_buf) - 1] = '\0';
        free(printed);
        return s_json_str_buf;
    }
    return "";
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
