#include "webos_bridge.h"
#include "cJSON.h"
#include "SDL.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/videodev2.h>
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

/* NDL Media (backed by Android MediaPlayer via JNI) */
static jclass find_main_activity_class(JNIEnv *env) {
    if (!env) return NULL;
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!act) return NULL;
    jclass cls = (*env)->GetObjectClass(env, act);
    (*env)->DeleteLocalRef(env, act);
    return cls;
}

int NDL_MediaInit(const char *appId) { return 0; }
int NDL_MediaQuit(void) { return NDL_MediaUnload(); }

int NDL_MediaLoad(const char *mediaInfo, void *cb) {
    if (!mediaInfo) return 0;
    char path_buf[512] = {0};
    if (mediaInfo[0] == '{') {
        cJSON *root = cJSON_Parse(mediaInfo);
        if (root) {
            cJSON *src = cJSON_GetObjectItem(root, "source");
            if (!src) src = cJSON_GetObjectItem(root, "uri");
            if (!src) src = cJSON_GetObjectItem(root, "file");
            if (!src) src = cJSON_GetObjectItem(root, "path");
            if (!src) src = cJSON_GetObjectItem(root, "url");
            if (!src) src = cJSON_GetObjectItem(root, "src");
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
    int res = 0;
    if (env && cls) {
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "ndlMediaLoad", "(Ljava/lang/String;)I");
        if (mid) {
            jstring jpath = (*env)->NewStringUTF(env, path_buf);
            res = (*env)->CallStaticIntMethod(env, cls, mid, jpath);
            (*env)->DeleteLocalRef(env, jpath);
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    LOGI("NDL_MediaLoad(%s -> %s) -> %d", mediaInfo, path_buf, res);
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
static SDL_Window *s_lgnc_window = NULL;
static SDL_GLContext s_lgnc_glctx = NULL;
static int s_lgnc_w = 1920;
static int s_lgnc_h = 1080;
static int s_lgnc_es_ver = 1;

int LGNC_SYSTEM_Initialize(int argc, char **argv, void *cb) { return 0; }
int LGNC_SYSTEM_Finalize(void) { return 0; }
int LGNC_SYSTEM_GetLanguage(char *out) { if (out) strcpy(out, "en"); return 0; }
int LGNC_SYSTEM_GetSaveDirectory(char *out) { if (out) strcpy(out, "."); return 0; }
int LGNC_SYSTEM_SetResolution(int w, int h) {
    if (w > 0) s_lgnc_w = w;
    if (h > 0) s_lgnc_h = h;
    return 0;
}
int LGNC_SYSTEM_GetDisplayId(int *out) { if (out) *out = 0; return 0; }
int LGNC_SYSTEM_GetDisplayResolution(int *w, int *h) {
    if (w) *w = s_lgnc_w;
    if (h) *h = s_lgnc_h;
    return 0;
}
int LGNC_SYSTEM_GetCursorPosition(short *x, short *y) {
    if (x) *x = (short)s_ptr_x;
    if (y) *y = (short)s_ptr_y;
    return 0;
}
int LGNC_SYSTEM_SetCursorShape(int shape, int x, int y) { return 0; }
int LGNC_SYSTEM_SetCustomCursor(void *data, int w, int h, int hx, int hy) { return 0; }
int LGNC_SYSTEM_GetVolume(int *vol) { if (vol) *vol = 50; return 0; }
int LGNC_SYSTEM_SetVolume(int vol) { return 0; }
int LGNC_SYSTEM_GetMute(int *mute) { if (mute) *mute = 0; return 0; }
int LGNC_SYSTEM_SetMute(int mute) { return 0; }
int LGNC_S3D_GetGraphicMode(void *mode) { return 0; }
int LGNC_S3D_GetVideoMode(void *mode) { return 0; }
int LGNC_S3D_SetVideoModeOnly(int mode) { return 0; }
int LGNC_S3D_SetAllMode(int mode) { return 0; }
int LGNC_MEDIA_Initialize(void *cb, int *handle) { if (handle) *handle = 1; return 0; }
int LGNC_MEDIA_Finalize(void) { return NDL_MediaUnload(); }
int LGNC_MEDIA_PlayFile(int handle, const char *path, int loop) { return NDL_MediaLoad(path, NULL); }
int LGNC_MEDIA_PlayBuffer(unsigned int handle, const void *buf, size_t size) { return 0; }
int LGNC_MEDIA_Stop(unsigned int handle) { return NDL_MediaUnload(); }
int LGNC_DIRECTAUDIO_Open(void *info, int *handle) { if (handle) *handle = 1; return 0; }
int LGNC_DIRECTAUDIO_Close(int handle) { return 0; }
int LGNC_DIRECTAUDIO_Play(int handle, const void *buf, int size) { return 0; }
int LGNC_MRCU_GetDeviceID(int *id) { if (id) *id = 0; return 0; }
int LGNC_APPDRM_CheckEncrypted(const char *path, int *encrypted) { if (encrypted) *encrypted = 0; return 0; }
int LGNC_APPDRM_open(const char *path, int flags, ...) { return open(path, flags); }
int LGNC_APPDRM_read(int fd, void *buf, size_t count) { return (int)read(fd, buf, count); }
int LGNC_APPDRM_close(int fd) { return close(fd); }
int LGNC_IAP_GetAppID(char *out) { if (out) out[0] = '\0'; return 0; }
int LGNC_IAP_GetDeviceID(char *out) { if (out) out[0] = '\0'; return 0; }
int LGNC_IAP_GetUserID(char *out) { if (out) out[0] = '\0'; return 0; }
int LGNC_IAP_CheckPaid(void *a, void *b) { return 0; }

void *LGNC_eglGetDisplay(void *native_display) {
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        SDL_SetHint("SDL_AUDIODRIVER", "android");
        SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS", "0");
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS);
    }
    return (void *)1;
}

int LGNC_eglInitialize(void *dpy, int *major, int *minor) {
    if (major) *major = 1;
    if (minor) *minor = 4;
    return 1;
}

int LGNC_eglGetConfigs(void *dpy, void **configs, int config_size, int *num_config) {
    if (configs && config_size > 0) configs[0] = (void *)1;
    if (num_config) *num_config = 1;
    return 1;
}

int LGNC_eglChooseConfig(void *dpy, const int *attrib_list, void **configs, int config_size, int *num_config) {
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != 0x3038 /* EGL_NONE */; i += 2) {
            if (attrib_list[i] == 0x3040 /* EGL_RENDERABLE_TYPE */) {
                if (attrib_list[i + 1] & 4) s_lgnc_es_ver = 2;
                else if (attrib_list[i + 1] & 1) s_lgnc_es_ver = 1;
            }
        }
    }
    if (configs && config_size > 0) configs[0] = (void *)1;
    if (num_config) *num_config = 1;
    return 1;
}

