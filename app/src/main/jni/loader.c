#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <android/log.h>

#include "SDL.h"
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

/* Custom symbol resolution table */
struct SymbolOverride {
    const char *name;
    void *func;
};

static void *dummy_stub(void) {
    LOGW("Called unhandled stub!");
    return NULL;
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

    /* Libc overrides */
    { "ftime", (void *)ftime },
    { "__pthread_key_create", (void *)pthread_key_create },
    { "__isoc99_sscanf", (void *)sscanf },
    { "stderr", (void *)&stderr },
    { "stdout", (void *)&stdout },

    { NULL, NULL }
};

static void *resolve_symbol(const char *name) {
    if (!name || !*name) return NULL;

    /* 1. Check custom overrides */
    for (int i = 0; g_overrides[i].name != NULL; i++) {
        if (strcmp(g_overrides[i].name, name) == 0) {
            return g_overrides[i].func;
        }
    }

    /* 2. Try exact name via dlsym */
    void *sym = dlsym(RTLD_DEFAULT, name);
    if (sym) return sym;

    /* 3. Strip version suffix (e.g. '@GLIBC_2.4', '@GLIBCXX_3.4') */
    char stripped[256];
    strncpy(stripped, name, sizeof(stripped) - 1);
    stripped[sizeof(stripped) - 1] = '\0';
    char *at = strchr(stripped, '@');
    if (at) {
        *at = '\0';
        /* Check overrides with stripped name */
        for (int i = 0; g_overrides[i].name != NULL; i++) {
            if (strcmp(g_overrides[i].name, stripped) == 0) {
                return g_overrides[i].func;
            }
        }
        sym = dlsym(RTLD_DEFAULT, stripped);
        if (sym) return sym;
    }

    LOGW("Unresolved symbol: %s (stripped: %s)", name, stripped);
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

    /* 2. Open binary */
    FILE *f = fopen("mainstream", "rb");
    if (!f) {
        /* Try current directory or fallback */
        f = fopen("./mainstream", "rb");
    }
    if (!f) {
        LOGE("Failed to open mainstream binary! Current dir: %s, errno: %s", internal_path ? internal_path : ".", strerror(errno));
        return 1;
    }

    /* 3. Pre-load bundled GNU libstdc++ */
    void *libstdcpp = dlopen("libstdc++.so.6", RTLD_GLOBAL | RTLD_NOW);
    if (libstdcpp) {
        LOGI("Successfully loaded libstdc++.so.6");
    } else {
        LOGW("Could not load libstdc++.so.6 directly: %s (will fallback to libc++)", dlerror());
    }

    /* 4. Map PT_LOAD Segment 0 (Code/RO Data) */
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

    /* 5. Map PT_LOAD Segment 1 (Data / BSS) */
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

    /* 6. Read dynamic symbol table and string table for relocations */
    /* .dynsym at 0xd370, size 0xa8a0 (count 2698 entries of 16 bytes) */
    Elf32_Sym *dynsym = (Elf32_Sym *)((char *)code_map + 0xd370);
    const char *dynstr = (const char *)((char *)code_map + 0x17c10);

    /* 7. Process .rel.plt (offset 0x2e160 in memory, size 0xb50 = 362 entries) */
    Elf32_Rel *rel_plt = (Elf32_Rel *)((char *)code_map + 0x2e160);
    size_t plt_count = 0xb50 / sizeof(Elf32_Rel);
    LOGI("Relocating %zu PLT entries...", plt_count);

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

    /* 8. Process .rel.dyn (offset 0x2e088 in memory, size 0xd8 = 27 entries) */
    Elf32_Rel *rel_dyn = (Elf32_Rel *)((char *)code_map + 0x2e088);
    size_t dyn_count = 0xd8 / sizeof(Elf32_Rel);
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
            if (resolved) {
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
    typedef int (*game_main_t)(int, char **);
    game_main_t game_main = (game_main_t)GAME_ENTRY;
    LOGI("Entering Mainstream main() at %p...", game_main);

    char *game_argv[] = { "mainstream", NULL };
    int ret = game_main(1, game_argv);
    LOGI("Game main returned: %d", ret);

    return ret;
}
