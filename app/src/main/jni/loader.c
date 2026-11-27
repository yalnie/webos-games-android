#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <android/log.h>
#include <GLES2/gl2.h>
#include <jni.h>

#include "SDL.h"
#include "webos_bridge.h"

#define TAG "MainstreamLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static char g_game_dir[1024] = {0};
static char g_game_main[256] = "mainstream";
static int g_game_w = 1920;
static int g_game_h = 1080;

JNIEXPORT void JNICALL Java_com_ru_lgerp_mainstream_MainActivity_nativeSetGameConfig(
        JNIEnv *env, jclass cls, jstring gameDir, jstring gameMain, jint width, jint height) {
    if (gameDir) {
        const char *s = (*env)->GetStringUTFChars(env, gameDir, NULL);
        if (s) {
            strncpy(g_game_dir, s, sizeof(g_game_dir) - 1);
            g_game_dir[sizeof(g_game_dir) - 1] = '\0';
            (*env)->ReleaseStringUTFChars(env, gameDir, s);
        }
    }
    if (gameMain) {
        const char *s = (*env)->GetStringUTFChars(env, gameMain, NULL);
        if (s) {
            strncpy(g_game_main, s, sizeof(g_game_main) - 1);
            g_game_main[sizeof(g_game_main) - 1] = '\0';
            (*env)->ReleaseStringUTFChars(env, gameMain, s);
        }
    }
    if (width > 0) g_game_w = (int)width;
    if (height > 0) g_game_h = (int)height;
    LOGI("Configured game: dir=%s main=%s res=%dx%d", g_game_dir, g_game_main, g_game_w, g_game_h);
}

JNIEXPORT void JNICALL Java_com_ru_lgerp_mainstream_MainActivity_nativeUpdateSensors(
        JNIEnv *env, jclass cls,
        jfloat gx, jfloat gy, jfloat gz,
        jfloat ax, jfloat ay, jfloat az,
        jfloat q0, jfloat q1, jfloat q2, jfloat q3) {
    webos_update_sensors(gx, gy, gz, ax, ay, az, q0, q1, q2, q3);
}

static FILE *g_stderr_ptr = NULL;
static FILE *g_stdout_ptr = NULL;
static void *g_libstdcpp = NULL;
static void *g_libstdcpp_empty_rep = NULL;
static void *g_app_empty_rep = NULL;

/* Custom symbol resolution table */
struct SymbolOverride {
    const char *name;
    void *func;
};

static void safe_free(void *ptr) {
    if (!ptr) return;
    /* Never free static ELF .bss/.data pointers (e.g. R_ARM_COPY _S_empty_rep_storage) */
    if ((uintptr_t)ptr < 0x02000000) return;
    if (g_app_empty_rep && ptr == g_app_empty_rep) return;
    if (g_libstdcpp_empty_rep && ptr == g_libstdcpp_empty_rep) return;
    free(ptr);
}