int LGNC_eglGetConfigAttrib(void *dpy, void *config, int attribute, int *value) {
    if (value) {
        if (attribute == 0x3040) *value = (s_lgnc_es_ver == 1) ? 1 : 4;
        else *value = 8;
    }
    return 1;
}

void *LGNC_eglCreateWindowSurface(void *dpy, void *config, void *win, const int *attrib_list) {
    if (!s_lgnc_window) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, s_lgnc_es_ver);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, (s_lgnc_es_ver == 1) ? 1 : 0);
        s_lgnc_window = SDL_CreateWindow("LGNC", 0, 0, s_lgnc_w, s_lgnc_h,
                                         SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
        LOGI("LGNC_eglCreateWindowSurface: created %dx%d window (GLES %d)", s_lgnc_w, s_lgnc_h, s_lgnc_es_ver);
    }
    return (void *)s_lgnc_window;
}

void *LGNC_eglCreateContext(void *dpy, void *config, void *share_context, const int *attrib_list) {
    int req_ver = s_lgnc_es_ver;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != 0x3038 /* EGL_NONE */; i += 2) {
            if (attrib_list[i] == 0x3098 /* EGL_CONTEXT_CLIENT_VERSION */) {
                req_ver = attrib_list[i + 1];
            }
        }
    }
    if (s_lgnc_window && req_ver != s_lgnc_es_ver) {
        if (s_lgnc_glctx) { SDL_GL_DeleteContext(s_lgnc_glctx); s_lgnc_glctx = NULL; }
        SDL_DestroyWindow(s_lgnc_window);
        s_lgnc_window = NULL;
        s_lgnc_es_ver = req_ver;
    } else {
        s_lgnc_es_ver = req_ver;
    }
    if (!s_lgnc_window) {
        LGNC_eglCreateWindowSurface(dpy, config, NULL, NULL);
    }
    if (!s_lgnc_glctx && s_lgnc_window) {
        s_lgnc_glctx = SDL_GL_CreateContext(s_lgnc_window);
        LOGI("LGNC_eglCreateContext: GLES %d ctx=%p", s_lgnc_es_ver, s_lgnc_glctx);
    }
    return (void *)s_lgnc_glctx;
}

