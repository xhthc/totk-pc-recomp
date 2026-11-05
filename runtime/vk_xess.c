// Intel XeSS-SR's C API, loaded from the executable's folder. No Intel runtime is linked into the game.
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "vk.h"
#include "vk_xess.h"
#include "vk_rec.h"

#define XESS_FN(name) __typeof__(&name) name
static struct {
    void *lib;
    int tried, disabled, prepared;
    xess_context_handle_t context;
    xess_quality_settings_t quality;
    xess_2d_t output, minimum, maximum;
    xess_2d_t last_invalid;
    unsigned frames;
    XESS_FN(xessVKGetRequiredInstanceExtensions);
    XESS_FN(xessVKGetRequiredDeviceExtensions);
    XESS_FN(xessVKGetRequiredDeviceFeatures);
    XESS_FN(xessVKCreateContext);
    XESS_FN(xessGetOptimalInputResolution);
    XESS_FN(xessVKInit);
    XESS_FN(xessVKExecute);
    XESS_FN(xessDestroyContext);
    XESS_FN(xessGetVersion);
} X;
#undef XESS_FN

static void select_mode(void *unused) {
    (void)unused;
    static const struct { const char *name; xess_quality_settings_t quality; } modes[] = {
        {"native", XESS_QUALITY_SETTING_AA}, {"ultra_quality", XESS_QUALITY_SETTING_ULTRA_QUALITY},
        {"quality", XESS_QUALITY_SETTING_QUALITY}, {"balanced", XESS_QUALITY_SETTING_BALANCED},
        {"performance", XESS_QUALITY_SETTING_PERFORMANCE},
    };
    const char *mode = cfg_value("TOTK_XESS");
    for (size_t i = 0; i < sizeof modes / sizeof *modes; i++) {
        if (!strcmp(mode, modes[i].name)) {
            X.quality = modes[i].quality;
            return;
        }
    }
    if (strcmp(mode, "off")) rt_log("xess: unknown mode %s: XeSS off", mode);
}

int vk_xess_wanted(void) {
    static PlatOnce once = PLAT_ONCE_INIT;
    plat_once(&once, select_mode, NULL);
    return X.quality != 0;
}
int vk_xess_available(void) { return X.prepared && !X.disabled; }

static int fail(const char *what, xess_result_t r) {
    rt_log("xess: %s failed (%d): XeSS off for this run", what, (int)r);
    X.disabled = 1;
    return 0;
}

static int load(void) {
    if (X.tried) return X.lib && !X.disabled;
    X.tried = 1;
#ifndef _WIN32
    return fail("the Intel XeSS runtime is currently Windows-only", XESS_RESULT_ERROR_UNSUPPORTED_DEVICE);
#else
    wchar_t path[PLAT_PATH_MAX];
    DWORD len = GetModuleFileNameW(NULL, path, PLAT_PATH_MAX);
    if (!len || len >= PLAT_PATH_MAX) return fail("executable path", XESS_RESULT_ERROR_UNKNOWN);
    wchar_t *last = wcsrchr(path, L'\\');
    if (!last || (size_t)(last - path) + sizeof L"/libxess.dll" / sizeof(wchar_t) > PLAT_PATH_MAX)
        return fail("runtime path", XESS_RESULT_ERROR_UNKNOWN);
    wcscpy(last + 1, L"libxess.dll");
    X.lib = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!X.lib) {
        rt_log("xess: libxess.dll or a dependency could not load (Windows error %lu)", GetLastError());
        return fail("loading libxess.dll (install it and the Microsoft VC++ x64 runtime)", XESS_RESULT_ERROR_CANT_LOAD_LIBRARY);
    }
#define LOAD(name) do { X.name = (__typeof__(X.name))plat_lib_sym(X.lib, #name); \
    if (!X.name) return fail(#name " missing from libxess.dll", XESS_RESULT_ERROR_UNSUPPORTED); } while (0)
    LOAD(xessVKGetRequiredInstanceExtensions);
    LOAD(xessVKGetRequiredDeviceExtensions);
    LOAD(xessVKGetRequiredDeviceFeatures);
    LOAD(xessVKCreateContext);
    LOAD(xessGetOptimalInputResolution);
    LOAD(xessVKInit);
    LOAD(xessVKExecute);
    LOAD(xessDestroyContext);
    LOAD(xessGetVersion);
#undef LOAD
    xess_version_t v = {0};
    X.xessGetVersion(&v);
    rt_log("xess: runtime %u.%u.%u, mode %s", v.major, v.minor, v.patch, cfg_value("TOTK_XESS"));
    return 1;
#endif
}

static int append(const char **out, uint32_t *n, uint32_t max, const char *const *names, uint32_t count) {
    uint32_t end = *n;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t j = 0;
        while (j < end && strcmp(out[j], names[i])) j++;
        if (j < end) continue;
        if (end == max) return fail("Vulkan extension list capacity", XESS_RESULT_ERROR_UNSUPPORTED);
        out[end++] = names[i];
    }
    *n = end;
    return 1;
}

void vk_xess_instance_exts(const char **ext, uint32_t *n, uint32_t max, uint32_t *api) {
    if (!vk_xess_wanted() || !load()) return;
    uint32_t count = 0, version = 0;
    const char *const *names = NULL;
    xess_result_t r = X.xessVKGetRequiredInstanceExtensions(&count, &names, &version);
    if (r < 0) { fail("instance requirements", r); return; }
    if (append(ext, n, max, names, count) && version > *api) *api = version;
}