static int patch_gnustl_cb(struct dl_phdr_info *info, size_t size, void *data) {
    if (!info->dlpi_name || !strstr(info->dlpi_name, "libgnustl_shared.so")) return 0;
    uintptr_t base = info->dlpi_addr;
    Elf32_Dyn *dyn = NULL;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (Elf32_Dyn *)(base + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;
    Elf32_Rel *jmprel = NULL;
    size_t pltrelsz = 0;
    for (; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
            case DT_SYMTAB: symtab = (Elf32_Sym *)(base + dyn->d_un.d_ptr); break;
            case DT_STRTAB: strtab = (const char *)(base + dyn->d_un.d_ptr); break;
            case DT_JMPREL: jmprel = (Elf32_Rel *)(base + dyn->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = dyn->d_un.d_val; break;
            default: break;
        }
    }
    if (!symtab || !strtab || !jmprel) return 0;

    size_t count = pltrelsz / sizeof(Elf32_Rel);
    for (size_t i = 0; i < count; i++) {
        uint32_t sym_idx = ELF32_R_SYM(jmprel[i].r_info);
        const char *name = strtab + symtab[sym_idx].st_name;
        if (strcmp(name, "free") == 0) {
            uintptr_t *got_entry = (uintptr_t *)(base + jmprel[i].r_offset);
            uintptr_t page = ((uintptr_t)got_entry) & ~0xFFFU;
            mprotect((void *)page, 4096, PROT_READ | PROT_WRITE);
            *got_entry = (uintptr_t)safe_free;
            LOGI("Patched libgnustl_shared.so free@GOT (%p) -> safe_free", got_entry);
        }
    }
    return 1;
}

static void *dummy_stub(void) {
    LOGW("Called unhandled stub!");
    return NULL;
}

static int stub_isnan(double x) {
    return isnan(x);
}

static void stub_sincosf(float x, float *s, float *c) {
    if (s) *s = sinf(x);
    if (c) *c = cosf(x);
}

static void stub_sincos(double x, double *s, double *c) {
    if (s) *s = sin(x);
    if (c) *c = cos(x);
}

static int *override_errno_location(void) {
    return &errno;
}

static void fix_empty_rep(void *str) {
    if (str && g_app_empty_rep && g_libstdcpp_empty_rep) {
        char *p = *(char **)str;
        if (p == (char *)g_app_empty_rep + 12) {
            *(char **)str = (char *)g_libstdcpp_empty_rep + 12;
        }
    }
}

typedef void (*real_string_rep_destroy_t)(void *rep, void *alloc);
static real_string_rep_destroy_t s_real_string_rep_destroy = NULL;
static real_string_rep_destroy_t s_real_string_rep_dispose = NULL;

static void override_string_rep_destroy(void *rep, void *alloc) {
    if (!rep || rep == g_libstdcpp_empty_rep || rep == g_app_empty_rep) {
        return;
    }
    if (!s_real_string_rep_destroy && g_libstdcpp) {
        s_real_string_rep_destroy = (real_string_rep_destroy_t)dlsym(g_libstdcpp, "_ZNSs4_Rep10_M_destroyERKSaIcE");
    }
    if (s_real_string_rep_destroy) {
        s_real_string_rep_destroy(rep, alloc);
    }
}

static void override_string_rep_dispose(void *rep, void *alloc) {
    if (!rep || rep == g_libstdcpp_empty_rep || rep == g_app_empty_rep) {
        return;
    }
    if (!s_real_string_rep_dispose && g_libstdcpp) {
        s_real_string_rep_dispose = (real_string_rep_destroy_t)dlsym(g_libstdcpp, "_ZNSs4_Rep10_M_disposeERKSaIcE");
    }
    if (s_real_string_rep_dispose) {
        s_real_string_rep_dispose(rep, alloc);
    }
}

typedef void (*real_string_mutate_t)(void *str, size_t pos, size_t len1, size_t len2);
static real_string_mutate_t s_real_string_mutate = NULL;
static void override_string_mutate(void *str, size_t pos, size_t len1, size_t len2) {
    fix_empty_rep(str);
    if (!s_real_string_mutate && g_libstdcpp) {
        s_real_string_mutate = (real_string_mutate_t)dlsym(g_libstdcpp, "_ZNSs9_M_mutateEjjj");
    }
    if (s_real_string_mutate) {
        s_real_string_mutate(str, pos, len1, len2);
    }
}

typedef void *(*real_string_assign_cstr_t)(void *str, const char *s, size_t n);
static real_string_assign_cstr_t s_real_string_assign_cstr = NULL;
static void *override_string_assign_cstr(void *str, const char *s, size_t n) {
    fix_empty_rep(str);
    if (!s_real_string_assign_cstr && g_libstdcpp) {
        s_real_string_assign_cstr = (real_string_assign_cstr_t)dlsym(g_libstdcpp, "_ZNSs6assignEPKcj");
    }
    return s_real_string_assign_cstr ? s_real_string_assign_cstr(str, s, n) : str;
}

typedef void *(*real_string_assign_str_t)(void *str, const void *other);
static real_string_assign_str_t s_real_string_assign_str = NULL;
static void *override_string_assign_str(void *str, const void *other) {
    fix_empty_rep(str);
    if (!s_real_string_assign_str && g_libstdcpp) {
        s_real_string_assign_str = (real_string_assign_str_t)dlsym(g_libstdcpp, "_ZNSs6assignERKSs");
    }
    return s_real_string_assign_str ? s_real_string_assign_str(str, other) : str;
}

typedef void *(*real_string_append_cstr_t)(void *str, const char *s, size_t n);
static real_string_append_cstr_t s_real_string_append_cstr = NULL;
static void *override_string_append_cstr(void *str, const char *s, size_t n) {
    fix_empty_rep(str);
    if (!s_real_string_append_cstr && g_libstdcpp) {
        s_real_string_append_cstr = (real_string_append_cstr_t)dlsym(g_libstdcpp, "_ZNSs6appendEPKcj");
    }
    return s_real_string_append_cstr ? s_real_string_append_cstr(str, s, n) : str;
}

typedef void *(*real_string_append_str_t)(void *str, const void *other);
static real_string_append_str_t s_real_string_append_str = NULL;
static void *override_string_append_str(void *str, const void *other) {
    fix_empty_rep(str);
    if (!s_real_string_append_str && g_libstdcpp) {
        s_real_string_append_str = (real_string_append_str_t)dlsym(g_libstdcpp, "_ZNSs6appendERKSs");
    }
    return s_real_string_append_str ? s_real_string_append_str(str, other) : str;
}

typedef void *(*real_string_cstr_t)(void *str, const char *s);
static real_string_cstr_t s_real_string_assign_cstr0 = NULL;
static void *override_string_assign_cstr0(void *str, const char *s) {
    fix_empty_rep(str);
    if (!s_real_string_assign_cstr0 && g_libstdcpp) {
        s_real_string_assign_cstr0 = (real_string_cstr_t)dlsym(g_libstdcpp, "_ZNSs6assignEPKc");
    }
    return s_real_string_assign_cstr0 ? s_real_string_assign_cstr0(str, s) : str;
}

static real_string_cstr_t s_real_string_append_cstr0 = NULL;
static void *override_string_append_cstr0(void *str, const char *s) {
    fix_empty_rep(str);
    if (!s_real_string_append_cstr0 && g_libstdcpp) {
        s_real_string_append_cstr0 = (real_string_cstr_t)dlsym(g_libstdcpp, "_ZNSs6appendEPKc");
    }
    return s_real_string_append_cstr0 ? s_real_string_append_cstr0(str, s) : str;
}

static real_string_cstr_t s_real_string_op_assign_cstr = NULL;
static void *override_string_op_assign_cstr(void *str, const char *s) {
    fix_empty_rep(str);
    if (!s_real_string_op_assign_cstr && g_libstdcpp) {
        s_real_string_op_assign_cstr = (real_string_cstr_t)dlsym(g_libstdcpp, "_ZNSsaSEPKc");
    }
    return s_real_string_op_assign_cstr ? s_real_string_op_assign_cstr(str, s) : str;
}

static real_string_assign_str_t s_real_string_op_assign_str = NULL;
static void *override_string_op_assign_str(void *str, const void *other) {
    fix_empty_rep(str);
    if (!s_real_string_op_assign_str && g_libstdcpp) {
        s_real_string_op_assign_str = (real_string_assign_str_t)dlsym(g_libstdcpp, "_ZNSsaSERKSs");
    }
    return s_real_string_op_assign_str ? s_real_string_op_assign_str(str, other) : str;
}

typedef void *(*real_string_replace_t)(void *str, size_t pos, size_t n1, const char *s, size_t n2);
static real_string_replace_t s_real_string_replace = NULL;
static void *override_string_replace(void *str, size_t pos, size_t n1, const char *s, size_t n2) {
    fix_empty_rep(str);
    if (!s_real_string_replace && g_libstdcpp) {
        s_real_string_replace = (real_string_replace_t)dlsym(g_libstdcpp, "_ZNSs7replaceEjjPKcj");
    }
    return s_real_string_replace ? s_real_string_replace(str, pos, n1, s, n2) : str;
}

typedef void (*real_string_leak_hard_t)(void *str);
static real_string_leak_hard_t s_real_string_leak_hard = NULL;
static void override_string_leak_hard(void *str) {
    fix_empty_rep(str);
    if (!s_real_string_leak_hard && g_libstdcpp) {
        s_real_string_leak_hard = (real_string_leak_hard_t)dlsym(g_libstdcpp, "_ZNSs12_M_leak_hardEv");
    }
    if (s_real_string_leak_hard) {
        s_real_string_leak_hard(str);
    }
}

typedef void (*real_basic_ios_init_t)(void *ios, void *sb);
static real_basic_ios_init_t s_real_basic_ios_init = NULL;

typedef void (*locale_ctor_t)(void *loc);
static locale_ctor_t s_locale_ctor = NULL;

static void override_basic_ios_init(void *ios, void *sb) {
    if (ios) {
        void **loc_ptr = (void **)((char *)ios + 0x6c);
        if (!*loc_ptr) {
            if (!s_locale_ctor && g_libstdcpp) {
                s_locale_ctor = (locale_ctor_t)dlsym(g_libstdcpp, "_ZNSt6localeC1Ev");
            }
            if (s_locale_ctor) {
                s_locale_ctor(loc_ptr);
            }
        }
    }
    if (!s_real_basic_ios_init && g_libstdcpp) {
        s_real_basic_ios_init = (real_basic_ios_init_t)dlsym(g_libstdcpp, "_ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E");
    }
    if (s_real_basic_ios_init) {
        s_real_basic_ios_init(ios, sb);
    }
}

/* Bridge glibc vs bionic std::basic_filebuf<char> _M_file offset mismatch */
static pthread_mutex_t s_fb_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *s_open_filebufs[32] = {0};

static void track_open_filebuf(void *fb) {
    pthread_mutex_lock(&s_fb_mutex);
    for (int i = 0; i < 32; i++) {
        if (s_open_filebufs[i] == fb) { pthread_mutex_unlock(&s_fb_mutex); return; }
    }
    for (int i = 0; i < 32; i++) {
        if (!s_open_filebufs[i]) { s_open_filebufs[i] = fb; break; }
    }
    pthread_mutex_unlock(&s_fb_mutex);
}

static void untrack_open_filebuf(void *fb) {
    pthread_mutex_lock(&s_fb_mutex);
    for (int i = 0; i < 32; i++) {
        if (s_open_filebufs[i] == fb) s_open_filebufs[i] = NULL;
    }
    pthread_mutex_unlock(&s_fb_mutex);
}

static int is_tracked_filebuf_member(void *bf) {
    pthread_mutex_lock(&s_fb_mutex);
    for (int i = 0; i < 32; i++) {
        if (s_open_filebufs[i]) {
            uintptr_t fb = (uintptr_t)s_open_filebufs[i];
            uintptr_t p = (uintptr_t)bf;
            if (p >= fb && p < fb + 128) {
                pthread_mutex_unlock(&s_fb_mutex);
                return 1;
            }
        }
    }
    pthread_mutex_unlock(&s_fb_mutex);
    return 0;
}

typedef void *(*real_filebuf_open_t)(void *fb, const char *s, int mode);
static real_filebuf_open_t s_real_filebuf_open = NULL;
static void *override_filebuf_open(void *fb, const char *s, int mode) {
    if (!s_real_filebuf_open && g_libstdcpp) {
        s_real_filebuf_open = (real_filebuf_open_t)dlsym(g_libstdcpp, "_ZNSt13basic_filebufIcSt11char_traitsIcEE4openEPKcSt13_Ios_Openmode");
    }
    void *res = s_real_filebuf_open ? s_real_filebuf_open(fb, s, mode) : NULL;
    if (res) track_open_filebuf(fb);
    return res;
}

typedef void *(*real_filebuf_close_t)(void *fb);
static real_filebuf_close_t s_real_filebuf_close = NULL;
static void *override_filebuf_close(void *fb) {
    untrack_open_filebuf(fb);
    if (!s_real_filebuf_close && g_libstdcpp) {
        s_real_filebuf_close = (real_filebuf_close_t)dlsym(g_libstdcpp, "_ZNSt13basic_filebufIcSt11char_traitsIcEE5closeEv");
    }
    return s_real_filebuf_close ? s_real_filebuf_close(fb) : NULL;
}

typedef void (*real_filebuf_dtor_t)(void *fb);
static real_filebuf_dtor_t s_real_filebuf_dtor = NULL;
static void override_filebuf_dtor(void *fb) {
    untrack_open_filebuf(fb);
    if (!s_real_filebuf_dtor && g_libstdcpp) {
        s_real_filebuf_dtor = (real_filebuf_dtor_t)dlsym(g_libstdcpp, "_ZNSt13basic_filebufIcSt11char_traitsIcEED1Ev");
    }
    if (s_real_filebuf_dtor) s_real_filebuf_dtor(fb);
}

typedef int (*real_basic_file_is_open_t)(void *bf);
static real_basic_file_is_open_t s_real_basic_file_is_open = NULL;
static int override_basic_file_is_open(void *bf) {
    if (is_tracked_filebuf_member(bf)) return 1;
    if (!s_real_basic_file_is_open && g_libstdcpp) {
        s_real_basic_file_is_open = (real_basic_file_is_open_t)dlsym(g_libstdcpp, "_ZNKSt12__basic_fileIcE7is_openEv");
    }
    return s_real_basic_file_is_open ? s_real_basic_file_is_open(bf) : 0;
}

typedef void (*real_string_reserve_t)(void *str, size_t n);
static real_string_reserve_t s_real_string_reserve = NULL;

static void override_string_reserve(void *str, size_t n) {
    if (n >= 0x70000000) {
        LOGW("Intercepted invalid std::string::reserve(%zu), clamping to 0", n);
        return;
    }
    fix_empty_rep(str);
    if (!s_real_string_reserve && g_libstdcpp) {
        s_real_string_reserve = (real_string_reserve_t)dlsym(g_libstdcpp, "_ZNSs7reserveEj");
    }
    if (s_real_string_reserve) {
        s_real_string_reserve(str, n);
    }
}

static const uint8_t s_silent_wav[44] = {
    'R', 'I', 'F', 'F',
    36, 0, 0, 0,        /* ChunkSize: 36 + SubChunk2Size (0) */
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    16, 0, 0, 0,        /* Subchunk1Size: 16 */
    1, 0,               /* AudioFormat: 1 (PCM) */
    1, 0,               /* NumChannels: 1 (Mono) */
    0x44, 0xac, 0, 0,   /* SampleRate: 44100 */
    0x88, 0x58, 0x01, 0,/* ByteRate: 44100 * 1 * 2 = 88200 */
    2, 0,               /* BlockAlign: 2 */
    16, 0,              /* BitsPerSample: 16 */
    'd', 'a', 't', 'a',
    0, 0, 0, 0          /* Subchunk2Size: 0 */
};

static FILE *override_fopen(const char *filename, const char *mode) {
    if (!filename) return NULL;
    FILE *f = fopen(filename, mode);
    if (!f && strstr(filename, ".wav")) {
        LOGW("Audio file [%s] not found, providing silent dummy WAV stream", filename);
        FILE *dummy = fopen("dummy_silent.wav", "wb");
        if (dummy) {
            fwrite(s_silent_wav, 1, sizeof(s_silent_wav), dummy);
            fclose(dummy);
            f = fopen("dummy_silent.wav", mode);
        }
    }
    return f;
}

static int s_screen_w = 0;
static int s_screen_h = 0;
static int s_vp_x = 0;
static int s_vp_y = 0;
static int s_vp_w = 0;
static int s_vp_h = 0;

static void ensure_viewport_metrics(void) {
    if (s_screen_w <= 0 || s_screen_h <= 0) {
        SDL_Window *win = SDL_GL_GetCurrentWindow();
        if (win) {
            SDL_GL_GetDrawableSize(win, &s_screen_w, &s_screen_h);
        }
    }
    if (s_screen_w > 0 && s_screen_h > 0 && s_vp_w <= 0) {
        float screen_aspect = (float)s_screen_w / (float)s_screen_h;
        float target_aspect = 16.0f / 9.0f;
        if (screen_aspect >= target_aspect) {
            /* Wider screen (e.g. 20:9 phone): pillarbox left & right */
            s_vp_h = s_screen_h;
            s_vp_w = (int)((float)s_screen_h * target_aspect);
            s_vp_x = (s_screen_w - s_vp_w) / 2;
            s_vp_y = 0;
        } else {
            /* Taller screen: letterbox top & bottom */
            s_vp_w = s_screen_w;
            s_vp_h = (int)((float)s_screen_w / target_aspect);
            s_vp_x = 0;
            s_vp_y = (s_screen_h - s_vp_h) / 2;
        }
        LOGI("Screen resolution: %dx%d -> 16:9 viewport: %dx%d at (%d, %d)",
             s_screen_w, s_screen_h, s_vp_w, s_vp_h, s_vp_x, s_vp_y);
    }
}

static void override_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    ensure_viewport_metrics();
    if (s_vp_w > 0 && s_vp_h > 0) {
        GLint nx = s_vp_x + (GLint)((float)x * s_vp_w / (float)g_game_w);
        GLint ny = s_vp_y + (GLint)((float)y * s_vp_h / (float)g_game_h);
        GLsizei nw = (GLsizei)((float)width * s_vp_w / (float)g_game_w);
        GLsizei nh = (GLsizei)((float)height * s_vp_h / (float)g_game_h);
        glViewport(nx, ny, nw, nh);
    } else {
        glViewport(x, y, width, height);
    }
}