int LGNC_eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx) {
    if (s_lgnc_window && s_lgnc_glctx) {
        SDL_GL_MakeCurrent(s_lgnc_window, s_lgnc_glctx);
    }
    return 1;
}

int LGNC_eglQuerySurface(void *dpy, void *surface, int attribute, int *value) {
    if (value) {
        if (attribute == 0x3057) *value = s_lgnc_w; /* EGL_WIDTH */
        else if (attribute == 0x3056) *value = s_lgnc_h; /* EGL_HEIGHT */
        else *value = 0;
    }
    return 1;
}

int LGNC_eglSwapBuffers(void *dpy, void *surface) {
    if (s_lgnc_window) SDL_GL_SwapWindow(s_lgnc_window);
    return 1;
}

int LGNC_eglSwapInterval(void *dpy, int interval) {
    SDL_GL_SetSwapInterval(interval);
    return 1;
}

int LGNC_eglDestroySurface(void *dpy, void *surface) { return 1; }
int LGNC_eglDestroyContext(void *dpy, void *ctx) { return 1; }
int LGNC_eglTerminate(void *dpy) { return 1; }
int LGNC_eglGetError(void) { return 0x3000; /* EGL_SUCCESS */ }

/* Virtual V4L2 Camera (/dev/video0 -> Android Camera) */
#define V4L2_FAKE_FD 0x70001001
static pthread_mutex_t s_cam_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_cam_cond = PTHREAD_COND_INITIALIZER;
static int s_cam_open = 0;
static int s_cam_streaming = 0;
static int s_cam_w = 640;
static int s_cam_h = 480;
static unsigned char *s_cam_yuyv_buf = NULL;
static size_t s_cam_buf_len = 0;
static int s_cam_frame_ready = 0;

static void jni_call_camera(const char *method, int w, int h) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = env ? (jobject)SDL_AndroidGetActivity() : NULL;
    if (env && act) {
        jclass cls = (*env)->GetObjectClass(env, act);
        if (cls) {
            if (strcmp(method, "startV4L2Camera") == 0) {
                jmethodID mid = (*env)->GetStaticMethodID(env, cls, "startV4L2Camera", "(II)I");
                if (mid) (*env)->CallStaticIntMethod(env, cls, mid, (jint)w, (jint)h);
            } else {
                jmethodID mid = (*env)->GetStaticMethodID(env, cls, "stopV4L2Camera", "()V");
                if (mid) (*env)->CallStaticVoidMethod(env, cls, mid);
            }
            (*env)->DeleteLocalRef(env, cls);
        }
        (*env)->DeleteLocalRef(env, act);
    }
}