void vk_xess_device_exts(VkInstance instance, VkPhysicalDevice phys, const char **ext, uint32_t *n, uint32_t max) {
    if (!X.lib || X.disabled) return;
    uint32_t count = 0;
    const char *const *names = NULL;
    xess_result_t r = X.xessVKGetRequiredDeviceExtensions(instance, phys, &count, &names);
    if (r < 0) { fail("device extensions", r); return; }
    append(ext, n, max, names, count);
}

int vk_xess_device_features(VkInstance instance, VkPhysicalDevice phys, void **features) {
    if (!X.lib || X.disabled) return 0;
    xess_result_t r = X.xessVKGetRequiredDeviceFeatures(instance, phys, features);
    return r < 0 ? fail("device features", r) : 1;
}

void vk_xess_vulkan_failed(const char *what, VkResult result) {
    rt_log("xess: %s with XeSS requirements failed (%d); retrying without XeSS", what, (int)result);
    X.disabled = 1;
}

void vk_xess_init(void) {
    if (!X.lib || X.disabled) return;
    const VkCtx *c = vk_ctx();
    xess_result_t r = X.xessVKCreateContext(c->instance, c->phys, c->device, &X.context);
    if (r < 0) fail("context creation", r);
    else rt_log("xess: Vulkan context ready on %s", c->props.deviceName);
}

int vk_xess_configure(uint32_t w, uint32_t h, xess_2d_t *input) {
    if (!X.context || X.disabled) return 0;
    X.output = (xess_2d_t){w, h};
    xess_result_t r = X.xessGetOptimalInputResolution(X.context, &X.output, X.quality,
                                                     input, &X.minimum, &X.maximum);
    if (r < 0) return fail("input resolution", r);
    // TotK rounds scene extents down to blocks of eight. Round up first to stay above the SDK minimum.
    if (X.quality != XESS_QUALITY_SETTING_AA) {
        uint32_t aligned_w = (input->x + 7u) & ~7u, aligned_h = (input->y + 7u) & ~7u;
        if (aligned_w <= X.maximum.x) input->x = aligned_w;
        if (aligned_h <= X.maximum.y) input->y = aligned_h;
    }
    // The D32F scene uses reversed depth (GREATER/GEQUAL in vk_dlaa_draw's scene survey).
    xess_vk_init_params_t init = {.outputResolution = X.output, .qualitySetting = X.quality,
        .initFlags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE};
    r = X.xessVKInit(X.context, &init);
    if (r < 0) return fail("initialization", r);
    X.prepared = 1;
    rt_log("xess: initialized %ux%u -> %ux%u; allowed input %ux%u..%ux%u", input->x, input->y,
           w, h, X.minimum.x, X.minimum.y, X.maximum.x, X.maximum.y);
    return 1;
}

void vk_xess_prepare(void) {
    if (!vk_xess_wanted()) return;
    unsigned out[2], scene[2];
    res_sizes(out, scene);
    xess_2d_t input;
    if (vk_xess_configure(out[0], out[1], &input) && res_xess_scene(input.x, input.y)) return;
    if (X.prepared) fail("setting scene size (the game needs resolution patches)", XESS_RESULT_ERROR_INVALID_ARGUMENT);
    res_xess_scene(0, 0);
}

static void input_layouts(VkCommandBuffer cb, const xess_vk_execute_params_t *p, int reading) {
    const xess_vk_image_view_info *images[] = {&p->colorTexture, &p->velocityTexture, &p->depthTexture};
    VkImageMemoryBarrier2 ib[3];
    for (int i = 0; i < 3; i++) {
        ib[i] = (VkImageMemoryBarrier2){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            .oldLayout = reading ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = reading ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i]->image, .subresourceRange = images[i]->subresourceRange};
    }
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                           .imageMemoryBarrierCount = 3, .pImageMemoryBarriers = ib};
    vkCmdPipelineBarrier2(cb, &di);
}

int vk_xess_input_valid(uint32_t w, uint32_t h) {
    if (w >= X.minimum.x && w <= X.maximum.x && h >= X.minimum.y && h <= X.maximum.y) return 1;
    if (w != X.last_invalid.x || h != X.last_invalid.y) {
        rt_log("xess: skipping %ux%u scene (outside SDK input range)", w, h);
        X.last_invalid = (xess_2d_t){w, h};
    }
    return 0;
}

int vk_xess_execute(VkCommandBuffer cb, const xess_vk_execute_params_t *frame) {
    if (!vk_xess_available() || !vk_xess_input_valid(frame->inputWidth, frame->inputHeight)) return 0;
    // shortcut: output size stays fixed for this run; recreate the context before adding live resolution changes.
    if (frame->outputTexture.width != X.output.x || frame->outputTexture.height != X.output.y)
        return fail("output size changed (restart required)", XESS_RESULT_ERROR_INVALID_ARGUMENT);
    input_layouts(cb, frame, 1);
    rec_drain();   // the SDK writes directly into this command buffer, outside our recorder
    xess_result_t r = X.xessVKExecute(X.context, cb, frame);
    input_layouts(cb, frame, 0);   // the rest of the renderer keeps all these images in GENERAL
    if (r < 0) return fail("execution", r);
    if (++X.frames == 1 || X.frames % 300 == 0)
        rt_log("xess: %u frames resolved (%ux%u -> %ux%u)", X.frames,
               frame->inputWidth, frame->inputHeight, X.output.x, X.output.y);
    return 1;
}

void vk_xess_shutdown(void) {
    if (X.context) X.xessDestroyContext(X.context);
    X.context = NULL;
    X.prepared = 0;
}
