#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <elf.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <android/log.h>
#include <GLES2/gl2.h>

#include "SDL.h"
#include "SDL_mixer.h"
#include "webos_bridge.h"

#define TAG "MainstreamLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define CODE_BASE  0x00008000
#define CODE_SIZE  0x001ca3c0
#define DATA_BASE  0x001db000
#define DATA_SIZE  0x0004dea4
#define INIT_ARRAY 0x001db000
#define INIT_COUNT 8
#define GAME_ENTRY 0x0003015c

/* Absolute virtual addresses of dynamic ELF tables in mainstream */
#define DYNSYM_ADDR  0x0000d370
#define DYNSTR_ADDR  0x00017c10
#define REL_DYN_ADDR 0x0002e088
#define REL_DYN_SIZE 0x000000d8
#define REL_PLT_ADDR 0x0002e160
#define REL_PLT_SIZE 0x00000b50

static FILE *g_stderr_ptr = NULL;
static FILE *g_stdout_ptr = NULL;
static void *g_libstdcpp = NULL;

/* Custom symbol resolution table */
struct SymbolOverride {
    const char *name;
    void *func;
};

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

typedef void (*real_string_reserve_t)(void *str, size_t n);
static real_string_reserve_t s_real_string_reserve = NULL;

static void override_string_reserve(void *str, size_t n) {
    if (n >= 0x70000000) {
        LOGW("Intercepted invalid std::string::reserve(%zu), clamping to 0", n);
        return;
    }
    if (!s_real_string_reserve && g_libstdcpp) {
        s_real_string_reserve = (real_string_reserve_t)dlsym(g_libstdcpp, "_ZNSs7reserveEj");
    }
    if (s_real_string_reserve) {
        s_real_string_reserve(str, n);
    }
}

static FILE *override_fopen(const char *filename, const char *mode) {
    if (!filename) return NULL;
    FILE *f = fopen(filename, mode);
    if (!f && strstr(filename, ".wav")) {
        LOGW("Audio file [%s] not found, falling back to button_press.wav", filename);
        f = fopen("./snd/button_press.wav", mode);
        if (!f) f = fopen("snd/button_press.wav", mode);
    }
    return f;
}

static int override_Mix_OpenAudio(int frequency, uint16_t format, int channels, int chunksize) {
    LOGI("Mix_OpenAudio requested: freq=%d, format=0x%x, channels=%d, chunksize=%d",
         frequency, format, channels, chunksize);
    /* Enforce 4096 sample buffer to prevent Android AudioTrack buffer underruns/crackling */
    if (chunksize < 4096) chunksize = 4096;
    return Mix_OpenAudio(frequency, format, channels, chunksize);
}

static int override_SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    if (desired) {
        LOGI("SDL_OpenAudio requested: freq=%d, format=0x%x, channels=%d, samples=%d",
             desired->freq, desired->format, desired->channels, desired->samples);
        if (desired->samples < 4096) desired->samples = 4096;
    }
    return SDL_OpenAudio(desired, obtained);
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
        GLint nx = s_vp_x + (GLint)((float)x * s_vp_w / 1920.0f);
        GLint ny = s_vp_y + (GLint)((float)y * s_vp_h / 1080.0f);
        GLsizei nw = (GLsizei)((float)width * s_vp_w / 1920.0f);
        GLsizei nh = (GLsizei)((float)height * s_vp_h / 1080.0f);
        glViewport(nx, ny, nw, nh);
    } else {
        glViewport(x, y, width, height);
    }
}

static void override_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    ensure_viewport_metrics();
    if (s_vp_w > 0 && s_vp_h > 0) {
        GLint nx = s_vp_x + (GLint)((float)x * s_vp_w / 1920.0f);
        GLint ny = s_vp_y + (GLint)((float)y * s_vp_h / 1080.0f);
        GLsizei nw = (GLsizei)((float)width * s_vp_w / 1920.0f);
        GLsizei nh = (GLsizei)((float)height * s_vp_h / 1080.0f);
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

static int override_SDL_PollEvent(SDL_Event *event) {
    int res = SDL_PollEvent(event);
    if (!res || !event) return res;

    ensure_viewport_metrics();

    /* 1. Scale touch/mouse coordinates from centered 16:9 space to 1920x1080 */
    if (s_vp_w > 0 && s_vp_h > 0) {
        if (event->type == SDL_MOUSEMOTION) {
            float mapped_x = ((float)event->motion.x - (float)s_vp_x) * 1920.0f / (float)s_vp_w;
            float mapped_y = ((float)event->motion.y - (float)s_vp_y) * 1080.0f / (float)s_vp_h;
            if (mapped_x < 0.0f) mapped_x = 0.0f;
            if (mapped_x > 1920.0f) mapped_x = 1920.0f;
            if (mapped_y < 0.0f) mapped_y = 0.0f;
            if (mapped_y > 1080.0f) mapped_y = 1080.0f;
            event->motion.x = (int)mapped_x;
            event->motion.y = (int)mapped_y;
        } else if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
            float mapped_x = ((float)event->button.x - (float)s_vp_x) * 1920.0f / (float)s_vp_w;
            float mapped_y = ((float)event->button.y - (float)s_vp_y) * 1080.0f / (float)s_vp_h;
            if (mapped_x < 0.0f) mapped_x = 0.0f;
            if (mapped_x > 1920.0f) mapped_x = 1920.0f;
            if (mapped_y < 0.0f) mapped_y = 0.0f;
            if (mapped_y > 1080.0f) mapped_y = 1080.0f;
            event->button.x = (int)mapped_x;
            event->button.y = (int)mapped_y;
        }
    }

    /* 2. Map Android Back button to Escape / webOS TV Back */
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        if (event->key.keysym.sym == SDLK_AC_BACK ||
            event->key.keysym.scancode == SDL_SCANCODE_AC_BACK) {
            LOGI("Intercepted Back key event, mapping to SDLK_ESCAPE / SDL_SCANCODE_WEBOS_TV_BACK");
            event->key.keysym.sym = SDLK_ESCAPE;
            event->key.keysym.scancode = (SDL_Scancode)482; // SDL_SCANCODE_WEBOS_TV_BACK
        }
    }

    return res;
}

