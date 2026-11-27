#ifndef WEBOS_BRIDGE_H
#define WEBOS_BRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Time */
struct timeb {
    time_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
};
int ftime(struct timeb *tp);

/* Glib Base64 */
char *g_base64_encode(const unsigned char *data, size_t len);
unsigned char *g_base64_decode(const char *text, size_t *out_len);
void g_free(void *ptr);

/* NDL (Native Development Layer) webOS */
typedef int (*NDL_Callback)(void *params, void *user_data);
int NDL_ServiceInit(int timeout);
int NDL_ServiceQuit(int reason);
int NDL_ServiceCall(const char *service, const char *params);
int NDL_ServiceCallWithCallback(const char *service, const char *params, NDL_Callback cb, void *user_data, int async);
const char *NDL_ServiceGetError(void);
int NDL_UnregisterServiceCallback(NDL_Callback cb);

/* NDL Media */
int NDL_MediaInit(const char *appId);
int NDL_MediaQuit(void);
int NDL_MediaLoad(const char *mediaInfo, void *cb);
int NDL_MediaUnload(void);
int NDL_MediaPlay(void);
int NDL_MediaPause(void);
int NDL_MediaSeekTo(int posMs);
int NDL_MediaSubscribeEvents(void *cb, void *userData);
const char *NDL_MediaGetError(void);

/* Magic Remote / Sensor bridge */
void webos_update_sensors(float gx, float gy, float gz, float ax, float ay, float az, float q0, float q1, float q2, float q3);
void webos_update_pointer(int x, int y);

/* LG HAL */
const char *lg_hal_m3g_OpenGLES_getHint(const char *hint);
int lg_hal_m3g_stereo3d_isUserAllowed(void);
int lg_hal_m3g_stereo3d_getModeCount(void);

/* LGNC (NetCast) */
int LGNC_SYSTEM_Initialize(int argc, char **argv, void *cb);
int LGNC_SYSTEM_Finalize(void);
const char *LGNC_SYSTEM_GetLanguage(void);
const char *LGNC_SYSTEM_GetSaveDirectory(void);
int LGNC_SYSTEM_SetResolution(int w, int h);
int LGNC_SYSTEM_GetDisplayId(void);
int LGNC_SYSTEM_SetCursorShape(int shape, int x, int y);
int LGNC_SYSTEM_SetCustomCursor(void *data, int w, int h, int hx, int hy);
int LGNC_S3D_GetGraphicMode(void *mode);
int LGNC_S3D_SetAllMode(int mode);
int LGNC_MEDIA_Initialize(void);
int LGNC_MEDIA_Finalize(void);
int LGNC_MEDIA_PlayFile(const char *path, int loop);
int LGNC_MEDIA_PlayBuffer(const void *buf, int size, int loop);
int LGNC_MEDIA_Stop(void);
void *LGNC_eglGetDisplay(void *native_display);
int LGNC_eglInitialize(void *dpy, int *major, int *minor);
int LGNC_eglGetConfigs(void *dpy, void **configs, int config_size, int *num_config);
int LGNC_eglChooseConfig(void *dpy, const int *attrib_list, void **configs, int config_size, int *num_config);
int LGNC_eglGetConfigAttrib(void *dpy, void *config, int attribute, int *value);
void *LGNC_eglCreateWindowSurface(void *dpy, void *config, void *win, const int *attrib_list);
void *LGNC_eglCreateContext(void *dpy, void *config, void *share_context, const int *attrib_list);
int LGNC_eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx);
int LGNC_eglQuerySurface(void *dpy, void *surface, int attribute, int *value);
int LGNC_eglSwapBuffers(void *dpy, void *surface);
int LGNC_eglSwapInterval(void *dpy, int interval);
int LGNC_eglDestroySurface(void *dpy, void *surface);
int LGNC_eglDestroyContext(void *dpy, void *ctx);
int LGNC_eglTerminate(void *dpy);
int LGNC_eglGetError(void);

/* Curl Stubs */
void *curl_easy_init(void);
int curl_easy_setopt(void *curl, int option, ...);
int curl_easy_perform(void *curl);
void curl_easy_cleanup(void *curl);
const char *curl_easy_strerror(int code);
char *curl_easy_escape(void *curl, const char *string, int length);
void curl_free(void *p);
void *curl_slist_append(void *list, const char *string);
void curl_slist_free_all(void *list);
int curl_global_init(long flags);
void curl_global_cleanup(void);

/* JSON-C API implemented with cJSON */
void *json_tokener_parse(const char *str);
void *json_object_object_get(void *jso, const char *key);
void *json_object_array_get_idx(void *jso, int idx);
int json_object_get_boolean(void *jso);
int json_object_put(void *jso);
int json_object_get_int(void *jso);
const char *json_object_get_string(void *jso);
int json_object_array_length(void *jso);
double json_object_get_double(void *jso);
int json_object_get_type(void *jso);

#ifdef __cplusplus
}
#endif

#endif /* WEBOS_BRIDGE_H */