void webos_update_camera_frame(const unsigned char *nv21, int src_w, int src_h) {
    if (!nv21 || src_w <= 0 || src_h <= 0) return;
    pthread_mutex_lock(&s_cam_mutex);
    if (!s_cam_open || !s_cam_yuyv_buf) {
        pthread_mutex_unlock(&s_cam_mutex);
        return;
    }
    int dst_w = s_cam_w;
    int dst_h = s_cam_h;
    const unsigned char *y_plane = nv21;
    const unsigned char *vu_plane = nv21 + src_w * src_h;

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;
        int uv_row = (sy >> 1) * src_w;
        int y_row = sy * src_w;
        unsigned char *out = s_cam_yuyv_buf + (y * dst_w * 2);
        for (int x = 0; x < dst_w; x += 2) {
            int sx0 = (x * src_w) / dst_w;
            int sx1 = ((x + 1) * src_w) / dst_w;
            unsigned char y0 = y_plane[y_row + sx0];
            unsigned char y1 = y_plane[y_row + sx1];
            int uv_idx = uv_row + (sx0 & ~1);
            unsigned char v = vu_plane[uv_idx];
            unsigned char u = vu_plane[uv_idx + 1];
            out[0] = y0;
            out[1] = u;
            out[2] = y1;
            out[3] = v;
            out += 4;
        }
    }
    s_cam_frame_ready = 1;
    pthread_cond_broadcast(&s_cam_cond);
    pthread_mutex_unlock(&s_cam_mutex);
}

int v4l2_open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (pathname && strncmp(pathname, "/dev/video", 10) == 0) {
        pthread_mutex_lock(&s_cam_mutex);
        s_cam_open = 1;
        s_cam_streaming = 0;
        s_cam_frame_ready = 0;
        if (!s_cam_yuyv_buf) {
            s_cam_buf_len = (size_t)s_cam_w * (size_t)s_cam_h * 2;
            s_cam_yuyv_buf = (unsigned char *)calloc(1, s_cam_buf_len);
        }
        pthread_mutex_unlock(&s_cam_mutex);
        LOGI("v4l2_open(%s) -> virtual camera fd", pathname);
        return V4L2_FAKE_FD;
    }
    return (flags & O_CREAT) ? open(pathname, flags, mode) : open(pathname, flags);
}

int v4l2_close(int fd) {
    if (fd == V4L2_FAKE_FD) {
        jni_call_camera("stopV4L2Camera", 0, 0);
        pthread_mutex_lock(&s_cam_mutex);
        s_cam_open = 0;
        s_cam_streaming = 0;
        pthread_cond_broadcast(&s_cam_cond);
        pthread_mutex_unlock(&s_cam_mutex);
        return 0;
    }
    return close(fd);
}