static void override_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    ensure_viewport_metrics();
    if (s_vp_w > 0 && s_vp_h > 0) {
        GLint nx = s_vp_x + (GLint)((float)x * s_vp_w / (float)g_game_w);
        GLint ny = s_vp_y + (GLint)((float)y * s_vp_h / (float)g_game_h);
        GLsizei nw = (GLsizei)((float)width * s_vp_w / (float)g_game_w);
        GLsizei nh = (GLsizei)((float)height * s_vp_h / (float)g_game_h);
        glScissor(nx, ny, nw, nh);
    } else {
        glScissor(x, y, width, height);
    }
}

static void override_SDL_RenderPresent(SDL_Renderer *renderer) {
    ensure_viewport_metrics();
    if (s_vp_w > 0 && s_screen_w > s_vp_w) {
        /* Clear left and right pillarboxes with solid black */
        glEnable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        /* Left bar */
        glScissor(0, 0, s_vp_x, s_screen_h);
        glClear(GL_COLOR_BUFFER_BIT);

        /* Right bar */
        glScissor(s_vp_x + s_vp_w, 0, s_screen_w - (s_vp_x + s_vp_w), s_screen_h);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
    }
    SDL_RenderPresent(renderer);
}

/* WebOS 2.0.3 SDL_KeyboardEvent layout as expected by mainstream binary */
struct WebOS_KeyboardEvent {
    Uint32 type;        /* 0: SDL_KEYDOWN (0x300 = 768) or SDL_KEYUP (0x301 = 769) */
    Uint32 timestamp;   /* 4 */
    Uint32 windowID;    /* 8 */
    Uint32 which;       /* 12: 0 */
    Uint8 state;        /* 16: SDL_PRESSED (1) or SDL_RELEASED (0) */
    Uint8 repeat;       /* 17: MUST BE 0! */
    Uint8 padding2;     /* 18 */
    Uint8 padding3;     /* 19 */
    Uint32 scancode;    /* 20 (0x14) */
    Uint32 sym;         /* 24 (0x18) */
    Uint16 mod;         /* 28 */
    Uint32 unused;      /* 32 */
};