static const struct SymbolOverride g_overrides[] = {
    /* NDL */
    { "NDL_ServiceInit", (void *)NDL_ServiceInit },
    { "NDL_ServiceQuit", (void *)NDL_ServiceQuit },
    { "NDL_ServiceCall", (void *)NDL_ServiceCall },
    { "NDL_ServiceCallWithCallback", (void *)NDL_ServiceCallWithCallback },
    { "NDL_ServiceGetError", (void *)NDL_ServiceGetError },
    { "NDL_UnregisterServiceCallback", (void *)NDL_UnregisterServiceCallback },

    /* LG HAL */
    { "lg_hal_m3g_OpenGLES_getHint", (void *)lg_hal_m3g_OpenGLES_getHint },
    { "lg_hal_m3g_stereo3d_isUserAllowed", (void *)lg_hal_m3g_stereo3d_isUserAllowed },
    { "lg_hal_m3g_stereo3d_getModeCount", (void *)lg_hal_m3g_stereo3d_getModeCount },

    /* GLib Base64 */
    { "g_base64_encode", (void *)g_base64_encode },
    { "g_base64_decode", (void *)g_base64_decode },
    { "g_free", (void *)g_free },

    /* Curl */
    { "curl_global_init", (void *)curl_global_init },
    { "curl_easy_init", (void *)curl_easy_init },
    { "curl_easy_setopt", (void *)curl_easy_setopt },
    { "curl_easy_perform", (void *)curl_easy_perform },
    { "curl_easy_cleanup", (void *)curl_easy_cleanup },
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
    { "ftime", (void *)ftime },
    { "__pthread_key_create", (void *)pthread_key_create },
    { "__isoc99_sscanf", (void *)sscanf },
    { "__isnan", (void *)stub_isnan },
    { "sincosf", (void *)stub_sincosf },
    { "__gmon_start__", (void *)dummy_stub },
    { "_ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E", (void *)override_basic_ios_init },
    { "_ZNSs7reserveEj", (void *)override_string_reserve },

    /* SDL / Audio / Display overrides */
    { "Mix_OpenAudio", (void *)override_Mix_OpenAudio },
    { "SDL_OpenAudio", (void *)override_SDL_OpenAudio },
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
    LOGI("Mainstream loader starting up...");

    /* 1. Set working directory to app internal files directory where assets are extracted */
    const char *internal_path = SDL_AndroidGetInternalStoragePath();
    if (internal_path && *internal_path) {
        if (chdir(internal_path) == 0) {
            LOGI("Working directory changed to: %s", internal_path);
        } else {
            LOGW("chdir(%s) failed: %s", internal_path, strerror(errno));
        }
    }

    /* Audio hints */
    SDL_SetHint("SDL_AUDIO_RESAMPLING_MODE", "fast");

    /* 2. Open binary */
    FILE *f = fopen("mainstream", "rb");
    if (!f) {
        f = fopen("./mainstream", "rb");
    }
    if (!f) {
        LOGE("Failed to open mainstream binary! Current dir: %s, errno: %s", internal_path ? internal_path : ".", strerror(errno));
        return 1;
    }

    /* 3. Pre-load bundled GNU STL */
    g_libstdcpp = dlopen("libgnustl_shared.so", RTLD_GLOBAL | RTLD_NOW);
    if (g_libstdcpp) {
        LOGI("Successfully loaded GNU STL (libgnustl_shared.so)");
    } else {
        LOGW("Could not load libgnustl_shared.so: %s", dlerror());
    }

    /* 4. Map PT_LOAD Segment 0 (Code/RO Data) at fixed base 0x8000 */
    void *code_map = mmap((void *)CODE_BASE, CODE_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (code_map == MAP_FAILED) {
        LOGE("mmap CODE_BASE 0x%x failed: %s", CODE_BASE, strerror(errno));
        fclose(f);
        return 1;
    }

    fseek(f, 0, SEEK_SET);
    if (fread(code_map, 1, CODE_SIZE, f) != CODE_SIZE) {
        LOGE("Failed to read CODE segment data!");
        fclose(f);
        return 1;
    }

    /* 5. Map PT_LOAD Segment 1 (Data / BSS) at fixed base 0x1db000 */
    void *data_map = mmap((void *)DATA_BASE, DATA_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (data_map == MAP_FAILED) {
        LOGE("mmap DATA_BASE 0x%x failed: %s", DATA_BASE, strerror(errno));
        fclose(f);
        return 1;
    }

    /* Data segment in file is at offset 0x1cb000, size 0x4288 */
    fseek(f, 0x1cb000, SEEK_SET);
    size_t file_data_size = 0x4288;
    if (fread(data_map, 1, file_data_size, f) != file_data_size) {
        LOGE("Failed to read DATA segment data!");
        fclose(f);
        return 1;
    }
    /* Zero out BSS */
    memset((char *)data_map + file_data_size, 0, DATA_SIZE - file_data_size);

    /* 6. Dynamic symbol table and string table (mapped at their fixed virtual addresses) */
    Elf32_Sym *dynsym = (Elf32_Sym *)DYNSYM_ADDR;
    const char *dynstr = (const char *)DYNSTR_ADDR;

    /* 7. Process .rel.plt (at fixed virtual address 0x2e160, 362 entries) */
    Elf32_Rel *rel_plt = (Elf32_Rel *)REL_PLT_ADDR;
    size_t plt_count = REL_PLT_SIZE / sizeof(Elf32_Rel);
    LOGI("Relocating %zu PLT entries from %p...", plt_count, rel_plt);

    for (size_t i = 0; i < plt_count; i++) {
        uint32_t r_offset = rel_plt[i].r_offset;
        uint32_t r_info = rel_plt[i].r_info;
        uint32_t sym_idx = ELF32_R_SYM(r_info);
        uint32_t r_type = ELF32_R_TYPE(r_info);

        const char *sym_name = dynstr + dynsym[sym_idx].st_name;
        void *resolved = resolve_symbol(sym_name);

        if (r_type == R_ARM_JUMP_SLOT) {
            *(uintptr_t *)r_offset = (uintptr_t)resolved;
        } else {
            LOGW("Unexpected PLT rel type %u at 0x%x for %s", r_type, r_offset, sym_name);
        }
    }

    /* 8. Process .rel.dyn (at fixed virtual address 0x2e088, 27 entries) */
    Elf32_Rel *rel_dyn = (Elf32_Rel *)REL_DYN_ADDR;
    size_t dyn_count = REL_DYN_SIZE / sizeof(Elf32_Rel);
    LOGI("Relocating %zu DYN entries from %p...", dyn_count, rel_dyn);

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
            if (resolved && resolved != (void *)dummy_stub && dynsym[sym_idx].st_size > 0) {
                memcpy((void *)r_offset, resolved, dynsym[sym_idx].st_size);
            }
        }
    }

    /* 9. Set permissions on code segment: PROT_READ | PROT_EXEC */
    if (mprotect((void *)CODE_BASE, CODE_SIZE, PROT_READ | PROT_EXEC) != 0) {
        LOGE("mprotect PROT_READ|PROT_EXEC failed: %s", strerror(errno));
        fclose(f);
        return 1;
    }

    fclose(f);
    LOGI("Memory mapping and relocations finished successfully!");

    /* 10. Run static constructors in .init_array */
    typedef void (*ctor_t)(void);
    ctor_t *ctors = (ctor_t *)INIT_ARRAY;
    for (int i = 0; i < INIT_COUNT; i++) {
        if (ctors[i]) {
            LOGI("Executing constructor [%d] at %p...", i, ctors[i]);
            ctors[i]();
        }
    }

    /* 11. Launch game main */
    FILE *test_appinfo = fopen("appinfo.json", "rb");
    if (test_appinfo) {
        fseek(test_appinfo, 0, SEEK_END);
        long sz = ftell(test_appinfo);
        fclose(test_appinfo);
        LOGI("Verified appinfo.json exists (size: %ld bytes)", sz);
    } else {
        LOGE("Warning: appinfo.json open failed: %s (cwd: %s)", strerror(errno), internal_path ? internal_path : ".");
    }

    typedef int (*game_main_t)(int, char **);
    game_main_t game_main = (game_main_t)GAME_ENTRY;
    LOGI("Entering Mainstream main() at %p...", game_main);

    char *game_argv[] = { "mainstream", NULL };
    int ret = game_main(1, game_argv);
    LOGI("Game main returned: %d", ret);

    return ret;
}