int v4l2_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    if (fd != V4L2_FAKE_FD) {
        return ioctl(fd, (int)request, arg);
    }

    switch (request) {
        case VIDIOC_QUERYCAP: {
            struct v4l2_capability *cap = (struct v4l2_capability *)arg;
            if (!cap) { errno = EINVAL; return -1; }
            memset(cap, 0, sizeof(*cap));
            strcpy((char *)cap->driver, "android_v4l2");
            strcpy((char *)cap->card, "Android Camera");
            strcpy((char *)cap->bus_info, "platform:android");
            cap->version = 0x00010000;
            cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
            return 0;
        }
        case VIDIOC_CROPCAP: {
            struct v4l2_cropcap *cc = (struct v4l2_cropcap *)arg;
            if (!cc) { errno = EINVAL; return -1; }
            memset(cc, 0, sizeof(*cc));
            cc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            cc->bounds.width = s_cam_w;
            cc->bounds.height = s_cam_h;
            cc->defrect = cc->bounds;
            cc->pixelaspect.numerator = 1;
            cc->pixelaspect.denominator = 1;
            return 0;
        }
        case VIDIOC_S_FMT: {
            struct v4l2_format *fmt = (struct v4l2_format *)arg;
            if (!fmt) { errno = EINVAL; return -1; }
            pthread_mutex_lock(&s_cam_mutex);
            if (fmt->fmt.pix.width > 0 && fmt->fmt.pix.height > 0) {
                s_cam_w = (int)fmt->fmt.pix.width;
                s_cam_h = (int)fmt->fmt.pix.height;
            }
            s_cam_buf_len = (size_t)s_cam_w * (size_t)s_cam_h * 2;
            free(s_cam_yuyv_buf);
            s_cam_yuyv_buf = (unsigned char *)calloc(1, s_cam_buf_len);
            fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
            fmt->fmt.pix.field = V4L2_FIELD_NONE;
            fmt->fmt.pix.bytesperline = (uint32_t)(s_cam_w * 2);
            fmt->fmt.pix.sizeimage = (uint32_t)s_cam_buf_len;
            pthread_mutex_unlock(&s_cam_mutex);
            return 0;
        }
        case VIDIOC_REQBUFS: {
            struct v4l2_requestbuffers *req = (struct v4l2_requestbuffers *)arg;
            if (!req) { errno = EINVAL; return -1; }
            if (req->count > 1) req->count = 1;
            return 0;
        }
        case VIDIOC_QUERYBUF: {
            struct v4l2_buffer *buf = (struct v4l2_buffer *)arg;
            if (!buf) { errno = EINVAL; return -1; }
            buf->length = (uint32_t)s_cam_buf_len;
            buf->m.offset = 0;
            buf->bytesused = (uint32_t)s_cam_buf_len;
            return 0;
        }
        case VIDIOC_QBUF:
            return 0;
        case VIDIOC_DQBUF: {
            struct v4l2_buffer *buf = (struct v4l2_buffer *)arg;
            if (!buf) { errno = EINVAL; return -1; }
            pthread_mutex_lock(&s_cam_mutex);
            if (!s_cam_frame_ready && s_cam_open && s_cam_streaming) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 50000000L;
                if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
                pthread_cond_timedwait(&s_cam_cond, &s_cam_mutex, &ts);
            }
            s_cam_frame_ready = 0;
            buf->index = 0;
            buf->bytesused = (uint32_t)s_cam_buf_len;
            pthread_mutex_unlock(&s_cam_mutex);
            return 0;
        }
        case VIDIOC_STREAMON:
            pthread_mutex_lock(&s_cam_mutex);
            s_cam_streaming = 1;
            pthread_mutex_unlock(&s_cam_mutex);
            LOGI("v4l2_ioctl(VIDIOC_STREAMON %dx%d)", s_cam_w, s_cam_h);
            jni_call_camera("startV4L2Camera", s_cam_w, s_cam_h);
            return 0;
        case VIDIOC_STREAMOFF:
            pthread_mutex_lock(&s_cam_mutex);
            s_cam_streaming = 0;
            pthread_mutex_unlock(&s_cam_mutex);
            jni_call_camera("stopV4L2Camera", 0, 0);
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int v4l2_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    if (nfds > V4L2_FAKE_FD || (s_cam_open && nfds == V4L2_FAKE_FD + 1)) {
        return 1;
    }
    return select(nfds, (fd_set *)readfds, (fd_set *)writefds, (fd_set *)exceptfds, (struct timeval *)timeout);
}

void *v4l2_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    if (fd == V4L2_FAKE_FD) {
        pthread_mutex_lock(&s_cam_mutex);
        if (!s_cam_yuyv_buf && length > 0) {
            s_cam_buf_len = length;
            s_cam_yuyv_buf = (unsigned char *)calloc(1, length);
        }
        void *p = s_cam_yuyv_buf;
        pthread_mutex_unlock(&s_cam_mutex);
        return p ? p : MAP_FAILED;
    }
    return mmap(addr, length, prot, flags, fd, (off_t)offset);
}

int v4l2_munmap(void *addr, size_t length) {
    if (addr && addr == s_cam_yuyv_buf) {
        return 0;
    }
    return munmap(addr, length);
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