static int override_SDL_PollEvent(SDL_Event *event) {
    int res = SDL_PollEvent(event);
    if (!res || !event) return res;

    ensure_viewport_metrics();

    /* 1. Scale touch/mouse coordinates from centered 16:9 space to game resolution */
    if (s_vp_w > 0 && s_vp_h > 0) {
        if (event->type == SDL_MOUSEMOTION) {
            float mapped_x = ((float)event->motion.x - (float)s_vp_x) * (float)g_game_w / (float)s_vp_w;
            float mapped_y = ((float)event->motion.y - (float)s_vp_y) * (float)g_game_h / (float)s_vp_h;
            if (mapped_x < 0.0f) mapped_x = 0.0f;
            if (mapped_x > (float)g_game_w) mapped_x = (float)g_game_w;
            if (mapped_y < 0.0f) mapped_y = 0.0f;
            if (mapped_y > (float)g_game_h) mapped_y = (float)g_game_h;
            event->motion.x = (int)mapped_x;
            event->motion.y = (int)mapped_y;
            webos_update_pointer(event->motion.x, event->motion.y);
        } else if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
            float mapped_x = ((float)event->button.x - (float)s_vp_x) * (float)g_game_w / (float)s_vp_w;
            float mapped_y = ((float)event->button.y - (float)s_vp_y) * (float)g_game_h / (float)s_vp_h;
            if (mapped_x < 0.0f) mapped_x = 0.0f;
            if (mapped_x > (float)g_game_w) mapped_x = (float)g_game_w;
            if (mapped_y < 0.0f) mapped_y = 0.0f;
            if (mapped_y > (float)g_game_h) mapped_y = (float)g_game_h;
            event->button.x = (int)mapped_x;
            event->button.y = (int)mapped_y;
            webos_update_pointer(event->button.x, event->button.y);
        }
    }

    /* 2. Translate modern SDL2 KeyboardEvent to WebOS 2.0.3 layout and map keys */
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        SDL_KeyboardEvent modern = event->key;
        struct WebOS_KeyboardEvent *webos_key = (struct WebOS_KeyboardEvent *)event;

        Uint32 scancode = (Uint32)modern.keysym.scancode;
        Uint32 sym = (Uint32)modern.keysym.sym;

        /* Map Back / Escape / Backspace keys */
        if (sym == SDLK_AC_BACK || scancode == SDL_SCANCODE_AC_BACK ||
            sym == SDLK_ESCAPE || sym == SDLK_BACKSPACE) {
            LOGI("Mapping Back/Escape key event to SDLK_ESCAPE / SDL_SCANCODE_WEBOS_TV_BACK");
            scancode = 482; // SDL_SCANCODE_WEBOS_TV_BACK
            sym = 27;       // SDLK_ESCAPE
        }
        /* Map Enter / Select / DPAD_CENTER / Space */
        else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER ||
                 sym == SDLK_SELECT || sym == SDLK_SPACE) {
            sym = 13;       // SDLK_RETURN
        }
        /* Map DPAD navigation and WASD */
        else if (sym == SDLK_UP || sym == SDLK_w) {
            sym = 1073741906;
        } else if (sym == SDLK_DOWN || sym == SDLK_s) {
            sym = 1073741905;
        } else if (sym == SDLK_LEFT || sym == SDLK_a) {
            sym = 1073741904;
        } else if (sym == SDLK_RIGHT || sym == SDLK_d) {
            sym = 1073741903;
        }
        /* Map Numpad 0..9 to ASCII '0'..'9' */
        else if (sym == SDLK_KP_0) {
            sym = '0';
        } else if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9) {
            sym = '1' + (sym - SDLK_KP_1);
        }

        webos_key->type = modern.type;
        webos_key->timestamp = modern.timestamp;
        webos_key->windowID = modern.windowID;
        webos_key->which = 0;
        webos_key->state = modern.state;
        webos_key->repeat = 0;
        webos_key->padding2 = 0;
        webos_key->padding3 = 0;
        webos_key->scancode = scancode;
        webos_key->sym = sym;
        webos_key->mod = modern.keysym.mod;
        webos_key->unused = 0;
    }

    return res;
}

