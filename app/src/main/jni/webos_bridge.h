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

/* LG HAL */
const char *lg_hal_m3g_OpenGLES_getHint(const char *hint);
int lg_hal_m3g_stereo3d_isUserAllowed(void);
int lg_hal_m3g_stereo3d_getModeCount(void);

/* Curl Stubs */
void *curl_easy_init(void);
int curl_easy_setopt(void *curl, int option, ...);
int curl_easy_perform(void *curl);
void curl_easy_cleanup(void *curl);
char *curl_easy_escape(void *curl, const char *string, int length);
void curl_free(void *p);
void *curl_slist_append(void *list, const char *string);
void curl_slist_free_all(void *list);
int curl_global_init(long flags);

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