static int override_xstat(int ver, const char *path, struct stat *buf) {
    return stat(path, buf);
}

static int (*g_captured_main)(int, char **, char **) = NULL;
static int override_libc_start_main(
        int (*main_fn)(int, char **, char **),
        int argc, char **ubp_av,
        void (*init)(void), void (*fini)(void),
        void (*rtld_fini)(void), void *stack_end) {
    g_captured_main = main_fn;
    LOGI("Captured main_fn=%p via __libc_start_main!", main_fn);
    if (init) init();
    if (main_fn) {
        extern char **environ;
        return main_fn(argc, ubp_av, environ);
    }
    return 0;
}

static const struct SymbolOverride g_overrides[] = {
    /* NDL */
    { "NDL_ServiceInit", (void *)NDL_ServiceInit },
    { "NDL_ServiceQuit", (void *)NDL_ServiceQuit },
    { "NDL_ServiceCall", (void *)NDL_ServiceCall },
    { "NDL_ServiceCallWithCallback", (void *)NDL_ServiceCallWithCallback },
    { "NDL_ServiceGetError", (void *)NDL_ServiceGetError },
    { "NDL_UnregisterServiceCallback", (void *)NDL_UnregisterServiceCallback },

    /* NDL Media */
    { "NDL_MediaInit", (void *)NDL_MediaInit },
    { "NDL_MediaQuit", (void *)NDL_MediaQuit },
    { "NDL_MediaLoad", (void *)NDL_MediaLoad },
    { "NDL_MediaUnload", (void *)NDL_MediaUnload },
    { "NDL_MediaPlay", (void *)NDL_MediaPlay },
    { "NDL_MediaPause", (void *)NDL_MediaPause },
    { "NDL_MediaSeekTo", (void *)NDL_MediaSeekTo },
    { "NDL_MediaSubscribeEvents", (void *)NDL_MediaSubscribeEvents },
    { "NDL_MediaGetError", (void *)NDL_MediaGetError },

    /* LG HAL */
    { "lg_hal_m3g_OpenGLES_getHint", (void *)lg_hal_m3g_OpenGLES_getHint },
    { "lg_hal_m3g_stereo3d_isUserAllowed", (void *)lg_hal_m3g_stereo3d_isUserAllowed },
    { "lg_hal_m3g_stereo3d_getModeCount", (void *)lg_hal_m3g_stereo3d_getModeCount },

    /* LGNC (NetCast) */
    { "LGNC_SYSTEM_Initialize", (void *)LGNC_SYSTEM_Initialize },
    { "LGNC_SYSTEM_Finalize", (void *)LGNC_SYSTEM_Finalize },
    { "LGNC_SYSTEM_GetLanguage", (void *)LGNC_SYSTEM_GetLanguage },
    { "LGNC_SYSTEM_GetSaveDirectory", (void *)LGNC_SYSTEM_GetSaveDirectory },
    { "LGNC_SYSTEM_SetResolution", (void *)LGNC_SYSTEM_SetResolution },
    { "LGNC_SYSTEM_GetDisplayId", (void *)LGNC_SYSTEM_GetDisplayId },
    { "LGNC_SYSTEM_SetCursorShape", (void *)LGNC_SYSTEM_SetCursorShape },
    { "LGNC_SYSTEM_SetCustomCursor", (void *)LGNC_SYSTEM_SetCustomCursor },
    { "LGNC_S3D_GetGraphicMode", (void *)LGNC_S3D_GetGraphicMode },
    { "LGNC_S3D_SetAllMode", (void *)LGNC_S3D_SetAllMode },
    { "LGNC_MEDIA_Initialize", (void *)LGNC_MEDIA_Initialize },
    { "LGNC_MEDIA_Finalize", (void *)LGNC_MEDIA_Finalize },
    { "LGNC_MEDIA_PlayFile", (void *)LGNC_MEDIA_PlayFile },
    { "LGNC_MEDIA_PlayBuffer", (void *)LGNC_MEDIA_PlayBuffer },
    { "LGNC_MEDIA_Stop", (void *)LGNC_MEDIA_Stop },
    { "LGNC_eglGetDisplay", (void *)LGNC_eglGetDisplay },
    { "LGNC_eglInitialize", (void *)LGNC_eglInitialize },
    { "LGNC_eglGetConfigs", (void *)LGNC_eglGetConfigs },
    { "LGNC_eglChooseConfig", (void *)LGNC_eglChooseConfig },
    { "LGNC_eglGetConfigAttrib", (void *)LGNC_eglGetConfigAttrib },
    { "LGNC_eglCreateWindowSurface", (void *)LGNC_eglCreateWindowSurface },
    { "LGNC_eglCreateContext", (void *)LGNC_eglCreateContext },
    { "LGNC_eglMakeCurrent", (void *)LGNC_eglMakeCurrent },
    { "LGNC_eglQuerySurface", (void *)LGNC_eglQuerySurface },
    { "LGNC_eglSwapBuffers", (void *)LGNC_eglSwapBuffers },
    { "LGNC_eglSwapInterval", (void *)LGNC_eglSwapInterval },
    { "LGNC_eglDestroySurface", (void *)LGNC_eglDestroySurface },
    { "LGNC_eglDestroyContext", (void *)LGNC_eglDestroyContext },
    { "LGNC_eglTerminate", (void *)LGNC_eglTerminate },
    { "LGNC_eglGetError", (void *)LGNC_eglGetError },

    /* GLib Base64 */
    { "g_base64_encode", (void *)g_base64_encode },
    { "g_base64_decode", (void *)g_base64_decode },
    { "g_free", (void *)g_free },

    /* Curl */
    { "curl_global_init", (void *)curl_global_init },
    { "curl_global_cleanup", (void *)curl_global_cleanup },
    { "curl_easy_init", (void *)curl_easy_init },
    { "curl_easy_setopt", (void *)curl_easy_setopt },
    { "curl_easy_perform", (void *)curl_easy_perform },
    { "curl_easy_cleanup", (void *)curl_easy_cleanup },
    { "curl_easy_strerror", (void *)curl_easy_strerror },
    { "curl_easy_escape", (void *)curl_easy_escape },
    { "curl_free", (void *)curl_free },
    { "curl_slist_append", (void *)curl_slist_append },
    { "curl_slist_free_all", (void *)curl_slist_free_all },

    /* JSON */
    { "json_tokener_parse", (void *)json_tokener_parse },
    { "json_object_object_get", (void *)json_object_object_get },
    { "json_object_array_get_idx", (void *)json_object_array_get_idx },
    { "json_object_get_boolean", (void *)json_object_get_boolean },
    { "json_object_put", (void *)json_object_put },
    { "json_object_get_int", (void *)json_object_get_int },
    { "json_object_get_string", (void *)json_object_get_string },
    { "json_object_array_length", (void *)json_object_array_length },
    { "json_object_get_double", (void *)json_object_get_double },
    { "json_object_get_type", (void *)json_object_get_type },

    /* Libc / Math / C++ overrides */
    { "fopen", (void *)override_fopen },
    { "free", (void *)safe_free },
    { "ftime", (void *)ftime },
    { "__errno_location", (void *)override_errno_location },
    { "__strdup", (void *)strdup },
    { "__xstat", (void *)override_xstat },
    { "__libc_start_main", (void *)override_libc_start_main },
    { "__pthread_key_create", (void *)pthread_key_create },
    { "pthread_setcancelstate", (void *)dummy_stub },
    { "pthread_setcanceltype", (void *)dummy_stub },
    { "pthread_cancel", (void *)dummy_stub },
    { "__isoc99_sscanf", (void *)sscanf },
    { "__isnan", (void *)stub_isnan },
    { "sincosf", (void *)stub_sincosf },
    { "sincos", (void *)stub_sincos },
    { "__gmon_start__", NULL },
    { "_Jv_RegisterClasses", NULL },
    { "_ITM_deregisterTMCloneTable", NULL },
    { "_ITM_registerTMCloneTable", NULL },
    { "_ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E", (void *)override_basic_ios_init },
    { "_ZNSt13basic_filebufIcSt11char_traitsIcEE4openEPKcSt13_Ios_Openmode", (void *)override_filebuf_open },
    { "_ZNSt13basic_filebufIcSt11char_traitsIcEE5closeEv", (void *)override_filebuf_close },
    { "_ZNSt13basic_filebufIcSt11char_traitsIcEED1Ev", (void *)override_filebuf_dtor },
    { "_ZNKSt12__basic_fileIcE7is_openEv", (void *)override_basic_file_is_open },
    { "_ZNSs7reserveEj", (void *)override_string_reserve },
    { "_ZNSs9_M_mutateEjjj", (void *)override_string_mutate },
    { "_ZNSs6assignEPKcj", (void *)override_string_assign_cstr },
    { "_ZNSs6assignEPKc", (void *)override_string_assign_cstr0 },
    { "_ZNSs6assignERKSs", (void *)override_string_assign_str },
    { "_ZNSs6appendEPKcj", (void *)override_string_append_cstr },
    { "_ZNSs6appendEPKc", (void *)override_string_append_cstr0 },
    { "_ZNSs6appendERKSs", (void *)override_string_append_str },
    { "_ZNSsaSEPKc", (void *)override_string_op_assign_cstr },
    { "_ZNSsaSERKSs", (void *)override_string_op_assign_str },
    { "_ZNSs7replaceEjjPKcj", (void *)override_string_replace },
    { "_ZNSs12_M_leak_hardEv", (void *)override_string_leak_hard },
    { "_ZNSs4_Rep10_M_disposeERKSaIcE", (void *)override_string_rep_dispose },
    { "_ZNSs4_Rep10_M_destroyERKSaIcE", (void *)override_string_rep_destroy },

    /* SDL / Display overrides */
    { "glViewport", (void *)override_glViewport },
    { "glScissor", (void *)override_glScissor },
    { "SDL_PollEvent", (void *)override_SDL_PollEvent },
    { "SDL_RenderPresent", (void *)override_SDL_RenderPresent },

    { NULL, NULL }
};

static void *resolve_symbol(const char *name) {
    if (!name || !*name) return NULL;

    /* Clean version suffix if present (e.g. '@GLIBC_2.4', '@GLIBCXX_3.4') */
    char clean_name[256];
    strncpy(clean_name, name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    char *at = strchr(clean_name, '@');
    if (at) *at = '\0';

    /* Standard stream pointers */
    if (strcmp(clean_name, "stderr") == 0) {
        if (!g_stderr_ptr) g_stderr_ptr = stderr;
        return &g_stderr_ptr;
    }
    if (strcmp(clean_name, "stdout") == 0) {
        if (!g_stdout_ptr) g_stdout_ptr = stdout;
        return &g_stdout_ptr;
    }

    /* 1. Check custom overrides */
    for (int i = 0; g_overrides[i].name != NULL; i++) {
        if (strcmp(g_overrides[i].name, clean_name) == 0) {
            return g_overrides[i].func;
        }
    }

    /* 2. Try GNU STL (libgnustl_shared) */
    if (g_libstdcpp) {
        void *sym = dlsym(g_libstdcpp, clean_name);
        if (sym) return sym;
        sym = dlsym(g_libstdcpp, name);
        if (sym) return sym;
    }

    /* 3. Try global dlsym with clean_name */
    void *sym = dlsym(RTLD_DEFAULT, clean_name);
    if (sym) return sym;

    /* 4. Try global dlsym with full name */
    sym = dlsym(RTLD_DEFAULT, name);
    if (sym) return sym;

    LOGW("Unresolved symbol: %s (%s)", name, clean_name);
    return (void *)dummy_stub;
}

int SDL_main(int argc, char *argv[]) {
    LOGI("Universal webOS ELF loader starting up...");

    /* 1. Set working directory to game directory (or internal files fallback) */
    const char *work_dir = g_game_dir[0] ? g_game_dir : SDL_AndroidGetInternalStoragePath();
    if (work_dir && *work_dir) {
        if (chdir(work_dir) == 0) {
            LOGI("Working directory changed to: %s", work_dir);
        } else {
            LOGW("chdir(%s) failed: %s", work_dir, strerror(errno));
        }
    }

    /* Force Android AudioTrack driver (avoids AAudio MMAP/low-latency crackling) */
    SDL_SetHint("SDL_AUDIODRIVER", "android");
    /* Disable touch-to-mouse synthesis (keep real mouse input enabled) */
    SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS", "0");

    /* 2. Open binary */
    const char *bin_name = g_game_main[0] ? g_game_main : "mainstream";
    FILE *f = fopen(bin_name, "rb");
    if (!f) {
        char rel[300];
        snprintf(rel, sizeof(rel), "./%s", bin_name);
        f = fopen(rel, "rb");
    }
    if (!f) {
        LOGE("Failed to open game binary [%s] in %s: %s", bin_name, work_dir ? work_dir : ".", strerror(errno));
        return 1;
    }

    /* 3. Pre-load bundled GNU STL */
    g_libstdcpp = dlopen("libgnustl_shared.so", RTLD_GLOBAL | RTLD_NOW);
    if (g_libstdcpp) {
        LOGI("Successfully loaded GNU STL (libgnustl_shared.so)");
        g_libstdcpp_empty_rep = dlsym(g_libstdcpp, "_ZNSs4_Rep20_S_empty_rep_storageE");
        dl_iterate_phdr(patch_gnustl_cb, NULL);
    } else {
        LOGW("Could not load libgnustl_shared.so: %s", dlerror());
    }

    /* 4. Parse ELF32 Header & Program Headers dynamically */
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, 1, sizeof(ehdr), f) != sizeof(ehdr)) {
        LOGE("Failed to read ELF header");
        fclose(f);
        return 1;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr.e_machine != EM_ARM) {
        LOGE("Unsupported ELF binary (expected ARM32 ELF)");
        fclose(f);
        return 1;
    }

    if (ehdr.e_phnum == 0 || ehdr.e_phnum > 32) {
        LOGE("Invalid e_phnum: %u", ehdr.e_phnum);
        fclose(f);
        return 1;
    }

    Elf32_Phdr phdrs[32];
    fseek(f, ehdr.e_phoff, SEEK_SET);
    if (fread(phdrs, sizeof(Elf32_Phdr), ehdr.e_phnum, f) != ehdr.e_phnum) {
        LOGE("Failed to read program headers");
        fclose(f);
        return 1;
    }

    /* 5. Map all PT_LOAD segments at their fixed virtual addresses */
    uintptr_t dyn_vaddr = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dyn_vaddr = phdrs[i].p_vaddr;
        }
        if (phdrs[i].p_type != PT_LOAD || phdrs[i].p_memsz == 0) continue;

        uintptr_t seg_start = phdrs[i].p_vaddr & ~0xFFFU;
        uintptr_t seg_end = (phdrs[i].p_vaddr + phdrs[i].p_memsz + 0xFFFU) & ~0xFFFU;
        size_t map_len = seg_end - seg_start;

        void *mapped = mmap((void *)seg_start, map_len,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (mapped == MAP_FAILED) {
            LOGE("mmap segment %d at 0x%x (len 0x%zx) failed: %s",
                 i, (unsigned)seg_start, map_len, strerror(errno));
            fclose(f);
            return 1;
        }
        memset(mapped, 0, map_len);

        if (phdrs[i].p_filesz > 0) {
            fseek(f, phdrs[i].p_offset, SEEK_SET);
            if (fread((void *)phdrs[i].p_vaddr, 1, phdrs[i].p_filesz, f) != phdrs[i].p_filesz) {
                LOGE("Failed to read PT_LOAD segment %d", i);
                fclose(f);
                return 1;
            }
        }
        LOGI("Mapped PT_LOAD[%d]: vaddr=0x%x filesz=0x%x memsz=0x%x flags=0x%x",
             i, phdrs[i].p_vaddr, phdrs[i].p_filesz, phdrs[i].p_memsz, phdrs[i].p_flags);
    }

    /* 6. Parse PT_DYNAMIC table */
    Elf32_Sym *dynsym = NULL;
    const char *dynstr = NULL;
    Elf32_Rel *rel_plt = NULL;
    size_t rel_plt_sz = 0;
    Elf32_Rel *rel_dyn = NULL;
    size_t rel_dyn_sz = 0;
    uintptr_t dt_init = 0;
    uintptr_t dt_init_array = 0;
    size_t dt_init_array_sz = 0;
    size_t dynsym_count = 0;

    if (dyn_vaddr) {
        Elf32_Dyn *dyn = (Elf32_Dyn *)dyn_vaddr;
        for (; dyn->d_tag != DT_NULL; dyn++) {
            switch (dyn->d_tag) {
                case DT_SYMTAB: dynsym = (Elf32_Sym *)dyn->d_un.d_ptr; break;
                case DT_STRTAB: dynstr = (const char *)dyn->d_un.d_ptr; break;
                case DT_JMPREL: rel_plt = (Elf32_Rel *)dyn->d_un.d_ptr; break;
                case DT_PLTRELSZ: rel_plt_sz = dyn->d_un.d_val; break;
                case DT_REL: rel_dyn = (Elf32_Rel *)dyn->d_un.d_ptr; break;
                case DT_RELSZ: rel_dyn_sz = dyn->d_un.d_val; break;
                case DT_INIT: dt_init = dyn->d_un.d_ptr; break;
                case DT_INIT_ARRAY: dt_init_array = dyn->d_un.d_ptr; break;
                case DT_INIT_ARRAYSZ: dt_init_array_sz = dyn->d_un.d_val; break;
                default: break;
            }
        }
    }

    if (dynsym && dynstr && (uintptr_t)dynstr > (uintptr_t)dynsym) {
        dynsym_count = ((uintptr_t)dynstr - (uintptr_t)dynsym) / sizeof(Elf32_Sym);
    }

    /* 7. Process .rel.plt */
    if (rel_plt && dynsym && dynstr) {
        size_t plt_count = rel_plt_sz / sizeof(Elf32_Rel);
        LOGI("Relocating %zu PLT entries...", plt_count);
        for (size_t i = 0; i < plt_count; i++) {
            uint32_t r_offset = rel_plt[i].r_offset;
            uint32_t r_info = rel_plt[i].r_info;
            uint32_t sym_idx = ELF32_R_SYM(r_info);
            uint32_t r_type = ELF32_R_TYPE(r_info);
            const char *sym_name = dynstr + dynsym[sym_idx].st_name;
            void *resolved = resolve_symbol(sym_name);
            if (r_type == R_ARM_JUMP_SLOT) {
                *(uintptr_t *)r_offset = resolved ? (uintptr_t)resolved : (uintptr_t)dummy_stub;
            }
        }
    }

    /* 8. Process .rel.dyn */
    if (rel_dyn && dynsym && dynstr) {
        size_t dyn_count = rel_dyn_sz / sizeof(Elf32_Rel);
        LOGI("Relocating %zu DYN entries...", dyn_count);
        for (size_t i = 0; i < dyn_count; i++) {
            uint32_t r_offset = rel_dyn[i].r_offset;
            uint32_t r_info = rel_dyn[i].r_info;
            uint32_t sym_idx = ELF32_R_SYM(r_info);
            uint32_t r_type = ELF32_R_TYPE(r_info);
            const char *sym_name = dynstr + dynsym[sym_idx].st_name;
            void *resolved = resolve_symbol(sym_name);
            if (r_type == R_ARM_GLOB_DAT) {
                *(uintptr_t *)r_offset = (uintptr_t)resolved;
            } else if (r_type == R_ARM_COPY) {
                if (strstr(sym_name, "_ZNSs4_Rep20_S_empty_rep_storageE")) {
                    g_app_empty_rep = (void *)r_offset;
                }
                if (resolved && resolved != (void *)dummy_stub && dynsym[sym_idx].st_size > 0) {
                    memcpy((void *)r_offset, resolved, dynsym[sym_idx].st_size);
                }
            }
        }
    }

    /* 9. Apply PROT_READ | PROT_EXEC on executable PT_LOAD segments */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD && (phdrs[i].p_flags & PF_X) && phdrs[i].p_memsz > 0) {
            uintptr_t seg_start = phdrs[i].p_vaddr & ~0xFFFU;
            uintptr_t seg_end = (phdrs[i].p_vaddr + phdrs[i].p_memsz + 0xFFFU) & ~0xFFFU;
            if (mprotect((void *)seg_start, seg_end - seg_start, PROT_READ | PROT_EXEC) != 0) {
                LOGE("mprotect RX failed for segment %d: %s", i, strerror(errno));
                fclose(f);
                return 1;
            }
            __builtin___clear_cache((char *)seg_start, (char *)seg_end);
        }
    }

    /* Look up exported 'main' in .dynsym before closing file */
    uintptr_t game_main_addr = 0;
    if (dynsym && dynstr) {
        for (size_t i = 0; i < dynsym_count; i++) {
            const char *nm = dynstr + dynsym[i].st_name;
            if (strcmp(nm, "main") == 0 && dynsym[i].st_value != 0) {
                game_main_addr = dynsym[i].st_value;
                break;
            }
        }
    }

    fclose(f);
    LOGI("Memory mapping and relocations finished! dt_init=0x%x init_array=0x%x main=0x%x e_entry=0x%x",
         (unsigned)dt_init, (unsigned)dt_init_array, (unsigned)game_main_addr, (unsigned)ehdr.e_entry);

    /* 10. Run DT_INIT and DT_INIT_ARRAY constructors */
    typedef void (*ctor_t)(void);
    if (dt_init) {
        ((ctor_t)dt_init)();
    }
    if (dt_init_array && dt_init_array_sz > 0) {
        ctor_t *ctors = (ctor_t *)dt_init_array;
        size_t count = dt_init_array_sz / sizeof(ctor_t);
        for (size_t i = 0; i < count; i++) {
            if (ctors[i] && (uintptr_t)ctors[i] != (uintptr_t)-1) {
                ctors[i]();
            }
        }
    }

    /* 11. Launch game main */
    char arg0[256];
    snprintf(arg0, sizeof(arg0), "./%s", bin_name);
    char *fake_argv[] = { arg0, NULL };

    if (game_main_addr) {
        typedef int (*game_main_t)(int, char **);
        game_main_t game_main = (game_main_t)game_main_addr;
        LOGI("Entering %s main() at %p...", bin_name, game_main);
        int ret = game_main(1, fake_argv);
        LOGI("%s main() exited with code %d", bin_name, ret);
        return ret;
    } else if (ehdr.e_entry) {
        /* Stripped binary (no exported main): call _start which invokes override_libc_start_main */
        LOGI("No exported main(), entering _start at 0x%x...", ehdr.e_entry);
        uintptr_t entry = ehdr.e_entry;
        register int r0_res __asm__("r0");
        __asm__ volatile (
            "sub sp, sp, #16\n"
            "mov r1, #1\n"
            "str r1, [sp, #0]\n"
            "str %1, [sp, #4]\n"
            "mov r1, #0\n"
            "str r1, [sp, #8]\n"
            "str r1, [sp, #12]\n"
            "mov r0, #0\n"
            "blx %2\n"
            "add sp, sp, #16\n"
            : "=r"(r0_res)
            : "r"(arg0), "r"(entry)
            : "r1", "r2", "r3", "r12", "lr", "memory"
        );
        return r0_res;
    }

    LOGE("No entry point found in %s", bin_name);
    return 1;
}
