/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- optional GPU raster backend.
 *
 *          The CPU interpreter and span JIT are the default renderer and
 *          the runtime fallback. This backend is off by default. The
 *          "gpu_raster" device config option turns it on per VM, and the
 *          R128_GPU environment variable overrides that. It turns itself
 *          off, and logs why, whenever the host Vulkan stack cannot
 *          support it. Every hook in the device checks dev->gpu for NULL
 *          first, so with the backend off only the CPU renderer runs.
 *
 *          Segments: triangle setup still runs on the CPU, inside
 *          rage128_3d_tri. For a draw state the kernels support, the
 *          per-row span seeds are captured into host-visible buffers
 *          instead of being rasterized. A segment collects many draws;
 *          it splits into runs wherever the kernel variant (texture
 *          stages, LOD, blend, z) or the combine tuple changes, and each
 *          run is one dispatch. GPU_RING_SLOTS slots let the CPU build
 *          the next segment while earlier ones execute. A flush submits
 *          without waiting unless its cause needs the result at once
 *          (gpu_cause_drains); otherwise a fence is waited on only where
 *          a later access overlaps the bytes a submitted segment touches.
 *
 *          Ordering: one workgroup owns one framebuffer row and walks
 *          that row's spans in submission order, so overlapping draws
 *          land in draw order without barriers between workgroups.
 *          svga.vram is imported zero-copy through
 *          VK_EXT_external_memory_host; the kernels work on a
 *          device-local mirror of the bytes a segment touches when one
 *          could be allocated, and on the import otherwise.
 *
 *          Threading: capture and flush run on the CCE thread. The CPU
 *          thread may flush only after the CCE has been quiesced
 *          (rage128_pm4_drain_wait); rage128_gpu_cpu_barrier does that.
 *          Off the CCE thread, rage128_gpu_present only publishes in
 *          async mode, and scanout fences per line instead.
 *
 *          R128_GPU=verify replays each flushed segment through the
 *          interpreter, compares the bytes and keeps the interpreter's
 *          result.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/path.h>
#include <86box/timer.h>
#include <86box/version.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/ui.h>
#include <86box/vid_ati_rage128.h>

/* Backend selection. The per-VM config option is the user-facing
   off / on switch. Verify mode is reachable only through R128_GPU,
   which wins whenever it is set; any nonzero config value, 2 included,
   means on. Config reads need the device context, so callers must stay
   inside device init. */
int
rage128_gpu_mode(void)
{
    const char *env = getenv("R128_GPU");

    if (env && env[0]) {
        if (!strcmp(env, "0"))
            return R128_GPU_OFF;
        return !strcmp(env, "verify") ? R128_GPU_VERIFY : R128_GPU_ON;
    }
    return device_get_config_int("gpu_raster") ? R128_GPU_ON : R128_GPU_OFF;
}

#ifdef R128_GPU_HAVE_VULKAN

#    define VK_NO_PROTOTYPES
#    include <vulkan/vulkan.h>
#    ifdef _WIN32
#        include <windows.h>
#    else
#        include <dlfcn.h>
#        include <unistd.h>
#    endif
#    include <86box/plat.h>
#    include <86box/vid_ati_rage128_gpu_spv.h>
#    include <86box/vid_ati_rage128_gpu_comb.h>

/* Tester telemetry. With R128_GPU_TELEMETRY set, every line this file
   logs is also written to a telemetry file in the VM folder, flushed per
   line: a report then needs no -L launch, and a hang still leaves
   everything up to its last line. */
static FILE *gpu_tel;

#    if defined(_WIN32)
#        define GPU_TEL_OS "windows"
#    elif defined(__APPLE__)
#        define GPU_TEL_OS "macos"
#    elif defined(__linux__)
#        define GPU_TEL_OS "linux"
#    else
#        define GPU_TEL_OS "other"
#    endif
#    if defined(__aarch64__) || defined(_M_ARM64)
#        define GPU_TEL_ARCH "arm64"
#    elif defined(__x86_64__) || defined(_M_X64)
#        define GPU_TEL_ARCH "x86_64"
#    else
#        define GPU_TEL_ARCH "other"
#    endif

static void
gpu_log(const char *fmt, ...)
{
    char    buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    pclog("%s", buf);
    if (gpu_tel) {
        fputs(buf, gpu_tel);
        fflush(gpu_tel);
    }
}

/* Append one formatted field to a log line held in a fixed buffer and
   move the write position past what was stored. The C library's
   formatted print returns the length a field needed, not the length it
   stored, so adding that return to the write position would step past
   the buffer once a line fills, and the next field would then be written
   past its end. Here a field that does not fit ends the line: what fits
   is kept, the write position is parked at the end of the buffer, and
   every later field is skipped. A formatting error ends the line the
   same way, with nothing of the field stored. */
static void
gpu_append(char **p, char *end, const char *fmt, ...)
{
    va_list ap;
    int     n;

    if (*p >= end)
        return;
    va_start(ap, fmt);
    n = vsnprintf(*p, (size_t) (end - *p), fmt, ap);
    va_end(ap);
    if (n < 0)
        **p = '\0';
    if (n < 0 || n >= end - *p)
        *p = end;
    else
        *p += n;
}

static void
gpu_tel_open(rage128_t *dev)
{
    /* a guest hard reset re-inits the device in the same process; the
       earlier session's lines stay, only a fresh launch truncates */
    static int opened;
    char       path[1024 + 32];
    char       stamp[32];
    time_t     now = time(NULL);
    struct tm  tm;

    const char *te = getenv("R128_GPU_TELEMETRY");

    dev->gpu_telemetry_en = te && te[0] && strcmp(te, "0") != 0;
    if (!dev->gpu_telemetry_en || gpu_tel)
        return;
    path_append_filename(path, usr_path, "r128-gpu-telemetry.log");
    gpu_tel = plat_fopen(path, opened ? "a" : "w");
    opened  = 1;
    if (!gpu_tel) {
        pclog("RAGE128 GPU: telemetry: cannot open %s\n", path);
        return;
    }
    /* The time is converted into this function's own structure, not the
       one buffer the plain conversion shares between threads. A failed
       conversion leaves the stamp reading "unknown". */
    snprintf(stamp, sizeof(stamp), "unknown");
#    ifdef _WIN32
    if (localtime_s(&tm, &now) == 0)
#    else
    if (localtime_r(&now, &tm))
#    endif
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    gpu_log("RAGE128 GPU: telemetry: 86Box %s, host %s/%s, %s\n",
            EMU_VERSION_FULL, GPU_TEL_OS, GPU_TEL_ARCH, stamp);
    /* a frame rate means little without the guest CPU behind it; the
       test harnesses have no guest CPU and skip the line */
    {
        const char *family, *model;
        unsigned    mhz;

        if (rage128_guest_cpu(&family, &model, &mhz))
            gpu_log("RAGE128 GPU: guest cpu \"%s\" \"%s\" %u MHz\n", family, model, mhz);
    }
}

static void
gpu_tel_close(void)
{
    if (gpu_tel) {
        fclose(gpu_tel);
        gpu_tel = NULL;
    }
}

/* The 64-bit fields of seg_span_t/seg_tri_t (edge functions, zline,
   FOG_TABLE) are written as host words and read as uvec2 limbs, low
   half first -- the whole record ABI is little-endian by construction. */
#    if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) \
        && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#        error "rage128 GPU backend: the segment record ABI is little-endian only"
#    endif

/* Host primitives 86Box has no wrapper for. The Windows arm exists
   because MSYS2/UCRT, the only supported Windows toolchain, has none of
   dlopen, setenv, sysconf or posix_memalign. */
#    ifdef _WIN32
#        define r128_dl_open(p)      ((void *) LoadLibraryA(p))
#        define r128_dl_sym(h, n)    ((void *) (uintptr_t) GetProcAddress((HMODULE) (h), (n)))
#        define r128_aligned_free(p) _aligned_free(p)

static int
r128_aligned_alloc(void **out, size_t align, size_t sz)
{
    *out = _aligned_malloc(sz, align);
    return *out ? 0 : -1;
}

static long
r128_cpu_count(void)
{
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return (long) si.dwNumberOfProcessors;
}
#    else
#        define r128_dl_open(p)                    dlopen((p), RTLD_NOW | RTLD_LOCAL)
#        define r128_dl_sym(h, n)                  dlsym((h), (n))
#        define r128_aligned_free(p)               free(p)
#        define r128_aligned_alloc(out, align, sz) posix_memalign((out), (align), (sz))

static long
r128_cpu_count(void)
{
    return (long) sysconf(_SC_NPROCESSORS_ONLN);
}
#    endif

/* setenv(3) semantics, including the overwrite flag _putenv_s lacks. */
static void
r128_setenv(const char *key, const char *val, int overwrite)
{
    if (!overwrite && getenv(key))
        return;
#    ifdef _WIN32
    _putenv_s(key, val);
#    else
    setenv(key, val, 1);
#    endif
}

static void
r128_unsetenv(const char *key)
{
#    ifdef _WIN32
    _putenv_s(key, "");
#    else
    unsetenv(key);
#    endif
}

/* The Vulkan library is opened at init with dlopen, after the MoltenVK
   settings and the VK_ICD_FILENAMES environment are staged. A linked
   loader initializes at process start and reads its driver-search
   environment before the device can set it; with a linked loader,
   instance creation failed. Opening it late also keeps 86Box free of a
   hard Vulkan link: a host without Vulkan just gets the disabled log
   line. */
#    define R128_VK_GLOBAL_FNS(X) \
        X(vkCreateInstance)

#    define R128_VK_INSTANCE_FNS(X)                 \
        X(vkEnumeratePhysicalDevices)               \
        X(vkGetPhysicalDeviceProperties2)           \
        X(vkGetPhysicalDeviceFeatures2)             \
        X(vkGetPhysicalDeviceQueueFamilyProperties) \
        X(vkEnumerateDeviceExtensionProperties)     \
        X(vkGetPhysicalDeviceMemoryProperties)      \
        X(vkCreateDevice)                           \
        X(vkGetDeviceQueue)                         \
        X(vkGetDeviceProcAddr)                      \
        X(vkCreateBuffer)                           \
        X(vkGetBufferMemoryRequirements)            \
        X(vkAllocateMemory)                         \
        X(vkBindBufferMemory)                       \
        X(vkMapMemory)                              \
        X(vkCreateDescriptorSetLayout)              \
        X(vkCreatePipelineLayout)                   \
        X(vkCreateShaderModule)                     \
        X(vkCreateComputePipelines)                 \
        X(vkCreatePipelineCache)                    \
        X(vkGetPipelineCacheData)                   \
        X(vkDestroyPipelineCache)                   \
        X(vkDestroyShaderModule)                    \
        X(vkCreateDescriptorPool)                   \
        X(vkAllocateDescriptorSets)                 \
        X(vkUpdateDescriptorSets)                   \
        X(vkCreateCommandPool)                      \
        X(vkAllocateCommandBuffers)                 \
        X(vkCreateFence)                            \
        X(vkCreateQueryPool)                        \
        X(vkBeginCommandBuffer)                     \
        X(vkCmdResetQueryPool)                      \
        X(vkCmdWriteTimestamp)                      \
        X(vkCmdBindPipeline)                        \
        X(vkCmdBindDescriptorSets)                  \
        X(vkCmdPushConstants)                       \
        X(vkCmdDispatch)                            \
        X(vkCmdFillBuffer)                          \
        X(vkCmdPipelineBarrier)                     \
        X(vkEndCommandBuffer)                       \
        X(vkQueueSubmit)                            \
        X(vkWaitForFences)                          \
        X(vkGetFenceStatus)                         \
        X(vkCmdCopyBuffer)                          \
        X(vkGetQueryPoolResults)                    \
        X(vkResetFences)                            \
        X(vkDeviceWaitIdle)                         \
        X(vkDestroyQueryPool)                       \
        X(vkDestroyFence)                           \
        X(vkDestroyCommandPool)                     \
        X(vkDestroyDescriptorPool)                  \
        X(vkDestroyPipeline)                        \
        X(vkDestroyPipelineLayout)                  \
        X(vkDestroyDescriptorSetLayout)             \
        X(vkDestroyBuffer)                          \
        X(vkFreeMemory)                             \
        X(vkDestroyDevice)                          \
        X(vkDestroyInstance)

static struct {
    void                     *lib;
    PFN_vkGetInstanceProcAddr gipa;
#    define R128_VK_DECL(name) PFN_##name name;
    R128_VK_GLOBAL_FNS(R128_VK_DECL)
    R128_VK_INSTANCE_FNS(R128_VK_DECL)
#    undef R128_VK_DECL
} r128_vk;

/* Load the first candidate that exists and resolve the pre-instance
   entry points. 1 = ready for vkCreateInstance. */
static int
r128_vk_load_from(const char *const *cands, size_t n)
{
    if (r128_vk.lib)
        return 1;
    for (size_t i = 0; i < n; i++)
        if ((r128_vk.lib = r128_dl_open(cands[i]))) {
            /* name the winning candidate: fallback here is silent and
               candidates can differ by whole library versions */
            gpu_log("RAGE128 GPU: loaded Vulkan library %s\n", cands[i]);
            break;
        }
    if (!r128_vk.lib)
        return 0;
    r128_vk.gipa = (PFN_vkGetInstanceProcAddr) r128_dl_sym(r128_vk.lib,
                                                           "vkGetInstanceProcAddr");
    if (!r128_vk.gipa)
        return 0;
#    define R128_VK_LOAD_G(name)                               \
        r128_vk.name = (PFN_##name) r128_vk.gipa(NULL, #name); \
        if (!r128_vk.name)                                     \
            return 0;
    R128_VK_GLOBAL_FNS(R128_VK_LOAD_G)
#    undef R128_VK_LOAD_G
    return 1;
}

/* macOS only: MoltenVK opened directly as the driver, with no loader,
   so no ICD manifest or VK_ICD_FILENAMES staging is involved. The
   bundled copy comes first, which makes the packaged app
   self-contained; the rest cover a development tree running against a
   system or Homebrew install. Every other platform has an installable
   loader, so there is no driver to open directly. */
static int
r128_vk_load_direct(void)
{
#    ifdef __APPLE__
    static const char *const cands[] = {
        "@executable_path/../Frameworks/libMoltenVK.dylib",
        "libMoltenVK.dylib",
        "/opt/homebrew/lib/libMoltenVK.dylib",
        "/usr/local/lib/libMoltenVK.dylib",
    };

    return r128_vk_load_from(cands, sizeof(cands) / sizeof(cands[0]));
#    else
    return 0;
#    endif
}

/* The installable Vulkan loader, which does need a driver manifest to
   find an ICD. The only path on Windows and Linux; the fallback on
   macOS. */
static int
r128_vk_load_loader(void)
{
    static const char *const cands[] = {
#    if defined(_WIN32)
        "vulkan-1.dll",
#    elif defined(__APPLE__)
        "libvulkan.1.dylib",
        "/opt/homebrew/lib/libvulkan.1.dylib",
        "/usr/local/lib/libvulkan.1.dylib",
#    else
        "libvulkan.so.1",
        "libvulkan.so",
#    endif
    };

    return r128_vk_load_from(cands, sizeof(cands) / sizeof(cands[0]));
}

/* Resolve everything else against the created instance. */
static int
r128_vk_load_instance(VkInstance inst)
{
#    define R128_VK_LOAD_I(name)                               \
        r128_vk.name = (PFN_##name) r128_vk.gipa(inst, #name); \
        if (!r128_vk.name)                                     \
            return 0;
    R128_VK_INSTANCE_FNS(R128_VK_LOAD_I)
#    undef R128_VK_LOAD_I
    return 1;
}

/* every vk call in this file goes through the table */
#    define vkCreateInstance                         r128_vk.vkCreateInstance
#    define vkEnumeratePhysicalDevices               r128_vk.vkEnumeratePhysicalDevices
#    define vkGetPhysicalDeviceProperties2           r128_vk.vkGetPhysicalDeviceProperties2
#    define vkGetPhysicalDeviceFeatures2             r128_vk.vkGetPhysicalDeviceFeatures2
#    define vkGetPhysicalDeviceQueueFamilyProperties r128_vk.vkGetPhysicalDeviceQueueFamilyProperties
#    define vkEnumerateDeviceExtensionProperties     r128_vk.vkEnumerateDeviceExtensionProperties
#    define vkGetPhysicalDeviceMemoryProperties      r128_vk.vkGetPhysicalDeviceMemoryProperties
#    define vkCreateDevice                           r128_vk.vkCreateDevice
#    define vkGetDeviceQueue                         r128_vk.vkGetDeviceQueue
#    define vkGetDeviceProcAddr                      r128_vk.vkGetDeviceProcAddr
#    define vkCreateBuffer                           r128_vk.vkCreateBuffer
#    define vkGetBufferMemoryRequirements            r128_vk.vkGetBufferMemoryRequirements
#    define vkAllocateMemory                         r128_vk.vkAllocateMemory
#    define vkBindBufferMemory                       r128_vk.vkBindBufferMemory
#    define vkMapMemory                              r128_vk.vkMapMemory
#    define vkCreateDescriptorSetLayout              r128_vk.vkCreateDescriptorSetLayout
#    define vkCreatePipelineLayout                   r128_vk.vkCreatePipelineLayout
#    define vkCreateShaderModule                     r128_vk.vkCreateShaderModule
#    define vkCreateComputePipelines                 r128_vk.vkCreateComputePipelines
#    define vkCreatePipelineCache                    r128_vk.vkCreatePipelineCache
#    define vkGetPipelineCacheData                   r128_vk.vkGetPipelineCacheData
#    define vkDestroyPipelineCache                   r128_vk.vkDestroyPipelineCache
#    define vkDestroyShaderModule                    r128_vk.vkDestroyShaderModule
#    define vkCreateDescriptorPool                   r128_vk.vkCreateDescriptorPool
#    define vkAllocateDescriptorSets                 r128_vk.vkAllocateDescriptorSets
#    define vkUpdateDescriptorSets                   r128_vk.vkUpdateDescriptorSets
#    define vkCreateCommandPool                      r128_vk.vkCreateCommandPool
#    define vkAllocateCommandBuffers                 r128_vk.vkAllocateCommandBuffers
#    define vkCreateFence                            r128_vk.vkCreateFence
#    define vkCreateQueryPool                        r128_vk.vkCreateQueryPool
#    define vkBeginCommandBuffer                     r128_vk.vkBeginCommandBuffer
#    define vkCmdResetQueryPool                      r128_vk.vkCmdResetQueryPool
#    define vkCmdWriteTimestamp                      r128_vk.vkCmdWriteTimestamp
#    define vkCmdBindPipeline                        r128_vk.vkCmdBindPipeline
#    define vkCmdBindDescriptorSets                  r128_vk.vkCmdBindDescriptorSets
#    define vkCmdPushConstants                       r128_vk.vkCmdPushConstants
#    define vkCmdDispatch                            r128_vk.vkCmdDispatch
#    define vkCmdFillBuffer                          r128_vk.vkCmdFillBuffer
#    define vkCmdPipelineBarrier                     r128_vk.vkCmdPipelineBarrier
#    define vkEndCommandBuffer                       r128_vk.vkEndCommandBuffer
#    define vkQueueSubmit                            r128_vk.vkQueueSubmit
#    define vkWaitForFences                          r128_vk.vkWaitForFences
#    define vkGetFenceStatus                         r128_vk.vkGetFenceStatus
#    define vkCmdCopyBuffer                          r128_vk.vkCmdCopyBuffer
#    define vkGetQueryPoolResults                    r128_vk.vkGetQueryPoolResults
#    define vkResetFences                            r128_vk.vkResetFences
#    define vkDeviceWaitIdle                         r128_vk.vkDeviceWaitIdle
#    define vkDestroyQueryPool                       r128_vk.vkDestroyQueryPool
#    define vkDestroyFence                           r128_vk.vkDestroyFence
#    define vkDestroyCommandPool                     r128_vk.vkDestroyCommandPool
#    define vkDestroyDescriptorPool                  r128_vk.vkDestroyDescriptorPool
#    define vkDestroyPipeline                        r128_vk.vkDestroyPipeline
#    define vkDestroyPipelineLayout                  r128_vk.vkDestroyPipelineLayout
#    define vkDestroyDescriptorSetLayout             r128_vk.vkDestroyDescriptorSetLayout
#    define vkDestroyBuffer                          r128_vk.vkDestroyBuffer
#    define vkFreeMemory                             r128_vk.vkFreeMemory
#    define vkDestroyDevice                          r128_vk.vkDestroyDevice
#    define vkDestroyInstance                        r128_vk.vkDestroyInstance

/* Must match GPU_SLOT_W in vid_ati_rage128_gpu_abi.glsl: the kernel cuts
   its 256-thread workgroup into slots of this width and indexes slots[]
   with the resulting count. A mismatch silently drops or aliases
   spans. */
#    define GPU_SLOT_W      16u
#    define GPU_SLOT_SHIFT  4u
#    define GPU_BATCH_SLOTS (256u / GPU_SLOT_W)
/* above this many spans in one row, skip the O(n^2) leveling */
#    define GPU_BATCH_LEVEL_MAX 1024u

#    define GPU_SPAN_CAP        65536u
#    define GPU_TRI_CAP         8192u
/* Soft submit cadence in spans, checked only at a draw boundary so a
   whole triangle is still captured together. Half the span cap is a
   tuning choice: it submits before a capacity flush would, without
   cutting segments small enough to leave the GPU idle. */
#    define GPU_CADENCE (GPU_SPAN_CAP / 2)
/* read-fence address histogram slots (profiling only) */
#    define GPU_RF_BUCKETS 256u
/* Rows one segment may span. The scissor's x and y fields are 14 bits
   wide (RRG: SC_BOTTOM_RIGHT, p. 3-162 / PDF 180), so 16384 covers any
   value they can hold, and the gate's row bound never rejects a draw
   for a wide-open scissor (Mesa r128 r128_state.c r128DDInitState sets
   SC_BOTTOM_RIGHT to 0x1fff1fff). Costs memory only: every loop over
   the row arrays is bounded by the run's own row span, not by the
   cap. */
#    define GPU_ROW_CAP  16384u
#    define GPU_RUN_CAP  64u
#    define GPU_KERNELS  20
#    define GPU_WGH_BINS 9u
/* leveling row-shape bins: bin b holds rows with [2^b, 2^(b+1)) spans */
#    define GPU_LVBINS 12u
/* storage bindings, must match the kernel's layout(binding=) set */
#    define GPU_NBIND 13
/* CI4/CI8 palette snapshots per segment (256 ARGB words each); a draw
   needing a fresh snapshot past this cap flushes the segment first */
#    define GPU_PAL_CAP 64u
/* Interned combine tuples. A safety limit, not a tuning knob: a draw
   past it runs the unfolded pipeline, which writes the same bytes more
   slowly. */
/* clang-format off */
#define GPU_TUPLE_CAP 256u
#define GPU_TUPLE_COMB 14u /* {comb,fmsb,cfac,ifac,comba,afac,ifaca} x 2 */
#define GPU_TUPLE_SEL  21u /* + [14] stencil-live, [15] dead-color,
                              [16..19] {dt, s3tc} x 2 (texel format),
                              [20] texture-lighting word: TEX_CNTL_C
                              TEX_LIGHT_FN | bit 6 << 4 |
                              ALPHA_LIGHT_FN << 5, 0 = off (SDK: Texture
                              Mapping, p. 6-46 / PDF 158) */
#define GPU_TUPLE_LIGHT 20u
/* clang-format on */
/* format fields only: this stage's texel format stays a runtime
   selector in the folded pipeline (off/dead stages, seed tuples) */
#    define GPU_TUPLE_FMT_ANY 0xffffffffu
/* z-ladder words per segment (u32 each; a table-fog pixel takes two: the
   rung and the fog fraction). Raising this together with GPU_SPAN_CAP
   lets segments grow, but the host-side leveling in gpu_batch_row is
   quadratic in spans per row, so its cost grows faster than the GPU
   wait shrinks. */
#    define GPU_PX_CAP (8u << 20)
/* Serial (self-aliasing) draws run one lane in one workgroup, measured
   at about 17 us per pixel on an Apple M1 Pro, so this cap holds one
   such draw to about a quarter second of GPU time. A larger alias draw
   goes to the CPU lane, which is much faster for it and writes the same
   bytes. */
#    define GPU_SERIAL_PX_CAP 16384u

/* GPU-side records; must match the std430 structs in
   vid_ati_rage128_gpu_abi.glsl (8-byte members first; the asserts below
   pin the size and key offsets).
   st_cfg: 8 per stage {dt, s3tc, clamp_s, clamp_t, border, minb, mag,
   mipdis}; comb_cfg: 8 per stage {comb, fmsb, cfac, ifac, comba, afac,
   ifaca, x}, where x is the texture-lighting word in stage 0 and unused
   in stage 1. */
typedef struct seg_span_t {
    int64_t  e0, e1, e2;
    uint64_t zline;
    int32_t  x0, x1, py, tri;
    uint32_t drow, zrow, px_base, pad1;
} seg_span_t;

typedef struct seg_tri_t {
    int64_t  e0dxi, e1dxi, e2dxi;
    uint64_t dZdx;
    float    invs, lod_bias;
    float    texw0, texh0, texw1, texh1;
    uint32_t zfn, z_wr;
    uint32_t atest_en, atest_fn, atest_ref;
    uint32_t bsrc, bdst, bfcn;
    uint32_t aux_cntl;
    int32_t  aux_x0[3], aux_x1[3], aux_y0[3], aux_y1[3];
    float    cc[4];
    float    vca[4], vcb[4], vcc[4];
    float    sta, stb, stc, tta, ttb, ttc;
    float    s2a, s2b, s2c, t2a, t2b, t2c;
    float    arhw, brhw, crhw;
    float    dSdx, dSdy, dTdx, dTdy, dWdx, dWdy;
    float    dS2dx, dS2dy, dT2dx, dT2dy;
    /* the secondary stage's own W per vertex and its screen gradients,
       with its perspective enable and whether that W is the vertex
       rhw2 (SEC_SRC_SEL_W); see sec_persp_diff and sel_w in
       r3d_draw_state_derive */
    float    a2rhw, b2rhw, c2rhw, dW2dx, dW2dy;
    uint32_t persp2, sel_w, stip_en;
    uint32_t sec_sel, need_lod2;
    int32_t  top0, top1;
    uint32_t st_cfg[16];
    uint32_t comb_cfg[16];
    uint32_t slot0[44]; /* 11 x {lw, lh, base, mask} */
    uint32_t slot1[44];
    uint32_t dst_dt, wmask, dither, persp;
    uint32_t zbpp, zshift, sten_ctl, sten_rm;
    float    spa[3], spb[3], spc[3];
    uint32_t spec_en;
    float    fga, fgb, fgc;
    float    fogr, fogg, fogb;
    uint32_t fog_en, ftab_en;
    uint32_t ck3d_clr, ck3d_msk, ckc_clr, ckc_msk;
    uint32_t ck_ctl, pal_base;
    uint32_t fog_table[64]; /* FOG_TABLE snapshot, packed little-endian */
    uint32_t stipple[32];
} seg_tri_t;

_Static_assert(sizeof(seg_span_t) == 64, "span layout drift");
_Static_assert(sizeof(seg_tri_t) == 1344, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, stip_en) == 332, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, stipple) == 1216, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, cc) == 140, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, st_cfg) == 352, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, slot0) == 480, "tri layout drift");
/* a batch slot packs span index << 8 | pixel offset, and 0xffffffff is
   the idle marker: the span index must fit in 24 bits, and then no
   packed slot can equal the marker (the offset's low bits are 0 or the
   bit-0 level flag) */
_Static_assert(GPU_SPAN_CAP <= (1u << 24), "span index overflows batch slot");
/* a widest span (the 256 px capture chunk) fills the whole slot table;
   a smaller table could never hold one and would overrun */
_Static_assert(GPU_BATCH_SLOTS == 256u / GPU_SLOT_W, "slot table mis-sized");
_Static_assert(GPU_SLOT_W == (1u << GPU_SLOT_SHIFT), "slot shift mismatch");
/* offsets share the low byte with the level-open flag in bit 0 */
_Static_assert(GPU_SLOT_W >= 2u, "slot offset collides with open flag");

/* One run: a maximal stretch of consecutive tris sharing a kernel id, a
   combine tuple (the folded pipeline key) and the serial flag. Each run
   is one dispatch; gpu_disp_runs puts a barrier before a run only when
   its rows overlap the runs since the last barrier, or one side samples
   bytes the other stores. */
typedef struct seg_run_t {
    uint32_t span0; /* first span index of the run */
    uint32_t kernel;
    int32_t  tuple;  /* interned combine tuple; -1 = unfolded pipeline */
    uint32_t serial; /* self-aliasing draw(s): 1-workgroup serial dispatch */
    int32_t  pymin, pymax;
    /* exact byte ranges (card space, hi exclusive, empty = lo>hi):
       c/z from captured spans (staged spans excluded -- arena buffers
       cannot alias the vram import), q from every resident mip slot of
       every draw in the run */
    uint32_t c0, c1, z0, z1, q0, q1;
} seg_run_t;

/* one recorded dispatch at flush time */
typedef struct gpu_disp_t {
    uint32_t rows_base, nrows, kernel;
    int32_t  tuple;
    uint32_t serial;
    int32_t  pymin, pymax;
    uint32_t c0, c1, z0, z1, q0, q1;
    uint32_t run; /* source run index: mid fills order against it */
} gpu_disp_t;

/* One interned combine tuple: normalized selectors (stage 1 zeroed for
   1-stage kernels) plus its lazily compiled pipeline per kernel. */
typedef struct gpu_tuple_t {
    uint32_t   sel[GPU_TUPLE_SEL];
    VkPipeline pipes[GPU_KERNELS]; /* VK_NULL_HANDLE until compiled */
    uint8_t    failed[GPU_KERNELS];
    /* claimed by a thread that has left fold_mtx to sit in the driver;
       nobody else may create this pair until it publishes */
    uint8_t busy[GPU_KERNELS];
    /* parked on the live-compile queue; cleared when its worker has
       published. Guarded by fold_mtx like busy. */
    uint8_t queued[GPU_KERNELS];
    uint8_t seeded; /* from the enumeration table, not live discovery */
    /* tuple with the same combine/sten/dead sel but format-ANY (-1 =
       none): serves this tuple's draws combine-folded while the
       format-pinned pipeline is still compiling */
    int32_t any_tid;
} gpu_tuple_t;

/* One (kernel, tuple) pipeline to build. The boot pass materializes the
   whole list up front so workers claim ranges by atomic index. */
typedef struct gpu_fold_job_t {
    uint32_t kern;
    int32_t  tid;
} gpu_fold_job_t;

#    define GPU_BOOT_THREADS_MAX 16
#    define GPU_RING_SLOTS       12
/* depth governor thresholds, tenths of a percent of the pace window:
   read-fence wait above HI halves the cap, below LO grows it by one */
#    define GPU_DEPTH_HI_X 50
#    define GPU_DEPTH_LO_X 15
/* reshows of one snapshot before falling back to the fenced live scan;
   higher trades staleness on a genuinely static buffer for fewer
   per-line scan fences */
#    define GPU_SNAP_STALE_MAX 3u
#    define GPU_2D_TAGS        (R128_GPU_2D_REG_BRES + 1u)
/* Exact texture-read ranges a segment samples (one entry per distinct
   resident mip level across its draws). Past the cap the segment falls
   back to its single [q_lo,q_hi) hull, which is never less conservative. */
#    define GPU_TEXQ_CAP 256u
/* Exact VRAM intervals the segment's queued 2D stores write (one entry
   per queued fill, copy or blit destination). Past the cap the segment
   falls back to its single [f_lo,f_hi) hull, which is never less
   conservative. */
#    define GPU_FQ_CAP 64u

/* Reject-shape keys: one per gate predicate in gpu_state_kernel, in
   gate order. gpu_rej_name must list the names in the same order. */
enum {
    GPU_REJ_DRAW,     /* draw_ok=0 (capture refused the dst) */
    GPU_REJ_DST,      /* dst datatype/bpp outside the kernel set */
    GPU_REJ_ROWCAP,   /* scissor rows outside [0, GPU_ROW_CAP) */
    GPU_REJ_CZSTAGE,  /* staged c/z target not addressable */
    GPU_REJ_TEXTILE,  /* an unstaged tiled-covered texture level */
    GPU_REJ_ZSHAPE,   /* zmax-zbpp disagree */
    GPU_REJ_ZROW,     /* zrowpx=0 with a live z cell */
    GPU_REJ_STEN16,   /* stencil on a 16-bit z cell */
    GPU_REJ_FTAB,     /* reserved; table fog does not reject a draw */
    GPU_REJ_S0_FMT,   /* stage 0 texel datatype undecodable */
    GPU_REJ_S0_TOP,   /* stage 0 mip top outside [0,10] */
    GPU_REJ_S0_STAGE, /* stage 0 staged level without the arena import */
    GPU_REJ_S1_FMT,
    GPU_REJ_S1_TOP,
    GPU_REJ_S1_STAGE,
    GPU_REJ_NOKERN,  /* every predicate passed, axes name no kernel */
    GPU_REJ_KEYS
};
static const char *const gpu_rej_name[GPU_REJ_KEYS] = {
    "draw", "dst", "rowcap", "czstage", "tex-tiled", "zshape",
    "zrow", "sten16", "ftab", "s0-fmt", "s0-top", "s0-stage", "s1-fmt",
    "s1-top", "s1-stage", "nokern"
};
#    define GPU_FILL_CAP     256
#    define GPU_2DSTAGE_SIZE (1u << 21)
/* qskip/qdone tables: 0 mono, 1 host color, 2 pattern paint, 3 screen
   blit, 4 keyed blit, 5 stretch, 6 line, 7 gradient, 8 rmw,
   9 pattern-mono. Fill/copy routes >= 3 map to table route + 1. */
#    define GPU_Q2D_TABLES 10
#    define GPU_SNAPS      3

/* One deferred 2D op, executed at the head of the next submit. kind 0 =
   solid fill (vkCmdFillBuffer, rows x len bytes at pitch stride); kind 1
   = staged copy (vkCmdCopyBuffer from the host-visible 2D staging ring
   at src, spitch stride -- mono-expanded text, host-data and pattern
   pixels; spitch 0 broadcasts one staged row to every dst row); kind 2 =
   VRAM-to-VRAM copy (screen blit, src/spitch in VRAM, byte ranges
   disjoint from dst -- overlapping blits bounce through the staging
   ring instead); kind 3 = VRAM-to-staging copy (the bounce's first hop:
   addr/pitch address the staging ring, src/spitch the VRAM source).
   kind 4 = generic RMW compute dispatch (out = (d & A) | (~d & B) with
   A/B byte planes staged at src (A) and spitch (B), color = the plane
   row-index mask amod). bar = this op's byte ranges conflict with an
   earlier queued op (WAW/RAW/WAR), so a barrier must order the pair
   within the command buffer. */
typedef struct gpu_fill_t {
    uint32_t addr, pitch, len, rows, color;
    uint32_t src, spitch;
    uint8_t  kind, bar;
    /* mid: -1 = head-of-segment op. >= 0 = a host-data store into bytes
       the segment's earlier draws sample: recorded into the mirror
       between run mid-1 and run mid (stream order, no split) and into
       the import after the mirror-back. kind 1, mirror on only. */
    int32_t mid;
} gpu_fill_t;

/* VRAM byte intervals of a queued 2D op: [wlo,whi) written, [rlo,rhi)
   read (both hi-exclusive; empty = lo > hi). Staging-ring bytes are not
   VRAM and do not appear: the staged reservation protocol (write-once
   before queue, drain-all on wrap) plus the forced bar on a bounce's
   second hop order them. */
static void
gpu_fill_vranges(const gpu_fill_t *f, uint64_t *wlo, uint64_t *whi,
                 uint64_t *rlo, uint64_t *rhi)
{
    uint64_t wext = (uint64_t) (f->rows - 1) * f->pitch + f->len;
    uint64_t rext = (uint64_t) (f->rows - 1) * f->spitch + f->len;

    *wlo = 1;
    *whi = 0;
    *rlo = 1;
    *rhi = 0;
    switch (f->kind) {
        case 0:
        case 1:
            *wlo = f->addr;
            *whi = f->addr + wext;
            break;
        case 2:
            *wlo = f->addr;
            *whi = f->addr + wext;
            *rlo = f->src;
            *rhi = f->src + rext;
            break;
        case 3:
            *rlo = f->src;
            *rhi = f->src + rext;
            break;
        case 4:
            /* RMW reads and writes the dst window (rows rounded to words) */
            *wlo = f->addr & ~3ull;
            *whi = (f->addr + wext + 3u) & ~3ull;
            *rlo = *wlo;
            *rhi = *whi;
            break;
        default:
            break;
    }
}

enum {
    GF_PRESENT,
    GF_2D,
    GF_CPU,
    GF_RT,
    GF_TEX,
    GF_CAP,
    GF_OTHERDRAW,
    GF_CZHAZ,
    GF_STAGE,
    GF_STAGE_DRAW,
    GF_2DQ,
    GF_CADENCE,
    GF_IDLE,
    GF_SCAN,
    GF_CAUSES
};
static const char *const gf_name[GF_CAUSES] = {
    "present", "2d", "cpu", "rt-switch", "rtt", "capacity", "other-draw",
    "cz-hazard", "stage-recycle", "stage-draw", "2d-split", "cadence",
    "idle-poll", "scan-kick"
};

static const char *
gpu_2d_tag_name(uint32_t tag)
{
    /* clang-format off */
    switch (tag) {
        case 0x11:                return "paint-nc";
        case 0x19:                return "hostrow";
        case 0x1b:                return "bitblt-nc";
        case 0x1d:                return "ply-nextscan";
        case 0x26:                return "scale-config";
        case 0x28:                return "blit-multi";
        case 0x2d:                return "scale-flush";
        case 0x91:                return "paint";
        case 0x92:                return "cntl-bitblt";
        case 0x93:                return "smalltext";
        case 0x94:                return "hostdata-blt";
        case 0x95:                return "polyline";
        case 0x96:                return "scaling";
        case 0x97:                return "trans-scaling";
        case 0x98:                return "spanlist";
        case 0x9a:                return "paint-multi";
        case 0x9b:                return "bitblt-multi";
        case 0x9c:                return "trans-bitblt";
        case R128_GPU_2D_REG_GUI:  return "reg-gui";
        case R128_GPU_2D_REG_BRES: return "reg-bres";
        default:                    return "packet";
    }
    /* clang-format on */
}

typedef struct r128_gpu_buf_t {
    VkBuffer       b;
    VkDeviceMemory m;
    void          *map;
} r128_gpu_buf_t;

typedef struct gpu_slot_t {
    VkCommandBuffer cb;
    VkFence         fence;
    /* guards vkWaitForFences/vkResetFences on this fence: retirement
       (submit thread) vs the read-path fence wait (CPU thread) */
    pthread_mutex_t fence_mtx;
    /* 1 = some reader saw this submission's fence signaled; later
       readers skip the mutex and the wait (the bytes are already
       visible). Reset before each submitted=1 release-store, so an
       acquire-load of submitted orders the reset correctly. */
    atomic_int      fence_seen;
    VkQueryPool     tqpool;
    VkDescriptorSet dset;
    r128_gpu_buf_t  b_rows, b_order, b_spans, b_tris, b_lad, b_pal;
    uint32_t        c_lo, c_hi, z_lo, z_hi, q_lo, q_hi, r_lo, r_hi,
        f_lo, f_hi;
    /* the exact texture ranges behind q_lo/q_hi (the exact texture range
       list of r128_gpu_t, described there); published with the hull,
       before the submitted release-store */
    uint32_t texq_lo[GPU_TEXQ_CAP], texq_hi[GPU_TEXQ_CAP];
    uint32_t ntexq;
    int      texq_ovf;
    /* the exact queued 2D store intervals behind f_lo/f_hi (the exact
       2D store list of r128_gpu_t, described there); published with
       the hull, before the submitted release-store */
    uint32_t fq_lo[GPU_FQ_CAP], fq_hi[GPU_FQ_CAP];
    uint32_t nfq;
    int      fq_ovf;
    uint64_t seq;
    int      timestamped;
    /* segment samples staged texels: the arena must not recycle or move
       until this slot retires (submit thread only, like build_slot) */
    int stage_ref;
    /* bit 0 / bit 1: the color / z target is an AGP staging, so its
       interval takes no mirror copy (the kernels address the arena) */
    unsigned mir_stg;
    /* segment writes a staged c/z arena: quiesce before that arena is
       written back to guest or re-seated (submit thread only) */
    int cz_ref;
    /* segment shape for the slow-segment tripwire (prof only) */
    uint32_t   pb_ndisp, pb_rows, pb_kmask;
    atomic_int submitted;
} gpu_slot_t;

/* Present snapshot: one GPU-copied, unchanging frame in the hidden half
   of the doubled vram block. The CRTC scans the newest complete
   snapshot instead of the guest buffer, which may already carry the
   next frame's draws, so no thread waits for the GPU at flip or scan
   time. GPU_SNAPS slots work as a mailbox: one on display, one
   complete, one in flight. A flip that finds no free slot drops its
   copy, and the display keeps showing the latest complete one. */
typedef struct gpu_snap_t {
    VkCommandBuffer cb;
    VkFence         fence;
    /* CPU-thread fence poll vs CCE-thread reset+record+submit */
    pthread_mutex_t mtx;
    /* used/seq/src: written under mtx, scanned lock-free by the pick
       and victim loops -- atomic (relaxed) for C11; the ready/
       snap_displayed seq_cst handshake carries the semantic ordering */
    atomic_int       used;  /* submitted at least once; fence meaningful */
    atomic_int       ready; /* fence seen signaled since last submit */
    _Atomic uint64_t seq;   /* copy order, 0 = never */
    atomic_uint      src;   /* guest byte base this snapshot copied */
    uint32_t         len;
    uint32_t         dst; /* fixed hidden-region byte base */
} gpu_snap_t;

typedef struct r128_gpu_t {
    int verify, async;
    int fold_en; /* 0 = R128_GPU_FOLD=0, uber-only diagnostic mode */
    /* VRAM mirror: the seg kernels address a device-local copy of VRAM
       instead of the host-pointer import. Each segment copies its
       touched color/z/texture byte ranges down before the dispatches
       and the color/z ranges back after, outside the ladder/shade
       timestamps, so the import stays the only authoritative copy for
       every other consumer. The reason is shader speed: measured on an
       NVIDIA RTX 5070 under Windows, shading from the imported host
       pointer took 20.7 ns per pixel and from device-local memory
       1.6 ns. mirror_en clears only when the allocation fails. */
    int            mirror_en;
    VkBuffer       mirror_buf;
    VkDeviceMemory mirror_mem;
    VkDeviceSize   mirror_cap;

    /* Vulkan objects shared by all in-flight segment slots */
    VkInstance            inst;
    VkPhysicalDevice      phys;
    VkDevice              dev;
    VkQueue               queue;
    uint32_t              qfam;
    VkCommandPool         pool;
    uint32_t              tqbits, max_wg_x;
    float                 tqperiod;
    VkDescriptorPool      dpool;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout      plyt;
    VkPipeline            pipes[GPU_KERNELS]; /* unfolded (uber) fallbacks */
    /* V_SERIAL=1 ubers for self-aliasing draws; built on the boot
       thread. serial_ready gates acceptance: until the whole set exists
       an aliasing draw takes the CPU fallback (counted, correct). */
    VkPipeline pipes_serial[GPU_KERNELS];
    atomic_int serial_ready;
    VkPipeline lad_pipe; /* z-ladder pre-pass */
    /* generic 2D RMW op: own 2-binding layout + 28-byte push range so
       the shared seg ABI (and its warm Metal cache) is untouched */
    VkPipeline            rmw_pipe;
    VkDescriptorSetLayout rmw_dsl;
    VkPipelineLayout      rmw_plyt;
    VkDescriptorSet       rmw_dset;
    /* OV0 overlay compose: its own 2-binding layout, push block and
       command pool (the shared pool records on the CCE thread; this
       command buffer records on the vsync thread), and a fence created
       signaled so the first wait returns at once. The out buffer is
       allocated on the first compose and grows to the window size. */
    VkPipeline            ov0_pipe;
    VkDescriptorSetLayout ov0_dsl;
    VkPipelineLayout      ov0_plyt;
    VkDescriptorSet       ov0_dset;
    VkCommandPool         ov0_pool;
    VkCommandBuffer       ov0_cb;
    VkFence               ov0_fence;
    r128_gpu_buf_t        ov0_out;
    uint32_t              ov0_cap;      /* out buffer bytes, 0 = unallocated */
    uint32_t              ov0_w, ov0_h; /* geometry of the valid compose */
    int                   ov0_ok, ov0_valid;
    /* vkQueueSubmit is externally synchronized; the CCE thread and the
       vsync-thread OV0 compose can race, so every submit site locks. */
    pthread_mutex_t queue_mtx;
    /* Recursive: the whole segment build (accumulator, per-row sort
       arrays, run table, the slot's mapped span/tri buffers) is one
       backend-wide object, and the CCE thread and the CPU thread both
       reach a flush. Nested flushes are normal -- a stage write-back
       quiesces from inside one. */
    pthread_mutex_t flush_mtx;
    VkPipelineCache pcache;   /* persisted across sessions */
    VkShaderModule  seg_sm;   /* kept alive for lazy folded compiles */
    VkBuffer        vram_buf; /* imported svga.vram */
    VkDeviceMemory  vram_mem;
    /* imported tex_stage arena (staged AGP texture levels, zero-copy).
       stage_ok gates the wider state gate: without the import the kernel
       cannot see the arena and staged draws stay on the CPU path. */
    VkBuffer       stage_buf;
    VkDeviceMemory stage_mem;
    int            stage_ok;
    int            seg_staged; /* building segment packed a staged slot */
    /* imported c_stage / z_stage span arenas (staged AGP color/Z render
       targets). czstage_ok requires BOTH imports: the gate
       keeps staged-RT draws on the CPU path without it. */
    VkBuffer         cstage_buf, zstage_buf;
    VkDeviceMemory   cstage_mem, zstage_mem;
    int              czstage_ok;
    int              seg_czstaged; /* building segment writes a staged c/z */
    gpu_slot_t       slot[GPU_RING_SLOTS];
    uint32_t         build_slot, flush_2d_tag;
    _Atomic uint64_t submit_seq;
    /* seq of the newest segment queued at or before the last CRTC_OFFSET
       write: work up to it belongs to the frame the flip presents and
       must land before the scan reads it. Anything newer was drawn into
       a buffer already on display, and the scan is allowed to show it
       half drawn, as a tearing display would. */
    _Atomic uint64_t flip_seq;

    /* segment accumulation (CCE/submit thread) */
    uint32_t nspans, ntris, seg_px;
    /* deferred 2D solid fills, always recorded ahead of this segment's
       dispatches (a fill arriving with spans pending splits the segment
       first, so within any one submit every fill precedes every span) */
    gpu_fill_t fills[GPU_FILL_CAP];
    uint32_t   nfills;
    /* mid fills queued this segment (fills[].mid >= 0); run_break makes
       the next draw open a new run so it orders after the last one */
    uint32_t nmid;
    int      run_break;
    int      verify2d;
    /* CI palette snapshots this segment: count + the tex_pal_gen the
       last one was taken at (a stale gen forces a fresh snapshot) */
    uint32_t npal, pal_up_gen;
    int32_t  cur_tri;    /* tri record for the rage128_3d_tri call in flight */
    int32_t  cur_kernel; /* kernel id of the draw in flight */
    int32_t  cur_tuple;  /* interned combine tuple of the draw in flight */
    int32_t  cur_serial; /* draw in flight is a serialized alias draw */

    /* combine-tuple interning (CCE/submit thread) */
    gpu_tuple_t *tuples; /* GPU_TUPLE_CAP entries */
    uint32_t     ntuples;
    /* (kernel, tuple) pairs read from the learned list, interned but not
       yet built -- the boot pass owns building them */
    gpu_fold_job_t *learned;
    uint32_t        nlearn;
    int32_t         memo_tuple;   /* last lookup, hit by consecutive draws */
    int             quiet_intern; /* init preload: suppress the unknown log */
    /* The boot precompile runs on its own thread: device init runs on
       the Qt main thread, so a synchronous compile pass would freeze the
       whole UI for minutes. The guest boots while shaders compile; a
       segment arriving early runs the uber pipeline, which writes the
       same bytes. fold_mtx guards only the pipes[]/busy[] claim; the
       create runs outside it, so the boot fan-out and the live-compile
       workers can be in the driver at once (a pipeline cache created
       without the externally-synchronized flag is internally
       synchronized). */
    pthread_t       boot_thr;
    int             boot_thr_live;
    atomic_uint     boot_done;  /* pipelines built across all boot stages */
    uint32_t        boot_total; /* uber + serial + the folded boot set    */
    atomic_int      boot_abort;
    pthread_mutex_t fold_mtx;
    /* Live fold-compile queue. The dispatch path never compiles inline:
       a miss runs the segment on the uber pipeline, which writes the
       same bytes, and parks the (kernel, tuple) pair here for these
       workers. An inline create would stall the CCE thread for one full
       driver compile (observed at tens of seconds on a cold cache with
       AMD drivers under Windows). The ring never fills: queued[] keeps
       each pair in it at most once, and the ring holds every pair. */
    gpu_fold_job_t lq_jobs[GPU_TUPLE_CAP * GPU_KERNELS];
    uint32_t       lq_head, lq_tail; /* guarded by lq_mtx */
    uint32_t       lq_inflight;      /* guarded by lq_mtx */
    uint32_t       lq_burst;         /* guarded by lq_mtx; queued since
                                        the last drain-to-empty */
    int             lq_stop;         /* guarded by lq_mtx */
    pthread_mutex_t lq_mtx;
    pthread_cond_t  lq_cv;
    pthread_t       lq_thr[2];
    int             lq_nthr;
    seg_run_t       runs[GPU_RUN_CAP];
    uint32_t        nruns;
    uint16_t        py_of[GPU_SPAN_CAP]; /* host shadow of span py, for the sort */
    /* host shadow of each span's within-row color/z byte ranges (hi
       exclusive; z0==z1 = no z cell). The leveler's O(n^2) pair loop
       reads these instead of following sp->tri into the mapped tri
       records, which are slow for the host to read. 32-bit fields: x
       is bounded only by the scissor, and a range end that wrapped to
       0 would read as empty, so the leveler would treat overlapping
       spans as disjoint. */
    struct {
        uint32_t c0, c1, z0, z1;
    } bb_of[GPU_SPAN_CAP];
    /* commutative-stencil class per span: 0 = not in the class, else
       the draw's sten_ctl (bit 0 is set when stencil is on, so it is
       nonzero). Two spans of the class with equal (sctl, srm) never
       alias: the only bytes they share are the stencil byte, which the
       kernel updates atomically with operations that commute. The test
       here must match s_atomic in the seg kernel. */
    uint32_t sctl_of[GPU_SPAN_CAP];
    uint32_t srm_of[GPU_SPAN_CAP];
    /* sort -> batch handoff: ord = spans row-major in draw order,
       beg = each row's first index into it */
    uint32_t ord[GPU_SPAN_CAP];
    uint32_t beg[GPU_ROW_CAP];
    /* per-row span counter, then the row's scatter cursor. Kept here
       rather than on gpu_flush_body's stack; one copy is enough because
       flush_mtx lets only one flush run at a time */
    uint32_t cnt[GPU_ROW_CAP];
    /* batch leveling scratch (per row) */
    uint32_t lvl[GPU_SPAN_CAP], bylv[GPU_SPAN_CAP], lvcnt[GPU_SPAN_CAP + 1];
    /* the pair scan's byte ranges, gathered through ord[] once per row so
       the O(n^2) loop reads four flat arrays instead of indexing bb_of
       at random on every pair. Only the pairwise path fills these, so
       they need the leveling cap, not the span cap. */
    uint32_t lv_c0[GPU_BATCH_LEVEL_MAX], lv_c1[GPU_BATCH_LEVEL_MAX];
    uint32_t lv_z0[GPU_BATCH_LEVEL_MAX], lv_z1[GPU_BATCH_LEVEL_MAX];
    /* Segment render-target identity. Carries every field that maps a
       pixel row to bytes (the stride, pitch*bpp, and the tile bit, which
       permutes x within the row), so one segment cannot hold two row
       layouts over the same bases: workgroups own rows, and aliased
       rows would execute unordered. The batch builder's disjointness
       test compares within-row byte ranges, which is only valid for one
       layout at a time. The Z half is recorded only by draws that touch
       Z cells. */
    int      rt_valid, rt_z_valid;
    uint32_t rt_dst, rt_pitch, rt_bpp, rt_z, rt_zstride;
    int      rt_ctiled, rt_ztiled;
    /* segment-touched byte ranges (CCE writes, CPU thread reads) */
    atomic_uint c_lo, c_hi; /* color rows written  */
    atomic_uint z_lo, z_hi; /* z rows written      */
    /* the segment's color / z target is an AGP staging (arena-
       addressed by the kernels, its interval raw): no mirror copies */
    atomic_int  c_stg, z_stg;
    atomic_uint q_lo, q_hi; /* texture bytes queued spans sample */
    /* VRAM bytes queued blits read. Kept apart from q: blit sources
       (front and back buffer) unioned into the texture interval would
       span most of VRAM, and nearly every guest LFB store would then
       quiesce the CCE. */
    atomic_uint r_lo, r_hi;
    /* VRAM bytes queued 2D fills/copies/blits WRITE. Separate from c so
       c stays pure draw stores: the draw-path RTT/cz-hazard gates and
       the 2D split gate test draws only, while CPU/scan/read barriers
       (which must see every queued GPU write) test both. */
    atomic_uint f_lo, f_hi;
    /* Exact texture ranges behind the q hull: every resident mip level
       the pending draws sample, deduplicated, capped (texq_ovf = fall
       back to the hull). The hull is one interval unioned over every
       texture, so a 2D store that lands between two sampled textures
       reads as a write-after-read hazard against the whole segment and
       waits on its fence; the list says whether the bytes are really
       sampled. It has three writers: the draw capture appends to it
       (gpu_texq_add from capture_run_texrange), gpu_dispatch copies it
       into the slot, and gpu_accum_drop_3d resets it, also when
       rage128_gpu_abandon reaches that reset from the CPU thread with
       the executor parked. It is read only by the 2D executor's hazard
       tests. All of these run on the ring executor or on the CPU thread
       with the executor quiesced (the register-triggered 2D entry drains
       first), so unlike the hull the list needs no atomics. The
       CPU-thread barriers that can race the capture (aperture stores,
       scanout) keep testing the hull. The list holds only levels sampled
       in place from VRAM: a level staged in the texture arena (a tiled
       level copied detiled, or one in AGP memory) is left out, so a 2D
       store over the VRAM bytes of a staged tiled texture does not fence
       against a segment that samples the arena copy. That is safe
       because the copy is a snapshot taken at stage time, arena slices
       are bump-allocated and never refreshed in place (a CPU store into
       the source bytes bumps tgen, and the next draw stages a new slice),
       and an arena wrap quiesces the queued segments before any offset
       is reused. */
    uint32_t texq_lo[GPU_TEXQ_CAP], texq_hi[GPU_TEXQ_CAP];
    uint32_t ntexq;
    int      texq_ovf;
    /* Exact intervals behind the f hull: the destination bytes of every
       queued 2D fill, copy and blit of the segment, deduplicated,
       capped (fq_ovf = fall back to the hull). The hull is one interval
       unioned over every queued store, so a swap blit into the front
       buffer and a clear of the back buffer make it span the bytes
       between the two, which no queued store writes; a guest aperture
       store there would read as a hazard against the whole segment and
       quiesce the CCE for every slot in flight, and a scanned line of
       the displayed buffer there would be marked stale and re-rendered
       every frame. The hull and the list change together, through
       gpu_f_add on the thread that queues the store, and gpu_dispatch
       copies the list into the slot. The SLOT copy is tested through
       gpu_slot_f_hit by the three range tests gpu_range_hit,
       gpu_range_hit_2d and gpu_fence_range; the other readers of a
       slot's f hull (the read fence, the flip ownership test, the
       read-fence poll) keep the hull, which is never less conservative.
       The slot fields land before the submitted release-store and never
       change while the slot is submitted, so a reader's acquire of
       submitted orders them, on the CPU thread as on the executor;
       gpu_slot_f_hit says how a reader overtaken by a reuse of the slot
       is kept conservative. The PENDING list is tested through
       gpu_pending_f_hit by the two pending tests gpu_pending_hit and
       gpu_pending_hit_2d, which the CPU-thread barriers (aperture
       stores, scanout, present reads) run while the executor may be
       appending to it. fq_gen makes that read safe: every change of the
       pending f hull and list (gpu_f_add, rng_reset) sits between two
       increments of the generation, so the count is odd while a change
       is in progress, and a reader that finds the generation odd, or
       changed across its walk, has no verdict and falls back to the hull
       hit it already has. The release fence after the first increment
       and before the second, with the acquire fences of the reader,
       order the hull and list words against the generation on every
       target; a reader can still load a list word the writer is storing,
       and discards it through the generation check. gpu_fq_add is the
       list half of a change and is only called inside one. */
    uint32_t         fq_lo[GPU_FQ_CAP], fq_hi[GPU_FQ_CAP];
    uint32_t         nfq;
    int              fq_ovf;
    _Atomic uint32_t fq_gen;
    /* the resident ranges of the draw the list last absorbed, per stage
       and mip slot (two stages, eleven slots), with the top slot index
       of each stage (-1 = stage off). The range walk runs once per
       TRIANGLE, and a draw's thousands of triangles share one texture
       set, so comparing against this memo is what keeps the list build
       off the per-triangle cost; the dedup scan runs only when the set
       changes. */
    uint32_t texq_memo_lo[2][11], texq_memo_hi[2][11];
    int      texq_memo_top[2];
    int      texq_memo_valid;
    /* CPU 2D operations through rage128_gpu_2d_barrier whose hull hit
       the exact list cleared (no fence) or confirmed, one count per
       operation: the barrier asks the pending segment and every
       submitted slot more than once per operation, so gpu_texq_hit
       marks the two verdicts here and the barrier folds the marks into
       the counters once (gpu_texq_op_begin / gpu_texq_op_end). A
       confirmed operation is not also counted as cleared. The split
       gates of queued 2D operations (gpu_pending_draw_hit) set the
       marks too, but no operation boundary folds them, so they are not
       counted. st_texq_ovf counts segments whose list fell back to the
       hull, over the cap or by a wrapped fetch. */
    int        texq_op_spared, texq_op_real;
    uint64_t   st_texq_spared, st_texq_real, st_texq_ovf;
    /* slot f hull hits the exact list cleared / confirmed, counted per
       range test on whichever thread ran it, the same for pending f
       hull hits (a walk the generation check voided counts as neither),
       and segments whose list fell back to the hull over the cap */
    _Atomic uint64_t st_fq_spared, st_fq_real;
    _Atomic uint64_t st_fqp_spared, st_fqp_real;
    uint64_t         st_fq_ovf;
    atomic_int pending;
    /* submitted slots, counted up BEFORE a slot's submitted flag rises
       and down AFTER it clears, so a zero read here is never narrower
       than the slot scan. Lives on this line with pending: a guest
       aperture store between two instructions cannot afford twelve
       slot cache lines to learn the GPU is idle. */
    atomic_uint inflight;

    /* verify mode: shadow of the segment for interpreter replay */
    rage128_raster_state_t *v_states;
    uint32_t                v_state_n, v_state_cap;
    struct v_tri_t {
        uint32_t  state;
        uint32_t  line; /* 1 = line walk a->b (v[2] unused) */
        r3d_vtx_t v[3];
    }       *v_tris;
    uint32_t v_tri_n, v_tri_cap;
    uint8_t *v_save; /* pre-image + GPU snapshot scratch */
    uint32_t v_save_cap;

    /* stats */
    uint64_t st_segments, st_runs, st_spans, st_tris, st_fallback,
        st_flush[GF_CAUSES];
    uint64_t st_verify_seg, st_verify_bad;
    int      rejects_logged;
    /* Reject-shape tally: one bump per FAILED predicate per rejected
       draw (a draw failing two predicates counts under both), so the
       histogram names every gate clause live traffic is hitting even
       after the 16 verbatim lines go quiet. */
    uint64_t st_rej[GPU_REJ_KEYS], st_rej_total;
    /* Accept-side coverage tally (always on, per tri). A clean verify run
       only proves the state mix the title happened to use; this tells an
       exercised path from one that was never reached.
       Indexed by dst datatype / kernel id; partial = wmask not all-ones. */
    uint64_t st_cov_dt[16], st_cov_k[GPU_KERNELS];
    uint64_t st_cov_dith[2], st_cov_wmask_partial;
    uint64_t st_cov_spec, st_cov_fogv, st_cov_fogt, st_cov_ck;
    uint64_t st_cov_smip; /* tris sampling >=1 staged (AGP) mip level */
    /* tris writing a staged (AGP) color / Z render target */
    uint64_t st_cov_cstg, st_cov_zstg;
    uint64_t st_cov_affine; /* textured tris with the persp divide off  */
    /* line/point primitives routed through the tri decomposition; gpu =
       both half-tris captured, cpu = at least one half fell back */
    uint64_t st_cov_line[2], st_cov_point[2];
    uint64_t st_cov_affine_lod; /* affine tris that also consume per-pixel
                                   LOD (raw screen-space gradients) */
    /* texture format, per enabled stage per tri (a 2-stage tri counts
       twice); dt 0 splits by the cntl [27:26] S3TC block class */
    uint64_t st_cov_tex[16], st_cov_texs3[4];
    /* depth cell shape: [0] z off, [1] u16, [2] 4-byte depth-low,
       [3] 4-byte depth-high */
    uint64_t st_cov_z[4];
    /* stencil draws by byte position: [0] low ([7:0], 32-bit Z),
       [1] high ([31:24], 24-bit Z); [2] of those, Z test off */
    uint64_t st_cov_sten[3];

    /* R128_GPU_PROF=1: disjoint cost-center totals, submit thread only.
       capture = tri setup + span capture + tri record write;
       sort = flush-prep counting sort + rows/order build;
       record = cmdbuf record + queue submit; wait = actual fence waits. */
    int prof;
    /* memory types already named in the log by gpu_mk_buf (bit = index) */
    uint32_t mkbuf_logged;
    uint64_t pr_t0, pr_capture_ns, pr_sort_ns, pr_record_ns, pr_wait_ns;
    /* sort subphase split: level = the pairwise leveling scan, emit = the
       level ordering + slot emission, stat = the prof-only per-span
       accounting in the row loop (inside the sort bucket, so it must come
       back out before sort is attributed). The residual is the per-run
       counting sort and scatter. lvpairs counts alias tests: the
       quadratic term itself, independent of clock resolution. */
    uint64_t pr_sort_level_ns, pr_sort_emit_ns, pr_sort_stat_ns;
    uint64_t pr_sort_rows, pr_lvpairs;
    /* Frame timeline (dev->ftl_*), summed per prof interval: flip-to-flip
       period, flip-tail fence, CCE idle per frame, flip -> latch,
       flip -> first draw; frames whose latch/draw never came. */
    uint64_t pr_ftl_n, pr_ftl_period, pr_ftl_fence, pr_ftl_idle;
    uint64_t pr_ftl_latch, pr_ftl_draw, pr_ftl_nolatch, pr_ftl_nodraw;
    uint64_t pr_ftl_max_period, pr_ftl_max_fence, pr_ftl_max_idle;
    uint64_t pr_ftl_max_draw;
    /* Capture shape (prof only): rows that survive the early-out, pixels
       walked at the clip seed (the dependent double-add chain that steps
       zline from the bounding-box edge to the covered edge) and total
       bounding-box row width. capture_span is called per row, far too
       often to time with a clock, so these counts are kept instead. */
    uint64_t pr_cap_rows, pr_cap_lo, pr_cap_bbox;
    /* Leveling shape (prof only): rows, alias pairs and scan time binned
       by spans-in-row, plus row class -- [0] uniform commutative (fast
       path), [1] no commutative-stencil span at all (pure interval
       overlap), [2] mixed. Shows which rows the quadratic time is spent
       on and how much of it a range-max structure could remove. */
    uint64_t pr_lvbin_rows[GPU_LVBINS], pr_lvbin_pairs[GPU_LVBINS];
    uint64_t pr_lvbin_ns[GPU_LVBINS];
    uint64_t pr_lv_rows_cls[3], pr_lv_pairs_cls[3];
    uint64_t pr_wait_reuse_ns, pr_wait_drain_ns;
    uint64_t pr_wait_drain_cause_ns[GF_CAUSES];
    /* CPU-thread CCE quiesce inside rage128_gpu_cpu_barrier: ring pump
       plus executor join, not fence waits; the barrier's cost that the
       fence counters do not show. */
    uint64_t pr_cpu_quiesce_ns;
    uint64_t st_cpu_barrier_hits;
    uint64_t pr_read_fence_ns, st_read_fences;
    /* scanout over post-flip writes: pending segments it submitted without
       waiting, and lines it rendered ahead of an unfinished GPU write */
    uint64_t st_scan_kicks, st_scan_stale;
    /* Scanout's submit request to a busy CCE executor: the emulation
       thread sets scan_req when a scanned line overlaps the pending
       segment while the executor holds work; the executor clears it and
       submits (rage128_gpu_scan_serve). defers = scanned lines that left
       the submit to the executor (emulation thread), served = submits
       the executor made for a request (CCE thread; atomic because
       another thread may read it while the executor runs). */
    atomic_int       scan_req;
    uint64_t         st_scan_defers;
    _Atomic uint64_t st_scan_served;
    /* Fence-pressure depth governor. rf_win_ns accumulates the CPU
       thread's read-fence wait since the last pace window (only the CPU
       thread writes and resets it, so it needs no atomics). depth_cap
       bounds the submitted slots and is enforced at submit by retiring
       the oldest first; the CCE thread reads it and the CPU thread's
       control law stores it. The measured wait sets the cap directly. */
    uint64_t    rf_win_ns;
    atomic_uint depth_cap;
    uint64_t    pr_wait_depth_ns, st_wait_depth;
    /* Read-fence address histogram: 64 KB buckets keyed on addr >> 16,
       linear probe, tail past the table dropped rather than grown.
       Separates a driver polling one scratch word from a real readback. */
    uint32_t pr_rf_key[GPU_RF_BUCKETS], pr_rf_used, pr_rf_dropped;
    uint64_t pr_rf_cnt[GPU_RF_BUCKETS], pr_rf_ns[GPU_RF_BUCKETS];
    uint64_t pr_int_epoch, pr_int_t0, pr_int_gpu_ns, pr_int_reuse_ns;
    uint64_t pr_int_present_ns, pr_int_2d_ns;
    uint64_t pr_2d_tag_ns[GPU_2D_TAGS], pr_int_2d_tag_ns[GPU_2D_TAGS];
    uint64_t pr_int_submits, pr_int_chained;
    uint64_t pr_int_tsc, pr_int_polls, pr_int_cce_ns;
    /* per-segment cost split at interval start: GPU ladder vs mirror
       copies, host capture/sort/record -- names which side saturates
       when submits/s spike */
    uint64_t pr_int_lad_ns, pr_int_mir_ns, pr_int_mir_bytes;
    uint64_t pr_int_cap_ns, pr_int_sort_ns, pr_int_rec_ns;
    uint32_t pr_int_max_inflight;
    /* GUI_STAT idle poll: last answer and polls since the last fence
       query. A driver can read this register millions of times a second
       while a frame renders; one Vulkan status query per 32 polls bounds
       the host cost, at the price of reporting idle up to 31 polls
       late. CPU thread only. */
    uint32_t       idle_polls;
    int            idle_busy;
    uint32_t       pr_int_reads[0x4000 >> 2]; /* owner->tel_reads at interval start */
    rage128_lfbt_t pr_int_lfbt;               /* owner->lfbt at interval start */
    uint32_t       pr_int_hitlog;             /* barrier hits logged this interval */
    uint64_t       pr_px;
    /* occupancy shape: wg = workgroups dispatched (one per row per run);
       crit = summed longest row per dispatch in BATCHES, i.e. the serial
       pass chain the whole dispatch waits on while shorter rows idle.
       Wall time per dispatch tracks crit, so crit is the lever.
       crit_lvl = the same chain in LEVELS: crit / crit_lvl is how many
       batches a level splits into on the longest row, i.e. the same-level
       work one workgroup serializes that a fanout could spread. */
    uint64_t pr_wg, pr_crit, pr_bat, pr_crit_lvl;
    uint32_t pr_row_nlv; /* levels of the row gpu_batch_row just packed */
    /* pr_lvl vs pr_bat decides whether packing is level-bound (already
       optimal for this scheme) or lane-bound; pr_slots gives lane fill. */
    uint64_t pr_lvl, pr_slots, pr_px_k[GPU_KERNELS], pr_px_zless;
    uint64_t pr_dbar, pr_delide, pr_dtex;
    uint64_t pr_gpu_lad_ns, pr_gpu_shade_ns, pr_gpu_seg;
    uint64_t pr_gpu_mirror_ns, pr_mirror_bytes; /* mirror probe copies */
    uint64_t pr_rows_k[GPU_KERNELS], pr_erows_k[GPU_KERNELS];
    uint64_t pr_bat_k[GPU_KERNELS], pr_ebat_k[GPU_KERNELS];
    uint64_t pr_epx_k[GPU_KERNELS], pr_runs_k[GPU_KERNELS];
    uint64_t pr_wg_k[GPU_KERNELS], pr_pwg_k[GPU_KERNELS];
    uint64_t pr_wgh_cur[GPU_KERNELS][GPU_WGH_BINS];
    uint64_t pr_wgh_proj[GPU_KERNELS][GPU_WGH_BINS];
    uint64_t st_cap[4], st_nruns;
    /* correctness tripwires, printed on the close line unconditionally:
       st_toobig = draws priced past a whole segment, rejected to the CPU
       path (rendered correctly there); st_spandrop = spans the capture
       loop dropped past the accept bound (must stay 0 -- every one is a
       never-rendered pixel run). */
    uint64_t st_toobig, st_spandrop;
    uint64_t st_pxdrop; /* pixels the capture could not record (px cap) */
    /* exact re-price of draws whose bounding-box estimate exceeds a cap:
       count-only walk tallies (dry_*), st_reprice = walks run; st_toobig
       counts only draws still over the cap on the exact count.
       st_reprice_miss = accepted re-priced draws whose real capture used
       a different span/px count than the dry walk predicted. It must
       stay 0: the prediction is the arena reservation, and a miss can
       drop pixels. */
    uint64_t st_reprice, st_reprice_miss;
    uint64_t dry_spans, dry_px;
    /* per-site split of the two alias rejects feeding st_fallback:
       [0] = color/z cross-alias, [1] = render-to-texture self-sample.
       ser = draws kept on GPU by the serial path instead. */
    uint64_t st_alias_rej[2], st_alias_ser[2];
    /* accepted draws on the straddling-cell path (odd dst/z offset) */
    uint64_t st_odd_c, st_odd_z;
    /* shape split of st_fallback: line/point submits have no GPU path
       and always route through the guard; the tri-shaped remainder is
       kern<0 + toobig + alias rejects */
    uint64_t st_fb_line, st_fb_point;
    /* row-span width shape: wide = parent rows wider than the 256px
       workgroup chunk, px = their pixel share of st_px_total */
    uint64_t st_px_total, st_wide_spans, st_wide_px;
    uint64_t st_submits, st_chained, st_wait_reuse;
    uint64_t st_wait_drain[GF_CAUSES];
    uint64_t st_2d_requests[GPU_2D_TAGS], st_2d_waits[GPU_2D_TAGS];
    /* GPU fill path: enqueued rects/rows/px, cpu = eligible-shaped fills
       bounced to the CPU walk (AGP or wrapped range, or no staging
       space for the resolved bytes), bad = verify2d
       bytes that did not match the broadcast color; tiled = rects of
       those queued over a tiled dst as tile-layout pieces */
    uint64_t st_fill_rects, st_fill_rows, st_fill_px, st_fill_cpu, st_fill_bad,
        st_fill_tiled;
    uint64_t st_fill_cpu_r[2]; /* 0 AGP or wrap, 1 staging is NULL */
    uint64_t st_fill_skip[5];  /* rop, wmask, aux, pattern, cca */
    /* mono-expand queue: runs = leave-alone set-bit run fills, orects =
       opaque expanded copies; host-data copy queue: rects/rows/bytes.
       cpu = eligible-shaped op bounced by the gpu-side checks. qskip =
       predicate refusals per path (0 mono, 1 host-color, 2 pattern
       paint, 3 screen blit), bits in the order rage128_gpu_2d_qskip
       lists them: rop, wmask, aux, more runs than the cap, color
       compare, address range, staging full. */
    uint64_t st_mono_runs, st_mono_px, st_mono_orects, st_mono_obytes,
        st_mono_cpu;
    uint64_t st_copy_rects, st_copy_rows, st_copy_bytes, st_copy_cpu,
        st_copy_bad;
    /* host copies absorbed mid-segment / splits forced by a later op or
       draw conflicting with one already queued mid */
    uint64_t st_copy_mid, st_copy_mid_split;
    /* pattern-brush paint queue: copies = broadcast tile copies, runs =
       leave-alone run fills; screen-blit queue: bounce = overlapping
       rects routed through the staging ring. */
    uint64_t st_pat_copies, st_pat_runs, st_pat_px, st_pat_cpu;
    uint64_t st_blit_rects, st_blit_rows, st_blit_bytes, st_blit_bounce,
        st_blit_cpu, st_blit_bad;
    uint64_t st_qskip[GPU_Q2D_TABLES][7];
    /* whole 2D ops bounced to the CPU walk because a tile bit is live
       and the queue has no tile-aware form for the op (every queue
       path but the solid fill addresses rows linearly; the solid fill
       bounces only the shapes rage128_gpu_2d_fill_tiled refuses); its
       own key so a tiled workload is distinguishable from no 2D
       traffic at all */
    uint64_t st_tiled_cpu;
    /* CPU-resolved queue families (tables 4-7: keyed blit, stretch,
       line, gradient): rects = whole ops fully queued, runs/bytes ride
       the fill/copy route, cpu = gpu-side bounce, bad = bytes that
       differ from the CPU walk in 2D verify mode (vq2d). */
    uint64_t st_qdone[GPU_Q2D_TABLES], st_q_runs[GPU_Q2D_TABLES],
        st_q_bytes[GPU_Q2D_TABLES], st_q_cpu[GPU_Q2D_TABLES],
        st_qbad[GPU_Q2D_TABLES];
    /* whole ops queued at a non-32bpp dst (0 mono, 1 host color,
       2 pattern, 3 solid fill) -- any-bpp slice attribution */
    uint64_t st_qnb[4];
    /* whole ops queued under PER-PIXEL aux-scissor acceptance (aux on,
       whole-rect accept failed): 0 mono, 1 host color, 2 pattern,
       3 blit, 8 solid-fill RMW -- auxseg= attribution */
    uint64_t st_qaux[GPU_Q2D_TABLES];
    /* 2D staging ring (host-visible transfer source, bump-allocated; a
       recycle flushes and drains so no consumer is left in flight) */
    r128_gpu_buf_t stage2d;
    uint32_t       stage2d_head;
    int            stage2d_ok;
    uint64_t       st_2ds_bytes, st_2ds_recycles, st_2ds_full;
    uint64_t       st_reaped;
    /* present snapshots */
    gpu_snap_t  snaps[GPU_SNAPS];
    uint64_t    snap_seq_ctr;
    uint32_t    snap_stride;    /* hidden-region bytes per slot; 0 = off */
    atomic_uint snap_displayed; /* dst the CRTC scans this frame, 0 none */
    uint64_t    snap_last_seq;  /* CPU thread: staleness tracking */
    uint32_t    snap_stale;
    uint64_t    st_snap_copies, st_snap_drops, st_snap_shows, st_snap_real;
    /* live-scan fallback reasons: 0 backward, 1 stale, 2 slots hold
       other bases only, 3 a slot holds this base but its copy is in
       flight, 4 no slot holds anything yet */
    uint64_t    st_snap_why[5];
    uint64_t    st_snap_reshow, st_snap_late;
    atomic_int *pr_pace_hold; /* owner's pace_hold_us, for telemetry */
    rage128_t  *owner;        /* its poll and CCE-busy counters, for telemetry */
    uint32_t    st_max_inflight;
    /* combine-fold bookkeeping (always on, not prof-gated):
       st_tsplit = runs opened by a tuple change alone (the extra runs
       folding costs); st_tovfl = draws past GPU_TUPLE_CAP (unfolded
       fallback); st_tunknown = live tuples outside the enumeration
       table (vid_ati_rage128_gpu_comb.h), each one a gap in the table
       and logged; the boot/live counters give pipeline counts and
       compile time. */
    uint64_t st_tsplit, st_tovfl, st_tunknown;
    uint64_t st_boot_pipes, st_boot_ns, st_live_pipes, st_live_ns;
    /* OV0 compose: frames dispatched / window pixels / frames bounced to
       the CPU compositor / verify-mode pixel mismatches */
    uint64_t st_ov0_frames, st_ov0_px, st_ov0_cpu, st_ov0_bad;
} r128_gpu_t;

static inline uint64_t
prof_now(void)
{
    return rage128_now_ns();
}

static unsigned
prof_wgh_bin(uint32_t n)
{
    static const uint32_t edge[GPU_WGH_BINS - 1] = {
        48, 120, 240, 480, 960, 1920, 3840, 7680
    };

    for (unsigned i = 0; i < GPU_WGH_BINS - 1; i++)
        if (n < edge[i])
            return i;
    return GPU_WGH_BINS - 1;
}

/* bin b holds rows with [2^b, 2^(b+1)) spans */
static unsigned
prof_lv_bin(uint32_t n)
{
    unsigned b = 0;

    while (b < GPU_LVBINS - 1u && n >= (2u << b))
        b++;
    return b;
}

static uint64_t
gpu_ts_delta(const r128_gpu_t *g, uint64_t a, uint64_t b)
{
    if (g->tqbits >= 64)
        return b - a;
    return (b - a) & ((1ull << g->tqbits) - 1ull);
}

/* ------------------------------------------------------------------ */
/* Combine-tuple interning + folded pipelines.                         */
/* ------------------------------------------------------------------ */

static VkPipeline gpu_fold_compile(r128_gpu_t *g, uint32_t kern, int32_t tid,
                                   int boot);
static void       gpu_live_sb(r128_gpu_t *g);
static void       gpu_persist_save(r128_gpu_t *g);

/* Fold combine codes that compute the same result onto one encoding, so
   drivers that pick different codes for one operation do not create two
   pipelines for the same shader. DIS (0) outputs the texel color and
   alpha, COPY (1) the color and alpha factors (SDK: Texture Mapping,
   p. 6-42 / PDF 154, Table 6-7; SDK: Texture Mapping, p. 6-44 /
   PDF 156, Table 6-10), so the two agree only while COLOR_FACTOR
   selects the texel color (4) and ALPHA_FACTOR the texel alpha (6), as
   the interpreter (r3d_tex_combine) computes them. Mesa r128
   r128_texstate.c r128UpdateTextureEnv emits DIS with those factors for
   the OpenGL replace texture environment, while xf86-video-r128
   r128_exa_render.c
   R128CCEPrepareComposite emits R128_COMB_COPY with them. COPY ignores
   the R128_COMB_FCN_MSB bit and DIS does not (DIS with it subtracts),
   so a COPY clears that bit and folds to DIS only with the texel
   factor; a COPY with another factor keeps its own tuple. The alpha
   codes fold only on the first stage: on the second, DIS is modeled as
   passing the incoming alpha through, as the interpreter models it, so it no
   longer agrees with a COPY of the texel alpha there. A DIS alpha
   never reads its input factor on either stage, so that selector is
   pinned to one value. An all-zero stage is off and left alone. */
static void
gpu_tuple_canon(uint32_t sel[GPU_TUPLE_SEL])
{
    for (int st = 0; st < 2; st++) {
        uint32_t *s = &sel[st * 7];

        if (!s[0] && !s[1] && !s[2] && !s[3] && !s[4] && !s[5] && !s[6])
            continue;
        if (s[0] == 1) {
            s[1] = 0;
            if (s[2] == 4)
                s[0] = 0;
        }
        if (st == 0 && s[4] == 1 && s[5] == 6)
            s[4] = 0;
        if (s[4] == 0)
            s[6] = 2;
    }
}

/* Normalized selector tuple of the draw's combine state. Stage 1 is
   zeroed for 1-stage kernels so lightmap-off draws do not mint spurious
   tuples; untextured kernels never call this (they run unfolded). */
static void
gpu_tuple_normalize(const rage128_draw_state_t *d, int texn,
                    uint32_t sel[GPU_TUPLE_SEL])
{
    /* chroma key discards fragments, so a keyed draw's z/stencil writes
       depend on the texture pipe -- never dead-color fold it */
    int dead = d->wmask == 0 && !d->atest_en && !d->need_ck;

    for (int st = 0; st < 2; st++) {
        int on = st < texn && !dead;

        sel[st * 7 + 0] = on ? d->comb[st].comb : 0;
        sel[st * 7 + 1] = on ? d->comb[st].fmsb : 0;
        sel[st * 7 + 2] = on ? d->comb[st].cfac : 0;
        sel[st * 7 + 3] = on ? d->comb[st].ifac : 0;
        sel[st * 7 + 4] = on ? d->comb[st].comba : 0;
        sel[st * 7 + 5] = on ? d->comb[st].afac : 0;
        sel[st * 7 + 6] = on ? d->comb[st].ifaca : 0;
        /* texel format rides the tuple too: pinned for live
           stages, ANY for off/dead stages so a dead draw's texture
           state cannot mint spurious tuples */
        sel[16 + st * 2] = on ? (d->sh[st].dt | (d->sh[st].aone ? 0x10u : 0u)) : GPU_TUPLE_FMT_ANY;
        sel[17 + st * 2] = on ? d->sh[st].s3tc : GPU_TUPLE_FMT_ANY;
    }
    sel[14] = d->sten_on ? 1u : 0u;
    sel[15] = dead ? 1u : 0u;
    /* texture lighting rides the tuple as one packed word so the folded
       pipeline pins its three selectors too; 0 for every unlit draw */
    sel[GPU_TUPLE_LIGHT] = (d->light_on && !dead)
        ? (d->lcomb.comb | (d->lcomb.fmsb << 4) | (d->lcomb.comba << 5))
        : 0u;
    gpu_tuple_canon(sel);
}

/* Intern a normalized tuple; -1 past GPU_TUPLE_CAP (unfolded fallback).
   A live tuple missing from the seed table in vid_ati_rage128_gpu_comb.h
   still renders (its pipeline compiles on demand), but it is a gap in
   that table, so it is logged. */
static int32_t
gpu_tuple_intern(r128_gpu_t *g, const uint32_t sel[GPU_TUPLE_SEL])
{
    gpu_tuple_t *t;

    if (g->memo_tuple >= 0
        && !memcmp(g->tuples[g->memo_tuple].sel, sel,
                   GPU_TUPLE_SEL * sizeof(uint32_t)))
        return g->memo_tuple;
    for (uint32_t i = 0; i < g->ntuples; i++)
        if (!memcmp(g->tuples[i].sel, sel, GPU_TUPLE_SEL * sizeof(uint32_t))) {
            g->memo_tuple = (int32_t) i;
            return (int32_t) i;
        }
    if (g->ntuples >= GPU_TUPLE_CAP) {
        g->st_tovfl++;
        return -1;
    }
    t = &g->tuples[g->ntuples];
    memcpy(t->sel, sel, GPU_TUPLE_SEL * sizeof(uint32_t));
    t->seeded  = g->quiet_intern ? 1 : 0;
    t->any_tid = -1;
    {
        int any = sel[16] == GPU_TUPLE_FMT_ANY && sel[17] == GPU_TUPLE_FMT_ANY
            && sel[18] == GPU_TUPLE_FMT_ANY && sel[19] == GPU_TUPLE_FMT_ANY;

        /* link both directions: a format-pinned tuple finds its
           format-ANY partner, and an ANY tuple interned later adopts
           earlier pinned ones (the learned list is in no particular
           order). The partner is bound in place of the pinned pipeline
           while that one compiles, so it must match everything the
           pinned one pins except the format words: the combine,
           stencil and dead selectors and the light word. */
        for (uint32_t i = 0; i < g->ntuples; i++) {
            gpu_tuple_t *o = &g->tuples[i];

            if (memcmp(o->sel, sel, 16 * sizeof(uint32_t)) != 0
                || o->sel[GPU_TUPLE_LIGHT] != sel[GPU_TUPLE_LIGHT])
                continue;
            if (any && o->any_tid < 0
                && (o->sel[16] != GPU_TUPLE_FMT_ANY
                    || o->sel[17] != GPU_TUPLE_FMT_ANY
                    || o->sel[18] != GPU_TUPLE_FMT_ANY
                    || o->sel[19] != GPU_TUPLE_FMT_ANY))
                o->any_tid = (int32_t) g->ntuples;
            else if (!any && o->sel[16] == GPU_TUPLE_FMT_ANY
                     && o->sel[17] == GPU_TUPLE_FMT_ANY
                     && o->sel[18] == GPU_TUPLE_FMT_ANY
                     && o->sel[19] == GPU_TUPLE_FMT_ANY)
                t->any_tid = (int32_t) i;
        }
    }
    if (!t->seeded) {
        /* a stencil variant of a seeded combine is not a gap: the seed
           table lists combines only, so only the stencil bit is new */
        int comb_known = 0;

        for (uint32_t i = 0; i < g->ntuples; i++)
            if (g->tuples[i].seeded
                && !memcmp(g->tuples[i].sel, sel,
                           GPU_TUPLE_COMB * sizeof(uint32_t))) {
                comb_known = 1;
                break;
            }
        if (!comb_known) {
            g->st_tunknown++;
            gpu_log("RAGE128 GPU: combine tuple outside enumeration: "
                    "st0=%u/%u/%u/%u/%u/%u/%u st1=%u/%u/%u/%u/%u/%u/%u "
                    "sten=%u dead=%u fmt=%d/%d %d/%d\n",
                    sel[0], sel[1], sel[2], sel[3], sel[4], sel[5], sel[6],
                    sel[7], sel[8], sel[9], sel[10], sel[11], sel[12], sel[13],
                    sel[14], sel[15], (int32_t) sel[16], (int32_t) sel[17],
                    (int32_t) sel[18], (int32_t) sel[19]);
        }
    }
    g->memo_tuple = (int32_t) g->ntuples;
    return (int32_t) g->ntuples++;
}

/* Host-side cache files (driver-specific host state, not VM state):
   the Vulkan pipeline-cache blob and the learned (kernel, tuple) pair
   list, both in the 86Box global config dir. */
static void
gpu_cache_path(char *buf, size_t len, const char *name)
{
    char   dir[1024];
    size_t n;

    plat_get_global_config_dir(dir, sizeof(dir));
    /* the platform layer already appends its own separator; adding a
       second one mixes separators on hosts that use a backslash */
    n = strlen(dir);
    snprintf(buf, len, "%s%s%s", dir,
             (n && (dir[n - 1] == '/' || dir[n - 1] == '\\')) ? "" : "/",
             name);
}

/* Shared state for the boot fan-out: one job list, claimed by atomic
   index. Nothing else is written concurrently: pipes[]/failed[] are per
   (tuple, kernel) and no two jobs name the same pair. */
typedef struct gpu_boot_ctx_t {
    r128_gpu_t     *g;
    gpu_fold_job_t *jobs;
    uint32_t        njobs;
    atomic_uint     next;
    atomic_uint     done;
} gpu_boot_ctx_t;

/* Boot precompile policy. The default is the learned list. The full
   seed enumeration builds every seeded tuple for every kernel with the
   same stage count, most of which a title never binds, and on a cold
   cache that was observed to take hours with AMD drivers under
   Windows. Tuples not built at boot
   compile in the background on first draw while the uber kernel serves
   the frame. 0 = skip, 1 = learned list (default), 2 = full enumeration.
   R128_GPU_PRECOMP accepts 0, 1, list, 2, full; anything else means the
   default, never the full sweep. */
static int
gpu_precomp_mode(void)
{
    const char *pe = getenv("R128_GPU_PRECOMP");

    if (pe && !strcmp(pe, "0"))
        return 0;
    if (pe && (!strcmp(pe, "2") || !strcmp(pe, "full")))
        return 2;
    return 1;
}

/* A cold boot compiles in three stages (uber, serial, then folded),
   and all three count into one boot_done/boot_total so the bar is
   continuous from the first pipeline. In full mode the folded set comes
   from the static seed table; in list mode it is the learned list, so
   the caller must refresh the total once gpu_tuples_load has run. */
static uint32_t
gpu_boot_total(r128_gpu_t *g)
{
    uint32_t nk[3] = { 0, 0, 0 };
    uint32_t n     = 0;
    int      mode  = gpu_precomp_mode();

    if (mode == 0)
        return GPU_KERNELS * 2;
    if (mode == 1)
        return GPU_KERNELS * 2 + g->nlearn;
    for (uint32_t v = 0; v < GPU_KERNELS; v++)
        if (r128_gpu_variants[v].tex < 3)
            nk[r128_gpu_variants[v].tex]++;
    for (size_t i = 0; i < sizeof(r128_gpu_comb_seed) / sizeof(r128_gpu_comb_seed[0]); i++)
        if (r128_gpu_comb_seed[i].stages < 3)
            n += nk[r128_gpu_comb_seed[i].stages];
    /* the two hand-interned tuples: dead color and shadow-volume */
    n += 2 * nk[1];
    return n + GPU_KERNELS * 2;
}

/* Every worker calls this to count a finished pipeline, so the count is
   atomic. Only the caller passing ui = 1 writes the status bar, which
   keeps ui_sb_set_text single-threaded however wide the fan-out is. */
static void
gpu_boot_tick(r128_gpu_t *g, int ui)
{
    uint32_t d = atomic_fetch_add(&g->boot_done, 1) + 1;
    char     sb[96];

    if (!ui)
        return;
    snprintf(sb, sizeof(sb), "Compiling Shaders %u/%u", d,
             g->boot_total);
    ui_sb_set_text(sb);
}

/* Claim and compile boot jobs until the list is empty or the VM closes.
   Only the caller passing ui = 1 touches the status bar and the log. */
static void
gpu_boot_drain(gpu_boot_ctx_t *c, int ui)
{
    r128_gpu_t *g = c->g;

    for (;;) {
        uint32_t i = atomic_fetch_add(&c->next, 1);
        uint32_t d;

        if (i >= c->njobs || atomic_load(&g->boot_abort))
            return;
        gpu_fold_compile(g, c->jobs[i].kern, c->jobs[i].tid, 1);
        d = atomic_fetch_add(&c->done, 1) + 1;
        /* a cold first boot takes minutes and must not look like a hang */
        gpu_boot_tick(g, ui);
        if (ui)
            gpu_log("RAGE128 GPU: precompiling folded pipelines %u/%u\n", d,
                    c->njobs);
    }
}

static void *
gpu_boot_worker(void *arg)
{
    gpu_boot_drain((gpu_boot_ctx_t *) arg, 0);
    return NULL;
}

/* Compile threads. The pass is host CPU work (on macOS, SPIRV-Cross and
   then the Metal compiler) and the jobs are independent, so it scales
   with cores; two cores are left to the emulated machine and the UI,
   which keep running while it drains.

   Drivers that compile in process hold a large temporary allocation per
   concurrent compile, so the default thread count is bounded by free
   memory too, or a wide boot can run the host out of memory. The budget
   is about twice the worst per-compile peak seen during a one-thread
   precompile; re-measure it when the kernel source grows. Available
   physical memory already excludes what this process holds, so keeping
   a quarter of it free is the only other margin needed. */
#    define GPU_BOOT_MEM_PER_COMPILE (1ull << 30)

/* Threads past this buy nothing because the driver serializes
   internally: in a cold precompile, 16 threads were slower than 6 and
   used more memory. This cap, not memory, usually bounds the default. */
#    define GPU_BOOT_THREADS_AUTO_MAX 6

static int
gpu_mem_thread_cap(void)
{
    uint64_t avail = 0;
    int      n;

#    ifdef _WIN32
    MEMORYSTATUSEX msx = { .dwLength = sizeof(msx) };

    if (GlobalMemoryStatusEx(&msx))
        avail = (uint64_t) msx.ullAvailPhys;
#    elif defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long psize = sysconf(_SC_PAGESIZE);

    if (pages > 0 && psize > 0)
        avail = (uint64_t) pages * (uint64_t) psize;
#    endif
    if (!avail)
        return GPU_BOOT_THREADS_MAX;

    n = (int) ((avail * 3 / 4) / GPU_BOOT_MEM_PER_COMPILE);
    return n < 1 ? 1 : n;
}

static int
gpu_boot_threads(void)
{
    const char *e = getenv("R128_GPU_PRECOMP_THREADS");
    long        n;

    if (e && e[0]) {
        /* explicit override is taken verbatim */
        n = strtol(e, NULL, 10);
    } else {
        long cap = gpu_mem_thread_cap();

        n = r128_cpu_count() - 2;
        if (n > cap)
            n = cap;
        if (n > GPU_BOOT_THREADS_AUTO_MAX)
            n = GPU_BOOT_THREADS_AUTO_MAX;
    }
    if (n < 1)
        n = 1;
    if (n > GPU_BOOT_THREADS_MAX)
        n = GPU_BOOT_THREADS_MAX;
    return (int) n;
}

/* Create one seg-module pipeline for the spec constants named by
   sme/sd: the live uber module + VkSpecializationInfo. */
static VkResult
gpu_seg_create(r128_gpu_t *g, const VkSpecializationMapEntry *sme,
               const int32_t *sd, uint32_t n, VkPipeline *out)
{
    VkComputePipelineCreateInfo cpi = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
    };
    VkSpecializationInfo si;

    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.pName = "main";
    cpi.layout      = g->plyt;

    si                            = (VkSpecializationInfo) { n, sme, (size_t) n * 4, sd };
    cpi.stage.module              = g->seg_sm;
    cpi.stage.pSpecializationInfo = &si;
    return vkCreateComputePipelines(g->dev, g->pcache, 1, &cpi, NULL, out);
}

/* The unfolded (C_FOLD=0) uber pipelines: one per kernel, always
   present, and the correct fallback whenever a folded variant is
   missing. They are the largest shaders in the device, since no
   selector is pinned and nothing can be compiled out, and init runs on
   the Qt main thread. So the build fans out to workers while the caller
   waits in ui_progress_wait, which keeps the event loop running
   (repaint only, no user input) behind a progress dialog. Nothing
   here can be aborted, so the wait covers the full set before init
   returns. */
typedef struct gpu_uber_ctx_t {
    r128_gpu_t *g;
    atomic_uint next;
    atomic_uint done;
    atomic_int  vr; /* first non-SUCCESS result, sticky */
} gpu_uber_ctx_t;

static void
gpu_uber_drain(gpu_uber_ctx_t *c, int ui)
{
    static const VkSpecializationMapEntry sme[4] = {
        { 0, 0,  4 },
        { 1, 4,  4 },
        { 2, 8,  4 },
        { 3, 12, 4 }
    };
    r128_gpu_t *g = c->g;

    for (;;) {
        uint32_t v = atomic_fetch_add(&c->next, 1);
        int32_t  sd[4];
        VkResult vr;

        if (v >= GPU_KERNELS)
            return;
        sd[0] = r128_gpu_variants[v].tex;
        sd[1] = r128_gpu_variants[v].lod;
        sd[2] = r128_gpu_variants[v].blend;
        sd[3] = r128_gpu_variants[v].z;
        {
            uint64_t t0 = prof_now();

            vr = gpu_seg_create(g, sme, sd, 4, &g->pipes[v]);
            gpu_log("RAGE128 GPU: uber variant %u/%u (tex=%d lod=%d blend=%d "
                    "z=%d) in %llums\n",
                    v + 1, GPU_KERNELS, sd[0], sd[1],
                    sd[2], sd[3],
                    (unsigned long long) ((prof_now() - t0) / 1000000));
            gpu_boot_tick(g, ui);
        }
        if (vr != VK_SUCCESS)
            atomic_store(&c->vr, (int) vr);
        /* ticked on failure too: the progress wait ends at GPU_KERNELS
           regardless, the sticky vr reports the outcome */
        atomic_fetch_add(&c->done, 1);
    }
}

static void *
gpu_uber_worker(void *arg)
{
    gpu_uber_drain((gpu_uber_ctx_t *) arg, 0);
    return NULL;
}

static int
gpu_uber_poll(void *arg)
{
    return (int) atomic_load(&((gpu_uber_ctx_t *) arg)->done);
}

static VkResult
gpu_uber_build(r128_gpu_t *g)
{
    gpu_uber_ctx_t uc;
    pthread_t      thr[GPU_BOOT_THREADS_MAX];
    int            nthr = gpu_boot_threads(), live = 0;
    uint64_t       t0 = prof_now();

    uc.g = g;
    atomic_init(&uc.next, 0);
    atomic_init(&uc.done, 0);
    atomic_init(&uc.vr, (int) VK_SUCCESS);
    /* every job on a worker: the caller is the Qt main thread and spends
       the wait keeping the UI alive instead of draining pipelines */
    for (int i = 0; i < nthr; i++)
        if (pthread_create(&thr[live], NULL, gpu_uber_worker, &uc) == 0)
            live++;
    if (live == 0) {
        gpu_uber_drain(&uc, 1);
    } else {
        ui_progress_wait("Preparing Rage 128 graphics acceleration (first run only)",
                         GPU_KERNELS, gpu_uber_poll, &uc);
        for (int i = 0; i < live; i++)
            pthread_join(thr[i], NULL);
    }
    gpu_log("RAGE128 GPU: %d unfolded pipelines on %d threads in %llums\n",
            GPU_KERNELS, live ? live : 1,
            (unsigned long long) ((prof_now() - t0) / 1000000));
    return (VkResult) atomic_load(&uc.vr);
}

/* V_SERIAL=1 uber set, built off the init path (on the boot thread or
   its synchronous fallback; both go through gpu_boot_precompile). Not
   gated by the precompile policy: without these an aliasing draw can
   only take the CPU fallback. serial_ready publishes the complete set;
   a partial set is never used. Fanned out over threads like the other
   stages: built one pipeline at a time on a cold cache, this set alone
   was observed to take over ten minutes with AMD drivers under
   Windows. */
typedef struct gpu_serial_ctx_t {
    r128_gpu_t *g;
    atomic_uint next;
    atomic_uint built;
    atomic_int  fail;
} gpu_serial_ctx_t;

static void
gpu_serial_drain(gpu_serial_ctx_t *c, int ui)
{
    static const VkSpecializationMapEntry sme[5] = {
        { 0,  0,  4 },
        { 1,  4,  4 },
        { 2,  8,  4 },
        { 3,  12, 4 },
        { 24, 16, 4 }
    };
    r128_gpu_t *g = c->g;

    for (;;) {
        int32_t  sd[5];
        uint32_t v = atomic_fetch_add(&c->next, 1);

        if (v >= GPU_KERNELS || atomic_load(&g->boot_abort)
            || atomic_load(&c->fail))
            return;
        sd[0] = r128_gpu_variants[v].tex;
        sd[1] = r128_gpu_variants[v].lod;
        sd[2] = r128_gpu_variants[v].blend;
        sd[3] = r128_gpu_variants[v].z;
        sd[4] = 1; /* V_SERIAL */
        if (gpu_seg_create(g, sme, sd, 5, &g->pipes_serial[v])
            != VK_SUCCESS) {
            gpu_log("RAGE128 GPU: serial pipeline (%s) failed; aliasing draws "
                    "stay on the CPU path\n",
                    r128_gpu_variants[v].name);
            atomic_store(&c->fail, 1);
            return;
        }
        atomic_fetch_add(&c->built, 1);
        gpu_boot_tick(g, ui);
    }
}

static void *
gpu_serial_worker(void *arg)
{
    gpu_serial_drain((gpu_serial_ctx_t *) arg, 0);
    return NULL;
}

static void
gpu_serial_build(r128_gpu_t *g)
{
    gpu_serial_ctx_t ctx;
    pthread_t        thr[GPU_BOOT_THREADS_MAX];
    int              nthr = gpu_boot_threads(), live = 0;
    uint64_t         t0 = prof_now();

    ctx.g = g;
    atomic_init(&ctx.next, 0);
    atomic_init(&ctx.built, 0);
    atomic_init(&ctx.fail, 0);
    for (int i = 1; i < nthr; i++)
        if (pthread_create(&thr[live], NULL, gpu_serial_worker, &ctx) == 0)
            live++;
    gpu_serial_drain(&ctx, 1);
    for (int i = 0; i < live; i++)
        pthread_join(thr[i], NULL);
    if (atomic_load(&ctx.built) != GPU_KERNELS)
        return;
    atomic_store_explicit(&g->serial_ready, 1, memory_order_release);
    gpu_log("RAGE128 GPU: %d serial pipelines on %d threads in %llums\n",
            GPU_KERNELS, live + 1,
            (unsigned long long) ((prof_now() - t0) / 1000000));
}

/* Boot precompile. The default (list mode) compiles the learned pairs,
   the ones this host has bound before, and leaves the rest to the live
   queue; a compile that hits the pipeline cache takes milliseconds.
   R128_GPU_PRECOMP=full compiles every interned tuple for every kernel
   of its stage count, and =0 skips the folded set entirely. */
static void
gpu_boot_precompile(r128_gpu_t *g)
{
    int            mode = gpu_precomp_mode();
    uint64_t       t0   = prof_now();
    gpu_boot_ctx_t ctx;
    pthread_t      thr[GPU_BOOT_THREADS_MAX];
    int            nthr, live = 0;

    gpu_serial_build(g);
    if (mode == 0 || !g->fold_en)
        return;

    ctx.g     = g;
    ctx.njobs = 0;
    ctx.jobs  = calloc((size_t) GPU_TUPLE_CAP * GPU_KERNELS,
                       sizeof(gpu_fold_job_t));
    if (!ctx.jobs)
        return;
    atomic_init(&ctx.next, 0);
    atomic_init(&ctx.done, 0);

    if (mode == 1) {
        /* just what the learned list named, nothing widened */
        for (uint32_t i = 0; i < g->nlearn; i++) {
            const gpu_fold_job_t *j = &g->learned[i];

            if (g->tuples[j->tid].pipes[j->kern]
                || g->tuples[j->tid].failed[j->kern])
                continue;
            ctx.jobs[ctx.njobs++] = *j;
        }
    } else {
        for (uint32_t i = 0; i < g->ntuples; i++) {
            const gpu_tuple_t *t      = &g->tuples[i];
            int                stages = 1;

            for (int k = 7; k < 14; k++)
                if (t->sel[k])
                    stages = 2;
            for (uint32_t v = 0; v < GPU_KERNELS; v++) {
                if (r128_gpu_variants[v].tex != stages)
                    continue;
                if (t->pipes[v] || t->failed[v])
                    continue;
                ctx.jobs[ctx.njobs].kern = v;
                ctx.jobs[ctx.njobs].tid  = (int32_t) i;
                ctx.njobs++;
            }
        }
    }

    nthr = gpu_boot_threads();
    for (int i = 1; i < nthr; i++)
        if (pthread_create(&thr[live], NULL, gpu_boot_worker, &ctx) == 0)
            live++;
    gpu_boot_drain(&ctx, 1);
    for (int i = 0; i < live; i++)
        pthread_join(thr[i], NULL);

    ui_sb_set_text(NULL);
    /* this pass's own job count and wall time; st_boot_pipes counts
       every boot-time create, not just this pass's */
    gpu_log("RAGE128 GPU: precompiled %u folded pipelines on %d threads in "
            "%llums (%s)\n",
            atomic_load(&ctx.done), live + 1,
            (unsigned long long) ((prof_now() - t0) / 1000000),
            (mode == 1) ? "learned list" : "full");
    free(ctx.jobs);
    /* save now, not only at device close, so a session that ends
       abnormally keeps the pipelines it compiled cold */
    gpu_persist_save(g);
}

static void *
gpu_boot_thread(void *arg)
{
    gpu_boot_precompile((r128_gpu_t *) arg);
    return NULL;
}

/* Learned-pair list: the header GPU_TUPLES_HDR, then one pair per line
   as the kernel id followed by the GPU_TUPLE_SEL selector words
   (combine, stencil, dead, the four texel-format words with ANY stored
   as 4294967295, and the light word). A file with any other header is
   ignored rather than converted; one session learns the pairs again.
   Loading only interns the tuples and records the pairs in learned[];
   gpu_boot_precompile builds them in list mode, and full mode covers
   them anyway. The version in the header changes whenever the folding
   changes, because the rows store folded tuples: an older list may
   have merged two combines that now take different pipelines, and
   relearning is the only way to split them again. */
#    define GPU_TUPLES_HDR "r128gpu-tuples-v7"

static void
gpu_tuples_load(r128_gpu_t *g)
{
    char     path[1088];
    char     hdr[32];
    FILE    *f;
    unsigned kern, s[GPU_TUPLE_SEL];

    gpu_cache_path(path, sizeof(path), "r128gpu.tuples");
    f = plat_fopen(path, "r");
    if (!f)
        return;
    if (fscanf(f, "%31s", hdr) != 1 || strcmp(hdr, GPU_TUPLES_HDR)) {
        fclose(f);
        return;
    }
    for (;;) {
        if (fscanf(f,
                   "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u"
                   " %u %u %u %u %u",
                   &kern, &s[0], &s[1], &s[2], &s[3], &s[4], &s[5],
                   &s[6], &s[7], &s[8], &s[9], &s[10], &s[11], &s[12],
                   &s[13], &s[14], &s[15], &s[16], &s[17], &s[18],
                   &s[19], &s[20])
            != 22)
            break;

        uint32_t sel[GPU_TUPLE_SEL];
        int32_t  tid;

        if (kern >= GPU_KERNELS)
            continue;
        for (int i = 0; i < 16; i++)
            sel[i] = s[i] & 0xfu;
        sel[GPU_TUPLE_LIGHT] = s[20] & 0xffu;
        /* Format fields accept datatype plus aone (0..31) and s3tc (0..3);
           anything else leaves the format as a runtime selector. */
        for (int i = 16; i < 20; i++)
            sel[i] = (s[i] > ((i & 1) ? 3u : 31u)) ? GPU_TUPLE_FMT_ANY
                                                   : s[i];
        /* fold on the way in, so a file holding unfolded encodings
           cannot spend tuple slots on duplicates */
        gpu_tuple_canon(sel);
        tid = gpu_tuple_intern(g, sel);
        /* Intern only. This runs on the Qt main thread, so compiling
           here would put the whole learned list on the UI thread,
           serially, which the off-thread precompile exists to avoid.
           Record the pair and let gpu_boot_precompile build it in
           parallel; the full pass already covers every tuple for every
           kernel of its stage count, so only list mode needs the
           recorded pairs. */
        if (tid >= 0 && g->nlearn < GPU_TUPLE_CAP * GPU_KERNELS) {
            g->learned[g->nlearn].kern = kern;
            g->learned[g->nlearn].tid  = tid;
            g->nlearn++;
        }
    }
    fclose(f);
}

static void
gpu_persist_save(r128_gpu_t *g)
{
    char   path[1088];
    FILE  *f;
    size_t sz = 0;

    if (vkGetPipelineCacheData(g->dev, g->pcache, &sz, NULL) == VK_SUCCESS
        && sz > 0) {
        void *data = malloc(sz);

        if (data
            && vkGetPipelineCacheData(g->dev, g->pcache, &sz, data)
                == VK_SUCCESS) {
            gpu_cache_path(path, sizeof(path), "r128gpu.plcache");
            f = plat_fopen(path, "wb");
            if (f) {
                fwrite(data, 1, sz, f);
                fclose(f);
            }
        }
        free(data);
    }
    gpu_cache_path(path, sizeof(path), "r128gpu.tuples");
    f = plat_fopen(path, "w");
    if (!f)
        return;
    fprintf(f, GPU_TUPLES_HDR "\n");
    pthread_mutex_lock(&g->fold_mtx);
    for (uint32_t i = 0; i < g->ntuples; i++)
        for (uint32_t v = 0; v < GPU_KERNELS; v++)
            if (g->tuples[i].pipes[v]) {
                fprintf(f, "%u", v);
                for (int k = 0; k < (int) GPU_TUPLE_SEL; k++)
                    fprintf(f, " %u", g->tuples[i].sel[k]);
                fprintf(f, "\n");
            }
    pthread_mutex_unlock(&g->fold_mtx);
    fclose(f);
}

/* Fill the specialization map + values for (kernel, tuple). sme/sd are
   caller-owned so a batch can hold several live at once. */
static void
gpu_fold_ci(r128_gpu_t *g, uint32_t kern, int32_t tid,
            VkSpecializationMapEntry *sme, int32_t *sd)
{
    const gpu_tuple_t *t = &g->tuples[tid];

    /* constant ids: 0-3 the kernel axes, 7 C_FOLD, 8..21 combine
       selectors, 22 S_STEN, 23 C_DEAD, 25..28 texel formats,
       29 C_LIGHT (the texture-lighting word); they must
       match the constant_id values in vid_ati_rage128_gpu_seg.comp */
    for (int i = 0; i < 4; i++)
        sme[i] = (VkSpecializationMapEntry) { (uint32_t) i,
                                              (uint32_t) i * 4, 4 };
    sme[4] = (VkSpecializationMapEntry) { 7, 16, 4 };
    for (int i = 0; i < 14; i++)
        sme[5 + i] = (VkSpecializationMapEntry) { (uint32_t) (8 + i),
                                                  (uint32_t) (20 + i * 4), 4 };
    sme[19] = (VkSpecializationMapEntry) { 22, 76, 4 };
    sme[20] = (VkSpecializationMapEntry) { 23, 80, 4 };
    for (int i = 0; i < 4; i++)
        sme[21 + i] = (VkSpecializationMapEntry) { (uint32_t) (25 + i),
                                                   (uint32_t) (84 + i * 4), 4 };
    sd[0] = r128_gpu_variants[kern].tex;
    sd[1] = r128_gpu_variants[kern].lod;
    sd[2] = r128_gpu_variants[kern].blend;
    sd[3] = r128_gpu_variants[kern].z;
    sd[4] = 1; /* C_FOLD */
    for (int i = 0; i < 14; i++)
        sd[5 + i] = (int32_t) t->sel[i];
    sd[19] = (int32_t) t->sel[14]; /* S_STEN */
    sd[20] = (int32_t) t->sel[15]; /* C_DEAD */
    /* GPU_TUPLE_FMT_ANY casts to -1: that stage's format stays a
       runtime selector */
    for (int i = 0; i < 4; i++)
        sd[21 + i] = (int32_t) t->sel[16 + i];
    sme[25] = (VkSpecializationMapEntry) { 29, 100, 4 };
    sd[25]  = (int32_t) t->sel[GPU_TUPLE_LIGHT]; /* C_LIGHT */
}

/* Publish one finished create under fold_mtx and wake anyone parked on
   the claim. */
static void
gpu_fold_publish(r128_gpu_t *g, uint32_t kern, int32_t tid, VkPipeline p,
                 VkResult vr, uint64_t dt, int boot)
{
    gpu_tuple_t *t = &g->tuples[tid];

    pthread_mutex_lock(&g->fold_mtx);
    t->busy[kern] = 0;
    if (vr == VK_SUCCESS) {
        t->pipes[kern] = p;
        if (boot) {
            g->st_boot_pipes++;
            g->st_boot_ns += dt;
        } else {
            g->st_live_pipes++;
            g->st_live_ns += dt;
        }
    } else {
        t->pipes[kern]  = VK_NULL_HANDLE;
        t->failed[kern] = 1;
    }
    pthread_mutex_unlock(&g->fold_mtx);

    if (vr != VK_SUCCESS)
        gpu_log("RAGE128 GPU: folded pipeline (%s, tuple %d) failed (%d), "
                "using unfolded\n",
                r128_gpu_variants[kern].name, tid, (int) vr);
    else if (!boot)
        gpu_log("RAGE128 GPU: live pipeline compile (%s, tuple %d): %llums\n",
                r128_gpu_variants[kern].name, tid,
                (unsigned long long) (dt / 1000000));
}

/* Compile the folded pipeline for (kernel, tuple) through the persisted
   pipeline cache, counting the compile time into the boot or live
   totals. The create runs outside fold_mtx; the mutex only claims the
   pair. A caller that finds the pair already claimed does not wait: it
   gets null, and the segment takes the unfolded pipeline, which writes
   the same bytes, rather than stalling the CCE thread behind a shader
   compile. */
static VkPipeline
gpu_fold_compile(r128_gpu_t *g, uint32_t kern, int32_t tid, int boot)
{
    gpu_tuple_t             *t = &g->tuples[tid];
    VkSpecializationMapEntry sme[26];
    int32_t                  sd[26];
    VkPipeline               p = VK_NULL_HANDLE;
    uint64_t                 t0;
    VkResult                 vr;

    pthread_mutex_lock(&g->fold_mtx);
    if (t->failed[kern] || t->busy[kern]) {
        pthread_mutex_unlock(&g->fold_mtx);
        return VK_NULL_HANDLE;
    }
    if (t->pipes[kern]) {
        VkPipeline done = t->pipes[kern];

        pthread_mutex_unlock(&g->fold_mtx);
        return done;
    }
    t->busy[kern] = 1;
    pthread_mutex_unlock(&g->fold_mtx);

    gpu_fold_ci(g, kern, tid, sme, sd);
    t0 = prof_now();
    vr = gpu_seg_create(g, sme, sd, 26, &p);
    gpu_fold_publish(g, kern, tid, p, vr, prof_now() - t0, boot);
    return (vr == VK_SUCCESS) ? p : VK_NULL_HANDLE;
}

/* Dispatch-path lookup; never compiles. A hit returns the folded
   pipeline; a miss parks the pair on the live queue and returns null,
   and the segment runs the uber kernel, which writes the same bytes. */
static VkPipeline
gpu_fold_get(r128_gpu_t *g, uint32_t kern, int32_t tid)
{
    gpu_tuple_t *t = &g->tuples[tid];
    VkPipeline   p;

    pthread_mutex_lock(&g->fold_mtx);
    p = t->pipes[kern];
    if (p || t->failed[kern] || t->busy[kern] || t->queued[kern]) {
        pthread_mutex_unlock(&g->fold_mtx);
        return p;
    }
    t->queued[kern] = 1;
    pthread_mutex_unlock(&g->fold_mtx);

    pthread_mutex_lock(&g->lq_mtx);
    g->lq_jobs[g->lq_tail % (GPU_TUPLE_CAP * GPU_KERNELS)] = (gpu_fold_job_t) { kern, tid };
    g->lq_tail++;
    g->lq_burst++;
    gpu_live_sb(g);
    pthread_cond_signal(&g->lq_cv);
    pthread_mutex_unlock(&g->lq_mtx);
    return VK_NULL_HANDLE;
}

/* Status-bar progress for the live queue, shown as done/total for the
   current burst: total = everything queued since the queue last ran
   dry, done = total minus outstanding (parked + in flight). Posted
   from inside lq_mtx so two workers cannot write a stale count over a
   fresh one. It can briefly overwrite the boot progress text if a
   title draws during the boot stages; that is rare, since the first 3D
   draw comes after the guest desktop is up, and harmless. */
static void
gpu_live_sb(r128_gpu_t *g)
{
    uint32_t rem = g->lq_tail - g->lq_head + g->lq_inflight;
    char     sb[96];

    if (!rem) {
        g->lq_burst = 0;
        ui_sb_set_text(NULL);
        return;
    }
    snprintf(sb, sizeof(sb), "Compiling Shaders %u/%u",
             g->lq_burst - rem, g->lq_burst);
    ui_sb_set_text(sb);
}

static void *
gpu_live_worker(void *arg)
{
    r128_gpu_t *g = arg;

    for (;;) {
        gpu_fold_job_t j;

        pthread_mutex_lock(&g->lq_mtx);
        while (!g->lq_stop && g->lq_head == g->lq_tail)
            pthread_cond_wait(&g->lq_cv, &g->lq_mtx);
        if (g->lq_stop) {
            pthread_mutex_unlock(&g->lq_mtx);
            return NULL;
        }
        j = g->lq_jobs[g->lq_head % (GPU_TUPLE_CAP * GPU_KERNELS)];
        g->lq_head++;
        g->lq_inflight++;
        gpu_live_sb(g);
        pthread_mutex_unlock(&g->lq_mtx);

        /* returns null if the boot fan-out holds the claim; the pair
           re-queues on its next draw and finds the published pipe */
        gpu_fold_compile(g, j.kern, j.tid, 0);
        pthread_mutex_lock(&g->fold_mtx);
        g->tuples[j.tid].queued[j.kern] = 0;
        pthread_mutex_unlock(&g->fold_mtx);

        pthread_mutex_lock(&g->lq_mtx);
        g->lq_inflight--;
        gpu_live_sb(g);
        pthread_mutex_unlock(&g->lq_mtx);
    }
}

/* Why each create builds one pipeline. Measured on a cold cache on an
   Apple M1 Pro, 248 pipelines: one thread took 200.5 s, six threads
   57.4 s. Passing eight pipelines per vkCreateComputePipelines call
   made six threads 5.7% faster (54.3 s), but at about 1.3 s per
   pipeline per thread that is about 10 s inside one driver call that
   cannot be interrupted, and boot_abort (VM close) is only checked
   between calls. One pipeline per call keeps that wait near 1.3 s. */

/* ------------------------------------------------------------------ */
/* Range helpers (byte ranges over local card space; hi exclusive).    */
/* ------------------------------------------------------------------ */

static inline int
rng_hit(uint32_t lo, uint32_t hi, uint32_t a, uint32_t b)
{
    return hi > lo && b > a && a < hi && b > lo;
}

static inline void
rng_ext(atomic_uint *plo, atomic_uint *phi, uint32_t lo, uint32_t hi)
{
    if (hi <= lo)
        return;
    if (lo < atomic_load_explicit(plo, memory_order_relaxed))
        atomic_store_explicit(plo, lo, memory_order_relaxed);
    if (hi > atomic_load_explicit(phi, memory_order_relaxed))
        atomic_store_explicit(phi, hi, memory_order_relaxed);
}

/* Open and close one change of the pending f hull and its exact list
   (fq_gen of r128_gpu_t): the generation goes odd, the release fence
   orders the increment ahead of the hull and list stores, and the
   closing fence orders those stores ahead of the increment that makes
   it even again. Only the thread that queues 2D stores calls these, so
   the increments need no read-modify-write. */
static void
gpu_f_begin(r128_gpu_t *g)
{
    atomic_store_explicit(&g->fq_gen,
                          atomic_load_explicit(&g->fq_gen,
                                               memory_order_relaxed)
                              + 1u,
                          memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}

static void
gpu_f_end(r128_gpu_t *g)
{
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&g->fq_gen,
                          atomic_load_explicit(&g->fq_gen,
                                               memory_order_relaxed)
                              + 1u,
                          memory_order_relaxed);
}

static void
rng_reset(r128_gpu_t *g)
{
    atomic_store(&g->c_lo, 0xffffffffu);
    atomic_store(&g->c_hi, 0);
    atomic_store(&g->z_lo, 0xffffffffu);
    atomic_store(&g->z_hi, 0);
    atomic_store(&g->c_stg, 0);
    atomic_store(&g->z_stg, 0);
    atomic_store(&g->q_lo, 0xffffffffu);
    atomic_store(&g->q_hi, 0);
    atomic_store(&g->r_lo, 0xffffffffu);
    atomic_store(&g->r_hi, 0);
    gpu_f_begin(g);
    atomic_store(&g->f_lo, 0xffffffffu);
    atomic_store(&g->f_hi, 0);
    g->nfq    = 0;
    g->fq_ovf = 0;
    gpu_f_end(g);
}

static int
gpu_has_work(const r128_gpu_t *g)
{
    if (atomic_load_explicit(&g->pending, memory_order_acquire))
        return 1;
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++)
        if (atomic_load_explicit(&g->slot[i].submitted, memory_order_acquire))
            return 1;
    return 0;
}

/* Does [lo,hi) touch texture bytes the segment samples? The hull is
   tested first; on a hull hit the exact list decides, unless the list
   overflowed, in which case the hull hit stands. A hull hit the list
   clears or confirms is marked on g; the operation that asked folds the
   marks into the close-log counters (gpu_texq_op_end). */
static int
gpu_texq_hit(r128_gpu_t *g, const uint32_t *tlo, const uint32_t *thi,
             uint32_t n, int ovf, uint32_t qlo, uint32_t qhi,
             uint32_t lo, uint32_t hi)
{
    if (!rng_hit(qlo, qhi, lo, hi))
        return 0;
    if (ovf)
        return 1;
    for (uint32_t i = 0; i < n; i++)
        if (rng_hit(tlo[i], thi[i], lo, hi)) {
            g->texq_op_real = 1;
            return 1;
        }
    g->texq_op_spared = 1;
    return 0;
}

/* One 2D operation's worth of exact-list verdicts: begin clears the
   marks, end counts the operation once, as confirmed when any test
   confirmed a hull hit and otherwise as cleared when any test cleared
   one. */
static void
gpu_texq_op_begin(r128_gpu_t *g)
{
    g->texq_op_spared = 0;
    g->texq_op_real   = 0;
}

static void
gpu_texq_op_end(r128_gpu_t *g)
{
    if (g->texq_op_real)
        g->st_texq_real++;
    else if (g->texq_op_spared)
        g->st_texq_spared++;
}

/* The pending segment's / a submitted slot's exact texture test, for
   the 2D executor's hazard gates. */
static int
gpu_pending_texq_hit(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    return gpu_texq_hit(g, g->texq_lo, g->texq_hi, g->ntexq, g->texq_ovf,
                        atomic_load(&g->q_lo), atomic_load(&g->q_hi), lo, hi);
}

static int
gpu_slot_texq_hit(r128_gpu_t *g, const gpu_slot_t *sl, uint32_t lo,
                  uint32_t hi)
{
    return gpu_texq_hit(g, sl->texq_lo, sl->texq_hi, sl->ntexq, sl->texq_ovf,
                        sl->q_lo, sl->q_hi, lo, hi);
}

/* Does a submitted slot's queued 2D store list write [lo, hi)? The f
   hull rejects first; a hull hit is confirmed against the exact list
   unless the list overflowed, when the hull stands. Any thread that
   has acquired the slot's submitted flag may call this (the exact 2D
   store list of r128_gpu_t says why). A reader off the submit thread
   can be overtaken: the slot retires, is reused and is dispatched
   again while the scan runs, and the scan then mixes two generations
   of the list. The sequence number is written before each generation's
   submitted release-store, so a scan that ends under a different
   sequence, or with the flag down, has no verdict and answers hit,
   which only costs the wait the hull alone would have charged. */
static int
gpu_slot_f_hit(r128_gpu_t *g, const gpu_slot_t *sl, uint32_t lo, uint32_t hi)
{
    uint64_t seq = sl->seq;
    int      hit = 0;

    if (!rng_hit(sl->f_lo, sl->f_hi, lo, hi))
        return 0;
    if (sl->fq_ovf)
        return 1;
    for (uint32_t i = 0; i < sl->nfq; i++)
        if (rng_hit(sl->fq_lo[i], sl->fq_hi[i], lo, hi)) {
            hit = 1;
            break;
        }
    if (!atomic_load_explicit(&sl->submitted, memory_order_acquire)
        || sl->seq != seq)
        return 1;
    atomic_fetch_add_explicit(hit ? &g->st_fq_real : &g->st_fq_spared, 1,
                              memory_order_relaxed);
    return hit;
}

/* Does the pending segment's queued 2D store list write [lo, hi)? The
   f hull rejects first; a hull hit is confirmed against the exact list
   unless the list overflowed, when the hull stands. Any thread may call
   this while the queuing thread appends (fq_gen of r128_gpu_t says how
   the generation makes that safe): the generation is read before the
   hull and again, behind an acquire fence, after the list walk, and a
   walk that began inside a change or spanned one answers hit, which
   only costs the wait the hull alone would have charged. A reader that
   sees a widened hull but a list without the new entry is such a walk:
   the writer's generation went odd before the hull store, so the second
   read differs from the first. */
static int
gpu_pending_f_hit(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    uint32_t gen = atomic_load_explicit(&g->fq_gen, memory_order_acquire);
    int      hit = 0;

    if (!rng_hit(atomic_load(&g->f_lo), atomic_load(&g->f_hi), lo, hi))
        return 0;
    if ((gen & 1u) || g->fq_ovf)
        return 1;
    for (uint32_t i = 0, n = g->nfq; i < n && i < GPU_FQ_CAP; i++)
        if (rng_hit(g->fq_lo[i], g->fq_hi[i], lo, hi)) {
            hit = 1;
            break;
        }
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&g->fq_gen, memory_order_relaxed) != gen)
        return 1;
    atomic_fetch_add_explicit(hit ? &g->st_fqp_real : &g->st_fqp_spared, 1,
                              memory_order_relaxed);
    return hit;
}

/* Does the pending (unsubmitted) accumulation conflict with [lo,hi)?
   c/z = draw stores and f = queued 2D writes, tested always, since the
   CPU, scan and read barriers must see every GPU store; q/r = queued
   texture and transfer reads, tested only when the caller will store
   there (write after read). c, z, q and r are hulls: the CPU-thread
   barriers may run while the executor is still capturing, and only the
   f list is published for them (gpu_pending_f_hit). */
static int
gpu_pending_hit(r128_gpu_t *g, uint32_t lo, uint32_t hi, int texture)
{
    return atomic_load_explicit(&g->pending, memory_order_acquire)
        && (rng_hit(atomic_load(&g->c_lo), atomic_load(&g->c_hi), lo, hi)
            || rng_hit(atomic_load(&g->z_lo), atomic_load(&g->z_hi), lo, hi)
            || gpu_pending_f_hit(g, lo, hi)
            || (texture
                && (rng_hit(atomic_load(&g->q_lo), atomic_load(&g->q_hi),
                            lo, hi)
                    || rng_hit(atomic_load(&g->r_lo), atomic_load(&g->r_hi),
                               lo, hi))));
}

/* The 2D executor's form of the same test: the exact texture list in
   place of the q hull. */
static int
gpu_pending_hit_2d(r128_gpu_t *g, uint32_t lo, uint32_t hi, int writes)
{
    return atomic_load_explicit(&g->pending, memory_order_acquire)
        && (rng_hit(atomic_load(&g->c_lo), atomic_load(&g->c_hi), lo, hi)
            || rng_hit(atomic_load(&g->z_lo), atomic_load(&g->z_hi), lo, hi)
            || gpu_pending_f_hit(g, lo, hi)
            || (writes
                && (gpu_pending_texq_hit(g, lo, hi)
                    || rng_hit(atomic_load(&g->r_lo), atomic_load(&g->r_hi),
                               lo, hi))));
}

/* Split gate for queued 2D ops: draws only. Queued ops are recorded at
   the head of the next submit, before that submit's draws, so only a
   hazard against a pending draw forces a split; an earlier queued 2D op
   is ordered by gpu_fill_bar inside the transfer block instead. c/z =
   draw stores; q = draw texture reads (a 2D write there must not land
   before earlier draws sample the old bytes), tested against the exact
   list because the gate runs on the 2D executor. */
static int
gpu_pending_draw_hit(r128_gpu_t *g, uint32_t lo, uint32_t hi,
                     int texture)
{
    return atomic_load_explicit(&g->pending, memory_order_acquire)
        && (rng_hit(atomic_load(&g->c_lo), atomic_load(&g->c_hi), lo, hi)
            || rng_hit(atomic_load(&g->z_lo), atomic_load(&g->z_hi), lo, hi)
            || (texture && gpu_pending_texq_hit(g, lo, hi)));
}

/* bar flag for a new op vs everything already queued: any RAW, WAR or
   WAW byte conflict with an earlier op's VRAM ranges. */
static int
gpu_fill_bar(const r128_gpu_t *g, const gpu_fill_t *nf)
{
    uint64_t wlo, whi, rlo, rhi;

    gpu_fill_vranges(nf, &wlo, &whi, &rlo, &rhi);
    for (uint32_t i = 0; i < g->nfills; i++) {
        uint64_t owlo, owhi, orlo, orhi;

        gpu_fill_vranges(&g->fills[i], &owlo, &owhi, &orlo, &orhi);
        if ((wlo < owhi && whi > owlo)     /* WAW */
            || (wlo < orhi && whi > orlo)  /* WAR */
            || (rlo < owhi && rhi > owlo)) /* RAW */
            return 1;
    }
    return 0;
}

/* Does [lo,hi) touch a queued mid fill's bytes? The fill lands in the
   import only after this segment's draws, so a later queued 2D op
   reading or writing there would be recorded ahead of it in the
   transfer block, and a later draw storing there (c/z) would have its
   mirror-back overwritten. Either one splits the segment instead. */
static int
gpu_mid_range_hit(const r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    if (!g->nmid || lo >= hi)
        return 0;
    for (uint32_t i = 0; i < g->nfills; i++) {
        uint64_t olo, ohi;

        if (g->fills[i].mid < 0)
            continue;
        olo = g->fills[i].addr;
        ohi = (uint64_t) g->fills[i].addr
            + (uint64_t) (g->fills[i].rows - 1) * g->fills[i].pitch
            + g->fills[i].len;
        if (lo < ohi && hi > olo)
            return 1;
    }
    return 0;
}

/* CPU-thread barriers (aperture stores, scanout, present reads): pending
   accumulation plus every submitted slot, hull ranges only, since they
   may run while the executor is still capturing. */
static int
gpu_range_hit(r128_gpu_t *g, uint32_t lo, uint32_t hi, int texture)
{
    if (gpu_pending_hit(g, lo, hi, texture))
        return 1;
    if (!atomic_load_explicit(&g->inflight, memory_order_acquire))
        return 0;
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        const gpu_slot_t *sl = &g->slot[i];

        if (!atomic_load_explicit(&sl->submitted, memory_order_acquire))
            continue;
        if (rng_hit(sl->c_lo, sl->c_hi, lo, hi)
            || rng_hit(sl->z_lo, sl->z_hi, lo, hi)
            || gpu_slot_f_hit(g, sl, lo, hi)
            || (texture
                && (rng_hit(sl->q_lo, sl->q_hi, lo, hi)
                    || rng_hit(sl->r_lo, sl->r_hi, lo, hi))))
            return 1;
    }
    return 0;
}

/* The 2D executor's surface-map barrier: the same walk with the exact
   texture ranges in place of the q hull. */
static int
gpu_range_hit_2d(r128_gpu_t *g, uint32_t lo, uint32_t hi, int writes)
{
    if (gpu_pending_hit_2d(g, lo, hi, writes))
        return 1;
    if (!atomic_load_explicit(&g->inflight, memory_order_acquire))
        return 0;
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        const gpu_slot_t *sl = &g->slot[i];

        if (!atomic_load_explicit(&sl->submitted, memory_order_acquire))
            continue;
        if (rng_hit(sl->c_lo, sl->c_hi, lo, hi)
            || rng_hit(sl->z_lo, sl->z_hi, lo, hi)
            || gpu_slot_f_hit(g, sl, lo, hi)
            || (writes
                && (gpu_slot_texq_hit(g, sl, lo, hi)
                    || rng_hit(sl->r_lo, sl->r_hi, lo, hi))))
            return 1;
    }
    return 0;
}

/* Row band [y0, y1) -> byte interval on one surface
   (r128_rows_bytes64), truncated to 32 bits. This lane's ranges are
   compared in the masked space rng_fold folds them into, so they wrap
   rather than saturate; rb_rows_to_bytes on the raster lane clamps
   instead. An empty band leaves lo and hi untouched. */
static void
prim_rows_bytes(uint32_t base, uint32_t stride, int tiled, int y0, int y1,
                uint32_t *lo, uint32_t *hi)
{
    uint64_t a, b;

    if (y1 <= y0)
        return; /* empty band: the inclusive y1-1 has no meaning */
    r128_rows_bytes64(base, stride, tiled, (uint32_t) y0,
                      (uint32_t) y1 - 1u, &a, &b);
    *lo = (uint32_t) a;
    *hi = (uint32_t) b;
}

/* Fold a resident store interval into the masked space every consumer
   addresses: CPU-side hazard checks compare it against masked store
   addresses, and the mirror copy-back must never reach bytes the fence
   does not cover. An interval wholly above vram_size is rebased; one
   that crosses the top claims the whole card, since one [lo, hi) cannot
   name two pieces. A staged (AGP) target's interval stays raw: it names
   an arena staging, never VRAM. Residency comes from the state's
   resolved answer, never from the address: on a 32 MB card the local
   top is also where the AGP aperture image starts (RRG:
   AGP_APER_OFFSET, p. 3-187 / PDF 205), so the later rows of a resident
   target can carry an address that looks like AGP. */
static inline void
rng_fold(uint32_t vram_mask, int staged, uint32_t *lo, uint32_t *hi)
{
    uint32_t size = vram_mask + 1u;

    if (staged || *hi <= *lo || *hi <= size)
        return;
    if (*lo >= size) {
        uint32_t len = *hi - *lo;

        *lo &= vram_mask;
        *hi = *lo + len;
        if (*hi <= size)
            return;
    }
    *lo = 0;
    *hi = size;
}

/* Conservative row byte-ranges of a primitive under state rs: full rows
   over the vertex Y extent plus one row of slack each side (the raster
   walk's subpixel snap; over-coverage only), folded into masked space. */
static void
prim_ranges(const rage128_raster_state_t *rs, uint32_t vram_mask,
            const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c,
            uint32_t *clo, uint32_t *chi, uint32_t *zlo, uint32_t *zhi)
{
    const r3d_vtx_t *v[3] = { a, b, c };
    int              ymin = INT32_MAX, ymax = INT32_MIN;
    /* WINDOW_XY_OFFSET moves every row the walk touches, so it is added
       here; without it a nonzero window origin names the wrong rows. */
    float wof = rs->d.sub ? (float) rs->d.woyi / (float) rs->d.sub
                          : 0.0f;

    *clo = *zlo = 0xffffffffu;
    *chi = *zhi = 0;
    for (int k = 0; k < 3; k++) {
        if (!v[k])
            continue;
        float fy = v[k]->y + wof;

        if (fy >= -65536.0f && fy <= 65536.0f) {
            int y = (int) fy;

            if ((float) y > fy)
                y--; /* floor: the cast truncates toward zero */
            if (y < ymin)
                ymin = y;
            if (y > ymax)
                ymax = y;
        } else {
            ymin = 0;
            ymax = 0x7fff;
            break;
        }
    }
    if (ymax < ymin)
        return;
    if (ymin > 0)
        ymin--;
    if (ymax > 0x7fff)
        ymax = 0x7fff;
    ymax += 2;
    /* Clamp the subpixel slack to the primary scissor: the raster walk
       clips rows to [sy0, sy1] inclusive (aux scissors only reject
       more), so rows outside it are never touched. Without the clamp
       the slack rows reach past the render target into the next
       surface; the Windows 2000 OpenGL driver was seen placing Z
       directly after the back buffer, and every full-screen draw with
       Z on would then look like color/z self-aliasing. */
    {
        int sy0 = rs->d.sy0;
        int sy1 = rs->d.sy1;

        if (ymin < sy0)
            ymin = sy0;
        if (ymax > sy1 + 1)
            ymax = sy1 + 1;
        if (ymax <= ymin)
            return;
    }
    {
        uint32_t cs = rs->dst_pitch * rs->dst_bpp;

        if (cs) {
            prim_rows_bytes(rs->dst_offset, cs, rs->d.c_tiled, ymin, ymax,
                            clo, chi);
            rng_fold(vram_mask, rs->c_staged, clo, chi);
        }
    }
    /* z_wr is not required: a draw that only tests z still reads these
       rows, and the ranges feed the read-vs-write hazard checks. Stencil
       lives in the same cell, so a stencil draw owns the rows even with
       the Z test off. */
    if ((rs->d.z_en || rs->d.sten_on) && rs->d.zrowpx) {
        prim_rows_bytes(rs->t3d.z_offset,
                        rs->d.zrowpx * (uint32_t) rs->d.zbpp, rs->d.z_tiled,
                        ymin, ymax, zlo, zhi);
        rng_fold(vram_mask, rs->z_staged, zlo, zhi);
    }
}

/* Worst-case capture cost of one triangle: rows x 256 px chunks (spans)
   and rows x width (ladder rungs), from the vertex extents with the
   same subpixel slack prim_ranges uses. It can only overestimate: the
   raster walk clips to the bounding box and the scissor, so the real
   capture never exceeds it. Out-of-range float coordinates price as the
   full 14-bit extent.

   On a tiled surface the row walk splits at every tile-column edge it
   crosses, so its rows cost pieces, not 256 px chunks: one piece plus
   one per crossing, per tiled surface. A column is 64 bytes, at most 32
   pixels, so the 256 px chunk never binds there. Pricing this too low
   gives a wrong image, not a slow one: the accept bound is what
   reserves the span arena, and a capture that runs past it drops the
   pixels it could not record. */
static void
gpu_tri_need(const rage128_raster_state_t *rs, const r3d_vtx_t *a,
             const r3d_vtx_t *b, const r3d_vtx_t *c,
             uint64_t *need_spans, uint64_t *need_px)
{
    const r3d_vtx_t *v[3] = { a, b, c };
    int              ymin = INT32_MAX, ymax = INT32_MIN;
    int              xmin = INT32_MAX, xmax = INT32_MIN;
    uint64_t         rows, width;
    /* WINDOW_XY_OFFSET shifts every row and column the walk emits, the
       same term prim_ranges applies. Pricing the raw bounding box goes
       wrong wherever a clamp applies: a negative raw coordinate
       undercharges, and an all-negative box prices as nothing. */
    float wofy = rs->d.sub ? (float) rs->d.woyi / (float) rs->d.sub
                           : 0.0f;
    float wofx = rs->d.sub ? (float) rs->d.woxi / (float) rs->d.sub
                           : 0.0f;

    for (int k = 0; k < 3; k++) {
        float fy, fx;

        if (!v[k])
            continue;
        fy = v[k]->y + wofy;
        fx = v[k]->x + wofx;
        if (fy >= -65536.0f && fy <= 65536.0f) {
            int y = (int) fy;

            if (y < ymin)
                ymin = y;
            if (y > ymax)
                ymax = y;
        } else {
            ymin = 0;
            ymax = 0x7fff;
        }
        if (fx >= -65536.0f && fx <= 65536.0f) {
            int x = (int) fx;

            if (x < xmin)
                xmin = x;
            if (x > xmax)
                xmax = x;
        } else {
            xmin = 0;
            xmax = 0x3fff;
        }
    }
    if (ymax < ymin || xmax < xmin) {
        *need_spans = 0;
        *need_px    = 0;
        return;
    }
    ymin--;
    ymax += 2;
    if (ymin < 0)
        ymin = 0;
    if (ymax >= (int) GPU_ROW_CAP)
        ymax = (int) GPU_ROW_CAP - 1;
    xmin -= 1;
    xmax += 2;
    if (xmin < 0)
        xmin = 0;
    if (xmax > 0x3fff)
        xmax = 0x3fff;
    if (ymax < ymin || xmax < xmin) {
        *need_spans = 0;
        *need_px    = 0;
        return;
    }
    rows  = (uint64_t) (ymax - ymin + 1);
    width = (uint64_t) (xmax - xmin + 1);
    if (rs->d.c_tiled || rs->d.z_tiled) {
        uint64_t per_row = 1;

        if (rs->d.c_tiled)
            per_row += (width * (uint64_t) rs->d.bpp + 63u) / 64u;
        if (rs->d.z_tiled)
            per_row += (width * (uint64_t) rs->d.zbpp + 63u) / 64u;
        *need_spans = rows * per_row;
    } else
        *need_spans = rows * ((width + 255u) / 256u);
    *need_px = rows * width;
}

/* ------------------------------------------------------------------ */
/* Supported-state gate. The kernel id is a function of the texture    */
/* stage count, need_lod, alpha_en and whether the z cell is live      */
/* (gpu_kern_by_axes); the kernels cover everything else with runtime  */
/* per-tri selectors (format, clamp/border, filter, combine, aux       */
/* scissors, zfn, stencil, blend, sec_sel/need_lod2).                  */
/* ------------------------------------------------------------------ */

/* draw samples a CI4/CI8 (palette) texture on any enabled stage */
static int
gpu_draw_ci(const rage128_draw_state_t *d)
{
    return (d->tex_en && (d->sh[0].dt == 1 || d->sh[0].dt == 2))
        || (d->sec_en && (d->sh[1].dt == 1 || d->sh[1].dt == 2));
}

/* Can the kernel sample texture stage st: a texel format it decodes and
   a mip top in [0, 10]. Staged (AGP) mip levels are fine when the arena
   import is live, since the kernel samples them through the stage
   binding; without the import it cannot see the arena, so any staged
   level rejects. Returns 0 = usable, else 1 + the reject-key offset
   from the stage's first key (1 format, 2 top, 3 staged without the
   import). */
static int
gpu_stage_why(const r128_gpu_t *g, const rage128_raster_state_t *rs, int st)
{
    const r3d_stage_hdr_t *h  = &rs->d.sh[st];
    const uint32_t        *so = st ? rs->sec_stage_off : rs->prim_stage_off;

    /* every datatype the interpreter decodes. 10 (CI16) and 13 stay on
       the CPU path, where the interpreter has no decoder for them either
       and samples them as opaque white */
    if (!(h->dt == 4 || h->dt == 15 || h->dt == 6
          || h->dt == 1 || h->dt == 2 || h->dt == 3
          || h->dt == 5 || h->dt == 7 || h->dt == 8 || h->dt == 9
          || h->dt == 11 || h->dt == 12 || h->dt == 14
          || (h->dt == 0 && h->s3tc <= 3)))
        return 1;
    if (h->top < 0 || h->top > 10)
        return 2;
    if (!g->stage_ok)
        for (int sl = 0; sl <= h->top; sl++)
            if (so[sl] != R128_TEX_STAGE_NONE)
                return 3;
    return 0;
}

static int
gpu_stage_ok(const r128_gpu_t *g, const rage128_raster_state_t *rs, int st)
{
    return gpu_stage_why(g, rs, st) == 0;
}

/* Kernel id by [tex][lod][blend][z], built from the variant table at
   init so the gate cannot disagree with it: adding a variant widens the
   gate automatically, and a combination with no kernel stays -1 (CPU
   path). Read-only after gpu_axes_index(). */
static int8_t gpu_kern_by_axes[3][2][2][2];

static void
gpu_axes_index(void)
{
    memset(gpu_kern_by_axes, -1, sizeof(gpu_kern_by_axes));
    for (int v = 0; v < GPU_KERNELS; v++) {
        const struct r128_gpu_variant_t *a = &r128_gpu_variants[v];

        gpu_kern_by_axes[a->tex][a->lod][a->blend][a->z] = (int8_t) v;
    }
}

/* Bitmask of reachable mip slots living in the staging arena. With the
   arena import live these draws run on the GPU; the mask goes into the
   reject log so a reject for AGP residency without the import can be
   told apart from a format reject. */
static uint32_t
gpu_staged_mips(const rage128_raster_state_t *rs, int st)
{
    const uint32_t *so  = st ? rs->sec_stage_off : rs->prim_stage_off;
    int             top = rs->d.sh[st].top;
    uint32_t        m   = 0;

    if (top < 0)
        top = 0;
    if (top > 10)
        top = 10;
    for (int sl = 0; sl <= top; sl++)
        if (so[sl] != R128_TEX_STAGE_NONE)
            m |= 1u << sl;
    return m;
}

/* Staged AGP color/Z render targets: fine when both span arenas are
   imported (the kernels address them through bindings 9-12) and the
   staged length covers the draw's unclamped scissor extent.
   r3d_stage_extent clamps the staged extent to the bytes left in the
   32 MB half its base lives in; the interpreter skips pixels past a
   clamped surface's end as out of range, and the kernel has no such
   check. Otherwise the staged length always covers the extent, so the
   clamp is the only case this rejects. */
static int
gpu_czstage_ok(const r128_gpu_t *g, const rage128_t *dev,
               const rage128_raster_state_t *rs)
{
    const rage128_draw_state_t *d = &rs->d;

    if (!rs->c_staged && !rs->z_staged)
        return 1;
    if (!g->czstage_ok)
        return 0;
    if (rs->c_staged) {
        uint64_t need = (((uint64_t) d->sy1 * rs->dst_pitch + d->sx1 + 1u)
                             * (uint32_t) d->bpp
                         + 3u)
            & ~3ull;

        if (need > dev->c_stage.len)
            return 0;
    }
    if (rs->z_staged) {
        uint64_t need = (((uint64_t) d->sy1 * d->zrowpx + d->sx1 + 1u)
                             * (uint32_t) d->zbpp
                         + 3u)
            & ~3ull;

        if (need > dev->z_stage.len)
            return 0;
    }
    return 1;
}

/* -1 = no kernel covers this state (CPU path); else kernel id matching
   the r128_gpu_variants table */
static int
gpu_state_kernel(rage128_t *dev, const rage128_raster_state_t *rs)
{
    r128_gpu_t                 *g  = (r128_gpu_t *) dev->gpu;
    const rage128_draw_state_t *d  = &rs->d;
    const r3d_comb_desc_t      *cb = &d->comb[0];
    int                         k  = -1;
    /* dst: three 16bpp packed formats through the u16 view, ARGB8888
       through the coherent word view. Dither and the plane mask are
       runtime per-tri selectors. A cell that straddles its view's
       element (an odd dst_offset) takes the kernel's byte-assembled
       reads and byte-lane atomic stores; staged arena addresses are
       always aligned (arena-relative = row * pitch_bytes + px * bpp). */
    int dst_ok = (d->bpp == 2 && (d->dst_dt == 3 || d->dst_dt == 4 || d->dst_dt == 15))
        || (d->bpp == 4 && d->dst_dt == 6);
    /* premult is not gated: it only selects how the setup builds
       tctx.sta and the rest (premultiplied by rhw or raw), and the
       per-pixel formula the kernel mirrors uses the built values either
       way */
    /* row-indexed segment arrays: GPU_ROW_CAP spans the whole 14-bit
       scissor field, so this holds for every state the registers can
       express; it stays as the explicit tie to those arrays */
    int row_ok = d->sy0 >= 0 && d->sy1 < (int) GPU_ROW_CAP;
    int czs_ok = gpu_czstage_ok(g, dev, rs);
    /* A tiled color/Z surface needs no kernel transform: the capture
       walks each row in tile-column pieces and
       rebases the row address per piece, so the kernel's own (row base
       + x * bpp) lands on the tiled byte with nothing changed in the
       kernel. A tiled texture level has no per-row base to rebase,
       since each access would need the tile transform, so an unstaged
       one refuses. */
    int common = d->draw_ok && dst_ok && row_ok && czs_ok
        && !d->tex_tiled;
    /* The z axis means the z cell is live: depth test, stencil, or both
       (the TEX_CNTL_C stencil enable, R128_STENCIL_ENABLE in
       xf86-video-r128, keeps the cell live with the Z test off; the
       capture then pins zfn to ALWAYS and z_wr to 0). The kernel
       derives zmax from zbpp, so reject any state where the two
       disagree rather than let the two views of depth differ. Stencil
       is also held to the 4-byte cell here, though the state derivation
       already ensures it. */
    int zlive = d->z_en || d->sten_on;
    int zshape_ok = (d->zbpp == 2 && d->zmax == 0xffff)
        || (d->zbpp == 4 && d->zmax == 0xffffff);
    int sten_ok = !d->sten_on || d->zbpp == 4;
    int z_ok    = !zlive || (zshape_ok && d->zrowpx && sten_ok);
    int tex_ok  = 1;
    int texn    = 0;

    if (d->tex_en || d->sec_en) {
        /* do_persp is not gated: it is a runtime per-tri selector in the
           tri record, and the affine branch mirrors the interpreter's
           (no rhw dot, ir 1.0, raw screen-space LOD gradients).

           The JIT's texture gate is not consulted: the kernel mirrors
           the interpreter, not the JIT, and gpu_stage_ok is the GPU's
           own predicate. */
        tex_ok = (!d->tex_en || gpu_stage_ok(g, rs, 0))
            && (!d->sec_en || gpu_stage_ok(g, rs, 1));
        texn = d->sec_en ? 2 : 1;
    }

    if (common && z_ok && tex_ok)
        /* V_LOD is stage 0's LOD chain (stage 1's need_lod2 stays a
           runtime selector) and means nothing without a texture. The
           index holds -1 for any combination the table does not have,
           so the gate can never name a kernel that does not exist. */
        k = gpu_kern_by_axes[texn][texn ? !!d->need_lod : 0][!!d->alpha_en]
                            [!!zlive];
    if (k >= 0)
        return k;

    /* shape tally: every failed predicate of this reject, not just the
       first, so the close histogram names each clause traffic hits */
    g->st_rej_total++;
    if (!d->draw_ok)
        g->st_rej[GPU_REJ_DRAW]++;
    if (!dst_ok)
        g->st_rej[GPU_REJ_DST]++;
    if (!row_ok)
        g->st_rej[GPU_REJ_ROWCAP]++;
    if (!czs_ok)
        g->st_rej[GPU_REJ_CZSTAGE]++;
    if (d->tex_tiled)
        g->st_rej[GPU_REJ_TEXTILE]++;
    if (zlive && !zshape_ok)
        g->st_rej[GPU_REJ_ZSHAPE]++;
    if (zlive && !d->zrowpx)
        g->st_rej[GPU_REJ_ZROW]++;
    if (zlive && !sten_ok)
        g->st_rej[GPU_REJ_STEN16]++;
    if (d->tex_en) {
        int why = gpu_stage_why(g, rs, 0);

        if (why)
            g->st_rej[GPU_REJ_S0_FMT + (why - 1)]++;
    }
    if (d->sec_en) {
        int why = gpu_stage_why(g, rs, 1);

        if (why)
            g->st_rej[GPU_REJ_S1_FMT + (why - 1)]++;
    }
    if (common && z_ok && tex_ok)
        g->st_rej[GPU_REJ_NOKERN]++;

    if (g->prof && g->rejects_logged < 16) {
        g->rejects_logged++;
        gpu_log("RAGE128 GPU reject: dt=%u bpp=%d dith=%d wm=%08x aux=%d z=%d/%d zfn=%u zbpp=%d "
                "st=%d sp=%d fog=%d at=%d ab=%d b=%u/%u/%u ck=%d tex=%d/%d persp=%d pm=%d lod=%d/%d "
                "s0[dt=%u s3=%u cl=%u/%u minb=%u mag=%u md=%d top=%d] "
                "s1[dt=%u s3=%u cl=%u/%u minb=%u mag=%u md=%d top=%d] "
                "comb=%u/%u/%u/%u/%u/%u/%u cstg=%d zstg=%d smip=%03x/%03x "
                "stip=%d tld=%d/%d/%d\n",
                d->dst_dt, d->bpp, d->dither, d->wmask, d->aux_on, d->z_en, d->z_wr,
                d->zfn, d->zbpp, d->sten_on, d->spec_en, d->fog_en, d->atest_en,
                d->alpha_en, d->bsrc, d->bdst, d->bfcn, d->need_ck, d->tex_en,
                d->sec_en, d->do_persp, d->premult, d->need_lod, d->need_lod2,
                d->sh[0].dt, d->sh[0].s3tc,
                d->sh[0].clamp_s, d->sh[0].clamp_t, d->sh[0].minb, d->sh[0].mag,
                d->sh[0].mipdis, d->sh[0].top,
                d->sh[1].dt, d->sh[1].s3tc,
                d->sh[1].clamp_s, d->sh[1].clamp_t, d->sh[1].minb, d->sh[1].mag,
                d->sh[1].mipdis, d->sh[1].top,
                cb->comb, cb->cfac, cb->ifac,
                cb->fmsb, cb->comba, cb->ifaca, cb->afac,
                rs->c_staged, rs->z_staged,
                gpu_staged_mips(rs, 0), gpu_staged_mips(rs, 1),
                d->stip_en, d->c_tiled, d->z_tiled, d->tex_tiled);
    }
    return -1;
}

/* Census query: the gate's answer for rs, with the reject histogram and
   the reject log restored afterwards, so a census run's close lines
   match a plain run's. -2 = no backend attached. */
int
rage128_gpu_census_kernel(rage128_t *dev, const rage128_raster_state_t *rs)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint64_t    rej[GPU_REJ_KEYS], tot;
    int         logged, k;

    if (!g)
        return -2;
    memcpy(rej, g->st_rej, sizeof(rej));
    tot               = g->st_rej_total;
    logged            = g->rejects_logged;
    g->rejects_logged = 16; /* quota spent: the query prints nothing */
    k                 = gpu_state_kernel(dev, rs);
    memcpy(g->st_rej, rej, sizeof(rej));
    g->st_rej_total   = tot;
    g->rejects_logged = logged;
    return k;
}

/* ------------------------------------------------------------------ */
/* Boot-time gate census.                                              */
/*                                                                     */
/* Run gpu_state_kernel over a fixed synthetic state matrix and log     */
/* what this build accepts. The reject log only fires on traffic and    */
/* the close histogram only counts what was accepted, so a predicate    */
/* that wrongly rejects everything on some host would otherwise go      */
/* unnoticed until someone examined a title closely. The census names   */
/* the gap on the first boot, with no title and no traffic.             */
/* ------------------------------------------------------------------ */

/* A minimal state that passes every predicate: 640x480 ARGB8888 dst,
   32-bit z cell, resident ARGB8888 texels. Axis arguments select the
   kernel family; the shape rows below vary one field off this base. */
static void
gpu_census_state(rage128_raster_state_t *rs, int tex, int lod, int blend,
                 int z)
{
    memset(rs, 0, sizeof(*rs));
    for (int i = 0; i < 11; i++)
        rs->prim_stage_off[i] = rs->sec_stage_off[i] = R128_TEX_STAGE_NONE;
    rs->dst_pitch  = 640;
    rs->d.draw_ok  = 1;
    rs->d.dst_dt   = 6;
    rs->d.bpp      = 4;
    rs->d.sx1      = 639;
    rs->d.sy1      = 479;
    rs->d.zbpp     = 4;
    rs->d.zmax     = 0xffffff;
    rs->d.zrowpx   = 640;
    rs->d.z_en     = z;
    rs->d.alpha_en = blend;
    rs->d.tex_en   = tex > 0;
    rs->d.sec_en   = tex > 1;
    rs->d.need_lod = lod;
    for (int s = 0; s < 2; s++) {
        rs->d.sh[s].dt  = 6;
        rs->d.sh[s].top = 3;
    }
}

/* Append " name" to a bounded miss list. */
static void
gpu_census_miss(char *buf, size_t len, int *n, const char *name)
{
    size_t used = strlen(buf);

    (*n)++;
    if (used + strlen(name) + 2 < len)
        snprintf(buf + used, len - used, " %s", name);
}

static void
gpu_gate_census(rage128_t *dev)
{
    r128_gpu_t            *g = (r128_gpu_t *) dev->gpu;
    rage128_raster_state_t rs;
    uint64_t               rej[GPU_REJ_KEYS];
    uint64_t               rej_total = g->st_rej_total;
    int                    logged    = g->rejects_logged;
    char                   miss[384] = "";
    int                    nmiss     = 0;
    int                    kern_ok = 0, dst_ok = 0, tex_ok = 0, z_ok = 0;
    int                    tile_ok = 0;

    /* the census is not traffic: keep it out of both the reject log and
       the reject-shape histogram */
    memcpy(rej, g->st_rej, sizeof(rej));
    g->rejects_logged = 16;

    /* 1. kernel families: every combination the variant table ships */
    for (int tex = 0; tex <= 2; tex++)
        for (int lod = 0; lod <= (tex ? 1 : 0); lod++)
            for (int blend = 0; blend <= 1; blend++)
                for (int z = 0; z <= 1; z++) {
                    int want = gpu_kern_by_axes[tex][lod][blend][z];

                    if (want < 0)
                        continue;
                    gpu_census_state(&rs, tex, lod, blend, z);
                    if (gpu_state_kernel(dev, &rs) == want)
                        kern_ok++;
                    else
                        gpu_census_miss(miss, sizeof(miss), &nmiss,
                                        r128_gpu_variants[want].name);
                }

    /* 2. dst formats: the three packed 16bpp and ARGB8888 */
    {
        static const struct {
            uint32_t    dt;
            int         bpp;
            const char *name;
        } dsts[4] = {
            { 3,  2, "dst:1555" },
            { 4,  2, "dst:565"  },
            { 15, 2, "dst:4444" },
            { 6,  4, "dst:8888" }
        };

        for (int i = 0; i < 4; i++) {
            gpu_census_state(&rs, 0, 0, 0, 1);
            rs.d.dst_dt = dsts[i].dt;
            rs.d.bpp    = dsts[i].bpp;
            if (gpu_state_kernel(dev, &rs) >= 0)
                dst_ok++;
            else
                gpu_census_miss(miss, sizeof(miss), &nmiss, dsts[i].name);
        }
    }

    /* 3. texel formats the kernel decodes, stage 0 (10 = CI16 and 13
       have no decoder in either lane and are not listed) */
    {
        static const uint32_t dts[13] = { 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                          11, 12, 14, 15 };

        for (int i = 0; i < 13; i++) {
            gpu_census_state(&rs, 1, 0, 0, 1);
            rs.d.sh[0].dt = dts[i];
            if (gpu_state_kernel(dev, &rs) >= 0)
                tex_ok++;
            else {
                char n[16];

                snprintf(n, sizeof(n), "texel:dt%u", dts[i]);
                gpu_census_miss(miss, sizeof(miss), &nmiss, n);
            }
        }
        for (uint32_t s3 = 0; s3 <= 3; s3++) {
            gpu_census_state(&rs, 1, 0, 0, 1);
            rs.d.sh[0].dt   = 0;
            rs.d.sh[0].s3tc = s3;
            if (gpu_state_kernel(dev, &rs) >= 0)
                tex_ok++;
            else {
                char n[16];

                snprintf(n, sizeof(n), "texel:s3tc%u", s3);
                gpu_census_miss(miss, sizeof(miss), &nmiss, n);
            }
        }
    }

    /* 4. depth cell shapes: 16-bit z, 32-bit z, stencil, stencil-only */
    {
        gpu_census_state(&rs, 0, 0, 0, 1);
        rs.d.zbpp = 2;
        rs.d.zmax = 0xffff;
        if (gpu_state_kernel(dev, &rs) >= 0)
            z_ok++;
        else
            gpu_census_miss(miss, sizeof(miss), &nmiss, "z:16");

        gpu_census_state(&rs, 0, 0, 0, 1);
        if (gpu_state_kernel(dev, &rs) >= 0)
            z_ok++;
        else
            gpu_census_miss(miss, sizeof(miss), &nmiss, "z:32");

        gpu_census_state(&rs, 0, 0, 0, 1);
        rs.d.sten_on = 1;
        if (gpu_state_kernel(dev, &rs) >= 0)
            z_ok++;
        else
            gpu_census_miss(miss, sizeof(miss), &nmiss, "z:sten");

        gpu_census_state(&rs, 0, 0, 0, 0);
        rs.d.sten_on = 1;
        if (gpu_state_kernel(dev, &rs) >= 0)
            z_ok++;
        else
            gpu_census_miss(miss, sizeof(miss), &nmiss, "z:sten-noz");
    }

    /* 5. 64-byte x 16-row tiling. Color and Z are addressed in place
       (the capture splits each row at the tile-column edges and rebases
       each piece), so the gate must accept them. The Windows 98
       Direct3D driver was seen setting Z_TILE on nearly every draw, so
       a gap here would send almost all of its draws back to the
       interpreter. A tiled texture level must still refuse: the sampler
       has no per-row base to rebase, and accepting one would read
       linear bytes. */
    {
        static const struct {
            int         c, z, t, want;
            const char *name;
        } tl[4] = {
            { 1, 0, 0, 1, "tiled:color"          },
            { 0, 1, 0, 1, "tiled:z"              },
            { 1, 1, 0, 1, "tiled:color+z"        },
            { 0, 0, 1, 0, "tiled:texel-accepted" }
        };

        for (int i = 0; i < 4; i++) {
            gpu_census_state(&rs, tl[i].t, 0, 0, 1);
            rs.d.c_tiled   = tl[i].c;
            rs.d.z_tiled   = tl[i].z;
            rs.d.tex_tiled = tl[i].t;
            if ((gpu_state_kernel(dev, &rs) >= 0) == tl[i].want)
                tile_ok++;
            else
                gpu_census_miss(miss, sizeof(miss), &nmiss, tl[i].name);
        }
    }

    memcpy(g->st_rej, rej, sizeof(rej));
    g->st_rej_total   = rej_total;
    g->rejects_logged = logged;

    gpu_log("RAGE128 GPU: gate census: kernels %d/%d dst %d/4 texel %d/17 "
            "z %d/4 tile %d/4\n",
            kern_ok, GPU_KERNELS, dst_ok, tex_ok, z_ok, tile_ok);
    if (nmiss)
        gpu_log("RAGE128 GPU: gate census HOLES (%d):%s\n", nmiss, miss);
}

/* ------------------------------------------------------------------ */
/* Segment flush: sort and batch the spans, record the dispatches,     */
/* submit with a fence.                                                */
/* ------------------------------------------------------------------ */

static void gpu_verify_segment(rage128_t *dev, r128_gpu_t *g,
                               const gpu_disp_t *dp, uint32_t nd,
                               uint32_t clo, uint32_t chi, uint32_t zlo, uint32_t zhi,
                               uint32_t qlo, uint32_t qhi, int cause);

/* Same-row spans may share a batch only when their color and z byte
   ranges are disjoint. Disjoint pixels are not enough: a 32bpp draw and
   a 16bpp draw on one row cover the same bytes at different x, and
   mixed zbpp overlaps the z row the same way. The ranges come from the
   bb_of host shadow, filled when the span is appended. */
static inline int
gpu_spans_alias(const r128_gpu_t *g, uint32_t a, uint32_t b)
{
    /* commutative stencil pair: color untouched (dead), z field
       untouched (no z write), and the stencil byte updated atomically
       with operations that commute, so overlap is safe in any order */
    if (g->sctl_of[a] && g->sctl_of[a] == g->sctl_of[b]
        && g->srm_of[a] == g->srm_of[b])
        return 0;
    if (g->bb_of[a].c0 < g->bb_of[b].c1 && g->bb_of[b].c0 < g->bb_of[a].c1)
        return 1;
    if (g->bb_of[a].z0 < g->bb_of[b].z1 && g->bb_of[b].z0 < g->bb_of[a].z1)
        return 1;
    return 0;
}

/* Pack one row's spans (already in draw order) into batches for the
   shading kernel. A batch holds spans whose color and z byte ranges are
   pairwise disjoint and that together fit the workgroup's
   GPU_BATCH_SLOTS lane groups, so they shade in one pass. Draw order is
   kept by leveling (below): overlapping spans always land in different
   batches, in their original order. Byte ranges within one row are
   enough because a segment flushes on any change of render-target base,
   pitch, bpp or tiling (GF_RT), so a row's color and z addresses depend
   only on py. Returns the number of batches written, the first at
   slots + nbat * GPU_BATCH_SLOTS. */
static uint32_t
gpu_batch_row(r128_gpu_t *g, uint32_t *slots, uint32_t nbat,
              const uint32_t *ord, uint32_t c)
{
    const gpu_slot_t *sl   = &g->slot[g->build_slot];
    const seg_span_t *sp   = (const seg_span_t *) sl->b_spans.map;
    uint32_t          base = nbat, nlv = 0, used = 0;
    uint32_t          curlv = 0xffffffffu;
    uint64_t          pt0   = g->prof ? prof_now() : 0;
    uint64_t          pt1   = 0;

    /* Uniform commutative class: every pair is exempt, so the scan could
       only return level 0; skip it. A stencil-volume fill row has
       exactly this shape, and it is also the crowded row shape where the
       quadratic scan costs most. */
    if (c > 0) {
        uint32_t s0 = g->sctl_of[ord[0]], r0 = g->srm_of[ord[0]];

        if (s0 != 0) {
            uint32_t j = 1;

            while (j < c && g->sctl_of[ord[j]] == s0 && g->srm_of[ord[j]] == r0)
                j++;
            if (j == c) {
                memset(g->lvl, 0, (size_t) c * sizeof(uint32_t));
                if (g->prof)
                    g->pr_lv_rows_cls[0]++;
                goto leveled;
            }
        }
    }
    {
        uint32_t any = 0;

        for (uint32_t j = 0; j < c; j++)
            any |= g->sctl_of[ord[j]];
        if (g->prof) {
            unsigned cls = any ? 2u : 1u;

            g->pr_lv_rows_cls[cls]++;
            g->pr_lv_pairs_cls[cls] += (uint64_t) c * (c - 1u) / 2u;
        }
        /* No commutative-stencil span in the row: the exemption cannot
           apply, so aliasing is plain interval overlap. Gather the
           ranges through ord[] once, then scan flat
           arrays without branches: the same pairs and the same levels
           as the general loop below, without a random gather per pair.
           lv takes lvl[t]+1 only when that is larger, which is the max
           the general loop computes; a t that does not alias gives 0. */
        if (!any && c <= GPU_BATCH_LEVEL_MAX) {
            uint32_t *rc0 = g->lv_c0, *rc1 = g->lv_c1;
            uint32_t *rz0 = g->lv_z0, *rz1 = g->lv_z1;

            for (uint32_t j = 0; j < c; j++) {
                uint32_t si = ord[j];

                rc0[j] = g->bb_of[si].c0;
                rc1[j] = g->bb_of[si].c1;
                rz0[j] = g->bb_of[si].z0;
                rz1[j] = g->bb_of[si].z1;
            }
            for (uint32_t j = 0; j < c; j++) {
                uint32_t lv  = 0;
                uint32_t jc0 = rc0[j], jc1 = rc1[j];
                uint32_t jz0 = rz0[j], jz1 = rz1[j];

                for (uint32_t t = 0; t < j; t++) {
                    uint32_t ov = ((jc0 < rc1[t]) & (rc0[t] < jc1))
                        | ((jz0 < rz1[t]) & (rz0[t] < jz1));
                    uint32_t cand = (0u - ov) & (g->lvl[t] + 1u);

                    if (cand > lv)
                        lv = cand;
                }
                g->lvl[j] = lv;
            }
            if (g->prof)
                g->pr_lvpairs += (uint64_t) c * (c - 1u) / 2u;
            goto leveled;
        }
    }
    /* Level each span: it must land strictly after every earlier span it
       overlaps, so level = 1 + the max level of those. Spans that share
       a level are pairwise disjoint, so a level shades in one pass and
       splits further only to fit the lane budget. A crowded row then
       needs as many passes as its overlap depth, where packing
       consecutive spans greedily would cut a batch at every overlap.
       Crowded rows set the dispatch's wall time. */
    for (uint32_t j = 0; j < c; j++) {
        uint32_t lv = 0;

        /* leveling is O(n^2); a row past GPU_BATCH_LEVEL_MAX falls back
           to one span per level, which is correct, just unpacked */
        if (c > GPU_BATCH_LEVEL_MAX) {
            g->lvl[j] = j;
            continue;
        }
        for (uint32_t t = 0; t < j; t++)
            if (gpu_spans_alias(g, ord[j], ord[t]) && g->lvl[t] >= lv)
                lv = g->lvl[t] + 1;
        g->lvl[j] = lv;
        if (g->prof)
            g->pr_lvpairs += j;
    }
leveled:
    if (g->prof) {
        unsigned b = prof_lv_bin(c);

        pt1 = prof_now();
        g->pr_sort_level_ns += pt1 - pt0;
        g->pr_sort_rows++;
        g->pr_lvbin_rows[b]++;
        g->pr_lvbin_pairs[b] += (uint64_t) c * (c - 1u) / 2u;
        g->pr_lvbin_ns[b] += pt1 - pt0;
    }
    for (uint32_t j = 0; j < c; j++)
        if (g->lvl[j] + 1u > nlv)
            nlv = g->lvl[j] + 1u;
    if (g->prof) {
        g->pr_lvl += nlv;
        g->pr_row_nlv = nlv;
    }

    memset(g->lvcnt, 0, (size_t) (nlv + 1) * sizeof(uint32_t));
    for (uint32_t j = 0; j < c; j++)
        g->lvcnt[g->lvl[j] + 1u]++;
    for (uint32_t L = 0; L < nlv; L++)
        g->lvcnt[L + 1u] += g->lvcnt[L];
    for (uint32_t j = 0; j < c; j++)
        g->bylv[g->lvcnt[g->lvl[j]]++] = j;

    for (uint32_t k = 0; k < c; k++) {
        uint32_t j    = g->bylv[k];
        uint32_t si   = ord[j];
        uint32_t need = ((uint32_t) (sp[si].x1 - sp[si].x0) + GPU_SLOT_W)
            >> GPU_SLOT_SHIFT;
        uint32_t open = 0;

        if (used > 0 && (g->lvl[j] != curlv || used + need > GPU_BATCH_SLOTS)) {
            nbat++;
            used = 0;
        }
        if (used == 0) {
            for (uint32_t q = 0; q < GPU_BATCH_SLOTS; q++)
                slots[nbat * GPU_BATCH_SLOTS + q] = 0xffffffffu;
            /* Slot 0 bit 0 = "opens a level", the kernel's barrier flag.
               A batch cut by the lane budget alone continues its level,
               whose spans cannot alias, so the kernel skips its barrier;
               curlv's initial 0xffffffff marks the row's first batch.
               Pixel offsets are multiples of GPU_SLOT_W, so bit 0 is
               free; the kernel decodes the offset with GPU_SLOT_MASK. */
            open = (g->lvl[j] != curlv);
        }
        for (uint32_t q = 0; q < need; q++)
            slots[nbat * GPU_BATCH_SLOTS + used + q] = (si << 8)
                | (q * GPU_SLOT_W);
        slots[nbat * GPU_BATCH_SLOTS] |= open;
        if (g->prof)
            g->pr_slots += need;
        used += need;
        curlv = g->lvl[j];
    }
    if (g->prof)
        g->pr_sort_emit_ns += prof_now() - pt1;
    return (used > 0 ? nbat + 1 : nbat) - base;
}

/* Number of submitted slots. A submitted slot owns every buffer the GPU
   can still read; only retirement lets a slot be overwritten or
   recorded again. */
static unsigned
gpu_inflight_count(const r128_gpu_t *g)
{
    unsigned n = 0;

    for (unsigned i = 0; i < GPU_RING_SLOTS; i++)
        n += atomic_load_explicit(&g->slot[i].submitted, memory_order_acquire) != 0;
    return n;
}

/* Executor busy time up to now: the closed busy periods plus the one
   still open; otherwise a period spanning several intervals would be
   counted whole in the interval where it ends. Read across threads,
   profiling only: a race costs a little accuracy. */
static uint64_t
gpu_cce_busy_now(const rage128_t *dev, uint64_t now)
{
    uint64_t t0 = dev->cce_busy_t0;

    return dev->cce_busy_ns + (t0 && now > t0 ? now - t0 : 0);
}

/* LFB probe lines: cur minus base (base = the interval-start snapshot,
   or NULL for the whole run). Two lines: what the guest touched (count,
   width, direction, region, top 64 KB buckets) and what the path cost
   (per-access slices from the clocked eighth, the barrier prologue
   split, and the unsampled hit path). */
static void
gpu_lfbt_print(const rage128_lfbt_t *c, const rage128_lfbt_t *b,
               const char *tag, uint64_t ms)
{
    rage128_lfbt_t d;
    uint64_t       rdn, rdb, wrn, wrb, hn, tot;
    char           top[128];
    char          *p = top;

#    define LD(f) (d.f = c->f - (b ? b->f : 0))
    LD(total);
    for (int w = 0; w < 2; w++)
        for (int i = 0; i < 3; i++)
            LD(n[w][i]);
    for (int i = 0; i < 4; i++)
        LD(reg[i]);
    LD(split);
    LD(sampled);
    LD(xlate_ns);
    LD(barrier_ns);
    LD(store_ns);
    LD(tcd_ns);
    LD(rng_ns);
    LD(hits);
    LD(quiesce_ns);
    LD(flush_ns);
    LD(bkt_dropped);
#    undef LD
    if (!d.total)
        return;
    rdn = d.n[0][0] + d.n[0][1] + d.n[0][2];
    rdb = d.n[0][0] + 2 * d.n[0][1] + 4 * d.n[0][2];
    wrn = d.n[1][0] + d.n[1][1] + d.n[1][2];
    wrb = d.n[1][0] + 2 * d.n[1][1] + 4 * d.n[1][2];
    /* top 4 buckets by delta, a selection pass over the monotonic table;
       an empty table or no moving bucket leaves the list empty */
    top[0] = '\0';
    {
        uint64_t dc[RAGE128_LFBT_BKTS];
        uint32_t used = c->bkt_used;

        if (used > RAGE128_LFBT_BKTS)
            used = RAGE128_LFBT_BKTS;
        for (uint32_t i = 0; i < used; i++)
            dc[i] = c->bkt_cnt[i] - ((b && i < b->bkt_used) ? b->bkt_cnt[i] : 0);
        for (int k = 0; k < 4; k++) {
            uint32_t best = 0;

            for (uint32_t i = 1; i < used; i++)
                if (dc[i] > dc[best])
                    best = i;
            if (!used || !dc[best])
                break;
            gpu_append(&p, top + sizeof(top), "%s0x%06x/%llu", k ? "," : "",
                       c->bkt_key[best] << 16, (unsigned long long) dc[best]);
            dc[best] = 0;
        }
    }
    gpu_log("RAGE128 GPU: lfb %s: t=%llums rd=%llu/%lluB (b/w/l %llu/%llu/%llu) "
            "wr=%llu/%lluB (b/w/l %llu/%llu/%llu) split=%llu "
            "front=%llu dst=%llu z=%llu other=%llu top=%s dropped=%llu\n",
            tag, (unsigned long long) ms,
            (unsigned long long) rdn, (unsigned long long) rdb,
            (unsigned long long) d.n[0][0], (unsigned long long) d.n[0][1],
            (unsigned long long) d.n[0][2],
            (unsigned long long) wrn, (unsigned long long) wrb,
            (unsigned long long) d.n[1][0], (unsigned long long) d.n[1][1],
            (unsigned long long) d.n[1][2],
            (unsigned long long) d.split,
            (unsigned long long) d.reg[0], (unsigned long long) d.reg[1],
            (unsigned long long) d.reg[2], (unsigned long long) d.reg[3],
            top, (unsigned long long) d.bkt_dropped);
    hn  = d.sampled ? d.sampled : 1;
    tot = d.xlate_ns + d.barrier_ns + d.store_ns;
    gpu_log("RAGE128 GPU: lfb-ns %s: t=%llums sampled=%llu ns/acc=%llu "
            "(xlate=%llu barrier=%llu store=%llu; barrier tcd=%llu rng=%llu) "
            "est=%llums hits=%llu quiesce=%lluus flush=%lluus\n",
            tag, (unsigned long long) ms, (unsigned long long) d.sampled,
            (unsigned long long) (tot / hn),
            (unsigned long long) (d.xlate_ns / hn),
            (unsigned long long) (d.barrier_ns / hn),
            (unsigned long long) (d.store_ns / hn),
            (unsigned long long) (d.tcd_ns / hn),
            (unsigned long long) (d.rng_ns / hn),
            (unsigned long long) (tot / hn * d.total / 1000000),
            (unsigned long long) d.hits,
            (unsigned long long) (d.quiesce_ns / 1000),
            (unsigned long long) (d.flush_ns / 1000));
}

static void
gpu_prof_interval(r128_gpu_t *g, int force)
{
    uint64_t now, gpu, reuse, present, d2, opns = 0;
    uint32_t optag = 0;
    unsigned in;

    if (!g->prof)
        return;
    now = prof_now();
    if (!g->pr_int_t0) {
        g->pr_int_epoch      = now;
        g->pr_int_t0         = now;
        g->pr_int_gpu_ns     = g->pr_gpu_lad_ns + g->pr_gpu_shade_ns;
        g->pr_int_reuse_ns   = g->pr_wait_reuse_ns;
        g->pr_int_present_ns = g->pr_wait_drain_cause_ns[GF_PRESENT];
        g->pr_int_2d_ns      = g->pr_wait_drain_cause_ns[GF_2D];
        g->pr_int_submits    = g->st_submits;
        g->pr_int_chained    = g->st_chained;
        g->pr_int_tsc        = tsc;
        g->pr_int_polls      = g->owner->tel_polls;
        g->pr_int_cce_ns     = gpu_cce_busy_now(g->owner, now);
        memcpy(g->pr_int_reads, g->owner->tel_reads, sizeof(g->pr_int_reads));
        g->pr_int_lfbt      = g->owner->lfbt;
        g->pr_int_lad_ns    = g->pr_gpu_lad_ns;
        g->pr_int_mir_ns    = g->pr_gpu_mirror_ns;
        g->pr_int_mir_bytes = g->pr_mirror_bytes;
        g->pr_int_cap_ns    = g->pr_capture_ns;
        g->pr_int_sort_ns   = g->pr_sort_ns;
        g->pr_int_rec_ns    = g->pr_record_ns;
        return;
    }
    if (!force && now - g->pr_int_t0 < 1000000000ull)
        return;

    gpu     = g->pr_gpu_lad_ns + g->pr_gpu_shade_ns - g->pr_int_gpu_ns;
    reuse   = g->pr_wait_reuse_ns - g->pr_int_reuse_ns;
    present = g->pr_wait_drain_cause_ns[GF_PRESENT] - g->pr_int_present_ns;
    d2      = g->pr_wait_drain_cause_ns[GF_2D] - g->pr_int_2d_ns;
    for (uint32_t tag = 0; tag < GPU_2D_TAGS; tag++)
        if (g->pr_int_2d_tag_ns[tag] > opns) {
            optag = tag;
            opns  = g->pr_int_2d_tag_ns[tag];
        }
    in = gpu_inflight_count(g);
    /* emu = guest time over wall time, the speed percentage 86Box shows:
       tsc counts guest cycles at cpuclock. polls = the driver's reads
       of GUI_STAT and the ring pointers, and cce = executor busy time;
       together they tell a slow frame at full emulation speed that is
       guest-bound from one that waits on the emulated engine. spin =
       the register the guest read most this interval and how often: a
       busy-wait inside a frame shows here even when the close
       histogram's gap column is empty. */
    {
        uint64_t polls    = g->owner->tel_polls;
        uint64_t cce      = gpu_cce_busy_now(g->owner, now);
        double   guest    = cpuclock > 0.0 ? (double) (tsc - g->pr_int_tsc) / cpuclock : 0.0;
        double   wall     = (double) (now - g->pr_int_t0) / 1e9;
        unsigned emu      = wall > 0.0 ? (unsigned) (guest / wall * 100.0 + 0.5) : 0;
        unsigned spin_reg = 0;
        uint32_t spin_n   = 0;

        for (unsigned i = 0; i < 0x4000 >> 2; i++) {
            uint32_t d = g->owner->tel_reads[i] - g->pr_int_reads[i];

            if (d > spin_n) {
                spin_n   = d;
                spin_reg = i << 2;
            }
        }
        memcpy(g->pr_int_reads, g->owner->tel_reads, sizeof(g->pr_int_reads));

        /* lad/mir split the GPU side (ladder+shade vs mirror copies,
           mir bytes in MB); cap/sort/rec split the host side per second */
        gpu_log("RAGE128 GPU: interval: t=%llums dt=%llums emu=%u%% gpu=%llums "
                "reuse=%llums present=%llums 2d=%llums op=%03x/%s opwait=%llums "
                "submits=%llu chained=%llu max=%u in=%u cap=%u hold=%dus "
                "polls=%llu cce=%llums spin=0x%04x/%u "
                "lad=%llums mir=%llums/%lluMB cap=%llums sort=%llums rec=%llums\n",
                (unsigned long long) ((now - g->pr_int_epoch) / 1000000),
                (unsigned long long) ((now - g->pr_int_t0) / 1000000), emu,
                (unsigned long long) (gpu / 1000000),
                (unsigned long long) (reuse / 1000000),
                (unsigned long long) (present / 1000000),
                (unsigned long long) (d2 / 1000000), optag, gpu_2d_tag_name(optag),
                (unsigned long long) (opns / 1000000),
                (unsigned long long) (g->st_submits - g->pr_int_submits),
                (unsigned long long) (g->st_chained - g->pr_int_chained),
                g->pr_int_max_inflight, in,
                atomic_load_explicit(&g->depth_cap, memory_order_relaxed),
                g->pr_pace_hold ? atomic_load(g->pr_pace_hold) : 0,
                (unsigned long long) (polls - g->pr_int_polls),
                (unsigned long long) ((cce - g->pr_int_cce_ns) / 1000000),
                spin_reg, spin_n,
                (unsigned long long) ((g->pr_gpu_lad_ns - g->pr_int_lad_ns) / 1000000),
                (unsigned long long) ((g->pr_gpu_mirror_ns - g->pr_int_mir_ns) / 1000000),
                (unsigned long long) ((g->pr_mirror_bytes - g->pr_int_mir_bytes) >> 20),
                (unsigned long long) ((g->pr_capture_ns - g->pr_int_cap_ns) / 1000000),
                (unsigned long long) ((g->pr_sort_ns - g->pr_int_sort_ns) / 1000000),
                (unsigned long long) ((g->pr_record_ns - g->pr_int_rec_ns) / 1000000));
        g->pr_int_lad_ns    = g->pr_gpu_lad_ns;
        g->pr_int_mir_ns    = g->pr_gpu_mirror_ns;
        g->pr_int_mir_bytes = g->pr_mirror_bytes;
        g->pr_int_cap_ns    = g->pr_capture_ns;
        g->pr_int_sort_ns   = g->pr_sort_ns;
        g->pr_int_rec_ns    = g->pr_record_ns;
        g->pr_int_tsc       = tsc;
        g->pr_int_polls     = polls;
        g->pr_int_cce_ns    = cce;
        {
            rage128_lfbt_t cur = g->owner->lfbt;

            gpu_lfbt_print(&cur, &g->pr_int_lfbt, "int",
                           (now - g->pr_int_epoch) / 1000000);
            g->pr_int_lfbt   = cur;
            g->pr_int_hitlog = 0;
        }
    }

    if (g->pr_ftl_n) {
        uint64_t n = g->pr_ftl_n;

#    define FTL_MS(x) (unsigned long long) ((x) / 1000000), \
                      (unsigned) ((x) / 100000 % 10)
        gpu_log("RAGE128 GPU: frame: n=%llu period=%llu.%ums fence=%llu.%ums "
                "idle=%llu.%ums latch=%llu.%ums draw=%llu.%ums "
                "max period=%llu.%ums fence=%llu.%ums idle=%llu.%ums "
                "draw=%llu.%ums nolatch=%llu nodraw=%llu\n",
                (unsigned long long) n,
                FTL_MS(g->pr_ftl_period / n), FTL_MS(g->pr_ftl_fence / n),
                FTL_MS(g->pr_ftl_idle / n),
                FTL_MS(g->pr_ftl_latch / (n - g->pr_ftl_nolatch + !(n - g->pr_ftl_nolatch))),
                FTL_MS(g->pr_ftl_draw / (n - g->pr_ftl_nodraw + !(n - g->pr_ftl_nodraw))),
                FTL_MS(g->pr_ftl_max_period), FTL_MS(g->pr_ftl_max_fence),
                FTL_MS(g->pr_ftl_max_idle), FTL_MS(g->pr_ftl_max_draw),
                (unsigned long long) g->pr_ftl_nolatch,
                (unsigned long long) g->pr_ftl_nodraw);
#    undef FTL_MS
        g->pr_ftl_n = g->pr_ftl_period = g->pr_ftl_fence = 0;
        g->pr_ftl_idle = g->pr_ftl_latch = g->pr_ftl_draw = 0;
        g->pr_ftl_nolatch = g->pr_ftl_nodraw = 0;
        g->pr_ftl_max_period = g->pr_ftl_max_fence = 0;
        g->pr_ftl_max_idle = g->pr_ftl_max_draw = 0;
    }

    g->pr_int_t0           = now;
    g->pr_int_gpu_ns       = g->pr_gpu_lad_ns + g->pr_gpu_shade_ns;
    g->pr_int_reuse_ns     = g->pr_wait_reuse_ns;
    g->pr_int_present_ns   = g->pr_wait_drain_cause_ns[GF_PRESENT];
    g->pr_int_2d_ns        = g->pr_wait_drain_cause_ns[GF_2D];
    g->pr_int_submits      = g->st_submits;
    g->pr_int_chained      = g->st_chained;
    g->pr_int_max_inflight = in;
    memset(g->pr_int_2d_tag_ns, 0, sizeof(g->pr_int_2d_tag_ns));
}

static void
gpu_slot_retire(r128_gpu_t *g, gpu_slot_t *sl, int cause)
{
    uint64_t t0, dt;
    VkResult vr;

    if (!atomic_load_explicit(&sl->submitted, memory_order_acquire))
        return;
    t0 = g->prof ? prof_now() : 0;
    pthread_mutex_lock(&sl->fence_mtx);
    /* Recheck under the mutex: another retirer may have waited, reset
       the fence and cleared submitted between the check above and the
       lock, and waiting on a reset fence would never return. The read
       path's fence wait has the same guard. */
    if (!atomic_load_explicit(&sl->submitted, memory_order_acquire)) {
        pthread_mutex_unlock(&sl->fence_mtx);
        return;
    }
    /* Bounded waits, so a lost completion cannot hang this thread
       silently: each 5 s timeout logs the fence's current status and
       waits again. */
    for (unsigned tries = 0;;) {
        vr = vkWaitForFences(g->dev, 1, &sl->fence, VK_TRUE, 5000000000ull);
        if (vr == VK_SUCCESS)
            break;
        if (vr != VK_TIMEOUT)
            fatal("RAGE128 GPU: fence wait failed (%d)\n", vr);
        gpu_log("RAGE128 GPU: STUCK FENCE slot seq=%llu wait#%u status=%d\n",
                (unsigned long long) sl->seq, ++tries,
                vkGetFenceStatus(g->dev, sl->fence));
    }
    dt = g->prof ? prof_now() - t0 : 0;
    if (g->prof) {
        g->pr_wait_ns += dt;
        if (cause == -1)
            g->pr_wait_reuse_ns += dt;
        else if (cause == -4)
            g->pr_wait_depth_ns += dt;
        else if (cause >= 0) {
            g->pr_wait_drain_ns += dt;
            g->pr_wait_drain_cause_ns[cause] += dt;
            if (cause == GF_2D && g->flush_2d_tag < GPU_2D_TAGS) {
                g->pr_2d_tag_ns[g->flush_2d_tag] += dt;
                g->pr_int_2d_tag_ns[g->flush_2d_tag] += dt;
            }
        }
    }
    if (cause == -1)
        g->st_wait_reuse++;
    else if (cause == -4)
        g->st_wait_depth++;
    else if (cause >= 0) {
        g->st_wait_drain[cause]++;
        if (g->prof && cause == GF_2D && g->flush_2d_tag < GPU_2D_TAGS)
            g->st_2d_waits[g->flush_2d_tag]++;
    }

    if (sl->timestamped) {
        uint64_t tq[5];

        if (vkGetQueryPoolResults(g->dev, sl->tqpool, 0, 5, sizeof(tq), tq,
                                  sizeof(tq[0]), VK_QUERY_RESULT_64_BIT)
            == VK_SUCCESS) {
            uint64_t lad_ns   = (uint64_t) ((double) gpu_ts_delta(g, tq[0], tq[1])
                                          * g->tqperiod);
            uint64_t shade_ns = (uint64_t) ((double) gpu_ts_delta(g, tq[1], tq[2])
                                            * g->tqperiod);
            /* mirror copies: 3 -> 0 down, 2 -> 4 back (both ~0 when the
               mirror is unavailable) */
            uint64_t mir_ns = (uint64_t) ((double) (gpu_ts_delta(g, tq[3], tq[0])
                                                    + gpu_ts_delta(g, tq[2], tq[4]))
                                          * g->tqperiod);

            g->pr_gpu_lad_ns += lad_ns;
            g->pr_gpu_shade_ns += shade_ns;
            g->pr_gpu_mirror_ns += mir_ns;
            g->pr_gpu_seg++;
            /* Tripwire: a single segment holding the GPU for over half
               a second is a scheduling defect, not load; log it while
               the workload is still on screen. */
            if (lad_ns + shade_ns > 500000000ull)
                gpu_log("RAGE128 GPU: SLOW SEGMENT seq=%llu lad=%llums "
                        "shade=%llums ndisp=%u rows=%u kmask=%05x\n",
                        (unsigned long long) sl->seq,
                        (unsigned long long) (lad_ns / 1000000),
                        (unsigned long long) (shade_ns / 1000000),
                        sl->pb_ndisp, sl->pb_rows, sl->pb_kmask);
        }
        sl->timestamped = 0;
    }
    vkResetFences(g->dev, 1, &sl->fence);
    /* submitted must clear before the mutex releases: a read-path
       waiter that takes the mutex next would otherwise see submitted=1
       and wait forever on the freshly reset fence */
    sl->stage_ref = 0;
    sl->cz_ref    = 0;
    sl->mir_stg   = 0;
    atomic_store_explicit(&sl->submitted, 0, memory_order_release);
    atomic_fetch_sub_explicit(&g->inflight, 1, memory_order_acq_rel);
    pthread_mutex_unlock(&sl->fence_mtx);
    gpu_prof_interval(g, 0);
}

static void
gpu_drain_all(r128_gpu_t *g, int cause)
{
    for (;;) {
        gpu_slot_t *oldest = NULL;

        for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
            gpu_slot_t *sl = &g->slot[i];

            if (atomic_load_explicit(&sl->submitted, memory_order_acquire)
                && (!oldest || sl->seq < oldest->seq))
                oldest = sl;
        }
        if (!oldest)
            return;
        gpu_slot_retire(g, oldest, cause);
    }
}

/* Retire, oldest first, every submitted slot whose fence has already
   signaled; never waits. Nothing else retires a finished slot until it
   is reused or drained, so its stale ranges would otherwise make later
   range checks wait on fences that are already done. Submit thread
   only (or after a CCE quiesce): gpu_slot_retire changes submit-side
   state. */
static void
gpu_reap(r128_gpu_t *g)
{
    for (;;) {
        gpu_slot_t *oldest = NULL;

        for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
            gpu_slot_t *sl = &g->slot[i];

            if (atomic_load_explicit(&sl->submitted, memory_order_acquire)
                && (!oldest || sl->seq < oldest->seq))
                oldest = sl;
        }
        if (!oldest || vkGetFenceStatus(g->dev, oldest->fence) != VK_SUCCESS)
            return;
        g->st_reaped++;
        gpu_slot_retire(g, oldest, -3);
    }
}

/* Mirror copy pieces of one hazard interval. The kernels wrap every
   VRAM address through vram_mask and touch up to 3 bytes linearly past
   the masked address, while a texture interval carries unmasked card
   addresses: a [lo, hi) that crosses or sits above vram_size maps onto
   the wrapped low bytes, not onto bytes past the top (color/z arrive
   already folded by rng_fold). A piece that reaches the top also
   copies the 4-byte guard word, both ways: the kernel's straddling top
   cell writes into it, and nothing else can write it between a
   segment's copy-down and copy-back (present copies are separate
   submits, ordered after the segment). The caller passes resident
   intervals only: a staged (AGP) target's interval names an arena the
   kernels address directly, and its local alias copied back would
   land on bytes the fence never covered. */
#    define GPU_MIRROR_GUARD 4u

typedef struct gpu_rng_t {
    uint32_t lo, hi;
} gpu_rng_t;

static unsigned
gpu_mirror_pieces(uint32_t vram_mask, uint32_t lo, uint32_t hi,
                  gpu_rng_t out[2])
{
    uint32_t size = vram_mask + 1u;
    uint32_t a, b;

    if (hi <= lo)
        return 0;
    if (hi - lo >= size) {
        out[0].lo = 0;
        out[0].hi = size + GPU_MIRROR_GUARD;
        return 1;
    }
    a = lo & vram_mask;
    b = a + (hi - lo); /* < 2 * size: no overflow below 2 GB of VRAM */
    if (b < size) {
        out[0].lo = a;
        out[0].hi = b;
        return 1;
    }
    out[0].lo = a;
    out[0].hi = size + GPU_MIRROR_GUARD;
    if (b == size)
        return 1;
    out[1].lo = 0;
    out[1].hi = b - size;
    return 2;
}

/* Copy plan for up to GPU_MIRROR_NIN raw intervals: their wrapped pieces
   sorted and merged into disjoint ranges. Transfer writes from separate
   copy commands (or regions of one) are unordered, so two copies landing
   on the same bytes are a write-after-write hazard even when the data
   is identical, and such overlaps are common: a color/z pair packed
   back to back, or a texture union that spans the render target. */
#    define GPU_MIRROR_NIN 3

static unsigned
gpu_mirror_plan(uint32_t vram_mask, const gpu_rng_t *in, unsigned nin,
                gpu_rng_t out[GPU_MIRROR_NIN * 2])
{
    unsigned n = 0, m = 0;

    if (nin > GPU_MIRROR_NIN)
        nin = GPU_MIRROR_NIN;
    for (unsigned i = 0; i < nin; i++)
        n += gpu_mirror_pieces(vram_mask, in[i].lo, in[i].hi, out + n);
    /* insertion sort by lo (n <= 6) */
    for (unsigned i = 1; i < n; i++) {
        gpu_rng_t t = out[i];
        unsigned  j = i;

        while (j > 0 && out[j - 1].lo > t.lo) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = t;
    }
    /* merge overlapping or touching neighbors in place */
    for (unsigned i = 0; i < n; i++) {
        if (m && out[i].lo <= out[m - 1].hi) {
            if (out[i].hi > out[m - 1].hi)
                out[m - 1].hi = out[i].hi;
        } else
            out[m++] = out[i];
    }
    return m;
}

/* Copy raw VRAM intervals between the import and the device-local
   mirror as one disjoint region list. */
static void
gpu_mirror_copy(r128_gpu_t *g, gpu_slot_t *sl, VkBuffer src, VkBuffer dst,
                uint32_t vram_mask, const gpu_rng_t *in, unsigned nin)
{
    gpu_rng_t    p[GPU_MIRROR_NIN * 2];
    VkBufferCopy rc[GPU_MIRROR_NIN * 2];
    unsigned     n = gpu_mirror_plan(vram_mask, in, nin, p);

    if (!n)
        return;
    for (unsigned i = 0; i < n; i++) {
        rc[i].srcOffset = p[i].lo;
        rc[i].dstOffset = p[i].lo;
        rc[i].size      = p[i].hi - p[i].lo;
        g->pr_mirror_bytes += rc[i].size;
    }
    vkCmdCopyBuffer(sl->cb, src, dst, n, rc);
}

/* Record a queued copy's regions: rows x len from src (spitch stride,
   0 = broadcast one row) to addr (pitch stride), 64 regions per
   command. Contiguous row sets collapse to one region. */
static void
gpu_fill_copy_cmd(VkCommandBuffer cb, VkBuffer sb, VkBuffer db,
                  const gpu_fill_t *f)
{
    VkBufferCopy rc[64];

    if (f->rows == 1 || (f->pitch == f->len && f->spitch == f->len)) {
        rc[0].srcOffset = f->src;
        rc[0].dstOffset = f->addr;
        rc[0].size      = (VkDeviceSize) (f->rows - 1) * f->pitch + f->len;
        vkCmdCopyBuffer(cb, sb, db, 1, rc);
        return;
    }
    for (uint32_t done = 0; done < f->rows;) {
        uint32_t n = f->rows - done > 64 ? 64 : f->rows - done;

        for (uint32_t r = 0; r < n; r++) {
            rc[r].srcOffset = (VkDeviceSize) f->src
                + (VkDeviceSize) (done + r) * f->spitch;
            rc[r].dstOffset = (VkDeviceSize) f->addr
                + (VkDeviceSize) (done + r) * f->pitch;
            rc[r].size = f->len;
        }
        vkCmdCopyBuffer(cb, sb, db, n, rc);
        done += n;
    }
}

/* Order a mid fill's copy against everything recorded before it (the
   runs that sample the old bytes, earlier copies on the same bytes)
   and make its bytes visible to what follows. */
static void
gpu_mid_copy_cmd(VkCommandBuffer cb, VkBuffer sb, VkBuffer db,
                 const gpu_fill_t *f, VkAccessFlags dst_access,
                 VkPipelineStageFlags dst_stage)
{
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

    mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL,
                         0, NULL);
    gpu_fill_copy_cmd(cb, sb, db, f);
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, dst_stage, 0, 1,
                         &mb, 0, NULL, 0, NULL);
}

/* Head barrier against an in-flight slot. The source scope must include
   the transfer stage: an in-flight slot may have recorded 2D fills or
   copies, and a fill or copy on the same range, or a dispatch sampling
   it, would otherwise race it across submits. The destination scope
   includes transfer reads because queued VRAM blits read bytes an
   earlier submit may have written. */
static void
gpu_disp_head_barrier(r128_gpu_t *g, gpu_slot_t *sl)
{
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

    mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
        | (g->nfills ? VK_ACCESS_TRANSFER_WRITE_BIT
                   | VK_ACCESS_TRANSFER_READ_BIT
                     : 0);
    vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                             | (g->nfills ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0),
                         0, 1, &mb, 0, NULL, 0, NULL);
}

/* The queued head fills, copies and RMW dispatches (mid fills excluded),
   then one barrier making them visible to the draw dispatches. */
static void
gpu_disp_fills(r128_gpu_t *g, gpu_slot_t *sl, uint32_t nd)
{
    int have_rmw = 0;

    for (uint32_t i = 0; i < g->nfills; i++) {
        const gpu_fill_t *f = &g->fills[i];

        if (f->mid >= 0)
            continue; /* recorded between the runs and at the tail */
        if (f->bar) {
            /* both scopes span TRANSFER and COMPUTE: the earlier
               conflicting op may be a copy/fill or an RMW dispatch,
               and so may this one */
            VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
                | VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
                | VK_ACCESS_TRANSFER_READ_BIT
                | VK_ACCESS_SHADER_WRITE_BIT
                | VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT
                                     | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &mb, 0, NULL, 0, NULL);
        }
        if (f->kind == 4) {
            /* generic RMW: one thread per dst word per row */
            uint32_t pcv[7] = { f->addr, f->pitch, f->len, f->rows,
                                f->src, f->spitch, f->color };
            uint32_t words  = ((f->addr & 3u) + f->len + 3u) >> 2;
            uint64_t n      = (uint64_t) words * f->rows;

            vkCmdBindPipeline(sl->cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                              g->rmw_pipe);
            /* own layout/set: the slot's seg set 0 is re-bound
               after the fill block, before the draw dispatches */
            vkCmdBindDescriptorSets(sl->cb,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    g->rmw_plyt, 0, 1, &g->rmw_dset,
                                    0, NULL);
            vkCmdPushConstants(sl->cb, g->rmw_plyt,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 28, pcv);
            vkCmdDispatch(sl->cb, (uint32_t) ((n + 255u) / 256u), 1, 1);
            have_rmw = 1;
        } else if (f->kind) {
            /* the copy kinds share one region layout; only the buffer
               pair differs. kind 1: staging -> VRAM (spitch 0 =
               broadcast one staged row). kind 2: VRAM -> VRAM (the
               caller ensures source and destination do not overlap).
               kind 3: VRAM -> staging (addr/pitch are ring offsets). */
            VkBuffer sb = f->kind == 2 || f->kind == 3
                ? g->vram_buf
                : g->stage2d.b;
            VkBuffer db = f->kind == 3 ? g->stage2d.b : g->vram_buf;

            gpu_fill_copy_cmd(sl->cb, sb, db, f);
        } else if (f->rows == 1 || f->pitch == f->len)
            vkCmdFillBuffer(sl->cb, g->vram_buf, f->addr,
                            (VkDeviceSize) (f->rows - 1) * f->pitch + f->len,
                            f->color);
        else
            for (uint32_t r = 0; r < f->rows; r++)
                vkCmdFillBuffer(sl->cb, g->vram_buf,
                                (VkDeviceSize) f->addr
                                    + (VkDeviceSize) r * f->pitch,
                                f->len, f->color);
    }
    if (nd) {
        VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
            | (have_rmw ? VK_ACCESS_SHADER_WRITE_BIT : 0);
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
            | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_TRANSFER_BIT | (have_rmw ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0),
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &mb, 0, NULL, 0, NULL);
    }
}

/* Mirror down: gpu_disp_fills and any prior submit's copy-back wrote
   the import; the kernels read color/z (blend, z-test) and sample the
   texture interval from the mirror. +4 on the texture end keeps the
   last texel's linear overrun covered. */
static void
gpu_disp_mirror_down(r128_gpu_t *g, gpu_slot_t *sl, uint32_t vram_mask,
                     uint32_t clo, uint32_t chi, uint32_t zlo, uint32_t zhi,
                     uint32_t qlo, uint32_t qhi)
{
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT
        | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         1, &mb, 0, NULL, 0, NULL);
    {
        gpu_rng_t in[3] = {
            { clo, chi                        },
            { zlo, zhi                        },
            { qlo, qhi > qlo ? qhi + 4u : qhi }
        };

        if (sl->mir_stg & 1u)
            in[0].hi = 0;
        if (sl->mir_stg & 2u)
            in[1].hi = 0;
        gpu_mirror_copy(g, sl, g->vram_buf, g->mirror_buf, vram_mask,
                        in, 3);
    }
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
        | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         1, &mb, 0, NULL, 0, NULL);
}

/* The ladder dispatch over every span, its results fenced for the runs. */
static void
gpu_disp_ladder(r128_gpu_t *g, gpu_slot_t *sl, int use_tq)
{
    uint32_t        pcv[2] = { g->nspans, 0 };
    VkMemoryBarrier mb     = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

    vkCmdBindPipeline(sl->cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->lad_pipe);
    vkCmdPushConstants(sl->cb, g->plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8,
                       pcv);
    /* 32 lanes per span (closed-form ladder), 8 spans per workgroup */
    vkCmdDispatch(sl->cb, (g->nspans + 7u) / 8u, 1, 1);
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         1, &mb, 0, NULL, 0, NULL);
    if (use_tq)
        vkCmdWriteTimestamp(sl->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            sl->tqpool, 1);
}

/* One dispatch per run, mid fills ordered in before the run they
   precede, a barrier only where the row / texture hazard chain says so. */
static void
gpu_disp_runs(r128_gpu_t *g, gpu_slot_t *sl, uint32_t vram_mask,
              const gpu_disp_t *dp, uint32_t nd)
{
    int32_t  chain_lo = INT32_MAX, chain_hi = INT32_MIN;
    uint32_t ch_c0 = 0xffffffffu, ch_c1 = 0;
    uint32_t ch_z0 = 0xffffffffu, ch_z1 = 0;
    uint32_t ch_q0 = 0xffffffffu, ch_q1 = 0;
    uint32_t mid_next = 0; /* next mid fill to order in */

    for (uint32_t i = 0; i < nd; i++) {
        uint32_t   pcv[2] = { vram_mask, dp[i].rows_base };
        VkPipeline p      = dp[i].serial ? g->pipes_serial[dp[i].kernel]
                                         : g->pipes[dp[i].kernel];
        int        fresh  = i == 0;

        /* mid fills ordered before this run: new bytes into the mirror
           after the runs recorded so far sampled the old ones. The
           copy's own barriers fence every earlier run, so the row
           chain restarts here. */
        for (; g->mirror_en && mid_next < g->nfills; mid_next++) {
            const gpu_fill_t *f = &g->fills[mid_next];

            if (f->mid < 0)
                continue;
            if ((uint32_t) f->mid > dp[i].run)
                break;
            gpu_mid_copy_cmd(sl->cb, g->stage2d.b, g->mirror_buf, f,
                             VK_ACCESS_SHADER_READ_BIT
                                 | VK_ACCESS_SHADER_WRITE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            fresh = 1;
        }

        if (fresh) {
            chain_lo = dp[i].pymin;
            chain_hi = dp[i].pymax;
            ch_c0    = dp[i].c0;
            ch_c1    = dp[i].c1;
            ch_z0    = dp[i].z0;
            ch_z1    = dp[i].z1;
            ch_q0    = dp[i].q0;
            ch_q1    = dp[i].q1;
        } else {
            int overlap = dp[i].pymin <= chain_hi && chain_lo <= dp[i].pymax;
            /* byte-exact texture hazard vs the accumulated chain: WAR
               (chain sampled bytes this run stores) and RAW (this run
               samples bytes the chain stored -- excluded segment-wide
               by the accept-time GF_TEX flush, kept so this test is
               sound on its own). Row overlap stays the write-vs-write
               order guard: one segment has one render target (GF_RT)
               and no c/z cross-alias (GF_CZHAZ), so disjoint rows are
               disjoint store bytes. */
            int tex_bar = rng_hit(ch_q0, ch_q1, dp[i].c0, dp[i].c1)
                || rng_hit(ch_q0, ch_q1, dp[i].z0, dp[i].z1)
                || rng_hit(ch_c0, ch_c1, dp[i].q0, dp[i].q1)
                || rng_hit(ch_z0, ch_z1, dp[i].q0, dp[i].q1);

            if (tex_bar || overlap) {
                VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

                mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT
                    | VK_ACCESS_SHADER_WRITE_BIT;
                mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
                    | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                     1, &mb, 0, NULL, 0, NULL);
                chain_lo = dp[i].pymin;
                chain_hi = dp[i].pymax;
                ch_c0    = dp[i].c0;
                ch_c1    = dp[i].c1;
                ch_z0    = dp[i].z0;
                ch_z1    = dp[i].z1;
                ch_q0    = dp[i].q0;
                ch_q1    = dp[i].q1;
                if (g->prof) {
                    g->pr_dbar++;
                    g->pr_dtex += tex_bar;
                }
            } else {
                if (dp[i].pymin < chain_lo)
                    chain_lo = dp[i].pymin;
                if (dp[i].pymax > chain_hi)
                    chain_hi = dp[i].pymax;
                if (dp[i].c0 < ch_c0)
                    ch_c0 = dp[i].c0;
                if (dp[i].c1 > ch_c1)
                    ch_c1 = dp[i].c1;
                if (dp[i].z0 < ch_z0)
                    ch_z0 = dp[i].z0;
                if (dp[i].z1 > ch_z1)
                    ch_z1 = dp[i].z1;
                if (dp[i].q0 < ch_q0)
                    ch_q0 = dp[i].q0;
                if (dp[i].q1 > ch_q1)
                    ch_q1 = dp[i].q1;
                if (g->prof)
                    g->pr_delide++;
            }
        }

        if (g->fold_en && dp[i].tuple >= 0) {
            VkPipeline fp = gpu_fold_get(g, dp[i].kernel, dp[i].tuple);

            /* format-pinned pipeline still compiling: its format-ANY
               partner (same combine fold, runtime format) is faster than
               the uber pipeline */
            if (!fp && g->tuples[dp[i].tuple].any_tid >= 0)
                fp = gpu_fold_get(g, dp[i].kernel,
                                  g->tuples[dp[i].tuple].any_tid);
            if (fp)
                p = fp;
        }
        vkCmdBindPipeline(sl->cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
        vkCmdPushConstants(sl->cb, g->plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8,
                           pcv);
        vkCmdDispatch(sl->cb, dp[i].nrows, 1, 1);
    }
}

/* Mirror back: the kernels' color/z stores return to the import, which
   stays the authoritative copy for the CPU thread, scanout, 2D, the
   present snapshot and the next segment's mirror-down. The destination
   scope covers every later consumer. */
static void
gpu_disp_mirror_back(r128_gpu_t *g, gpu_slot_t *sl, uint32_t vram_mask,
                     uint32_t clo, uint32_t chi, uint32_t zlo, uint32_t zhi)
{
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         1, &mb, 0, NULL, 0, NULL);
    {
        gpu_rng_t in[2] = {
            { clo, chi },
            { zlo, zhi }
        };

        if (sl->mir_stg & 1u)
            in[0].hi = 0;
        if (sl->mir_stg & 2u)
            in[1].hi = 0;
        gpu_mirror_copy(g, sl, g->mirror_buf, g->vram_buf, vram_mask,
                        in, 2);
    }
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT
        | VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_SHADER_READ_BIT
        | VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(sl->cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT
                             | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                             | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &mb, 0, NULL, 0, NULL);
}

/* Mid fills into the import, recorded after the mirror-back so the fill
   lands last; no draw in the segment stores to those bytes, since such
   a draw splits the segment (gpu_mid_range_hit). Same visibility scope
   as the mirror-back: the host reads these bytes after the fence, and
   the next segment mirrors them down. */
static void
gpu_disp_tail_fills(r128_gpu_t *g, gpu_slot_t *sl)
{
    for (uint32_t i = 0; g->nmid && i < g->nfills; i++)
        if (g->fills[i].mid >= 0)
            gpu_mid_copy_cmd(sl->cb, g->stage2d.b, g->vram_buf, &g->fills[i],
                             VK_ACCESS_TRANSFER_READ_BIT
                                 | VK_ACCESS_TRANSFER_WRITE_BIT
                                 | VK_ACCESS_SHADER_READ_BIT
                                 | VK_ACCESS_SHADER_WRITE_BIT
                                 | VK_ACCESS_HOST_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT
                                 | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                 | VK_PIPELINE_STAGE_HOST_BIT);
}

/* Record and submit one slot. A head barrier orders shared VRAM against
   an older segment still in flight; the host does not wait here. */
static void
gpu_dispatch(r128_gpu_t *g, gpu_slot_t *sl, uint32_t vram_mask,
             const gpu_disp_t *dp, uint32_t nd,
             uint32_t clo, uint32_t chi, uint32_t zlo, uint32_t zhi,
             uint32_t qlo, uint32_t qhi, uint32_t rlo, uint32_t rhi,
             uint32_t flo, uint32_t fhi)
{
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkSubmitInfo             si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    uint64_t                 t0;
    int                      use_tq = g->prof && g->tqbits
        && sl->tqpool != VK_NULL_HANDLE;
    unsigned prior = gpu_inflight_count(g);

    gpu_prof_interval(g, 0);
    t0 = g->prof ? prof_now() : 0;
    /* Held across the recording, not just the submit: the CCE thread and
       the CPU thread both reach a flush, and a second begin on the
       slot's command buffer while the first is still recording leaves it
       out of the recording state; the driver then rejects every later
       command and the submit fails. */
    pthread_mutex_lock(&g->queue_mtx);
    vkBeginCommandBuffer(sl->cb, &bi);
    if (use_tq)
        vkCmdResetQueryPool(sl->cb, sl->tqpool, 0, 5);
    if (prior)
        gpu_disp_head_barrier(g, sl);
    if (g->nfills > g->nmid)
        gpu_disp_fills(g, sl, nd);
    /* Stamps 3 and 0 bracket the mirror-down at the bottom-of-pipe
       stage: a top-of-pipe stamp is taken when the command is reached,
       not when the copies (or the fills before them) have finished, and
       would charge their tail to the ladder interval 0 -> 1. */
    if (use_tq)
        vkCmdWriteTimestamp(sl->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            sl->tqpool, 3);
    if (g->mirror_en && nd)
        gpu_disp_mirror_down(g, sl, vram_mask, clo, chi, zlo, zhi, qlo, qhi);
    if (use_tq)
        vkCmdWriteTimestamp(sl->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            sl->tqpool, 0);
    vkCmdBindDescriptorSets(sl->cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->plyt,
                            0, 1, &sl->dset, 0, NULL);
    gpu_disp_ladder(g, sl, use_tq);
    gpu_disp_runs(g, sl, vram_mask, dp, nd);
    if (use_tq)
        vkCmdWriteTimestamp(sl->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            sl->tqpool, 2);
    if (g->mirror_en && nd)
        gpu_disp_mirror_back(g, sl, vram_mask, clo, chi, zlo, zhi);
    gpu_disp_tail_fills(g, sl);
    g->nfills = 0;
    g->nmid   = 0;
    if (use_tq)
        vkCmdWriteTimestamp(sl->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            sl->tqpool, 4);
    vkEndCommandBuffer(sl->cb);
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &sl->cb;
    if (vkQueueSubmit(g->queue, 1, &si, sl->fence) != VK_SUCCESS)
        fatal("RAGE128 GPU: queue submit failed\n");
    pthread_mutex_unlock(&g->queue_mtx);
    if (g->prof)
        g->pr_record_ns += prof_now() - t0;

    sl->c_lo = clo;
    sl->c_hi = chi;
    sl->z_lo = zlo;
    sl->z_hi = zhi;
    sl->q_lo = qlo;
    sl->q_hi = qhi;
    sl->r_lo = rlo;
    sl->r_hi = rhi;
    sl->f_lo = flo;
    sl->f_hi = fhi;
    /* the exact list travels with its hull; both land before the
       submitted release-store below, which is what a reader's acquire
       of submitted orders against */
    sl->ntexq    = g->ntexq;
    sl->texq_ovf = g->texq_ovf;
    memcpy(sl->texq_lo, g->texq_lo, g->ntexq * sizeof(uint32_t));
    memcpy(sl->texq_hi, g->texq_hi, g->ntexq * sizeof(uint32_t));
    sl->nfq    = g->nfq;
    sl->fq_ovf = g->fq_ovf;
    memcpy(sl->fq_lo, g->fq_lo, g->nfq * sizeof(uint32_t));
    memcpy(sl->fq_hi, g->fq_hi, g->nfq * sizeof(uint32_t));
    sl->seq = atomic_fetch_add_explicit(&g->submit_seq, 1,
                                        memory_order_relaxed)
        + 1;
    sl->pb_ndisp = nd;
    sl->pb_rows  = 0;
    sl->pb_kmask = 0;
    for (uint32_t k = 0; k < nd; k++) {
        sl->pb_rows += dp[k].nrows;
        sl->pb_kmask |= 1u << dp[k].kernel;
    }
    sl->timestamped = use_tq;
    atomic_store_explicit(&sl->fence_seen, 0, memory_order_relaxed);
    atomic_fetch_add_explicit(&g->inflight, 1, memory_order_acq_rel);
    atomic_store_explicit(&sl->submitted, 1, memory_order_release);
    g->st_submits++;
    g->st_chained += prior != 0;
    {
        unsigned in = gpu_inflight_count(g);

        if (in > g->st_max_inflight)
            g->st_max_inflight = in;
        if (in > g->pr_int_max_inflight)
            g->pr_int_max_inflight = in;
    }
    gpu_prof_interval(g, 0);
}

/* The 3D half of the accumulation; queued fills and the hazard ranges
   are left to the caller. */
static void
gpu_accum_drop_3d(r128_gpu_t *g)
{
    g->nspans          = 0;
    g->ntris           = 0;
    g->seg_px          = 0;
    g->nruns           = 0;
    g->ntexq           = 0;
    g->texq_ovf        = 0;
    g->texq_memo_valid = 0;
    g->cur_tri         = -1;
    g->rt_valid        = 0;
    g->rt_z_valid      = 0;
    g->v_tri_n         = 0;
    g->v_state_n       = 0;
    g->seg_staged      = 0;
    g->seg_czstaged    = 0;
    g->npal            = 0;
    g->pal_up_gen      = 0;
    /* kept mid fills now precede every run the segment will gain */
    for (uint32_t i = 0; g->nmid && i < g->nfills; i++)
        if (g->fills[i].mid > 0)
            g->fills[i].mid = 0;
    g->run_break = 0;
}

static void
gpu_accum_reset(r128_gpu_t *g)
{
    gpu_accum_drop_3d(g);
    g->nmid = 0;
    rng_reset(g);
    atomic_store_explicit(&g->pending, 0, memory_order_release);
}

/* SOFT_RESET_GUI: the pending segment is this lane's deferred batch, so
   its undispatched draws are dropped unrendered, as rage128_raster_abandon
   drops the CPU lane's. Queued 2D fills stay: they precede the spans and
   were accepted as complete ops. Their segment keeps its hazard ranges
   (an over-wide stale range only costs a fence). Executor parked, so no
   submit is in flight; the flush lock orders this against a flush the
   other thread may still be inside. */
void
rage128_gpu_abandon(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !atomic_load_explicit(&g->pending, memory_order_acquire))
        return;
    pthread_mutex_lock(&g->flush_mtx);
    if (g->nfills)
        gpu_accum_drop_3d(g);
    else
        gpu_accum_reset(g);
    pthread_mutex_unlock(&g->flush_mtx);
}

static int
gpu_cause_drains(const r128_gpu_t *g, int cause)
{
    return !g->async || g->verify || cause == GF_2D
        || cause == GF_CPU || cause == GF_OTHERDRAW;
}

static void
gpu_flush_body(rage128_t *dev, int cause)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    gpu_slot_t *sl;
    uint32_t   *rows, *order, *count;
    uint32_t    clo, chi, zlo, zhi, qlo, qhi, rlo, rhi, flo, fhi;
    gpu_disp_t  disp[GPU_RUN_CAP];
    uint32_t    ndisp = 0, rows_used = 0, nbat = 0;
    uint64_t    prof_t = 0;
    int         drain;

    if (!g)
        return;
    drain = gpu_cause_drains(g, cause);
    if (!g->nspans && !g->nfills) {
        if (drain)
            gpu_drain_all(g, cause);
        if (atomic_load_explicit(&g->pending, memory_order_acquire))
            gpu_accum_reset(g);
        return;
    }

    sl    = &g->slot[g->build_slot];
    count = g->cnt;
    /* Pacer 3D gate: GPU-lane render work counts (the CPU-raster
       parallel path sets this in raster_flush; nthreads is forced to 1
       in GPU mode so that site never fires). */
    if (g->ntris)
        atomic_store(&dev->pace_3d_seen, 1);
    clo = atomic_load(&g->c_lo);
    chi = atomic_load(&g->c_hi);
    zlo = atomic_load(&g->z_lo);
    zhi = atomic_load(&g->z_hi);
    qlo = atomic_load(&g->q_lo);
    qhi = atomic_load(&g->q_hi);
    rlo = atomic_load(&g->r_lo);
    rhi = atomic_load(&g->r_hi);
    flo = atomic_load(&g->f_lo);
    fhi = atomic_load(&g->f_hi);

    if (g->prof) {
        const seg_span_t *sp = (const seg_span_t *) sl->b_spans.map;

        for (uint32_t i = 0; i < g->nspans; i++)
            g->pr_px += (uint64_t) (sp[i].x1 - sp[i].x0 + 1);
        prof_t = prof_now();
    }

    /* per-run counting sort of spans by row, then a batch build per
       row: rows[] gets (batch base, batch count) pairs for the run's
       active rows and slots[] the packed lane-group assignments. */
    rows  = (uint32_t *) sl->b_rows.map;
    order = (uint32_t *) sl->b_order.map;
    for (uint32_t r = 0; r < g->nruns; r++) {
        uint32_t s0    = g->runs[r].span0;
        uint32_t s1    = (r + 1 < g->nruns) ? g->runs[r + 1].span0 : g->nspans;
        int      pymin = g->runs[r].pymin;
        int      span  = g->runs[r].pymax - pymin + 1;
        uint32_t kn    = g->runs[r].kernel;
        uint32_t nrows = 0, off = s0, maxb = 0, maxlv = 0, proj_wg = 0;
        uint64_t run_px = 0;

        if (s1 <= s0)
            continue; /* tri record opened a run but capped out of spans */
        if (g->runs[r].serial) {
            /* serial run: no sort, no batching -- the kernel walks the
               contiguous span range in capture order on one workgroup.
               The rows pair is repurposed as (first span, span count). */
            rows[rows_used * 2u]      = s0;
            rows[rows_used * 2u + 1u] = s1 - s0;
            disp[ndisp].rows_base     = rows_used;
            disp[ndisp].nrows         = 1;
            disp[ndisp].kernel        = g->runs[r].kernel;
            disp[ndisp].tuple         = -1;
            disp[ndisp].serial        = 1;
            disp[ndisp].pymin         = g->runs[r].pymin;
            disp[ndisp].pymax         = g->runs[r].pymax;
            disp[ndisp].run           = r;
            disp[ndisp].c0            = g->runs[r].c0;
            disp[ndisp].c1            = g->runs[r].c1;
            disp[ndisp].z0            = g->runs[r].z0;
            disp[ndisp].z1            = g->runs[r].z1;
            disp[ndisp].q0            = g->runs[r].q0;
            disp[ndisp].q1            = g->runs[r].q1;
            ndisp++;
            rows_used += 1;
            if (g->prof) {
                const seg_span_t *sp = (const seg_span_t *) sl->b_spans.map;

                g->pr_wg++;
                g->pr_runs_k[kn]++;
                g->pr_wg_k[kn]++;
                for (uint32_t i = s0; i < s1; i++)
                    g->pr_px_k[kn] += (uint64_t) (sp[i].x1 - sp[i].x0 + 1);
            }
            continue;
        }
        memset(count, 0, (size_t) span * sizeof(uint32_t));
        for (uint32_t i = s0; i < s1; i++)
            count[g->py_of[i] - pymin]++;
        for (int row = 0; row < span; row++) {
            uint32_t c = count[row];

            g->beg[row] = off;
            count[row]  = off; /* becomes the row's scatter cursor */
            off += c;
        }
        for (uint32_t i = s0; i < s1; i++)
            g->ord[count[g->py_of[i] - pymin]++] = i;
        for (int row = 0; row < span; row++) {
            uint32_t c = count[row] - g->beg[row];
            uint32_t nb;

            if (!c)
                continue;
            nb                                  = gpu_batch_row(g, order, nbat, &g->ord[g->beg[row]], c);
            rows[(rows_used + nrows) * 2u]      = nbat;
            rows[(rows_used + nrows) * 2u + 1u] = nb;
            nbat += nb;
            nrows++;
            if (g->prof) {
                const seg_span_t *sp  = (const seg_span_t *) sl->b_spans.map;
                uint32_t          ctl = 0, rm = 0;
                uint64_t          row_px   = 0;
                uint64_t          stat_t   = prof_now();
                int               eligible = 1;

                for (uint32_t j = g->beg[row]; j < count[row]; j++) {
                    uint32_t si = g->ord[j];

                    row_px += (uint64_t) (sp[si].x1 - sp[si].x0 + 1);
                    if (!g->sctl_of[si])
                        eligible = 0;
                    else if (!ctl) {
                        ctl = g->sctl_of[si];
                        rm  = g->srm_of[si];
                    } else if (ctl != g->sctl_of[si] || rm != g->srm_of[si]) {
                        eligible = 0;
                    }
                }
                g->pr_bat += nb;
                g->pr_rows_k[kn]++;
                g->pr_bat_k[kn] += nb;
                run_px += row_px;
                if (g->pr_row_nlv > maxlv)
                    maxlv = g->pr_row_nlv;
                if (eligible) {
                    g->pr_erows_k[kn]++;
                    g->pr_ebat_k[kn] += nb;
                    g->pr_epx_k[kn] += row_px;
                    proj_wg += nb;
                } else {
                    proj_wg++;
                }
                g->pr_sort_stat_ns += prof_now() - stat_t;
            }
            if (nb > maxb)
                maxb = nb;
        }
        if (g->prof) {
            const seg_span_t *sp     = (const seg_span_t *) sl->b_spans.map;
            uint64_t          stat_t = prof_now();

            g->pr_wg += nrows;
            g->pr_crit += maxb;
            g->pr_crit_lvl += maxlv;
            g->pr_runs_k[kn]++;
            g->pr_wg_k[kn] += nrows;
            g->pr_pwg_k[kn] += proj_wg;
            g->pr_wgh_cur[kn][prof_wgh_bin(nrows)] += run_px;
            g->pr_wgh_proj[kn][prof_wgh_bin(proj_wg)] += run_px;
            for (uint32_t i = s0; i < s1; i++) {
                uint64_t w = (uint64_t) (sp[i].x1 - sp[i].x0 + 1);

                g->pr_px_k[kn] += w;
                if (!r128_gpu_variants[kn].z)
                    g->pr_px_zless += w;
            }
            g->pr_sort_stat_ns += prof_now() - stat_t;
        }
        disp[ndisp].rows_base = rows_used;
        disp[ndisp].nrows     = nrows;
        disp[ndisp].kernel    = g->runs[r].kernel;
        disp[ndisp].tuple     = g->runs[r].tuple;
        disp[ndisp].serial    = 0;
        disp[ndisp].pymin     = g->runs[r].pymin;
        disp[ndisp].pymax     = g->runs[r].pymax;
        disp[ndisp].run       = r;
        disp[ndisp].c0        = g->runs[r].c0;
        disp[ndisp].c1        = g->runs[r].c1;
        disp[ndisp].z0        = g->runs[r].z0;
        disp[ndisp].z1        = g->runs[r].z1;
        disp[ndisp].q0        = g->runs[r].q0;
        disp[ndisp].q1        = g->runs[r].q1;
        ndisp++;
        rows_used += nrows;
    }
    if (g->prof)
        g->pr_sort_ns += prof_now() - prof_t;

    sl->stage_ref = g->seg_staged;
    sl->cz_ref    = g->seg_czstaged;
    sl->mir_stg   = (atomic_load(&g->c_stg) ? 1u : 0u)
        | (atomic_load(&g->z_stg) ? 2u : 0u);
    if (g->verify)
        gpu_verify_segment(dev, g, disp, ndisp, clo, chi, zlo, zhi,
                           qlo, qhi, cause);
    else
        gpu_dispatch(g, sl, dev->vram_mask, disp, ndisp,
                     clo, chi, zlo, zhi, qlo, qhi, rlo, rhi, flo, fhi);

    g->st_segments++;
    g->st_runs += ndisp;
    g->st_nruns += g->nruns; /* runs, not dispatches: like-for-like vs GPU_RUN_CAP */
    g->st_flush[cause]++;
    gpu_accum_reset(g);

    if (!g->verify) {
        uint32_t next = (g->build_slot + 1u) % GPU_RING_SLOTS;

        if (drain) {
            gpu_drain_all(g, cause);
        } else {
            gpu_reap(g);
            if (atomic_load_explicit(&g->slot[next].submitted,
                                     memory_order_acquire))
                gpu_slot_retire(g, &g->slot[next], -1);
            /* Depth cap: keep at most depth_cap slots submitted. The
               scan thread's read fence waits until the NEWEST
               conflicting slot completes -- the whole queue ahead of it
               -- so queue depth, not fence count, sets the guest's
               stall. Retiring oldest-first here moves that wait onto
               this thread, where it doubles as driver backpressure
               proportional to real GPU latency, while the GPU stays fed
               with cap segments (the per-flip sleep starves it
               instead). At cap = GPU_RING_SLOTS this loop never runs:
               the next-retire above already bounds depth at ring size. */
            {
                unsigned cap = atomic_load_explicit(&g->depth_cap,
                                                    memory_order_relaxed);

                while (gpu_inflight_count(g) > cap) {
                    gpu_slot_t *oldest = NULL;

                    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
                        gpu_slot_t *cand = &g->slot[i];

                        if (atomic_load_explicit(&cand->submitted,
                                                 memory_order_acquire)
                            && (!oldest || cand->seq < oldest->seq))
                            oldest = cand;
                    }
                    if (!oldest)
                        break;
                    gpu_slot_retire(g, oldest, -4);
                }
            }
        }
        g->build_slot = next;
    }
}

/* One flush at a time per backend. gpu_flush_body walks and rewrites
   state the whole backend shares, and both the CCE thread and the CPU
   thread reach it; without this two builds interleave and one of them
   dispatches rows whose batches the other has already consumed, so the
   segment runs and stores nothing. */
static void
gpu_flush_cause(rage128_t *dev, int cause)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g)
        return;
    pthread_mutex_lock(&g->flush_mtx);
    gpu_flush_body(dev, cause);
    pthread_mutex_unlock(&g->flush_mtx);
}

void
rage128_gpu_flush(rage128_t *dev, uint32_t tag)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g)
        return;
    if (g->prof) {
        if (tag >= GPU_2D_TAGS)
            tag = 0;
        /* left set through the op: the range-gated drain (if any) happens
           later, at the surface map inside the primitive, and must still
           attribute its wait to this op in the 2d table */
        g->flush_2d_tag = tag;
        g->st_2d_requests[tag]++;
    }
    /* Order point. In async mode ordering is enforced later, per byte
       range: queued fills and copies split the segment only on a real
       byte hazard (gpu_pending_draw_hit), and 2D ops done on the CPU
       fence at the surface map (rage128_gpu_2d_barrier). So pending 3D
       work is not submitted at every 2D op, which would cut segments
       too small to keep the ring busy. Verify and sync modes flush at
       every op: verify's per-draw replay depends on it. */
    if (!g->async || g->verify || g->verify2d)
        gpu_flush_cause(dev, GF_2DQ);
}

/* ------------------------------------------------------------------ */
/* Verify mode: dispatch, snapshot, replay through the interpreter     */
/* (whose output is the one kept), compare.                            */
/* ------------------------------------------------------------------ */

static void
gpu_verify_segment(rage128_t *dev, r128_gpu_t *g,
                   const gpu_disp_t *dp, uint32_t nd,
                   uint32_t clo, uint32_t chi, uint32_t zlo, uint32_t zhi,
                   uint32_t qlo, uint32_t qhi, int cause)
{
    gpu_slot_t *sl   = &g->slot[g->build_slot];
    uint32_t    clen = (chi > clo) ? chi - clo : 0;
    uint32_t    zlen = (zhi > zlo) ? zhi - zlo : 0;
    /* staged c/z arenas: the GPU wrote (and the replay below RMWs) the
       arena bytes, not vram -- snapshot/restore/compare their active
       extents alongside the vram intervals */
    uint32_t cslen = (g->seg_czstaged && dev->c_stage.active)
        ? dev->c_stage.len
        : 0;
    uint32_t zslen = (g->seg_czstaged && dev->z_stage.active)
        ? dev->z_stage.len
        : 0;
    uint32_t need;
    uint8_t *pre, *snap;

    if (clo + clen > dev->vram_size)
        clen = (clo < (uint32_t) dev->vram_size) ? dev->vram_size - clo : 0;
    if (zlo + zlen > dev->vram_size)
        zlen = (zlo < (uint32_t) dev->vram_size) ? dev->vram_size - zlo : 0;
    need = 2u * (clen + zlen + cslen + zslen);
    if (need > g->v_save_cap) {
        free(g->v_save);
        g->v_save     = (uint8_t *) malloc(need);
        g->v_save_cap = g->v_save ? need : 0;
        if (!g->v_save)
            return;
    }
    pre  = g->v_save;
    snap = g->v_save + clen + zlen + cslen + zslen;

    memcpy(pre, dev->svga.vram + clo, clen);
    memcpy(pre + clen, dev->svga.vram + zlo, zlen);
    memcpy(pre + clen + zlen, dev->c_stage.arena, cslen);
    memcpy(pre + clen + zlen + cslen, dev->z_stage.arena, zslen);

    /* verify mode refuses all queued 2D ops, so the transfer-read and
       queued-2D-write intervals are always empty here */
    gpu_dispatch(g, sl, dev->vram_mask, dp, nd,
                 clo, chi, zlo, zhi, qlo, qhi, 0xffffffffu, 0,
                 0xffffffffu, 0);
    gpu_slot_retire(g, sl, cause);

    memcpy(snap, dev->svga.vram + clo, clen);
    memcpy(snap + clen, dev->svga.vram + zlo, zlen);
    memcpy(snap + clen + zlen, dev->c_stage.arena, cslen);
    memcpy(snap + clen + zlen + cslen, dev->z_stage.arena, zslen);
    memcpy(dev->svga.vram + clo, pre, clen);
    memcpy(dev->svga.vram + zlo, pre + clen, zlen);
    memcpy(dev->c_stage.arena, pre + clen + zlen, cslen);
    memcpy(dev->z_stage.arena, pre + clen + zlen + cslen, zslen);

    /* interpreter replay (jfn NULL: pure interpreter, independent of
       both the JIT and this backend) */
    for (uint32_t i = 0; i < g->v_tri_n; i++) {
        if (g->v_tris[i].line)
            rage128_3d_line(dev, &g->v_states[g->v_tris[i].state], 0, 0,
                            &g->v_tris[i].v[0], &g->v_tris[i].v[1], NULL);
        else
            rage128_3d_tri(dev, &g->v_states[g->v_tris[i].state], 0, 0,
                           &g->v_tris[i].v[0], &g->v_tris[i].v[1],
                           &g->v_tris[i].v[2], NULL);
    }

    g->st_verify_seg++;
    {
        uint32_t bad = 0;

        for (uint32_t i = 0; i < clen; i++)
            if (dev->svga.vram[clo + i] != snap[i]) {
                if (bad < 4)
                    gpu_log("RAGE128 GPU verify MISMATCH c+0x%x: gpu=%02x interp=%02x\n",
                            clo + i, snap[i], dev->svga.vram[clo + i]);
                bad++;
            }
        for (uint32_t i = 0; i < zlen; i++)
            if (dev->svga.vram[zlo + i] != snap[clen + i]) {
                if (bad < 4)
                    gpu_log("RAGE128 GPU verify MISMATCH z+0x%x: gpu=%02x interp=%02x\n",
                            zlo + i, snap[clen + i], dev->svga.vram[zlo + i]);
                bad++;
            }
        for (uint32_t i = 0; i < cslen; i++)
            if (dev->c_stage.arena[i] != snap[clen + zlen + i]) {
                if (bad < 4)
                    gpu_log("RAGE128 GPU verify MISMATCH cs+0x%x: gpu=%02x interp=%02x\n",
                            i, snap[clen + zlen + i], dev->c_stage.arena[i]);
                bad++;
            }
        for (uint32_t i = 0; i < zslen; i++)
            if (dev->z_stage.arena[i] != snap[clen + zlen + cslen + i]) {
                if (bad < 4)
                    gpu_log("RAGE128 GPU verify MISMATCH zs+0x%x: gpu=%02x interp=%02x\n",
                            i, snap[clen + zlen + cslen + i],
                            dev->z_stage.arena[i]);
                bad++;
            }
        if (bad) {
            g->st_verify_bad++;
            gpu_log("RAGE128 GPU verify: segment %llu: %u mismatched bytes (spans=%u tris=%u)\n",
                    (unsigned long long) g->st_segments, bad, g->nspans, g->ntris);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Span capture. Runs as the jfn (span callback) of rage128_3d_tri, so */
/* triangle setup, scissoring and winding normalization are the        */
/* interpreter's own, and rage128_3d_tri resolves every mip slot up    */
/* front, as it does for an inline-textured JIT block.                 */
/* ------------------------------------------------------------------ */

/* Base word of one mip slot in the tri record. A resident level gets its
   VRAM byte offset; the kernel adds the texel offset and then applies
   the slot mask. A staged level (texbase points into the texture stage
   arena, not svga.vram) gets its arena byte offset with bit 31 set; the
   kernel then masks the texel offset first and adds it to the base,
   reading the arena binding. A staged slot also tags the segment
   (seg_staged), so the arena is not reused while the segment runs. */
static inline uint32_t
gpu_slot_base_pack(r128_gpu_t *g, const rage128_t *dev,
                   const struct r3d_slot_desc_t *sl)
{
    if (sl->texbase == dev->svga.vram)
        return sl->base;
    g->seg_staged = 1;
    return (uint32_t) (sl->texbase - dev->tex_stage.arena) | 0x80000000u;
}

static int
gpu_clip_edge(int64_t e, int64_t dx, int32_t *lo, int32_t *hi)
{
    if (dx > 0 && e < 0) {
        uint64_t n = (uint64_t) (-(e + 1)) + 1u;
        uint64_t d = (uint64_t) dx;
        uint64_t q = n / d + (n % d != 0);

        if (q > (uint64_t) *hi)
            return 0;
        if ((int32_t) q > *lo)
            *lo = (int32_t) q;
    } else if (dx < 0) {
        uint64_t d, q;

        if (e < 0)
            return 0;
        d = (uint64_t) (-(dx + 1)) + 1u;
        q = (uint64_t) e / d;
        if (q < (uint64_t) *hi)
            *hi = (int32_t) q;
    } else if (e < 0)
        return 0;
    return *lo <= *hi;
}

static inline int
gpu_edge32_span(int64_t e, int64_t dx, int32_t n)
{
    int64_t last = e + (int64_t) (n - 1) * dx;

    return e >= INT32_MIN && e <= INT32_MAX
        && last >= INT32_MIN && last <= INT32_MAX;
}

/* First span of a triangle: write its seg_tri_t record into the build
   slot from the JIT tri record, the draw state and the texture context. */
static seg_tri_t *
capture_tri_record(r128_gpu_t *g, rage128_t *dev, const r3d_texctx_t *tc,
                   const r128_jit_tri_t *tri)
{
    const rage128_draw_state_t *d = &tc->rs->d;
    seg_tri_t                  *tt;

    tt = &((seg_tri_t *) g->slot[g->build_slot].b_tris.map)[g->ntris];
    memset(tt, 0, sizeof(*tt));
    tt->e0dxi = tri->e0dxi;
    tt->e1dxi = tri->e1dxi;
    tt->e2dxi = tri->e2dxi;
    memcpy(&tt->dZdx, &tri->dZdx, 8);
    tt->invs     = tri->invs;
    tt->lod_bias = d->lod_bias;
    tt->texw0    = d->texw0;
    tt->texh0    = d->texh0;
    tt->texw1    = d->texw1;
    tt->texh1    = d->texh1;
    /* With z off (a stencil-only draw, for one) the cell is still
       visited, so the depth compare is set to always pass (7) and the
       depth field is not written. */
    tt->zfn    = d->z_en ? d->zfn : 7;
    tt->z_wr   = (d->z_en && d->z_wr) ? 1 : 0;
    tt->zbpp   = (uint32_t) d->zbpp;
    tt->zshift = (uint32_t) d->zshift;
    if (d->sten_on) {
        tt->sten_ctl = 1u | (d->sfn << 4) | (d->sfail_op << 8)
            | (d->zpass_op << 12) | (d->zfail_op << 16);
        tt->sten_rm = d->sref | (d->svmask << 8) | (d->swmask << 16);
    }
    tt->dst_dt  = d->dst_dt;
    tt->wmask   = d->wmask;
    tt->dither  = d->dither ? 1 : 0;
    tt->persp   = (d->do_persp ? 1u : 0u)
        | ((d->sec_en && !d->tex_en) ? 2u : 0u);
    tt->spec_en = d->spec_en ? 1 : 0;
    memcpy(tt->spa, tri->spa, sizeof(tt->spa));
    memcpy(tt->spb, tri->spb, sizeof(tt->spb));
    memcpy(tt->spc, tri->spc, sizeof(tt->spc));
    tt->fog_en  = d->fog_en ? 1 : 0;
    tt->ftab_en = (d->fog_en && d->fog_table_en) ? 1 : 0;
    if (tt->ftab_en)
        memcpy(tt->fog_table, tc->rs->t3d.fog_table, 256);
    /* CI4/CI8: copy the draw's palette into the slot's palette arena,
       from the rs copy so a verify replay reads the same entries; reuse
       the last copy while tex_pal_gen is unchanged. gpu_submit_prim
       flushed first if the arena was full, so npal cannot overflow. */
    if (gpu_draw_ci(d)) {
        if (g->npal == 0 || g->pal_up_gen != dev->tex_pal_gen) {
            memcpy((uint32_t *) g->slot[g->build_slot].b_pal.map
                       + (size_t) g->npal * 256,
                   tc->rs->t3d.tex_palette, 256 * sizeof(uint32_t));
            g->pal_up_gen = dev->tex_pal_gen;
            g->npal++;
        }
        tt->pal_base = (g->npal - 1) * 256;
    }
    tt->fga  = tri->fog[0];
    tt->fgb  = tri->fog[1];
    tt->fgc  = tri->fog[2];
    tt->fogr = d->fogr;
    tt->fogg = d->fogg;
    tt->fogb = d->fogb;
    /* the color key values and masks are set up only for a textured
       draw; for any other draw the memset above left ck_ctl 0 (keying
       off) */
    if (d->tex_en && d->need_ck) {
        tt->ck_ctl = (d->ck3d_on ? 1u : 0u) | (d->ckc_on ? 2u : 0u)
            | (d->ckfn == 3 ? 4u : 0u);
        tt->ck3d_clr = d->ck3d_clr;
        tt->ck3d_msk = d->ck3d_msk;
        tt->ckc_clr  = d->ckc_clr;
        tt->ckc_msk  = d->ckc_msk;
    }
    tt->atest_en  = d->atest_en ? 1 : 0;
    tt->atest_fn  = d->atest_fn;
    tt->atest_ref = d->atest_ref;
    tt->bsrc      = d->bsrc;
    tt->bdst      = d->bdst;
    tt->bfcn      = d->bfcn;
    tt->stip_en   = d->stip_en ? 1 : 0;
    if (tt->stip_en)
        memcpy(tt->stipple, tc->rs->stipple, sizeof(tt->stipple));
    tt->aux_cntl  = d->aux_on ? d->aux_cntl : 0;
    memcpy(tt->aux_x0, d->aux_x0, sizeof(tt->aux_x0));
    memcpy(tt->aux_x1, d->aux_x1, sizeof(tt->aux_x1));
    memcpy(tt->aux_y0, d->aux_y0, sizeof(tt->aux_y0));
    memcpy(tt->aux_y1, d->aux_y1, sizeof(tt->aux_y1));
    memcpy(tt->cc, d->cc, sizeof(tt->cc));
    memcpy(tt->vca, tri->vca, sizeof(tt->vca));
    memcpy(tt->vcb, tri->vcb, sizeof(tt->vcb));
    memcpy(tt->vcc, tri->vcc, sizeof(tt->vcc));
    tt->sta       = tc->sta;
    tt->stb       = tc->stb;
    tt->stc       = tc->stc;
    tt->tta       = tc->tta;
    tt->ttb       = tc->ttb;
    tt->ttc       = tc->ttc;
    tt->s2a       = tc->s2a;
    tt->s2b       = tc->s2b;
    tt->s2c       = tc->s2c;
    tt->t2a       = tc->t2a;
    tt->t2b       = tc->t2b;
    tt->t2c       = tc->t2c;
    tt->arhw      = tc->arhw;
    tt->brhw      = tc->brhw;
    tt->crhw      = tc->crhw;
    tt->dSdx      = tc->dSdx;
    tt->dSdy      = tc->dSdy;
    tt->dTdx      = tc->dTdx;
    tt->dTdy      = tc->dTdy;
    tt->dWdx      = tc->dWdx;
    tt->dWdy      = tc->dWdy;
    tt->dS2dx     = tc->dS2dx;
    tt->dS2dy     = tc->dS2dy;
    tt->dT2dx     = tc->dT2dx;
    tt->dT2dy     = tc->dT2dy;
    tt->a2rhw     = tc->a2rhw;
    tt->b2rhw     = tc->b2rhw;
    tt->c2rhw     = tc->c2rhw;
    tt->dW2dx     = tc->dW2dx;
    tt->dW2dy     = tc->dW2dy;
    tt->persp2    = (d->do_persp ^ d->sec_persp_diff) ? 1 : 0;
    tt->sel_w     = d->sel_w ? 1 : 0;
    tt->sec_sel   = d->sec_sel ? 1 : 0;
    tt->need_lod2 = d->need_lod2 ? 1 : 0;
    tt->top0      = d->sh[0].top;
    tt->top1      = d->sh[1].top;
    for (int st = 0; st < 2; st++) {
        const r3d_stage_hdr_t *h = &d->sh[st];

        tt->st_cfg[st * 8 + 0]   = h->dt | (h->aone ? 0x10u : 0u); /* 0x16 = opaque 8888 */
        tt->st_cfg[st * 8 + 1]   = h->s3tc;
        tt->st_cfg[st * 8 + 2]   = h->clamp_s;
        tt->st_cfg[st * 8 + 3]   = h->clamp_t;
        tt->st_cfg[st * 8 + 4]   = h->border;
        tt->st_cfg[st * 8 + 5]   = h->minb;
        tt->st_cfg[st * 8 + 6]   = h->mag;
        tt->st_cfg[st * 8 + 7]   = h->mipdis ? 1 : 0;
        tt->comb_cfg[st * 8 + 0] = d->comb[st].comb;
        tt->comb_cfg[st * 8 + 1] = d->comb[st].fmsb;
        tt->comb_cfg[st * 8 + 2] = d->comb[st].cfac;
        tt->comb_cfg[st * 8 + 3] = d->comb[st].ifac;
        tt->comb_cfg[st * 8 + 4] = d->comb[st].comba;
        tt->comb_cfg[st * 8 + 5] = d->comb[st].afac;
        tt->comb_cfg[st * 8 + 6] = d->comb[st].ifaca;
    }
    /* texture-lighting word in stage 0's spare slot: the unfolded
       pipelines read it per tri, the folded ones pin it (C_LIGHT) */
    tt->comb_cfg[7] = d->light_on
        ? (d->lcomb.comb | (d->lcomb.fmsb << 4) | (d->lcomb.comba << 5))
        : 0u;
    if (d->tex_en)
        for (int sl = 0; sl <= d->sh[0].top; sl++) {
            tt->slot0[sl * 4 + 0] = tc->sd0.slot[sl].lw;
            tt->slot0[sl * 4 + 1] = tc->sd0.slot[sl].lh;
            tt->slot0[sl * 4 + 2] = gpu_slot_base_pack(g, dev, &tc->sd0.slot[sl]);
            tt->slot0[sl * 4 + 3] = tc->sd0.slot[sl].mask;
        }
    if (d->sec_en)
        for (int sl = 0; sl <= d->sh[1].top; sl++) {
            tt->slot1[sl * 4 + 0] = tc->sd1.slot[sl].lw;
            tt->slot1[sl * 4 + 1] = tc->sd1.slot[sl].lh;
            tt->slot1[sl * 4 + 2] = gpu_slot_base_pack(g, dev, &tc->sd1.slot[sl]);
            tt->slot1[sl * 4 + 3] = tc->sd1.slot[sl].mask;
        }
    return tt;
}

/* Per-triangle coverage counters (the close-time stats printer). */
static void
capture_tri_coverage(r128_gpu_t *g, const r3d_texctx_t *tc,
                     const rage128_draw_state_t *d, const seg_tri_t *tt)
{
    uint32_t full = (d->dst_dt == 6) ? 0xffffffffu : 0xffffu;

    g->st_cov_dt[d->dst_dt & 15]++;
    if (g->cur_kernel >= 0 && g->cur_kernel < GPU_KERNELS)
        g->st_cov_k[g->cur_kernel]++;
    g->st_cov_dith[d->dither ? 1 : 0]++;
    if ((d->wmask & full) != full)
        g->st_cov_wmask_partial++;
    g->st_cov_z[!d->z_en ? 0 : (d->zbpp == 2 ? 1 : (d->zshift ? 3 : 2))]++;
    if (d->spec_en)
        g->st_cov_spec++;
    if (d->fog_en)
        g->st_cov_fogv++;
    if (d->fog_en && d->fog_table_en)
        g->st_cov_fogt++;
    if (d->tex_en && d->need_ck)
        g->st_cov_ck++;
    if ((d->tex_en || d->sec_en) && !d->do_persp) {
        g->st_cov_affine++;
        if (d->need_lod)
            g->st_cov_affine_lod++;
    }
    for (int st = 0; st < 2; st++)
        if (st == 0 ? d->tex_en : d->sec_en) {
            g->st_cov_tex[d->sh[st].dt & 15]++;
            if ((d->sh[st].dt & 15) == 0)
                g->st_cov_texs3[d->sh[st].s3tc & 3]++;
        }
    {
        int smip = 0;

        for (int sl = 0; sl <= d->sh[0].top && d->tex_en; sl++)
            smip |= (tt->slot0[sl * 4 + 2] & 0x80000000u) != 0;
        for (int sl = 0; sl <= d->sh[1].top && d->sec_en; sl++)
            smip |= (tt->slot1[sl * 4 + 2] & 0x80000000u) != 0;
        g->st_cov_smip += smip;
    }
    if (tc->rs->c_staged || tc->rs->z_staged) {
        g->seg_czstaged = 1;
        g->st_cov_cstg += tc->rs->c_staged != 0;
        g->st_cov_zstg += tc->rs->z_staged != 0;
    }
    if (d->sten_on) {
        g->st_cov_sten[d->zshift ? 0 : 1]++;
        if (!d->z_en)
            g->st_cov_sten[2]++;
    }
}

/* Open a new run when the kernel, the combine tuple (which selects the
   folded pipeline) or the serial flag changes, or a break was requested.
   A split caused by the tuple alone is counted in st_tsplit: it is the
   extra dispatches that pipeline folding costs. */
static void
capture_run_open(r128_gpu_t *g)
{
    if (g->nruns == 0 || g->run_break
        || g->runs[g->nruns - 1].kernel != (uint32_t) g->cur_kernel
        || g->runs[g->nruns - 1].tuple != g->cur_tuple
        || g->runs[g->nruns - 1].serial != (uint32_t) g->cur_serial) {
        seg_run_t *ru;

        if (g->nruns > 0 && !g->run_break
            && g->runs[g->nruns - 1].kernel == (uint32_t) g->cur_kernel)
            g->st_tsplit++;
        g->run_break = 0;
        ru           = &g->runs[g->nruns++];
        ru->span0    = g->nspans;
        ru->kernel   = (uint32_t) g->cur_kernel;
        ru->tuple    = g->cur_tuple;
        ru->serial   = (uint32_t) g->cur_serial;
        ru->pymin    = INT32_MAX;
        ru->pymax    = INT32_MIN;
        ru->c0 = ru->z0 = ru->q0 = 0xffffffffu;
        ru->c1 = ru->z1 = ru->q1 = 0;
    }
}

/* Add one exact texture range to the segment's exact texture list.
   Consecutive draws repeat the same few textures (a Quake 3 surface and
   its lightmap, each with its mip chain), so the dedup scan runs from
   the newest entry back and usually ends within a handful of compares;
   only a texture new to the segment walks the whole list, once. A
   segment that outgrows the cap sets texq_ovf, and its hazard tests
   then rely on the segment's texture hull (q_lo..q_hi) alone. */
static void
gpu_texq_add(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    if (g->texq_ovf || hi <= lo)
        return;
    for (uint32_t i = g->ntexq; i-- > 0;)
        if (g->texq_lo[i] == lo && g->texq_hi[i] == hi)
            return;
    if (g->ntexq == GPU_TEXQ_CAP) {
        g->texq_ovf = 1;
        g->st_texq_ovf++;
        return;
    }
    g->texq_lo[g->ntexq] = lo;
    g->texq_hi[g->ntexq] = hi;
    g->ntexq++;
}

/* Record the destination bytes of one queued 2D store in the exact
   list behind the f hull (the exact 2D store list of r128_gpu_t). The
   list half of gpu_f_add, which also widens the hull and holds the
   generation around both. A repeat of an interval already listed is
   dropped; past the cap the segment falls back to the hull. */
static void
gpu_fq_add(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    if (g->fq_ovf || hi <= lo)
        return;
    for (uint32_t i = g->nfq; i-- > 0;)
        if (g->fq_lo[i] == lo && g->fq_hi[i] == hi)
            return;
    if (g->nfq == GPU_FQ_CAP) {
        g->fq_ovf = 1;
        g->st_fq_ovf++;
        return;
    }
    g->fq_lo[g->nfq] = lo;
    g->fq_hi[g->nfq] = hi;
    g->nfq++;
}

/* Record one queued 2D store's destination bytes [lo, hi) in the
   pending segment: the f hull widens over them and the exact list gains
   them, inside one generation change so a reader on another thread
   never sees the hull without the entry (gpu_pending_f_hit). Called by
   every queue path on the thread that queues the store. */
static void
gpu_f_add(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    gpu_f_begin(g);
    rng_ext(&g->f_lo, &g->f_hi, lo, hi);
    gpu_fq_add(g, lo, hi);
    gpu_f_end(g);
}

/* Record the texture bytes this draw may sample: one exact range per
   resident mip slot, 0 to top, widening the run's q0..q1 and feeding the
   segment's exact list. Each level has its own offset register,
   TEX_0_OFFSET to TEX_10_OFFSET (SDK: Texture Mapping, p. 6-39 / PDF
   151), so the levels need not be adjacent and one hull per stage
   (rs->tex_lo/hi) can cover many bytes no level uses. Staged slots read
   the arena binding, which cannot alias the VRAM import, and are
   skipped. */
static void
capture_run_texrange(r128_gpu_t *g, const rage128_t *dev,
                     const r3d_texctx_t *tc, const rage128_draw_state_t *d)
{
    seg_run_t *ru = &g->runs[g->nruns - 1];
    /* this triangle's resident ranges, empty for a slot that is off,
       AGP or staged; same = equal to what the exact list last absorbed */
    uint32_t cur_lo[2][11], cur_hi[2][11];
    int      top[2], same = g->texq_memo_valid;

    for (int st = 0; st < 2; st++) {
        const r3d_stage_desc_t *sd = st ? &tc->sd1 : &tc->sd0;

        top[st] = (st == 0 ? d->tex_en : d->sec_en) ? d->sh[st].top : -1;
        if (same && top[st] != g->texq_memo_top[st])
            same = 0;
        for (int sl = 0; sl <= top[st]; sl++) {
            const struct r3d_slot_desc_t *so = &sd->slot[sl];
            uint32_t                      lo = 0, hi = 0;

            if (so->texbase == dev->svga.vram) {
                uint32_t len = r3d_level_bytes(sd->dt, sd->s3tc, so->lw, so->lh);

                if ((uint64_t) so->base + len > (uint64_t) dev->vram_mask + 1) {
                    /* the fetch wraps at the VRAM mask, and a linear
                       range cannot describe the wrapped bytes: claim
                       the whole range, and stop using the exact list
                       for this segment (counted once per segment, like
                       the cap in gpu_texq_add) */
                    ru->q0 = 0;
                    ru->q1 = 0xffffffffu;
                    if (!g->texq_ovf)
                        g->st_texq_ovf++;
                    g->texq_ovf = 1;
                    break;
                }
                if (so->base < ru->q0)
                    ru->q0 = so->base;
                if (so->base + len > ru->q1)
                    ru->q1 = so->base + len;
                lo = so->base;
                hi = so->base + len;
            }
            cur_lo[st][sl] = lo;
            cur_hi[st][sl] = hi;
            if (same && (g->texq_memo_lo[st][sl] != lo || g->texq_memo_hi[st][sl] != hi))
                same = 0;
        }
    }
    if (same || g->texq_ovf)
        return;
    for (int st = 0; st < 2; st++) {
        g->texq_memo_top[st] = top[st];
        for (int sl = 0; sl <= top[st]; sl++) {
            g->texq_memo_lo[st][sl] = cur_lo[st][sl];
            g->texq_memo_hi[st][sl] = cur_hi[st][sl];
            gpu_texq_add(g, cur_lo[st][sl], cur_hi[st][sl]);
        }
    }
    g->texq_memo_valid = 1;
}

/* Fill the host side tables for span si, pixels [cx, cx+cw). bb_of gets
   the span's color and z byte ranges within its row, for the leveler's
   alias test: color by the destination bpp, z by zbpp even when z is
   off, which can only over-report an overlap. sctl_of/srm_of get the
   commutative-stencil class, which must match s_atomic in the seg
   kernel: no color write (wmask 0), no alpha test or color key, no z
   write, stencil compare always (7), and zpass and zfail ops from keep,
   zero, replace, saturating increment and saturating decrement (0 to
   4) that are equal or have one of them keep. */
static void
capture_span_class(r128_gpu_t *g, uint32_t si, const rage128_draw_state_t *ds,
                   int32_t cx, int32_t cw)
{
    uint32_t cb = (ds->dst_dt == 6) ? 4u : 2u;

    g->bb_of[si].c0 = (uint32_t) cx * cb;
    g->bb_of[si].c1 = (uint32_t) (cx + cw) * cb;
    g->bb_of[si].z0 = (uint32_t) cx * (uint32_t) ds->zbpp;
    g->bb_of[si].z1 = (uint32_t) (cx + cw) * (uint32_t) ds->zbpp;
    if (ds->sten_on && ds->wmask == 0 && !ds->atest_en
        && !ds->need_ck
        && !(ds->z_en && ds->z_wr) && ds->sfn == 7
        && ds->zpass_op <= 4 && ds->zfail_op <= 4
        && (ds->zpass_op == ds->zfail_op || ds->zpass_op == 0
            || ds->zfail_op == 0)) {
        g->sctl_of[si] = 1u | (ds->sfn << 4)
            | (ds->sfail_op << 8)
            | (ds->zpass_op << 12)
            | (ds->zfail_op << 16);
        g->srm_of[si] = ds->sref | (ds->svmask << 8)
            | (ds->swmask << 16);
    } else {
        g->sctl_of[si] = 0;
        g->srm_of[si]  = 0;
    }
}

/* Widen the current run's row and store bounds by span si. The store
   ranges are built from the raw (unmasked) card addresses, so they
   compare directly against the card-space texture ranges in q0..q1.
   A staged color or z target is not in VRAM and adds no store range. */
static void
capture_run_bounds(r128_gpu_t *g, const r3d_texctx_t *tc, uint32_t si,
                   int32_t py, uint32_t drow, uint32_t zrow)
{
    seg_run_t *ru = &g->runs[g->nruns - 1];

    if (py < ru->pymin)
        ru->pymin = py;
    if (py > ru->pymax)
        ru->pymax = py;
    if (!tc->rs->c_staged) {
        if (drow + g->bb_of[si].c0 < ru->c0)
            ru->c0 = drow + g->bb_of[si].c0;
        if (drow + g->bb_of[si].c1 > ru->c1)
            ru->c1 = drow + g->bb_of[si].c1;
    }
    if (!tc->rs->z_staged) {
        if (zrow + g->bb_of[si].z0 < ru->z0)
            ru->z0 = zrow + g->bb_of[si].z0;
        if (zrow + g->bb_of[si].z1 > ru->z1)
            ru->z1 = zrow + g->bb_of[si].z1;
    }
}

/* The covered columns of a whole row, before the tiled walk splits it
   into tile-column pieces: the same accept test and the same edge clip
   rage128_gpu_capture_span applies to each piece, run once on the full
   row. The clip is exact per pixel (each edge keeps the columns where its
   integer edge value is >= 0), so the row's range is exactly the union of
   what the pieces would capture, and a piece outside it would capture
   nothing. Returns 0 when the row has no covered column. */
int
rage128_gpu_capture_row_cover(const r128_jit_tri_t *tri, int64_t e0,
                              int64_t e1, int64_t e2, int32_t py,
                              int32_t *cx0, int32_t *cx1)
{
    int32_t lo = 0, hi;

    if (py < 0 || py >= (int32_t) GPU_ROW_CAP || tri->x1 < tri->x0)
        return 0;
    hi = tri->x1 - tri->x0;
    if (!gpu_clip_edge(e0, tri->e0dxi, &lo, &hi)
        || !gpu_clip_edge(e1, tri->e1dxi, &lo, &hi)
        || !gpu_clip_edge(e2, tri->e2dxi, &lo, &hi))
        return 0;
    *cx0 = tri->x0 + lo;
    *cx1 = tri->x0 + hi;
    return 1;
}

uint64_t
rage128_gpu_capture_span(const r128_jit_tri_t *tri, int64_t e0, int64_t e1,
                         int64_t e2, double zline, uint32_t drow, uint32_t zrow,
                         int32_t py)
{
    const r3d_texctx_t *tc  = (const r3d_texctx_t *) tri->texctx;
    rage128_t          *dev = tc->dev;
    r128_gpu_t         *g   = (r128_gpu_t *) dev->gpu;
    int32_t             x0 = tri->x0, x1 = tri->x1;
    int32_t             lo = 0, hi;

    if (py < 0 || py >= (int32_t) GPU_ROW_CAP || x1 < x0)
        return 0xffffffffu; /* nothing to capture (rare: the walk clamps) */
    hi = x1 - x0;

    /* A triangle's intersection with one row is contiguous. Intersect the
       three exact edge inequalities once on the CPU so the GPU receives
       covered pixels only, not the full bounding-box row. */
    if (!gpu_clip_edge(e0, tri->e0dxi, &lo, &hi)
        || !gpu_clip_edge(e1, tri->e1dxi, &lo, &hi)
        || !gpu_clip_edge(e2, tri->e2dxi, &lo, &hi))
        return 0xffffffffu;
    if (g->prof) {
        g->pr_cap_rows++;
        g->pr_cap_lo += (uint64_t) lo;
        g->pr_cap_bbox += (uint64_t) (tri->x1 - tri->x0 + 1);
    }
    x0 += lo;
    x1 = tri->x0 + hi;
    e0 += (int64_t) lo * tri->e0dxi;
    e1 += (int64_t) lo * tri->e1dxi;
    e2 += (int64_t) lo * tri->e2dxi;
    for (int32_t q = 0; q < lo; q++)
        zline += tri->dZdx;

    if (g->cur_tri < 0) {
        const rage128_draw_state_t *d  = &tc->rs->d;
        const seg_tri_t            *tt = capture_tri_record(g, dev, tc, tri);

        g->cur_tri = (int32_t) g->ntris++;
        g->st_tris++;
        capture_tri_coverage(g, tc, d, tt);
        capture_run_open(g);
        capture_run_texrange(g, dev, tc, d);
    }

    /* Split the row into chunks of at most 256 pixels, one per kernel
       workgroup of 256 threads. Each chunk's z seed is reached by
       adding dZdx once per pixel, in double precision, so it rounds the
       same way as the interpreter's per-pixel walk and the ladder
       kernel's. */
    {
        double  zc = zline;
        int32_t cx = x0;

        g->st_px_total += (uint64_t) (x1 - x0 + 1);
        if (x1 - x0 + 1 > 256) {
            g->st_wide_spans++;
            g->st_wide_px += (uint64_t) (x1 - x0 + 1);
        }

        while (cx <= x1 && g->nspans < GPU_SPAN_CAP) {
            int32_t     cw = x1 - cx + 1;
            seg_span_t *sp;

            if (cw > 256)
                cw = 256;
            /* b_lad holds exactly GPU_PX_CAP words and the ladder kernel
               writes [px_base, px_base+cw*pxw) with no bound test, so
               seg_px must never pass the cap. The span is cut to fit;
               whatever does not fit is counted as dropped after the
               loop. A table-fog span stores two ladder words per pixel
               (the z rung and the fog fraction), so it counts at twice
               its width. */
            uint32_t pxw = (tc->rs->d.fog_en && tc->rs->d.fog_table_en) ? 2u : 1u;

            if (g->seg_px + (uint32_t) cw * pxw > GPU_PX_CAP) {
                cw = (int32_t) ((GPU_PX_CAP - g->seg_px) / pxw);
                if (cw <= 0)
                    break;
            }
            sp = &((seg_span_t *) g->slot[g->build_slot].b_spans.map)[g->nspans];
            /* The clip above used the biased edge values, so coverage
               keeps the fill rule. The shader reads the recorded edges
               only for the barycentric weights, so the record takes the
               unbiased values: the bias flags from the triangle record
               added back, as every CPU lane does before its convert. A
               constant attribute then lands unscaled on small
               triangles. */
            sp->e0 = e0 + tri->e0b;
            sp->e1 = e1 + tri->e1b;
            sp->e2 = e2 + tri->e2b;
            memcpy(&sp->zline, &zc, 8);
            sp->x0  = cx;
            sp->x1  = cx + cw - 1;
            sp->py  = py;
            sp->tri = g->cur_tri;
            /* A color or z target staged from AGP memory gets bit 31 and
               the row base relative to the stage arena (row address
               minus surface base, the interpreter's staged index); the
               kernel then skips the vram_mask wrap. A resident row base
               is masked here, so a large py * pitch product cannot set
               bit 31 by accident. The kernel's own add-then-mask result
               is unchanged, since (a + b) & m == ((a & m) + b) & m for a
               power-of-two mask. */
            sp->drow    = tc->rs->c_staged
                   ? ((drow - tc->rs->dst_offset) | 0x80000000u)
                   : (drow & dev->vram_mask);
            sp->zrow    = tc->rs->z_staged
                   ? ((zrow - tc->rs->t3d.z_offset) | 0x80000000u)
                   : (zrow & dev->vram_mask);
            sp->px_base = g->seg_px;
            /* the 32-bit range test runs on the recorded values, which
               are what the shader steps */
            sp->pad1 = gpu_edge32_span(sp->e0, tri->e0dxi, cw)
                && gpu_edge32_span(sp->e1, tri->e1dxi, cw)
                && gpu_edge32_span(sp->e2, tri->e2dxi, cw);
            g->seg_px += (uint32_t) cw * pxw;
            g->py_of[g->nspans] = (uint16_t) py;
            capture_span_class(g, g->nspans, &tc->rs->d, cx, cw);
            g->nspans++;
            g->st_spans++;
            capture_run_bounds(g, tc, g->nspans - 1, py, drow, zrow);

            /* Seed the next chunk only when one follows. The z walk is a
               dependent double-add per pixel and nothing reads its result
               past the last chunk; spans wider than one chunk (st_wide_spans)
               are the only ones that need it, and there the sequential add
               stays, matching the interpreter's and the ladder's rounding. */
            if (cx + cw <= x1) {
                for (int q = 0; q < cw; q++)
                    zc += tri->dZdx;
                e0 += (int64_t) cw * tri->e0dxi;
                e1 += (int64_t) cw * tri->e1dxi;
                e2 += (int64_t) cw * tri->e2dxi;
            }
            cx += cw;
        }
        /* Not expected to run: gpu_submit_prim reserved room for this
           triangle's spans and pixels before accepting it. If it runs
           anyway, log once and count the spans and pixels that were not
           rendered, so the loss shows up in the stats. */
        if (cx <= x1) {
            if (!g->st_spandrop && !g->st_pxdrop)
                gpu_log("RAGE128 GPU: span capture overflow past accept bound, "
                        "pixels dropped\n");
            g->st_spandrop += (uint64_t) (x1 - cx + 256) / 256u;
            g->st_pxdrop += (uint64_t) (x1 - cx + 1);
        }
    }
    /* report the row's edge-clipped covered range: it bounds every pixel
       the dispatch will write, for the caller's dirty marking */
    return ((uint64_t) (uint32_t) x1 << 32) | (uint32_t) x0;
}

/* Count-only version of rage128_gpu_capture_span: adds up the spans and
   pixels the capture would use, with the same guards, the same edge clip
   and the same 256-pixel chunks, and writes nothing. It returns the
   empty-row code, so the walker neither marks dirty rows nor counts
   stats for this pass. gpu_submit_prim uses the count to decide whether
   the capture of the same draw that follows fits the segment, so the two
   functions must agree: a count that is too low drops pixels. */
uint64_t
rage128_gpu_count_span(const r128_jit_tri_t *tri, int64_t e0, int64_t e1,
                       int64_t e2, double zline, uint32_t drow, uint32_t zrow,
                       int32_t py)
{
    const r3d_texctx_t *tc = (const r3d_texctx_t *) tri->texctx;
    r128_gpu_t         *g  = (r128_gpu_t *) tc->dev->gpu;
    int32_t             x0 = tri->x0, x1 = tri->x1;
    int32_t             lo = 0, hi;
    int32_t             cw;

    (void) zline;
    (void) drow;
    (void) zrow;
    if (py < 0 || py >= (int32_t) GPU_ROW_CAP || x1 < x0)
        return 0xffffffffu;
    hi = x1 - x0;
    if (!gpu_clip_edge(e0, tri->e0dxi, &lo, &hi)
        || !gpu_clip_edge(e1, tri->e1dxi, &lo, &hi)
        || !gpu_clip_edge(e2, tri->e2dxi, &lo, &hi))
        return 0xffffffffu;
    cw = hi - lo + 1;
    g->dry_spans += ((uint64_t) cw + 255u) / 256u;
    g->dry_px += (uint64_t) cw;
    return 0xffffffffu;
}

/* ------------------------------------------------------------------ */
/* Submit-side entry points (CCE/submit thread).                       */
/* ------------------------------------------------------------------ */

/* A primitive the backend refused is about to run on the CPU path. Flush
   first if it conflicts with GPU work not yet complete: its color or z
   stores against the GPU's stores and queued texture reads, its texture
   reads against the GPU's stores. A primitive on disjoint bytes runs at
   once without a flush; the CPU drawing it now and the GPU drawing the
   rest later gives the same bytes. */
static void
rage128_gpu_fallback_guard(rage128_t *dev, const rage128_raster_state_t *rs,
                           const r3d_vtx_t *a, const r3d_vtx_t *b,
                           const r3d_vtx_t *c)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint32_t    clo, chi, zlo, zhi;

    if (!gpu_has_work(g))
        return;
    g->st_fallback++;
    if (!b)
        g->st_fb_point++;
    else if (!c)
        g->st_fb_line++;
    prim_ranges(rs, dev->vram_mask, a, b, c, &clo, &chi, &zlo, &zhi);

    /* CPU writes conflict with GPU writes and queued texture reads. */
    if (gpu_range_hit(g, clo, chi, 1) || gpu_range_hit(g, zlo, zhi, 1)) {
        gpu_flush_cause(dev, GF_OTHERDRAW);
        return;
    }
    /* CPU texture reads conflict only with GPU writes. */
    for (int st = 0; st < 2; st++)
        for (uint32_t i = 0; i < rs->tex_nrng[st]; i++)
            if (gpu_range_hit(g, rs->tex_rng_lo[st][i], rs->tex_rng_hi[st][i], 0)) {
                gpu_flush_cause(dev, GF_OTHERDRAW);
                return;
            }
}

/* A point is split into the interpreter's own two triangles
   (r3d_point_tris) and each goes through the triangle path, so the GPU
   draws the same triangles the CPU lane would and matches it bit for
   bit. A triangle the backend refuses is drawn on the CPU right away;
   the refusal path first flushes any overlapping GPU work, so draw order
   holds. */
static int
gpu_submit_prim_tris(rage128_t *dev, const rage128_raster_state_t *rs,
                     const r3d_vtx_t v[6])
{
    int gpu = 1;

    for (int t = 0; t < 2; t++)
        if (!rage128_gpu_submit_tri(dev, rs, &v[t * 3], &v[t * 3 + 1],
                                    &v[t * 3 + 2])) {
            gpu = 0;
            rage128_3d_tri(dev, rs, 0, 0, &v[t * 3], &v[t * 3 + 1],
                           &v[t * 3 + 2], rage128_jit_get_block(dev, &rs->d));
        }
    return gpu;
}

static int gpu_submit_prim(rage128_t *dev, const rage128_raster_state_t *rs,
                           const r3d_vtx_t *a, const r3d_vtx_t *b,
                           const r3d_vtx_t *c, int line);

/* A line is one draw record whose spans are the Bresenham walk's runs
   (rage128_3d_line drives the count and capture span callbacks exactly
   as a triangle does); a rejection falls back to the CPU walk in the
   caller. */
int
rage128_gpu_submit_line(rage128_t *dev, const rage128_raster_state_t *rs,
                        const r3d_vtx_t *a, const r3d_vtx_t *b)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    int         gpu;

    if (a->x == b->x && a->y == b->y)
        return 1; /* zero-length: the interpreter draws nothing too */
    gpu = gpu_submit_prim(dev, rs, a, b, b, 1);
    g->st_cov_line[gpu]++;
    return gpu;
}

int
rage128_gpu_submit_point(rage128_t *dev, const rage128_raster_state_t *rs,
                         const r3d_vtx_t *p)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    r3d_vtx_t   v[6];

    r3d_point_tris(p, v);
    g->st_cov_point[gpu_submit_prim_tris(dev, rs, v)]++;
    return 1;
}

int
rage128_gpu_submit_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                       const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c)
{
    return gpu_submit_prim(dev, rs, a, b, c, 0);
}

/* Shared admission + capture. line: c is b (the pricing bbox and the
   hazard ranges only need the endpoints) and the rasterizer is the
   line walk; the fallback guard sees c NULL so it counts a line. */
static int
gpu_submit_prim(rage128_t *dev, const rage128_raster_state_t *rs,
                const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c,
                int line)
{
    r128_gpu_t      *g = (r128_gpu_t *) dev->gpu;
    uint32_t         clo, chi, zlo, zhi;
    uint64_t         need_spans, need_px;
    int              kern     = gpu_state_kernel(dev, rs);
    int32_t          tup      = -1;
    int              serial   = 0;
    int              repriced = 0;
    const r3d_vtx_t *gc       = line ? NULL : c;

    if (kern < 0) {
        rage128_gpu_fallback_guard(dev, rs, a, b, gc);
        return 0;
    }

    /* A triangle that does not fit even an empty segment would be cut
       off partway through capture after it was accepted, and the rest
       of its pixels would be drawn by no one. The quick estimate
       (gpu_tri_need) charges every bounding-box row at the full box
       width, and on a tiled surface charges one span per tile-column
       piece of each row, so a large triangle on a tiled target can
       exceed the span cap while its real spans fit. A draw over a cap is
       therefore counted exactly first, with a count-only walk through
       the same rasterizer, clip and chunking the capture uses, and is
       refused to the CPU path only if the exact count is still over.
       Only draws over a cap pay for the extra walk. */
    /* ladder words, not pixels: a table-fog draw takes two per pixel */
    uint64_t pxw = (rs->d.fog_en && rs->d.fog_table_en) ? 2u : 1u;

    gpu_tri_need(rs, a, b, c, &need_spans, &need_px);
    need_px *= pxw;
    if (need_spans > GPU_SPAN_CAP || need_px > GPU_PX_CAP) {
        g->dry_spans = 0;
        g->dry_px    = 0;
        if (line)
            rage128_3d_line(dev, rs, 0, 0, a, b, rage128_gpu_count_span);
        else
            rage128_3d_tri(dev, rs, 0, 0, a, b, c, rage128_gpu_count_span);
        g->st_reprice++;
        need_spans = g->dry_spans;
        need_px    = g->dry_px * pxw;
        if (need_spans > GPU_SPAN_CAP || need_px > GPU_PX_CAP) {
            g->st_toobig++;
            rage128_gpu_fallback_guard(dev, rs, a, b, gc);
            return 0;
        }
        repriced = 1;
    }

    /* Self-aliasing, tested before the tuple key: a draw whose color
       and z ranges overlap, or that samples its own color or z bytes,
       breaks the rule that one workgroup owns one row. Such a draw stays
       on the GPU as a serial run (one workgroup, lane 0, spans in
       capture order, which is the interpreter's pixel order, so a pixel
       sees earlier pixels' stores exactly as on the CPU) when every
       aliased surface is resident in VRAM. A staged color or z target or
       an AGP texture goes to the CPU instead: those are bound as arena
       copies, so a serial run would not see its own stores through them
       the way the interpreter does. So does a draw over
       GPU_SERIAL_PX_CAP pixels, or one that arrives before the serial
       pipelines are built. */
    prim_ranges(rs, dev->vram_mask, a, b, c, &clo, &chi, &zlo, &zhi);
    {
        int cz  = rng_hit(clo, chi, zlo, zhi) != 0;
        int rtt = 0, agp = 0;

        for (int st = 0; st < 2; st++)
            if (r3d_tex_rng_hit(rs, st, clo, chi)
                || r3d_tex_rng_hit(rs, st, zlo, zhi)) {
                rtt = 1;
                agp |= r128_card_is_agp(rs->tex_lo[st]);
            }
        if (cz || rtt) {
            if (rs->c_staged || rs->z_staged || agp
                || need_px / pxw > GPU_SERIAL_PX_CAP /* pixels, not words */
                || !atomic_load_explicit(&g->serial_ready,
                                         memory_order_acquire)) {
                g->st_alias_rej[cz ? 0 : 1]++;
                rage128_gpu_fallback_guard(dev, rs, a, b, gc);
                return 0;
            }
            g->st_alias_ser[cz ? 0 : 1]++;
            serial = 1;
        }
    }

    /* Count accepted draws whose color or z base is not aligned to the
       cell size, so a cell can straddle two words of the kernel's
       storage view. The close-time stats report these counts. */
    if (rs->dst_offset & (uint32_t) (rs->d.bpp - 1))
        g->st_odd_c++;
    if ((rs->d.z_en || rs->d.sten_on)
        && (rs->t3d.z_offset & (uint32_t) (rs->d.zbpp - 1)))
        g->st_odd_z++;

    /* Folded-pipeline key. Untextured kernels have no combine stage and
       run the unfolded pipeline (tuple -1), so they never split a run.
       Serial draws keep tuple -1 too: only unfolded (uber) serial
       pipelines are built. */
    if (!serial && r128_gpu_variants[kern].tex > 0) {
        uint32_t sel[GPU_TUPLE_SEL];

        gpu_tuple_normalize(&rs->d, r128_gpu_variants[kern].tex, sel);
        tup = gpu_tuple_intern(g, sel);
    }

    /* render-to-texture: this draw samples bytes the segment writes */
    if (atomic_load(&g->pending)) {
        uint32_t gc_lo = atomic_load(&g->c_lo), gc_hi = atomic_load(&g->c_hi);
        uint32_t gz_lo = atomic_load(&g->z_lo), gz_hi = atomic_load(&g->z_hi);

        for (int st = 0; st < 2; st++)
            if (r3d_tex_rng_hit(rs, st, gc_lo, gc_hi)
                || r3d_tex_rng_hit(rs, st, gz_lo, gz_hi)) {
                gpu_flush_cause(dev, GF_TEX);
                break;
            }
    }
    {
        int      zt = rs->d.z_en || rs->d.sten_on;
        uint32_t zs = rs->d.zrowpx * (uint32_t) rs->d.zbpp;

        if (g->rt_valid
            && ((rs->dst_offset != g->rt_dst || rs->dst_pitch != g->rt_pitch
                 || rs->dst_bpp != g->rt_bpp
                 || rs->d.c_tiled != g->rt_ctiled)
                || (zt && g->rt_z_valid
                    && (rs->t3d.z_offset != g->rt_z || zs != g->rt_zstride
                        || rs->d.z_tiled != g->rt_ztiled))))
            gpu_flush_cause(dev, GF_RT);
    }
    /* Soft submit cadence. The span cap is large enough to hold a whole
       frame, so without this a frame would reach the flip as one or two
       segments and the flip would wait for all of that GPU work.
       Submitting at GPU_CADENCE spans hands the work over earlier and
       changes nothing else. The test runs only at a draw boundary and
       never on an empty segment, so a triangle is always captured
       whole, and one larger than the cadence goes alone, up to the
       span cap. */
    if (g->nspans && g->nspans + need_spans > (uint64_t) GPU_CADENCE)
        gpu_flush_cause(dev, GF_CADENCE);

    {
        /* st_cap counts capacity flushes by the limit that was hit:
           0 spans, 1 tri records (palettes included), 2 ladder words,
           3 runs. */
        int cap = -1;

        if (g->nspans + need_spans > GPU_SPAN_CAP)
            cap = 0;
        else if (g->ntris + 2 > GPU_TRI_CAP)
            cap = 1;
        else if (g->seg_px + need_px > GPU_PX_CAP)
            cap = 2;
        else if (g->nruns == GPU_RUN_CAP
                 && (g->run_break
                     || g->runs[g->nruns - 1].kernel != (uint32_t) kern
                     || g->runs[g->nruns - 1].tuple != tup
                     || g->runs[g->nruns - 1].serial != (uint32_t) serial))
            cap = 3;
        /* A CI draw needs a new palette snapshot and the palette arena
           is full. After the flush npal is 0, so the snapshot fits.
           Counted under 1 with the tri records, each of which points at
           its palette (pal_base). */
        else if (gpu_draw_ci(&rs->d) && g->npal >= GPU_PAL_CAP
                 && g->pal_up_gen != dev->tex_pal_gen)
            cap = 1;
        if (cap >= 0) {
            g->st_cap[cap]++;
            gpu_flush_cause(dev, GF_CAP);
        }
    }

    /* Color/z hazard across draws: this draw's z bytes overlapping color
       bytes the segment already stores, or the reverse. The same bytes
       would then be owned by the workgroups of two different rows,
       which run in no fixed order. Flush, then accept. */
    if (atomic_load(&g->pending)
        && (rng_hit(atomic_load(&g->c_lo), atomic_load(&g->c_hi), zlo, zhi)
            || rng_hit(atomic_load(&g->z_lo), atomic_load(&g->z_hi), clo, chi)))
        gpu_flush_cause(dev, GF_CZHAZ);
    /* The draw stores over a queued mid fill. Mid fills are recorded
       after the segment's mirror-back (gpu_disp_tail_fills), so the
       fill would land on top of this draw. */
    if (gpu_mid_range_hit(g, clo, chi) || gpu_mid_range_hit(g, zlo, zhi)) {
        g->st_copy_mid_split++;
        gpu_flush_cause(dev, GF_2DQ);
    }

    g->rt_valid  = 1;
    g->rt_dst    = rs->dst_offset;
    g->rt_pitch  = rs->dst_pitch;
    g->rt_bpp    = rs->dst_bpp;
    g->rt_ctiled = rs->d.c_tiled;
    if (rs->d.z_en || rs->d.sten_on) {
        g->rt_z_valid = 1;
        g->rt_z       = rs->t3d.z_offset;
        g->rt_zstride = rs->d.zrowpx * (uint32_t) rs->d.zbpp;
        g->rt_ztiled  = rs->d.z_tiled;
    }

    if (g->verify) {
        if (g->v_state_n == 0
            || memcmp(&g->v_states[g->v_state_n - 1], rs, sizeof(*rs)) != 0) {
            if (g->v_state_n == g->v_state_cap) {
                g->v_state_cap = g->v_state_cap ? g->v_state_cap * 2 : 64;
                g->v_states    = realloc(g->v_states,
                                         g->v_state_cap * sizeof(*g->v_states));
            }
            g->v_states[g->v_state_n++] = *rs;
        }
        if (g->v_tri_n == g->v_tri_cap) {
            g->v_tri_cap = g->v_tri_cap ? g->v_tri_cap * 2 : 1024;
            g->v_tris    = realloc(g->v_tris, g->v_tri_cap * sizeof(*g->v_tris));
        }
        g->v_tris[g->v_tri_n].state = g->v_state_n - 1;
        g->v_tris[g->v_tri_n].line  = (uint32_t) line;
        g->v_tris[g->v_tri_n].v[0]  = *a;
        g->v_tris[g->v_tri_n].v[1]  = *b;
        g->v_tris[g->v_tri_n].v[2]  = *c;
        g->v_tri_n++;
    }

    /* Publish the draw's byte ranges before capture starts. A CPU access
       may run on another thread while the CCE thread captures, and it
       must not pass the hazard gate while this draw is half recorded. */
    rng_ext(&g->c_lo, &g->c_hi, clo, chi);
    rng_ext(&g->z_lo, &g->z_hi, zlo, zhi);
    if (rs->c_staged)
        atomic_store_explicit(&g->c_stg, 1, memory_order_relaxed);
    if (rs->z_staged)
        atomic_store_explicit(&g->z_stg, 1, memory_order_relaxed);
    for (int st = 0; st < 2; st++)
        if (rs->tex_hi[st] > rs->tex_lo[st] && !r128_card_is_agp(rs->tex_lo[st])) {
            const r3d_stage_hdr_t *h   = &rs->d.sh[st];
            const uint32_t        *off = st ? rs->sec_stage_off : rs->prim_stage_off;
            uint32_t               qlo = rs->tex_lo[st], qhi = rs->tex_hi[st];
            int                    top    = h->top < 0 ? 0 : (h->top > 10 ? 10 : h->top);
            int                    low    = ((st ? rs->d.need_lod2 : rs->d.need_lod)
                       && !h->mipdis && h->minb >= 2)
                                      ? 0
                                      : top;
            int                    staged = 1;

            /* The hull covers the levels low..top, chosen by the same
               rule rage128_raster_state_capture uses to build it. If
               any of them is sampled from VRAM, the fetch wraps at the
               VRAM mask and rng_fold folds the hull the way it folds the
               store ranges; a stage sampled entirely from the arena
               keeps its card-space hull. */
            for (int sl = low; sl <= top; sl++)
                if (off[sl] == R128_TEX_STAGE_NONE)
                    staged = 0;
            rng_fold(dev->vram_mask, staged, &qlo, &qhi);
            rng_ext(&g->q_lo, &g->q_hi, qlo, qhi);
        }
    atomic_store_explicit(&g->pending, 1, memory_order_release);

    g->cur_tri    = -1;
    g->cur_kernel = kern;
    g->cur_tuple  = tup;
    g->cur_serial = serial;
    {
        uint32_t sp0 = g->nspans;
        uint32_t px0 = g->seg_px;

        if (g->prof) {
            uint64_t t0 = prof_now();

            if (dev->ftl_open) {
                dev->ftl_open    = 0;
                dev->ftl_draw_ns = t0;
                atomic_store(&dev->ftl_gap, 0);
            }
            if (line)
                rage128_3d_line(dev, rs, 0, 0, a, b, rage128_gpu_capture_span);
            else
                rage128_3d_tri(dev, rs, 0, 0, a, b, c, rage128_gpu_capture_span);
            g->pr_capture_ns += prof_now() - t0;
        } else if (line) {
            rage128_3d_line(dev, rs, 0, 0, a, b, rage128_gpu_capture_span);
        } else {
            rage128_3d_tri(dev, rs, 0, 0, a, b, c, rage128_gpu_capture_span);
        }
        /* A re-counted draw was accepted on the count-only walk's
           figures, and the capture repeats that walk, so it must use
           exactly that many spans and ladder words. Log a mismatch. */
        if (repriced
            && ((uint64_t) (g->nspans - sp0) != need_spans
                || (uint64_t) (g->seg_px - px0) != need_px)) {
            if (!g->st_reprice_miss)
                gpu_log("RAGE128 GPU: reprice miss: predicted %llu/%llu spans/px,"
                        " capture consumed %u/%u\n",
                        (unsigned long long) need_spans,
                        (unsigned long long) need_px,
                        g->nspans - sp0, g->seg_px - px0);
            g->st_reprice_miss++;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* CPU-thread barriers.                                                */
/* ------------------------------------------------------------------ */

/* Fence for a CPU read of [lo,hi). A reader needs the stored bytes, not
   ordering against the engine. Once a slot's fence has signaled, its
   stores are visible through the coherent mapping, so this waits on the
   fences of the submitted slots that overlap the range and does nothing
   else: no CCE quiesce, no retire, no flush. Returns 0 when the pending
   (unsubmitted) segment covers the bytes; only a flush can resolve that,
   and the caller takes the drain path instead. Safe on any thread. */
static int
gpu_read_fence(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    if (atomic_load_explicit(&g->pending, memory_order_acquire)
        && (rng_hit(atomic_load(&g->c_lo), atomic_load(&g->c_hi), lo, hi)
            || rng_hit(atomic_load(&g->z_lo), atomic_load(&g->z_hi), lo, hi)
            || rng_hit(atomic_load(&g->f_lo), atomic_load(&g->f_hi), lo, hi)))
        return 0;
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        gpu_slot_t *sl = &g->slot[i];

        if (!atomic_load_explicit(&sl->submitted, memory_order_acquire))
            continue;
        if (!rng_hit(sl->c_lo, sl->c_hi, lo, hi)
            && !rng_hit(sl->z_lo, sl->z_hi, lo, hi)
            && !rng_hit(sl->f_lo, sl->f_hi, lo, hi))
            continue;
        if (atomic_load_explicit(&sl->fence_seen, memory_order_acquire))
            continue;
        /* Another thread may retire the slot and reuse it for a newer
           submission between the checks above and this wait; waiting on
           the newer one is merely more than needed. fence_seen must be
           set before the mutex is released: retirement takes this
           mutex, so the slot cannot be reused before the store. Set
           after the release, the flag could land on the next submission
           and let readers skip a fence that has not signaled. */
        pthread_mutex_lock(&sl->fence_mtx);
        if (atomic_load_explicit(&sl->submitted, memory_order_acquire)) {
            /* wait in 5 s steps and log each timeout, as retirement does,
               so a fence that never signals shows up in the log */
            for (unsigned tries = 0;;) {
                VkResult wr = vkWaitForFences(g->dev, 1, &sl->fence,
                                              VK_TRUE, 5000000000ull);
                if (wr != VK_TIMEOUT)
                    break;
                gpu_log("RAGE128 GPU: STUCK FENCE (read) seq=%llu wait#%u status=%d\n",
                        (unsigned long long) sl->seq, ++tries,
                        vkGetFenceStatus(g->dev, sl->fence));
            }
            atomic_store_explicit(&sl->fence_seen, 1, memory_order_release);
        }
        pthread_mutex_unlock(&sl->fence_mtx);
    }
    return 1;
}

/* Queue a GPU copy of the frame just flipped to, [src, src+len), into a
   free snapshot slot. Queue order puts the copy after every submitted
   draw of that frame, and the copy's command buffer carries barriers
   against the work before and after it, so no caller waits. When every
   slot is busy or on display the copy is dropped, and scanout keeps
   showing the latest complete snapshot. Call only from the submit
   thread: the CCE thread, or the CPU thread after the CCE is quiesced,
   the same rule as gpu_flush_cause. */
void
rage128_gpu_present_copy(rage128_t *dev, uint32_t src, uint32_t len)
{
    r128_gpu_t *g  = (r128_gpu_t *) dev->gpu;
    gpu_snap_t *sn = NULL;
    uint32_t    disp;

    if (!g || !g->async || g->verify || dev->synctel || !g->snap_stride
        || !len || len > g->snap_stride)
        return;
    /* The frame's last draws must be in the queue before the copy. Off
       the CCE thread rage128_gpu_present only publishes, so flush here,
       which the submit-thread rule above allows. */
    if (atomic_load_explicit(&g->pending, memory_order_acquire))
        gpu_flush_cause(dev, GF_PRESENT);
    src &= dev->vram_mask;
    /* Reuse the oldest slot that is not on display. The display pick
       publishes snap_displayed before its frame starts, so this read is
       at most one publish old. The handshake below catches the case
       where that old value hides the slot the pick has just put on
       display; the pick also never shows a slot whose fence has not
       signaled, so it cannot scan a slot mid-copy. */
    disp = atomic_load_explicit(&g->snap_displayed, memory_order_acquire);
    for (unsigned i = 0; i < GPU_SNAPS; i++) {
        gpu_snap_t *c = &g->snaps[i];

        if (disp && c->dst == disp)
            continue;
        if (!sn
            || atomic_load_explicit(&c->seq, memory_order_relaxed)
                < atomic_load_explicit(&sn->seq, memory_order_relaxed))
            sn = c;
    }
    if (!sn || pthread_mutex_trylock(&sn->mtx) != 0) {
        g->st_snap_drops++;
        return;
    }
    if (atomic_load_explicit(&sn->used, memory_order_relaxed)
        && vkGetFenceStatus(g->dev, sn->fence) != VK_SUCCESS) {
        pthread_mutex_unlock(&sn->mtx);
        g->st_snap_drops++;
        return;
    }
    /* Claim handshake with the display pick. The disp value read above
       can be one publish old, so the slot chosen on it can be the one
       the pick has just put on display. This side clears `ready` first,
       seq_cst, then reads snap_displayed again; the pick publishes
       snap_displayed seq_cst and then reads `ready` again. With both
       sides storing before they load under seq_cst, at least one of
       them sees the other's store, so a slot is never both on display
       and reused. A match here means the pick got the slot first: put
       `ready` back (nothing has been overwritten yet) and drop the
       copy. */
    atomic_store_explicit(&sn->ready, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&g->snap_displayed, memory_order_seq_cst)
        == sn->dst) {
        atomic_store_explicit(&sn->ready, 1, memory_order_release);
        pthread_mutex_unlock(&sn->mtx);
        g->st_snap_drops++;
        return;
    }
    {
        VkCommandBufferBeginInfo bi = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        VkMemoryBarrier mb  = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        VkBufferCopy    rgn = { src, sn->dst, len };
        VkSubmitInfo    si  = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkResult        vr;

        vkResetFences(g->dev, 1, &sn->fence);
        vkBeginCommandBuffer(sn->cb, &bi);
        /* read after write: the frame's shader and fill stores must be
           visible to the copy's read */
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
            | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(sn->cb,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                 | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
        vkCmdCopyBuffer(sn->cb, g->vram_buf, g->vram_buf, 1, &rgn);
        /* write after read: the next frame's draws must not overwrite
           the source while the copy still reads it. The barriers at the
           head of later submits cover compute work only and would not
           order against this transfer. */
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
            | VK_ACCESS_SHADER_WRITE_BIT
            | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(sn->cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                 | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
        vkEndCommandBuffer(sn->cb);
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &sn->cb;
        pthread_mutex_lock(&g->queue_mtx);
        vr = vkQueueSubmit(g->queue, 1, &si, sn->fence);
        pthread_mutex_unlock(&g->queue_mtx);
        if (vr != VK_SUCCESS)
            fatal("RAGE128 GPU: present copy submit failed (%d)\n", vr);
        atomic_store_explicit(&sn->seq, ++g->snap_seq_ctr,
                              memory_order_relaxed);
        atomic_store_explicit(&sn->src, src, memory_order_relaxed);
        sn->len = len;
        atomic_store_explicit(&sn->used, 1, memory_order_relaxed);
        g->st_snap_copies++;
    }
    pthread_mutex_unlock(&sn->mtx);
}

/* Pick the snapshot the CRTC scans this frame: the newest complete copy
   of want_src, one whose fence has signaled. It never blocks; a slot
   being recorded or not yet signaled is skipped. Returns 0, and the
   CRTC scans the guest buffer through the normal barrier, when there is
   no such copy or when the same copy has been shown for more than
   GPU_SNAP_STALE_MAX frames without a new flip: the guest may then be
   drawing to the front buffer directly (GDI after a flip, for one), and
   a held snapshot would hide that. Called on the CPU thread, once per
   scanned frame. */
int
rage128_gpu_snap_pick(rage128_t *dev, uint32_t want_src, uint32_t *dst,
                      uint64_t *seq)
{
    r128_gpu_t *g      = (r128_gpu_t *) dev->gpu;
    gpu_snap_t *best   = NULL;
    uint64_t    newest = 0, best_seq = 0;
    unsigned    nsrc = 0, nbase = 0;
    int         why = 0;

    if (!g || !g->async || g->verify || dev->synctel || !g->snap_stride)
        return 0;
    want_src &= dev->vram_mask;
    for (unsigned i = 0; i < GPU_SNAPS; i++) {
        gpu_snap_t *c = &g->snaps[i];
        uint64_t    cseq;

        if (!atomic_load_explicit(&c->used, memory_order_relaxed)
            || atomic_load_explicit(&c->src, memory_order_relaxed)
                != want_src) {
            if (atomic_load_explicit(&c->used, memory_order_relaxed))
                nbase++;
            continue;
        }
        nsrc++;
        cseq = atomic_load_explicit(&c->seq, memory_order_relaxed);
        if (cseq > newest)
            newest = cseq;
        if (best && cseq <= best_seq)
            continue;
        if (atomic_load_explicit(&c->ready, memory_order_acquire)) {
            best     = c;
            best_seq = cseq;
            continue;
        }
        if (pthread_mutex_trylock(&c->mtx) != 0)
            continue;
        if (atomic_load_explicit(&c->used, memory_order_relaxed)
            && atomic_load_explicit(&c->src, memory_order_relaxed)
                == want_src
            && vkGetFenceStatus(g->dev, c->fence) == VK_SUCCESS) {
            atomic_store_explicit(&c->ready, 1, memory_order_release);
            best     = c;
            best_seq = atomic_load_explicit(&c->seq, memory_order_relaxed);
        }
        pthread_mutex_unlock(&c->mtx);
    }
    if (best) {
        if (best_seq < newest)
            g->st_snap_late++;
        if (best_seq < g->snap_last_seq) {
            /* Older than what the screen already showed (a newer copy
               of this base exists but its fence has not signaled).
               Showing it would step the display back a frame, which
               reads as flicker. Scan the guest buffer, fenced,
               instead. */
            best = NULL;
            g->st_snap_why[0]++;
            why = 1;
        } else if (best_seq == g->snap_last_seq) {
            g->st_snap_reshow++;
            if (++g->snap_stale > GPU_SNAP_STALE_MAX) {
                best = NULL;
                g->st_snap_why[1]++;
                why = 1;
            }
        } else {
            g->snap_last_seq = best_seq;
            g->snap_stale    = 0;
        }
    }
    if (!best) {
        /* Reasons 0 (older copy) and 1 (stale) are counted above.
           Otherwise: 3, a slot holds this base but its copy has not
           signaled yet; 2, slots hold other bases only (the guest
           flipped to a buffer no snapshot covers); 4, nothing has been
           copied yet. */
        if (!why)
            g->st_snap_why[nsrc ? 3 : (nbase ? 2 : 4)]++;
        atomic_store_explicit(&g->snap_displayed, 0, memory_order_release);
        g->st_snap_real++;
        return 0;
    }
    atomic_store_explicit(&g->snap_displayed, best->dst,
                          memory_order_seq_cst);
    /* This side of the handshake in rage128_gpu_present_copy: the copy
       clears the slot's `ready` seq_cst before it reads snap_displayed
       again, and this side publishes seq_cst before it reads `ready`
       again, so at least one of the two reads sees the other's store.
       A cleared `ready` here means the copy claimed this slot between
       the choice above and the publish. Scan the guest buffer for this
       frame instead of showing a slot that is being overwritten. */
    if (!atomic_load_explicit(&best->ready, memory_order_seq_cst)) {
        atomic_store_explicit(&g->snap_displayed, 0, memory_order_seq_cst);
        g->st_snap_real++;
        return 0;
    }
    *dst = best->dst;
    *seq = best_seq;
    g->st_snap_shows++;
    return 1;
}

/* Count a read fence and its wait time against its 64 KB page, for the
   profile. The table is fixed and searched linearly; once it is full, a
   page not already in it only increments pr_rf_dropped. */
static void
gpu_rf_record(r128_gpu_t *g, uint32_t addr, uint64_t dt)
{
    uint32_t k = addr >> 16;

    for (uint32_t i = 0; i < g->pr_rf_used; i++)
        if (g->pr_rf_key[i] == k) {
            g->pr_rf_cnt[i]++;
            g->pr_rf_ns[i] += dt;
            return;
        }
    if (g->pr_rf_used >= GPU_RF_BUCKETS) {
        g->pr_rf_dropped++;
        return;
    }
    g->pr_rf_key[g->pr_rf_used] = k;
    g->pr_rf_cnt[g->pr_rf_used] = 1;
    g->pr_rf_ns[g->pr_rf_used]  = dt;
    g->pr_rf_used++;
}

/* The guest CPU, or scanout, is about to access local memory
   [addr, addr+len): wait for any GPU work on those bytes. With writes
   set the access stores there, so queued GPU texture reads conflict as
   well; a read (scanout, a framebuffer aperture read) conflicts only
   with GPU stores. A read whose bytes belong to submitted work only
   waits on those fences (gpu_read_fence). Otherwise the CCE is
   quiesced first: once rage128_pm4_drain_wait returns this thread is
   the only submitter, and it can flush without a race. */
void
rage128_gpu_cpu_barrier(rage128_t *dev, uint32_t addr, uint32_t len, int writes)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint32_t    b = addr + len;
    /* Aperture profile: on the sampled accesses (lfbt.sample) the time
       spent here is split into the texture-cache invalidate and the
       range test */
    uint64_t tp = dev->lfbt.sample ? prof_now() : 0;

    /* Before any early return: a store can hit cached staged texels
       even when it hits no range of queued GPU work. */
    if (writes) {
        r128_texcache_dirty(dev, addr, len);
        /* The overlay compositor keeps this frame's composed result
           and decides whether to dispatch again from the overlay
           registers alone, so it cannot see a store into its source
           pixels. Clearing gpu_frame makes the next scanned line
           compose again; without it the new pixels would not show
           until the next frame. */
        if (dev->ov0.gpu_frame && b > dev->ov0.gpu_src_lo
            && addr < dev->ov0.gpu_src_hi)
            dev->ov0.gpu_frame = 0;
    }
    if (tp) {
        uint64_t t = prof_now();

        dev->lfbt.tcd_ns += t - tp;
        tp = t;
    }
    if (!gpu_range_hit(g, addr, b, writes)) {
        if (tp)
            dev->lfbt.rng_ns += prof_now() - tp;
        return;
    }
    if (tp)
        dev->lfbt.rng_ns += prof_now() - tp;
    if (rage128_on_cce_thread) {
        gpu_reap(g);
        if (!gpu_range_hit(g, addr, b, writes))
            return;
        gpu_flush_cause(dev, GF_CPU);
        return;
    }
    if (!writes) {
        /* Timed even with profiling off: rf_win_ns is the input of the
           pacing control in rage128_gpu_pace_window, not a statistic.
           The cost is two clock reads per read that reaches here. */
        uint64_t t0 = prof_now();

        if (gpu_read_fence(g, addr, b)) {
            uint64_t dt = prof_now() - t0;

            g->rf_win_ns += dt;
            if (g->prof) {
                g->pr_read_fence_ns += dt;
                gpu_rf_record(g, addr, dt);
            }
            g->st_read_fences++;
            return;
        }
    }
    /* A store gets no fence-only path. The quiesce also drains the CCE
       ring, and an operation the guest queued before this store may not
       have reached the pending ranges yet, so a range test here could
       miss a conflict with it. */
    g->st_cpu_barrier_hits++;
    if (g->prof && g->st_cpu_barrier_hits <= 16)
        gpu_log("RAGE128 GPU: cpu barrier hit %llu: addr=%08x len=%u wr=%d\n",
                (unsigned long long) g->st_cpu_barrier_hits, addr, len, writes);
    /* Profile log of the first hits of each interval: the address next
       to the pending segment's ranges, the scanout base and the 2D
       destination, so an access that pays for a flush can be matched
       to a surface and a row */
    if (g->prof && g->pr_int_hitlog < 12) {
        g->pr_int_hitlog++;
        gpu_log("RAGE128 GPU: lfb hit: addr=%06x len=%u wr=%d pend=%d "
                "c=[%06x,%06x) z=[%06x,%06x) f=[%06x,%06x) crtc=%06x dst=%06x "
                "inflight=%u\n",
                addr, len, writes,
                gpu_pending_hit(g, addr, b, writes),
                atomic_load(&g->c_lo), atomic_load(&g->c_hi),
                atomic_load(&g->z_lo), atomic_load(&g->z_hi),
                atomic_load(&g->f_lo), atomic_load(&g->f_hi),
                dev->crtc_offset, dev->dst_offset, gpu_inflight_count(g));
    }
    {
        /* Timed even with profiling off, into lfbw_win_ns, the
           per-window aperture-wait total: the quiesce and flush here
           can stall the guest. The clock reads cost little, since this
           runs only on a range hit. */
        uint64_t t0 = prof_now();
        uint64_t t1 = t0;

        rage128_pm4_drain_wait(dev);
        if (g->prof) {
            t1 = prof_now();
            g->pr_cpu_quiesce_ns += t1 - t0;
        }
        /* Quiesced: this thread is the only submitter. The hit may have
           been the range of a slot that has finished, so reap first and
           flush only if a hit remains. */
        gpu_reap(g);
        if (gpu_range_hit(g, addr, b, writes))
            gpu_flush_cause(dev, GF_CPU);
        {
            uint64_t t2 = prof_now();

            dev->lfbw_win_ns += t2 - t0;
            if (g->prof) {
                dev->lfbt.hits++;
                dev->lfbt.quiesce_ns += t1 - t0;
                dev->lfbt.flush_ns += t2 - t1;
            }
        }
    }
}

/* Called on a CRTC_OFFSET write: every segment submitted so far, plus the
   pending one that submits next, belongs to the frame this flip shows.
   Records that as flip_seq. Either thread; off the CCE thread the caller
   has quiesced the CCE, so the pending flag and the sequence are
   stable. */
void
rage128_gpu_flip_mark(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint64_t    seq;

    if (!g)
        return;
    seq = atomic_load_explicit(&g->submit_seq, memory_order_relaxed);
    if (atomic_load_explicit(&g->pending, memory_order_acquire))
        seq++;
    atomic_store_explicit(&g->flip_seq, seq, memory_order_relaxed);
}

/* Is any unfinished GPU store over [lo,hi) part of a flipped frame, that
   is, queued at or before the CRTC_OFFSET write that put this buffer on
   display? Those stores must land before scanout reads the bytes, since
   the driver flips only to a finished frame. The pending segment takes
   submit_seq + 1 when it is submitted. An out-of-date read of either
   counter can only count newer work as flipped, which costs a wait,
   never a torn frame. */
static int
gpu_scan_flip_owned(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    uint64_t fseq = atomic_load_explicit(&g->flip_seq, memory_order_relaxed);

    if (gpu_pending_hit(g, lo, hi, 0)
        && atomic_load_explicit(&g->submit_seq, memory_order_relaxed) + 1
            <= fseq)
        return 1;
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        const gpu_slot_t *sl = &g->slot[i];

        if (!atomic_load_explicit(&sl->submitted, memory_order_acquire))
            continue;
        if (sl->seq > fseq
            || atomic_load_explicit(&sl->fence_seen, memory_order_acquire))
            continue;
        if (rng_hit(sl->c_lo, sl->c_hi, lo, hi)
            || rng_hit(sl->z_lo, sl->z_hi, lo, hi)
            || rng_hit(sl->f_lo, sl->f_hi, lo, hi))
            return 1;
    }
    return 0;
}

/* gpu_read_fence without the wait: has the fence of every submitted slot
   that stores inside [lo,hi) signaled? A signaled fence is recorded in
   fence_seen under the slot's fence mutex, as in the waiting path. Safe
   on any thread. */
static int
gpu_read_fence_poll(r128_gpu_t *g, uint32_t lo, uint32_t hi)
{
    int landed = 1;

    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        gpu_slot_t *sl = &g->slot[i];

        if (!atomic_load_explicit(&sl->submitted, memory_order_acquire))
            continue;
        if (!rng_hit(sl->c_lo, sl->c_hi, lo, hi)
            && !rng_hit(sl->z_lo, sl->z_hi, lo, hi)
            && !rng_hit(sl->f_lo, sl->f_hi, lo, hi))
            continue;
        if (atomic_load_explicit(&sl->fence_seen, memory_order_acquire))
            continue;
        if (vkGetFenceStatus(g->dev, sl->fence) != VK_SUCCESS) {
            landed = 0;
            continue;
        }
        pthread_mutex_lock(&sl->fence_mtx);
        if (atomic_load_explicit(&sl->submitted, memory_order_acquire)
            && vkGetFenceStatus(g->dev, sl->fence) == VK_SUCCESS)
            atomic_store_explicit(&sl->fence_seen, 1, memory_order_release);
        pthread_mutex_unlock(&sl->fence_mtx);
    }
    return landed;
}

/* Scanout is about to read one line [addr, addr+len). The CRTC shows
   whatever memory holds when it fetches: a frame the driver has flipped
   to is already complete, while a 2D operation into the buffer on
   display shows up partway through the frame. The GPU backend renders
   asynchronously, so the two cases are handled apart. Stores that
   belong to a flipped frame get the CPU barrier's full wait (a fence
   wait, or the drain while still unsubmitted). Any other store is
   never waited for: a pending segment is handed to Vulkan without a
   drain, since a 2D client that only writes MMIO never brings the CCE
   to idle and the picture must keep updating, and a submitted slot is
   only polled. Returns 0 when the line is scanned ahead of a store
   that has not landed; the caller marks it again so a later scan picks
   up the new bytes. CPU thread. */
int
rage128_gpu_scan_barrier(rage128_t *dev, uint32_t addr, uint32_t len)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint32_t    b = addr + len;

    if (!g->async || g->verify || rage128_on_cce_thread) {
        rage128_gpu_cpu_barrier(dev, addr, len, 0);
        return 1;
    }
    if (!gpu_range_hit(g, addr, b, 0))
        return 1;
    if (gpu_scan_flip_owned(g, addr, b)) {
        rage128_gpu_cpu_barrier(dev, addr, len, 0);
        return 1;
    }
    if (gpu_pending_hit(g, addr, b, 0) && rage128_pm4_active(dev)
        && !atomic_load(&dev->cce_hung)) {
        /* The CCE executor holds work, so it is the submitter: ask it to
           hand the pending segment over (rage128_gpu_scan_serve) and
           render this line from the bytes memory holds now. Submitting
           from here instead needs the executor quiesced first, and
           rage128_pm4_drain_wait returns only once the executor has run
           everything the guest has queued. With a client that keeps the
           ring full (a DRI swap blit into the displayed buffer, then the
           next frame) this thread would wait out a whole frame of CCE
           work on every such line. The executor answers between packets,
           between the packets of an indirect buffer, and while it waits
           for the rest of a packet; the wake event reaches it when it
           sleeps with ring work it cannot fetch yet. There is one wake
           per request, not per line. If the executor goes idle before
           it answers, the next line finds it idle and submits below. */
        if (!atomic_exchange_explicit(&g->scan_req, 1, memory_order_acq_rel))
            thread_set_event(dev->cce_wake_event);
        g->st_scan_defers++;
        g->st_scan_stale++;
        return 0;
    }
    if (gpu_pending_hit(g, addr, b, 0)) {
        /* The executor is idle, or hung and will never reach a packet
           boundary. The drain returns at once, this thread is then the
           only submitter, and this submit answers any request the
           executor left unserved. Timed into lfbw_win_ns like the store
           path, since the quiesce can stall the guest. */
        uint64_t t0 = prof_now();

        rage128_pm4_drain_wait(dev);
        gpu_reap(g);
        if (gpu_pending_hit(g, addr, b, 0))
            gpu_flush_cause(dev, GF_SCAN);
        atomic_store_explicit(&g->scan_req, 0, memory_order_relaxed);
        dev->lfbw_win_ns += prof_now() - t0;
        g->st_scan_kicks++;
    }
    if (gpu_read_fence_poll(g, addr, b))
        return 1;
    g->st_scan_stale++;
    return 0;
}

/* The executor's half of the scan request. Called from three points in
   vid_ati_rage128_pm4.c: the ring loop between packets, the indirect-
   buffer walk between its packets, and the wait for the next dword of a
   packet. At each the accumulation holds only whole operations: a
   type-3 packet's payload is collected before it dispatches, and a
   type-0 packet is a run of complete register writes, the same boundary
   an MMIO write sequence flushes at. This thread is the only submitter
   there, so the pending segment is handed over exactly as the emulation
   thread's direct submit would after a quiesce. The scanned line that
   asked keeps being re-rendered until the segment's slot signals. A
   request that finds nothing pending (a later flush already submitted
   it) is simply dropped. A no-op off the CCE thread, where the walk of
   a direct MMIO submit also passes through. */
void
rage128_gpu_scan_serve(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !rage128_on_cce_thread
        || !atomic_load_explicit(&g->scan_req, memory_order_relaxed))
        return;
    atomic_store_explicit(&g->scan_req, 0, memory_order_relaxed);
    if (!atomic_load_explicit(&g->pending, memory_order_acquire))
        return;
    gpu_flush_cause(dev, GF_SCAN);
    atomic_fetch_add_explicit(&g->st_scan_served, 1, memory_order_release);
}

/* Does GPU work, the pending segment or a submitted slot, store inside
   [lo, lo+len)? Called once per scanned frame at vsync: a 0 lets the
   whole frame scan without per-line read fences. CPU thread. */
int
rage128_gpu_scan_hazard(rage128_t *dev, uint32_t lo, uint32_t len)
{
    r128_gpu_t *g  = (r128_gpu_t *) dev->gpu;
    uint64_t    hi = (uint64_t) lo + len;

    if (!g || !len)
        return 0;
    if (hi > 0xffffffffull)
        hi = 0xffffffffull;
    /* No reap here: this runs on the vsync thread, and gpu_slot_retire
       may run only on the submit thread (two threads retiring at once
       would both reset a fence, and one would then wait forever). The
       range of a finished slot only costs fenced scan lines until the
       reap at the next submit, and fence_seen makes each of those one
       flag test. */
    return gpu_range_hit(g, lo, (uint32_t) hi, 0);
}

/* GUI_STAT.GUI_ACTIVE (RRG: GUI_STAT, p. 3-244 / PDF 262) with the GPU
   backend on: the engine reads as busy while a segment is pending or a
   submitted slot's fence has not signaled. A pending segment is
   submitted here without waiting for it. This poll is the driver
   waiting for its work to finish and nothing else would submit the
   segment, so answering busy without submitting would keep the guest
   polling forever. Called on the CPU thread with the CCE idle (the
   caller tested rage128_pm4_active), so after the drain wait this
   thread is the only submitter. No reap: retirement belongs to the
   submit side, and a signaled fence reads as idle without one. */
int
rage128_gpu_engine_busy(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g)
        return 0;
    if (atomic_load_explicit(&g->pending, memory_order_acquire)) {
        rage128_pm4_drain_wait(dev);
        gpu_flush_cause(dev, GF_IDLE);
        g->idle_busy  = 1;
        g->idle_polls = 0;
        return 1;
    }
    /* While the driver polls, a busy answer is reused and the fences are
       queried again only on every 32nd poll. An idle answer is never
       reused: it ends the driver's loop, so it always comes from a
       fresh query. */
    if (g->idle_busy && (++g->idle_polls & 31u))
        return 1;
    g->idle_busy = 0;
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        const gpu_slot_t *sl = &g->slot[i];

        if (!atomic_load_explicit(&sl->submitted, memory_order_acquire)
            || atomic_load_explicit(&sl->fence_seen, memory_order_acquire))
            continue;
        if (vkGetFenceStatus(g->dev, sl->fence) != VK_SUCCESS) {
            g->idle_busy = 1;
            break;
        }
    }
    return g->idle_busy;
}

/* A pace window (250 ms or more) has closed. Returns the window's
   read-fence wait in microseconds and adjusts depth_cap, the number of
   slots that may be submitted at once. When the wait is more than
   GPU_DEPTH_HI_X tenths of a percent of the window the cap halves; when
   it is less than GPU_DEPTH_LO_X the cap grows by one; in between it
   holds. The input is the wait the guest actually spent, so nothing has
   to guess whether the CPU or the GPU caused it. Runs on the CPU
   thread (the vsync callback), the thread that accumulates
   rf_win_ns. */
uint64_t
rage128_gpu_pace_window(rage128_t *dev, uint32_t wall_ms, unsigned *cap_out)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint64_t    rf_us;

    if (!g) {
        *cap_out = 0;
        return 0;
    }
    rf_us        = g->rf_win_ns / 1000;
    g->rf_win_ns = 0;
    if (g->async && !g->verify && wall_ms) {
        uint64_t wall_us = (uint64_t) wall_ms * 1000;
        unsigned cap     = atomic_load_explicit(&g->depth_cap,
                                                memory_order_relaxed);

        if (rf_us * 1000 > wall_us * (uint64_t) GPU_DEPTH_HI_X)
            cap = (cap > 1) ? cap / 2 : 1;
        else if (rf_us * 1000 < wall_us * (uint64_t) GPU_DEPTH_LO_X
                 && cap < GPU_RING_SLOTS)
            cap++;
        atomic_store_explicit(&g->depth_cap, cap, memory_order_relaxed);
    }
    *cap_out = atomic_load_explicit(&g->depth_cap, memory_order_relaxed);
    return rf_us;
}

/* Wait on the fences of the submitted slots whose ranges conflict with
   [lo,hi), and nothing else: no retire, no full drain. The test is
   gpu_read_fence's, plus the texture and transfer-read ranges when the
   caller will store (a store must not overtake a GPU read of the old
   bytes). The submitted recheck under fence_mtx covers a retire that
   resets the fence meanwhile, as in the read path. Submit thread only:
   there, ring order means every earlier guest operation is already in
   the pending ranges, which the caller has flushed on a conflict;
   another thread cannot know that without draining the ring. cause
   names the wait in the stats; GF_2D also feeds the per-operation 2D
   table. */
static void
gpu_fence_range(r128_gpu_t *g, uint32_t lo, uint32_t hi, int writes,
                int cause)
{
    uint64_t t0     = g->prof ? prof_now() : 0;
    int      waited = 0;

    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        gpu_slot_t *sl = &g->slot[i];

        if (!atomic_load_explicit(&sl->submitted, memory_order_acquire))
            continue;
        if (!rng_hit(sl->c_lo, sl->c_hi, lo, hi)
            && !rng_hit(sl->z_lo, sl->z_hi, lo, hi)
            && !gpu_slot_f_hit(g, sl, lo, hi)
            && !(writes
                 && (gpu_slot_texq_hit(g, sl, lo, hi)
                     || rng_hit(sl->r_lo, sl->r_hi, lo, hi))))
            continue;
        if (atomic_load_explicit(&sl->fence_seen, memory_order_acquire))
            continue;
        pthread_mutex_lock(&sl->fence_mtx);
        if (atomic_load_explicit(&sl->submitted, memory_order_acquire)) {
            for (unsigned tries = 0;;) {
                VkResult wr = vkWaitForFences(g->dev, 1, &sl->fence,
                                              VK_TRUE, 5000000000ull);
                if (wr != VK_TIMEOUT)
                    break;
                gpu_log("RAGE128 GPU: STUCK FENCE (2d) seq=%llu wait#%u "
                        "status=%d\n",
                        (unsigned long long) sl->seq, ++tries,
                        vkGetFenceStatus(g->dev, sl->fence));
            }
            atomic_store_explicit(&sl->fence_seen, 1, memory_order_release);
            waited = 1;
        }
        pthread_mutex_unlock(&sl->fence_mtx);
    }
    if (!waited)
        return;
    g->st_wait_drain[cause]++;
    if (g->prof) {
        uint64_t dt = prof_now() - t0;

        g->pr_wait_ns += dt;
        g->pr_wait_drain_ns += dt;
        g->pr_wait_drain_cause_ns[cause] += dt;
        if (cause == GF_2D && g->flush_2d_tag < GPU_2D_TAGS) {
            g->pr_2d_tag_ns[g->flush_2d_tag] += dt;
            g->pr_int_2d_tag_ns[g->flush_2d_tag] += dt;
            g->st_2d_waits[g->flush_2d_tag]++;
        }
    }
}

/* The wait of rage128_gpu_2d_barrier, for bytes [lo, hi). In async mode
   the pending segment can still hold spans and fills at this point,
   since 2D operations do not submit one by one. A conflict with it gets
   a submit that does not drain, and then only the conflicting slots'
   fences are waited on; earlier submits have usually signaled by then.
   Verify, synctel and synchronous modes use the draining flush. */
static void
gpu_2d_barrier_wait(rage128_t *dev, r128_gpu_t *g, uint32_t lo, uint32_t hi,
                    int writes)
{
    /* The exact texture list can be read here: this runs on the 2D
       executor (the ring thread, or the CPU thread after the drain in
       the register-triggered entry), never at the same time as a draw
       capture. */
    if (!gpu_range_hit_2d(g, lo, hi, writes))
        return;
    gpu_reap(g);
    if (!gpu_range_hit_2d(g, lo, hi, writes))
        return;
    if (g->async && !g->verify && !g->verify2d && !dev->synctel) {
        if (gpu_pending_hit_2d(g, lo, hi, writes))
            gpu_flush_cause(dev, GF_2DQ);
        gpu_fence_range(g, lo, hi, writes, GF_2D);
        return;
    }
    gpu_flush_cause(dev, GF_2D);
}

/* A 2D primitive is about to touch local bytes [lo, lo+len) on the CPU.
   Wait only if GPU work stores there: the pending segment, its queued
   fills, or a submitted slot. With writes set the primitive stores
   there, so queued texture reads conflict too. Called from the
   per-operation surface map, so this is where the exact texture list's
   verdicts are counted, once per operation. */
void
rage128_gpu_2d_barrier(rage128_t *dev, uint32_t lo, uint32_t len, int writes)
{
    r128_gpu_t *g  = (r128_gpu_t *) dev->gpu;
    uint64_t    hi = (uint64_t) lo + len;

    if (!g)
        return;
    /* Before any early return: a 2D store can hit cached staged texels
       even when it hits no range of queued GPU work. */
    if (writes)
        r128_texcache_dirty(dev, lo, len);
    if (hi > 0xffffffffull)
        hi = 0xffffffffull;
    gpu_texq_op_begin(g);
    gpu_2d_barrier_wait(dev, g, lo, (uint32_t) hi, writes);
    gpu_texq_op_end(g);
}

/* Queue a solid fill on the GPU instead of draining and filling on the
   CPU: rows rows of len bytes, pitch bytes apart, each pixel the low
   bpp bytes of color. Returns 1 when queued, and the caller must then
   skip its own store loop. Returns 0 when the fill does not qualify;
   the caller runs the CPU path, whose surface map waits through
   rage128_gpu_2d_barrier. With bpp 1, 2 or 4 and offset, pitch and
   length all multiples of 4 (what vkCmdFillBuffer requires), the rows
   are filled directly with the color repeated to a dword. Anything
   else (bpp 3, unaligned extents) stages one row of resolved bytes and
   copies it to every row with a source pitch of 0. Same single-
   submitter rule as the segment capture. route selects the stats
   bucket: 0 solid paint, 1 mono-expand run, 2 pattern run, 3 and up
   the CPU-resolved 2D families counted in st_q_* table route + 1. */
int
rage128_gpu_2d_fill(rage128_t *dev, uint32_t addr, uint32_t pitch,
                    uint32_t len, uint32_t rows, uint32_t color, int bpp,
                    int route)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    gpu_fill_t *f;
    uint64_t    end;
    uint32_t    fc   = color;
    uint32_t    soff = 0;
    int         direct;

    if (!g || g->verify)
        return 0;
    if (!len || !rows)
        return 1; /* empty rect: nothing to order */
    if (bpp == 1) {
        fc &= 0xffu;
        fc |= fc << 8;
        fc |= fc << 16;
    } else if (bpp == 2) {
        fc &= 0xffffu;
        fc |= fc << 16;
    }
    /* Rows that overlap (pitch < len) merge into one run. pitch is a
       whole number of pixels, so every row carries the same byte
       pattern at the same phase; their union is one contiguous run with
       that pattern, the same bytes the CPU walk leaves after writing
       the rows in order. (Age of Empires II issues its 8 bpp in-game
       fills with width = pitch + 1.) */
    if (rows > 1 && pitch < len) {
        len  = (rows - 1) * pitch + len;
        rows = 1;
    }
    /* vkCmdFillBuffer needs offset and size aligned to 4 and a pattern
       that repeats every dword, which 24 bpp never does; staged copies
       cover the rest. */
    direct = bpp != 3 && !((addr | pitch | len) & 3u);
    end    = (uint64_t) addr + (uint64_t) (rows - 1) * pitch + len;
    if (r128_card_is_agp(addr) || end > (uint64_t) dev->vram_size) {
        if (route == 1)
            g->st_mono_cpu++;
        else if (route == 2)
            g->st_pat_cpu++;
        else if (route >= 3)
            g->st_q_cpu[route + 1]++;
        else {
            g->st_fill_cpu++;
            g->st_fill_cpu_r[0]++;
        }
        return 0;
    }

    /* Piece list: one direct fill, or one staged copy. A single
       unaligned run of 8 bytes or more becomes a direct fill for the
       aligned middle plus staged copies for the edges (3 bytes or less
       each), so a long merged run never passes through the staging
       ring. At bpp 2 the middle must start on an even byte of the run
       to match the dword color; hn is even whenever addr is pixel
       aligned, so the test only catches a malformed odd addr. */
    struct fill_piece {
        int      kind;
        uint32_t addr, pitch, len, rows, src, spitch;
    } rec[3];
    int nrec = 0;

    if (direct) {
        rec[nrec++] = (struct fill_piece) { 0, addr, pitch, len, rows, 0, 0 };
    } else {
        uint32_t hn = (4u - (addr & 3u)) & 3u;

        if (rows == 1 && bpp != 3 && len >= 8 && !(bpp == 2 && (hn & 1u))) {
            uint32_t mid = (len - hn) & ~3u;
            uint32_t tn  = len - hn - mid;
            uint8_t *st  = NULL;

            if (hn + tn) {
                /* Reserve staging space before the hazard checks below:
                   a staging ring wrap flushes and drains everything, as
                   it does for a blit bounce. */
                st = (uint8_t *) rage128_gpu_2d_stage(dev, hn + tn, &soff);
                if (!st) {
                    if (route == 1)
                        g->st_mono_cpu++;
                    else if (route == 2)
                        g->st_pat_cpu++;
                    else if (route >= 3)
                        g->st_q_cpu[route + 1]++;
                    else {
                        g->st_fill_cpu++;
                        g->st_fill_cpu_r[1]++;
                    }
                    return 0;
                }
                for (uint32_t i = 0; i < hn; i++)
                    st[i] = (uint8_t) (color >> ((i % (uint32_t) bpp) * 8));
                for (uint32_t i = 0; i < tn; i++)
                    st[hn + i] = (uint8_t) (color
                                            >> (((hn + mid + i) % (uint32_t) bpp) * 8));
            }
            if (hn)
                rec[nrec++] = (struct fill_piece) { 1, addr, hn, hn, 1,
                                                    soff, 0 };
            rec[nrec++] = (struct fill_piece) { 0, addr + hn, mid, mid, 1,
                                                0, 0 };
            if (tn)
                rec[nrec++] = (struct fill_piece) { 1, addr + hn + mid, tn, tn,
                                                    1, soff + hn, 0 };
        } else {
            uint8_t *st = (uint8_t *) rage128_gpu_2d_stage(dev, len, &soff);

            if (!st) {
                if (route == 1)
                    g->st_mono_cpu++;
                else if (route == 2)
                    g->st_pat_cpu++;
                else if (route >= 3)
                    g->st_q_cpu[route + 1]++;
                else {
                    g->st_fill_cpu++;
                    g->st_fill_cpu_r[1]++;
                }
                return 0;
            }
            for (uint32_t i = 0; i < len; i++)
                st[i] = (uint8_t) (color >> ((i % (uint32_t) bpp) * 8));
            rec[nrec++] = (struct fill_piece) { 1, addr, pitch, len, rows,
                                                soff, 0 };
        }
    }

    /* Ordering: fills are recorded at the head of the next submit,
       before that submit's draws, so a pending draw that overlaps this
       range has to be submitted first; only a real byte overlap splits
       the segment. A queued mid fill on these bytes splits it too, since
       mid fills land after the draws. A full fill list is flushed the
       same way to make room. */
    if (gpu_mid_range_hit(g, addr, (uint32_t) end)) {
        g->st_copy_mid_split++;
        gpu_flush_cause(dev, GF_2DQ);
    }
    if (g->nfills > GPU_FILL_CAP - nrec
        || (g->nspans && gpu_pending_draw_hit(g, addr, (uint32_t) end, 1)))
        gpu_flush_cause(dev, GF_2DQ);

    for (int i = 0; i < nrec; i++) {
        f         = &g->fills[g->nfills];
        f->kind   = rec[i].kind;
        f->addr   = rec[i].addr;
        f->pitch  = rec[i].pitch;
        f->len    = rec[i].len;
        f->rows   = rec[i].rows;
        f->color  = fc;
        f->src    = rec[i].src;
        f->spitch = rec[i].spitch;
        f->bar    = gpu_fill_bar(g, f);
        f->mid    = -1;
        g->nfills++;
    }
    if (route == 1) {
        g->st_mono_runs++;
        g->st_mono_px += (uint64_t) rows * (len / (uint32_t) bpp);
    } else if (route == 2) {
        g->st_pat_runs++;
        g->st_pat_px += (uint64_t) rows * (len / (uint32_t) bpp);
    } else if (route >= 3) {
        g->st_q_runs[route + 1]++;
        g->st_q_bytes[route + 1] += (uint64_t) rows * len;
    } else {
        g->st_fill_rects++;
        g->st_fill_rows += rows;
        g->st_fill_px += (uint64_t) rows * (len / (uint32_t) bpp);
    }
    /* The queued store skips the CPU walk, so rage128_gpu_2d_barrier
       never sees it: drop cached staged texels over the range here. */
    r128_texcache_dirty(dev, addr, (uint32_t) (end - addr));
    gpu_f_add(g, addr, (uint32_t) end);
    atomic_store_explicit(&g->pending, 1, memory_order_release);

    if (g->verify2d) {
        uint64_t bad = 0;

        gpu_flush_cause(dev, GF_2DQ);
        gpu_drain_all(g, GF_2D);
        for (uint32_t r = 0; r < rows; r++) {
            const uint8_t *px = dev->svga.vram + addr + (uint64_t) r * pitch;

            for (uint32_t i = 0; i < len; i++)
                if (px[i] != (uint8_t) (color >> ((i % (uint32_t) bpp) * 8)))
                    bad++;
        }
        if (bad) {
            if (!g->st_fill_bad)
                gpu_log("RAGE128 GPU: 2d fill VERIFY MISMATCH: addr=%08x "
                        "pitch=%u len=%u rows=%u color=%08x bpp=%d bad=%llu\n",
                        addr, pitch, len, rows, color, bpp,
                        (unsigned long long) bad);
            g->st_fill_bad += bad;
        }
    }
    return 1;
}

/* Add one piece to a tiled fill's record list. Returns 0 when the list
   is full; the caller then sends the whole operation to the CPU walk
   rather than queue part of it. Queuing part would be correct, since a
   solid fill can be repeated, but the walk would then need its fence
   for the rest and nothing would be saved. */
static int
gpu_tfill_rec(gpu_fill_t *rec, uint32_t *n, uint64_t addr, uint32_t pitch,
              uint32_t len, uint32_t rows)
{
    if (*n == GPU_FILL_CAP)
        return 0;
    rec[*n].addr  = (uint32_t) addr;
    rec[*n].pitch = pitch;
    rec[*n].len   = len;
    rec[*n].rows  = rows;
    (*n)++;
    return 1;
}

/* Queue a solid fill over a tiled destination: byte columns [xb0, xb1)
   of linear rows [y0, y1] of the surface at tbase, with pitch tpitch (a
   whole number of 64-byte tile columns, r128_tiled_ok). The CPU walk
   stages a detiled window, stores the rows linearly and scatters the
   changed bytes back through r128_tile_off. This writes the same tiled
   bytes from the GPU queue. The walk gets its ordering from a fence on
   the submit thread (a depth clear must land after the submitted
   segment's z stores); here queue order gives it: the records sit at
   the head of the next submit, behind every submitted segment, and a
   pending draw on the range is submitted first, as for a linear fill.

   Layout (the transform in vid_ati_rage128.h, r128_tile_off): a tile
   is 64 bytes by 16 lines, stored contiguously, and a tile row holds
   the tiles of 16 lines side by side, 1 KB apart. Within one band of 16
   lines the rectangle's bytes are therefore: for each partial edge
   column, its lines at stride 64 (one record, pitch 64); for the run of
   full columns, whole 64-byte lines in tiles 1 KB apart (one record,
   pitch 1024, len = lines * 64, rows = columns). In a band whose 16
   lines all lie inside the rectangle, the full columns form one
   contiguous run of columns * 1 KB, so a run of such bands is one
   record at pitch 16 * tpitch, and a full-width rectangle of whole
   bands is a single vkCmdFillBuffer. The hazard range is the whole tile
   rows the rectangle touches: the bytes the transform can produce for
   rows [y0, y1], and the range the 2D barrier waited on for the walk.

   Returns 0 when the fill does not qualify; the caller walks it on the
   CPU and counts it in st_tiled_cpu. That is the case for a right edge
   past the pitch (the walk spills into the next row), bpp 3 (the color
   does not repeat every dword), a column edge not on a dword
   (vkCmdFillBuffer granularity; column starts are multiples of 64, so
   only the rectangle's own edges can be off), AGP memory or a range
   above VRAM, or more pieces than the fill list holds. */
int
rage128_gpu_2d_fill_tiled(rage128_t *dev, uint32_t tbase, uint32_t tpitch,
                          uint32_t xb0, uint32_t xb1, uint32_t y0,
                          uint32_t y1, uint32_t color, int bpp)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    gpu_fill_t  rec[GPU_FILL_CAP];
    uint32_t    nrec = 0;
    uint32_t    fc   = color;
    uint64_t    band, tlo, thi;
    uint32_t    c0, c1, cf0, cf1, t0, t1, ncol;
    /* partial edge columns: tile column index and the byte range [a, b)
       within the column that the rectangle covers */
    struct {
        uint32_t col, a, b;
    } part[2];
    uint32_t npart = 0;

    if (!g || g->verify)
        return 0;
    if (!r128_tiled_ok(1, tpitch) || xb0 >= xb1 || y0 > y1 || bpp == 3
        || xb1 > tpitch || ((xb0 | xb1) & 3u))
        return 0;
    band = 16ull * tpitch;
    t0   = y0 >> 4;
    t1   = y1 >> 4;
    tlo  = (uint64_t) tbase + band * t0;
    thi  = (uint64_t) tbase + band * (t1 + 1u);
    if (r128_card_is_agp(tbase) || thi > (uint64_t) dev->vram_size)
        return 0;
    if (bpp == 1) {
        fc &= 0xffu;
        fc |= fc << 8;
        fc |= fc << 16;
    } else if (bpp == 2) {
        fc &= 0xffffu;
        fc |= fc << 16;
    }

    /* An edge column is partial when the rectangle does not cover all
       64 of its bytes. [cf0, cf1) is the run of full columns between
       the edges (empty when the rectangle lies inside one partial
       column). */
    c0 = xb0 >> 6;
    c1 = (xb1 - 1u) >> 6;
    {
        uint32_t a0 = xb0 & 63u;
        uint32_t b1 = xb1 - (c1 << 6); /* 1..64 */

        cf0 = c0;
        cf1 = c1 + 1u;
        if (c0 == c1) {
            if (a0 || b1 != 64u) {
                part[npart].col = c0;
                part[npart].a   = a0;
                part[npart].b   = b1;
                npart++;
                cf1 = cf0;
            }
        } else {
            if (a0) {
                part[npart].col = c0;
                part[npart].a   = a0;
                part[npart].b   = 64u;
                npart++;
                cf0++;
            }
            if (b1 != 64u) {
                part[npart].col = c1;
                part[npart].a   = 0;
                part[npart].b   = b1;
                npart++;
                cf1--;
            }
        }
    }
    ncol = cf1 - cf0;

    /* partial edge columns: one record per band each */
    for (uint32_t t = t0; t <= t1; t++) {
        uint32_t l0 = t == t0 ? (y0 & 15u) : 0;
        uint32_t l1 = t == t1 ? (y1 & 15u) : 15u;

        for (uint32_t p = 0; p < npart; p++)
            if (!gpu_tfill_rec(rec, &nrec,
                               tlo + band * (t - t0) + (part[p].col << 10)
                                   + (l0 << 6) + part[p].a,
                               64u, part[p].b - part[p].a, l1 - l0 + 1u))
                return 0;
    }
    /* full columns: the line ranges in the first and last bands, then
       the run of bands whose 16 lines all lie inside the rectangle */
    if (ncol) {
        uint32_t l0 = y0 & 15u, l1 = y1 & 15u;
        int32_t  tf0 = (int32_t) t0 + (l0 != 0);
        int32_t  tf1 = (int32_t) t1 - (l1 != 15u);

        if (t0 == t1) {
            if (!gpu_tfill_rec(rec, &nrec, tlo + (cf0 << 10) + (l0 << 6),
                               1024u, (l1 - l0 + 1u) << 6, ncol))
                return 0;
        } else {
            if (l0 && !gpu_tfill_rec(rec, &nrec, tlo + (cf0 << 10) + (l0 << 6), 1024u, (16u - l0) << 6, ncol))
                return 0;
            if (l1 != 15u
                && !gpu_tfill_rec(rec, &nrec,
                                  tlo + band * (t1 - t0) + (cf0 << 10),
                                  1024u, (l1 + 1u) << 6, ncol))
                return 0;
            if (tf1 >= tf0
                && !gpu_tfill_rec(rec, &nrec,
                                  tlo + band * (uint32_t) (tf0 - (int32_t) t0)
                                      + (cf0 << 10),
                                  (uint32_t) band, ncol << 10,
                                  (uint32_t) (tf1 - tf0 + 1)))
                return 0;
        }
    }

    /* Ordering as in rage128_gpu_2d_fill: the records land at the head
       of the next submit, so a pending draw on the tile rows is
       submitted first. Every test here uses the whole tile rows as the
       range. */
    if (gpu_mid_range_hit(g, (uint32_t) tlo, (uint32_t) thi)) {
        g->st_copy_mid_split++;
        gpu_flush_cause(dev, GF_2DQ);
    }
    if (g->nfills > GPU_FILL_CAP - nrec
        || (g->nspans
            && gpu_pending_draw_hit(g, (uint32_t) tlo, (uint32_t) thi, 1)))
        gpu_flush_cause(dev, GF_2DQ);

    for (uint32_t i = 0; i < nrec; i++) {
        gpu_fill_t *f = &g->fills[g->nfills];

        f->kind   = 0;
        f->addr   = rec[i].addr;
        f->pitch  = rec[i].pitch;
        f->len    = rec[i].len;
        f->rows   = rec[i].rows;
        f->color  = fc;
        f->src    = 0;
        f->spitch = 0;
        f->bar    = gpu_fill_bar(g, f);
        f->mid    = -1;
        g->nfills++;
    }
    g->st_fill_rects++;
    g->st_fill_rows += y1 - y0 + 1u;
    g->st_fill_px += (uint64_t) (y1 - y0 + 1u) * ((xb1 - xb0) / (uint32_t) bpp);
    g->st_fill_tiled++;
    /* The queued store skips the CPU walk, so rage128_gpu_2d_barrier
       never sees it: drop cached staged texels over the whole tile rows
       here. */
    r128_texcache_dirty(dev, (uint32_t) tlo, (uint32_t) (thi - tlo));
    gpu_f_add(g, (uint32_t) tlo, (uint32_t) thi);
    atomic_store_explicit(&g->pending, 1, memory_order_release);

    if (g->verify2d) {
        /* The check uses the walk's addressing, not the piece list:
           every (row, byte) of the rectangle through the transform. */
        uint64_t bad = 0;

        gpu_flush_cause(dev, GF_2DQ);
        gpu_drain_all(g, GF_2D);
        for (uint32_t y = y0; y <= y1; y++)
            for (uint32_t xb = xb0; xb < xb1; xb++)
                if (dev->svga.vram[tbase + r128_tile_off(xb, y, tpitch)]
                    != (uint8_t) (color >> ((xb % (uint32_t) bpp) * 8)))
                    bad++;
        if (bad) {
            if (!g->st_fill_bad)
                gpu_log("RAGE128 GPU: 2d tiled fill VERIFY MISMATCH: "
                        "base=%08x pitch=%u xb=[%u,%u) y=[%u,%u] color=%08x "
                        "bpp=%d pieces=%u bad=%llu\n",
                        tbase, tpitch, xb0, xb1, y0, y1, color, bpp, nrec,
                        (unsigned long long) bad);
            g->st_fill_bad += bad;
        }
    }
    return 1;
}

/* Count one whole operation queued with a destination that is not
   32 bpp, so the stats show that path in use. table: 0 mono, 1 host
   color, 2 pattern, 3 solid fill. */
void
rage128_gpu_2d_nb(rage128_t *dev, int table)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (g)
        g->st_qnb[table]++;
}

/* Count a paint that was sent to the CPU before the fill queue's own
   tests ran. why bits: 1 ROP other than PATCOPY, 2 partial write mask,
   4 auxiliary scissor on, 8 pattern brush, 16 color compare active.
   Statistics only. */
void
rage128_gpu_2d_fill_skip(rage128_t *dev, int why, uint32_t rop,
                         uint32_t wmask)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint64_t    n = 0;

    if (!g || !why)
        return;
    for (int b = 0; b < 5; b++)
        if (why & (1 << b))
            n += g->st_fill_skip[b]++;
    if (g->prof && n < 8)
        gpu_log("RAGE128 GPU: 2d fill skip: why=%02x rop=%02x wmask=%08x\n",
                why, rop, wmask);
}

/* Count a mono, host-color, pattern or blit operation that failed the
   queue test and went to the CPU. table: 0 mono expand, 1 host color,
   2 pattern paint, 3 screen blit. why bits: 1 ROP the queue cannot do,
   2 partial write mask, 4 auxiliary scissor, 8 more staged or merged
   runs than the cap (a real pattern brush is counted in table 9
   instead), 16 color compare active, 32 address range (AGP, wrap,
   pitch or coordinates), 64 staging full. */
void
rage128_gpu_2d_qskip(rage128_t *dev, int table, int why)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !why)
        return;
    for (int b = 0; b < 7; b++)
        if (why & (1 << b))
            g->st_qskip[table][b]++;
}

/* Count a 2D operation sent to the CPU walk because a tile bit is set;
   the queue paths address rows linearly, so the refusal is required. */
void
rage128_gpu_2d_tiled_skip(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (g)
        g->st_tiled_cpu++;
}

void
rage128_gpu_2d_qdone(rage128_t *dev, int table)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (g)
        g->st_qdone[table]++;
}

void
rage128_gpu_2d_qaux(rage128_t *dev, int table)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (g)
        g->st_qaux[table]++;
}

int
rage128_gpu_2d_verifying(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    return g && g->verify2d;
}

void
rage128_gpu_2d_verify_bad(rage128_t *dev, int table, uint64_t bad)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !bad)
        return;
    if (!g->st_qbad[table])
        gpu_log("RAGE128 GPU: 2d op VERIFY MISMATCH: table=%d bad=%llu\n",
                table, (unsigned long long) bad);
    g->st_qbad[table] += bad;
}

/* Reserve `bytes` in the 2D staging ring for a queued copy's source
   pixels. Returns NULL when the ring is unavailable (no backend, verify
   replay, or the ring failed to allocate) or the request is larger than
   the ring; the caller then walks on the CPU. A wrap flushes and drains
   first, since every earlier reservation may still be read by a queued
   or submitted copy. Submit thread only. */
void *
rage128_gpu_2d_stage(rage128_t *dev, uint32_t bytes, uint32_t *off)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || g->verify || !g->stage2d_ok || !bytes)
        return NULL;
    bytes = (bytes + 3u) & ~3u;
    if (bytes > GPU_2DSTAGE_SIZE) {
        g->st_2ds_full++;
        return NULL;
    }
    if (g->stage2d_head + bytes > GPU_2DSTAGE_SIZE) {
        gpu_flush_cause(dev, GF_2DQ);
        gpu_drain_all(g, GF_STAGE);
        g->stage2d_head = 0;
        g->st_2ds_recycles++;
    }
    *off = g->stage2d_head;
    g->stage2d_head += bytes;
    g->st_2ds_bytes += bytes;
    return (uint8_t *) g->stage2d.map + *off;
}

/* Queue a copy of rows rows of len bytes from staging offset soff into
   VRAM, ordered like a queued fill. spitch is the source row stride; 0
   copies one staged row to every destination row. The caller has
   already written the source bytes. Queuing rules and return values are
   those of rage128_gpu_2d_fill. route selects the stats bucket: 0 host
   color, 1 mono opaque, 2 pattern, 3 and up the CPU-resolved 2D
   families. */
int
rage128_gpu_2d_copy(rage128_t *dev, uint32_t addr, uint32_t pitch,
                    uint32_t len, uint32_t rows, uint32_t soff,
                    uint32_t spitch, int route)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    gpu_fill_t *f;
    uint64_t    end;
    int32_t     mid;

    if (!g || g->verify || !g->stage2d_ok)
        return 0;
    if (!len || !rows)
        return 1;
    if (rows > 1 && (pitch < len || (spitch && spitch < len))) {
        if (route == 1)
            g->st_mono_cpu++;
        else if (route == 2)
            g->st_pat_cpu++;
        else if (route >= 3)
            g->st_q_cpu[route + 1]++;
        else
            g->st_copy_cpu++;
        return 0;
    }
    end = (uint64_t) addr + (uint64_t) (rows - 1) * pitch + len;
    if (r128_card_is_agp(addr) || end > (uint64_t) dev->vram_size
        || (uint64_t) soff + (uint64_t) (rows - 1) * spitch + len
            > GPU_2DSTAGE_SIZE) {
        if (route == 1)
            g->st_mono_cpu++;
        else if (route == 2)
            g->st_pat_cpu++;
        else if (route >= 3)
            g->st_q_cpu[route + 1]++;
        else
            g->st_copy_cpu++;
        return 0;
    }

    /* A copy into bytes that the pending draws only sample as texture
       (a lightmap or sprite upload between draws) is recorded in stream
       order instead of splitting the segment: a "mid" copy, recorded
       into the mirror after the runs queued so far and into the import
       after the mirror-back. If a pending draw stores to those bytes the
       segment still splits, because the import copy would land after
       that draw's mirror-back. A mid copy over an earlier mid copy is
       ordered by the copies' own barriers; a copy at the head of the
       submit over a mid copy is not, and splits. */
    mid = -1;
    if (g->nspans && gpu_pending_draw_hit(g, addr, (uint32_t) end, 1)
        && g->mirror_en && !g->verify2d && !dev->synctel
        && !gpu_pending_draw_hit(g, addr, (uint32_t) end, 0))
        mid = 0;
    if (mid < 0 && gpu_mid_range_hit(g, addr, (uint32_t) end)) {
        g->st_copy_mid_split++;
        gpu_flush_cause(dev, GF_2DQ);
    }
    if (g->nfills == GPU_FILL_CAP
        || (mid < 0 && g->nspans
            && gpu_pending_draw_hit(g, addr, (uint32_t) end, 1)))
        gpu_flush_cause(dev, GF_2DQ);
    if (mid >= 0 && !g->nspans)
        mid = -1; /* the capacity flush emptied the segment: no draws to follow */
    if (mid >= 0) {
        mid          = (int32_t) g->nruns;
        g->run_break = 1;
    }

    f         = &g->fills[g->nfills];
    f->kind   = 1;
    f->addr   = addr;
    f->pitch  = pitch;
    f->len    = len;
    f->rows   = rows;
    f->color  = 0;
    f->src    = soff;
    f->spitch = spitch;
    f->bar    = gpu_fill_bar(g, f);
    f->mid    = mid;
    g->nfills++;
    if (mid >= 0) {
        g->nmid++;
        g->st_copy_mid++;
    }
    if (route == 1) {
        g->st_mono_orects++;
        g->st_mono_obytes += (uint64_t) rows * len;
    } else if (route == 2) {
        g->st_pat_copies++;
        g->st_pat_px += (uint64_t) rows * (len >> 2);
    } else if (route >= 3) {
        g->st_q_runs[route + 1]++;
        g->st_q_bytes[route + 1] += (uint64_t) rows * len;
    } else {
        g->st_copy_rects++;
        g->st_copy_rows += rows;
        g->st_copy_bytes += (uint64_t) rows * len;
    }
    /* the queued store never passes rage128_gpu_2d_barrier */
    r128_texcache_dirty(dev, addr, (uint32_t) (end - addr));
    gpu_f_add(g, addr, (uint32_t) end);
    atomic_store_explicit(&g->pending, 1, memory_order_release);

    if (g->verify2d) {
        const uint8_t *sp  = (const uint8_t *) g->stage2d.map + soff;
        uint64_t       bad = 0;

        gpu_flush_cause(dev, GF_2DQ);
        gpu_drain_all(g, GF_2D);
        for (uint32_t r = 0; r < rows; r++)
            for (uint32_t i = 0; i < len; i++)
                if (dev->svga.vram[addr + (uint64_t) r * pitch + i]
                    != sp[(uint64_t) r * spitch + i])
                    bad++;
        if (bad) {
            if (!g->st_copy_bad)
                gpu_log("RAGE128 GPU: 2d copy VERIFY MISMATCH: addr=%08x "
                        "pitch=%u len=%u rows=%u soff=%u bad=%llu\n",
                        addr, pitch, len, rows, soff,
                        (unsigned long long) bad);
            g->st_copy_bad += bad;
        }
    }
    return 1;
}

/* Queue a general 2D read-modify-write: out = (d & A) | (~d & B) over
   rows rows of len destination bytes, pitch bytes apart. The A and B
   byte planes are already staged at aoff and boff, with row stride len;
   destination row r uses plane row r & amod (0: one row for every row,
   7: the 8-row pattern tile aligned to the screen, 0xffffffff: a
   separate row for each destination row). Any ROP3, the write mask and
   any destination bpp reduce to A and B, because the caller has already
   resolved the pattern and source operands. It runs as a compute
   dispatch (gpu_rmw.comp) inside the 2D transfer block, with the
   barriers between transfer and compute work set by the bar flag. The
   destination is both read and written, so it counts as both in the
   fill barrier test and in the f range. Otherwise as
   rage128_gpu_2d_fill; counted in st_q_* table 8. */
int
rage128_gpu_2d_rmw(rage128_t *dev, uint32_t addr, uint32_t pitch,
                   uint32_t len, uint32_t rows, uint32_t aoff,
                   uint32_t boff, uint32_t amod)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    gpu_fill_t *f;
    uint64_t    end, wend;
    uint32_t    span  = ((addr & 3u) + len + 3u) & ~3u;
    uint32_t    prows = (amod == 0xffffffffu) ? rows
                                              : (amod + 1 < rows ? amod + 1 : rows);

    if (!g || g->verify || !g->stage2d_ok)
        return 0;
    if (!len || !rows)
        return 1;
    /* One kernel thread owns one destination word, so two rows sharing
       a word at their boundary would race. A multi-row operation needs
       a word-aligned pitch at least as long as the row's word span. 2D
       pitches always are: DST_PITCH counts units of 8 pixels, or of
       8 bytes at 24 bpp (RRG: DST_PITCH, p. 3-137 / PDF 155). */
    if (rows > 1 && ((pitch & 3u) || pitch < span)) {
        g->st_q_cpu[8]++;
        return 0;
    }
    end  = (uint64_t) addr + (uint64_t) (rows - 1) * pitch + len;
    wend = (end + 3u) & ~3ull;
    if (r128_card_is_agp(addr) || wend > (uint64_t) dev->vram_size
        || (uint64_t) aoff + (uint64_t) prows * len > GPU_2DSTAGE_SIZE
        || (uint64_t) boff + (uint64_t) prows * len > GPU_2DSTAGE_SIZE) {
        g->st_q_cpu[8]++;
        return 0;
    }

    if (gpu_mid_range_hit(g, addr & ~3u, (uint32_t) wend)) {
        g->st_copy_mid_split++;
        gpu_flush_cause(dev, GF_2DQ);
    }
    if (g->nfills == GPU_FILL_CAP
        || (g->nspans && gpu_pending_draw_hit(g, addr, (uint32_t) wend, 1)))
        gpu_flush_cause(dev, GF_2DQ);

    f         = &g->fills[g->nfills];
    f->kind   = 4;
    f->addr   = addr;
    f->pitch  = pitch;
    f->len    = len;
    f->rows   = rows;
    f->color  = amod;
    f->src    = aoff;
    f->spitch = boff;
    f->bar    = gpu_fill_bar(g, f);
    f->mid    = -1;
    g->nfills++;
    g->st_q_runs[8]++;
    g->st_q_bytes[8] += (uint64_t) rows * len;
    /* the queued store never passes rage128_gpu_2d_barrier */
    r128_texcache_dirty(dev, addr & ~3u, (uint32_t) (wend - (addr & ~3u)));
    gpu_f_add(g, addr & ~3u, (uint32_t) wend);
    atomic_store_explicit(&g->pending, 1, memory_order_release);

    if (g->verify2d) {
        /* No byte check here: the result depends on the old
           destination bytes, so only the 2D verify comparison against
           the CPU walk can judge it. Drain so that comparison reads the
           bytes this operation wrote. */
        gpu_flush_cause(dev, GF_2DQ);
        gpu_drain_all(g, GF_2D);
    }
    return 1;
}

/* Queue a VRAM-to-VRAM screen blit of rows rows of len bytes
   (destination pitch bytes apart, source spitch bytes apart) instead of
   draining and copying on the CPU. Only a plain source copy qualifies:
   the bytes must equal the CPU walk's stores exactly, so the caller has
   already reduced the operation to a raw byte copy. Disjoint source and
   destination ranges record one direct copy. Overlapping rectangles (a
   scroll within one surface) go through the staging ring in two
   copies, since vkCmdCopyBuffer does not allow source and destination
   regions to overlap within one command. The source range is added to
   the r range (queued transfer reads), not to q, so a later CPU store
   into it waits without widening the texture range. Return values as
   in rage128_gpu_2d_fill. */
int
rage128_gpu_2d_blit(rage128_t *dev, uint32_t addr, uint32_t pitch,
                    uint32_t src, uint32_t spitch, uint32_t len,
                    uint32_t rows)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    gpu_fill_t *f;
    uint64_t    dend, send;
    uint32_t    soff = 0;
    int         overlap;

    if (!g || g->verify)
        return 0;
    if (!len || !rows)
        return 1;
    if (rows > 1 && (pitch < len || spitch < len)) {
        g->st_blit_cpu++;
        return 0;
    }
    dend = (uint64_t) addr + (uint64_t) (rows - 1) * pitch + len;
    send = (uint64_t) src + (uint64_t) (rows - 1) * spitch + len;
    if (r128_card_is_agp(addr) || r128_card_is_agp(src)
        || dend > (uint64_t) dev->vram_size
        || send > (uint64_t) dev->vram_size) {
        g->st_blit_cpu++;
        return 0;
    }

    overlap = (uint64_t) addr < send && dend > (uint64_t) src;
    if (overlap) {
        /* Reserve the bounce space first: a ring wrap flushes and
           drains everything, so it has to come before the pending
           hazard checks. Only the GPU writes this reservation; the host
           never touches it. */
        if (!g->stage2d_ok
            || !rage128_gpu_2d_stage(dev, rows * len, &soff)) {
            rage128_gpu_2d_qskip(dev, 3, 64);
            g->st_blit_cpu++;
            return 0;
        }
    }

    /* Ordering against pending draws: the copy is recorded at the head
       of the next submit, before those draws run, so a draw that writes
       either range, or reads the destination, must be submitted first.
       A bounce takes two list entries, hence GPU_FILL_CAP - 1. */
    if (gpu_mid_range_hit(g, addr, (uint32_t) dend)
        || gpu_mid_range_hit(g, src, (uint32_t) send)) {
        g->st_copy_mid_split++;
        gpu_flush_cause(dev, GF_2DQ);
    }
    if (g->nfills >= GPU_FILL_CAP - 1
        || (g->nspans
            && (gpu_pending_draw_hit(g, addr, (uint32_t) dend, 1)
                || gpu_pending_draw_hit(g, src, (uint32_t) send, 0))))
        gpu_flush_cause(dev, GF_2DQ);

    if (!overlap) {
        f         = &g->fills[g->nfills];
        f->kind   = 2;
        f->addr   = addr;
        f->pitch  = pitch;
        f->len    = len;
        f->rows   = rows;
        f->color  = 0;
        f->src    = src;
        f->spitch = spitch;
        f->bar    = gpu_fill_bar(g, f);
        f->mid    = -1;
        g->nfills++;
    } else {
        /* first copy: VRAM source -> staging, rows packed at stride len */
        f         = &g->fills[g->nfills];
        f->kind   = 3;
        f->addr   = soff;
        f->pitch  = len;
        f->len    = len;
        f->rows   = rows;
        f->color  = 0;
        f->src    = src;
        f->spitch = spitch;
        f->bar    = gpu_fill_bar(g, f);
        f->mid    = -1;
        g->nfills++;
        /* second copy: staging -> VRAM destination. bar is forced on:
           this copy reads the staging bytes the first one wrote, and
           the VRAM range test cannot see that dependency. */
        f         = &g->fills[g->nfills];
        f->kind   = 1;
        f->addr   = addr;
        f->pitch  = pitch;
        f->len    = len;
        f->rows   = rows;
        f->color  = 0;
        f->src    = soff;
        f->spitch = len;
        f->bar    = 1;
        f->mid    = -1;
        g->nfills++;
        g->st_blit_bounce++;
    }
    g->st_blit_rects++;
    g->st_blit_rows += rows;
    g->st_blit_bytes += (uint64_t) rows * len;
    /* the queued store never passes rage128_gpu_2d_barrier */
    r128_texcache_dirty(dev, addr, (uint32_t) (dend - addr));
    gpu_f_add(g, addr, (uint32_t) dend);
    rng_ext(&g->r_lo, &g->r_hi, src, (uint32_t) send);
    atomic_store_explicit(&g->pending, 1, memory_order_release);

    if (g->verify2d) {
        uint8_t *exp = (uint8_t *) malloc((size_t) rows * len);
        uint64_t bad = 0;

        /* The expected bytes are the source as it was before the blit,
           and the copy is already queued, so they are taken after the
           drain from wherever they still survive. For disjoint
           rectangles the blit left the source unchanged, so the source
           itself is the reference. For overlapping ones the blit may
           have overwritten the source, and the staging bounce holds the
           copy of it taken before the destination was written. */
        gpu_flush_cause(dev, GF_2DQ);
        gpu_drain_all(g, GF_2D);
        if (exp) {
            for (uint32_t r = 0; r < rows; r++)
                memcpy(exp + (size_t) r * len,
                       overlap ? (uint8_t *) g->stage2d.map + soff
                               + (size_t) r * len
                               : dev->svga.vram + src + (uint64_t) r * spitch,
                       len);
            for (uint32_t r = 0; r < rows; r++)
                for (uint32_t i = 0; i < len; i++)
                    if (dev->svga.vram[addr + (uint64_t) r * pitch + i]
                        != exp[(size_t) r * len + i])
                        bad++;
            free(exp);
        }
        if (bad) {
            if (!g->st_blit_bad)
                gpu_log("RAGE128 GPU: 2d blit VERIFY MISMATCH: addr=%08x "
                        "src=%08x pitch=%u spitch=%u len=%u rows=%u "
                        "ovl=%d bad=%llu\n",
                        addr, src, pitch, spitch,
                        len, rows, overlap, (unsigned long long) bad);
            g->st_blit_bad += bad;
        }
    }
    return 1;
}

/* Scanout base latch (present). This only submits: the flipped buffer's
   draws must be submitted before scanout can see the new base, but
   nothing here waits for them. The CRTC does not wait either, and every
   line the renderer reads passes a range-gated barrier
   (rage128_gpu_scan_barrier, called from rage128_scan_remap).
   Off the CCE thread this does nothing in async mode: submitting from
   here would need a full CCE quiesce, and the same per-line barrier
   covers whatever is still pending. With verify on or async off, the
   CCE is drained and the segment flushed, waiting for it. */
void
rage128_gpu_present(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!gpu_has_work(g))
        return;
    if (!rage128_on_cce_thread) {
        if (g->async && !g->verify)
            return;
        rage128_pm4_drain_wait(dev);
    }
    gpu_flush_cause(dev, GF_PRESENT);
}

/* Wait, here on the CCE thread, for the submitted draws of the frame
   just flipped to that store inside its scan range. The scan thread's
   first lines then find the buffer complete instead of each waiting on
   a read fence. The pixels are the same either way; only the thread
   that waits changes, and the flip still publishes right after. Submit
   thread only, as gpu_fence_range requires. */
void
rage128_gpu_present_fence(rage128_t *dev, uint32_t src, uint32_t len)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !g->async || g->verify || !len)
        return;
    src &= dev->vram_mask;
    if (g->prof) {
        uint64_t t0 = prof_now();

        gpu_fence_range(g, src, src + len, 0, GF_PRESENT);
        dev->ftl_fence_ns = prof_now() - t0;
    } else
        gpu_fence_range(g, src, src + len, 0, GF_PRESENT);
}

void
rage128_gpu_ftl_flip(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint64_t    now;

    if (!g || !g->prof)
        return;
    now = prof_now();
    if (dev->ftl_flip_ns) {
        uint64_t t0     = dev->ftl_flip_ns;
        uint64_t period = now - t0;
        uint64_t latch  = atomic_load(&dev->ftl_latch_ns);

        g->pr_ftl_n++;
        g->pr_ftl_period += period;
        g->pr_ftl_fence += dev->ftl_fence_ns;
        g->pr_ftl_idle += dev->ftl_idle_ns;
        if (period > g->pr_ftl_max_period)
            g->pr_ftl_max_period = period;
        if (dev->ftl_fence_ns > g->pr_ftl_max_fence)
            g->pr_ftl_max_fence = dev->ftl_fence_ns;
        if (dev->ftl_idle_ns > g->pr_ftl_max_idle)
            g->pr_ftl_max_idle = dev->ftl_idle_ns;
        if (latch >= t0)
            g->pr_ftl_latch += latch - t0;
        else
            g->pr_ftl_nolatch++;
        if (dev->ftl_draw_ns >= t0) {
            uint64_t d = dev->ftl_draw_ns - t0;

            g->pr_ftl_draw += d;
            if (d > g->pr_ftl_max_draw)
                g->pr_ftl_max_draw = d;
        } else
            g->pr_ftl_nodraw++;
    }
    dev->ftl_flip_ns  = now;
    dev->ftl_fence_ns = 0;
    dev->ftl_idle_ns  = 0;
    dev->ftl_draw_ns  = 0;
    dev->ftl_open     = 1;
    atomic_store(&dev->ftl_latch_ns, 0);
    atomic_store(&dev->ftl_gap, 1);
}

/* ------------------------------------------------------------------ */
/* OV0 overlay compose.                                                */
/* ------------------------------------------------------------------ */

static int gpu_mk_buf(r128_gpu_t *g, r128_gpu_buf_t *b, VkDeviceSize sz,
                      VkBufferUsageFlags usage);

int
rage128_gpu_ov0_verifying(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    return g && g->verify;
}

void
rage128_gpu_ov0_bad(rage128_t *dev, uint64_t bad)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !bad)
        return;
    if (!g->st_ov0_bad)
        gpu_log("RAGE128 GPU: OV0 VERIFY MISMATCH (first batch: %llu px)\n",
                (unsigned long long) bad);
    g->st_ov0_bad += bad;
}

const uint32_t *
rage128_gpu_ov0_row(rage128_t *dev, uint32_t row)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !g->ov0_valid || row >= g->ov0_h)
        return NULL;
    return (const uint32_t *) g->ov0_out.map + (size_t) row * g->ov0_w;
}

/* Dispatch the overlay compose (gpu_ov0.comp) for the whole overlay
   window and wait for its fence, so the mapped output rows can be read
   by the host on return. Called on the vsync thread by the per-scanline
   compositor, normally once per frame. The caller has already run the
   read barrier over the source bytes. Ordering against GPU stores
   already submitted comes from submission order and the memory
   barriers in this command buffer. */
int
rage128_gpu_ov0_compose(rage128_t *dev, const r128_ov0_frame_t *f)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint32_t    need;

    if (!g || !g->ov0_ok)
        return 0;
    g->ov0_valid = 0;
    if (!f->w || !f->h || f->w > 4096 || f->h > 4096) {
        g->st_ov0_cpu++;
        return 0;
    }
    need = f->w * f->h * 4;
    if (need > g->ov0_cap) {
        uint32_t cap = g->ov0_cap ? g->ov0_cap : (1u << 22);

        while (cap < need)
            cap <<= 1;
        /* the fence is created signaled and signals again after every
           submit, so once this wait returns the old buffer is idle */
        vkWaitForFences(g->dev, 1, &g->ov0_fence, VK_TRUE, 5000000000ull);
        if (g->ov0_out.b) {
            vkDestroyBuffer(g->dev, g->ov0_out.b, NULL);
            vkFreeMemory(g->dev, g->ov0_out.m, NULL);
            memset(&g->ov0_out, 0, sizeof(g->ov0_out));
        }
        g->ov0_cap = 0;
        if (!gpu_mk_buf(g, &g->ov0_out, cap,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
            gpu_log("RAGE128 GPU: OV0 out buffer alloc failed (%u bytes), "
                    "overlay stays on the CPU compositor\n",
                    cap);
            g->ov0_ok = 0;
            g->st_ov0_cpu++;
            return 0;
        }
        g->ov0_cap = cap;
        {
            VkDescriptorBufferInfo bi = { g->ov0_out.b, 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet   wr;

            memset(&wr, 0, sizeof(wr));
            wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr.dstSet          = g->ov0_dset;
            wr.dstBinding      = 1;
            wr.descriptorCount = 1;
            wr.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wr.pBufferInfo     = &bi;
            vkUpdateDescriptorSets(g->dev, 1, &wr, 0, NULL);
        }
    }
    {
        VkCommandBufferBeginInfo bi = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        VkSubmitInfo    si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkResult        vr;

        vkResetFences(g->dev, 1, &g->ov0_fence);
        vkBeginCommandBuffer(g->ov0_cb, &bi);
        /* read after write: stores by earlier submits into the source
           buffers must be visible to this dispatch, which runs after
           them in queue order */
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
            | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(g->ov0_cb,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                 | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
        vkCmdBindPipeline(g->ov0_cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                          g->ov0_pipe);
        vkCmdBindDescriptorSets(g->ov0_cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                g->ov0_plyt, 0, 1, &g->ov0_dset, 0, NULL);
        vkCmdPushConstants(g->ov0_cb, g->ov0_plyt,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           (uint32_t) sizeof(*f), f);
        vkCmdDispatch(g->ov0_cb, (f->w + 63u) / 64u, f->h, 1);
        /* write after read: a later submit's store into the source
           range must not start while this dispatch still reads it; a
           later submit does not always begin with a barrier of its
           own */
        mb.srcAccessMask = 0;
        mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT
            | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(g->ov0_cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                 | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
        vkEndCommandBuffer(g->ov0_cb);
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &g->ov0_cb;
        pthread_mutex_lock(&g->queue_mtx);
        vr = vkQueueSubmit(g->queue, 1, &si, g->ov0_fence);
        pthread_mutex_unlock(&g->queue_mtx);
        if (vr != VK_SUCCESS)
            fatal("RAGE128 GPU: OV0 submit failed (%d)\n", vr);
        for (unsigned tries = 0;;) {
            VkResult wr = vkWaitForFences(g->dev, 1, &g->ov0_fence, VK_TRUE,
                                          5000000000ull);

            if (wr != VK_TIMEOUT)
                break;
            gpu_log("RAGE128 GPU: STUCK FENCE (ov0) wait#%u status=%d\n",
                    ++tries, vkGetFenceStatus(g->dev, g->ov0_fence));
        }
    }
    g->ov0_w     = f->w;
    g->ov0_h     = f->h;
    g->ov0_valid = 1;
    g->st_ov0_frames++;
    g->st_ov0_px += (uint64_t) f->w * f->h;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Bring-up / teardown.                                                */
/* ------------------------------------------------------------------ */

static int
gpu_mk_buf(r128_gpu_t *g, r128_gpu_buf_t *b, VkDeviceSize sz,
           VkBufferUsageFlags usage)
{
    VkBufferCreateInfo               bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements             mr;
    VkPhysicalDeviceMemoryProperties mp;
    VkMemoryAllocateInfo             mai  = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    uint32_t                         mti  = ~0u;
    const VkMemoryPropertyFlags      want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    bci.size  = sz;
    bci.usage = usage;
    if (vkCreateBuffer(g->dev, &bci, NULL, &b->b) != VK_SUCCESS)
        return 0;
    vkGetBufferMemoryRequirements(g->dev, b->b, &mr);
    vkGetPhysicalDeviceMemoryProperties(g->phys, &mp);
    /* The sort reads spans back through the map. On a discrete card the
       first host-visible memory type is usually write-combined, where
       every CPU read crosses the bus, so a host-cached type is taken
       whenever the device offers one. */
    for (int pass = 0; pass < 2 && mti == ~0u; pass++) {
        VkMemoryPropertyFlags need = want
            | (pass == 0 ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0);

        for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
            if ((mr.memoryTypeBits & (1u << i))
                && (mp.memoryTypes[i].propertyFlags & need) == need) {
                mti = i;
                break;
            }
    }
    if (mti == ~0u)
        return 0;
    if (!(g->mkbuf_logged & (1u << mti))) {
        VkMemoryPropertyFlags f = mp.memoryTypes[mti].propertyFlags;

        g->mkbuf_logged |= 1u << mti;
        gpu_log("RAGE128 GPU: host buffer memory type %u flags 0x%x -- %s\n",
                mti, (unsigned) f,
                (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? "cached" : "UNCACHED");
    }
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = mti;
    if (vkAllocateMemory(g->dev, &mai, NULL, &b->m) != VK_SUCCESS)
        return 0;
    if (vkBindBufferMemory(g->dev, b->b, b->m, 0) != VK_SUCCESS)
        return 0;
    if (vkMapMemory(g->dev, b->m, 0, VK_WHOLE_SIZE, 0, &b->map) != VK_SUCCESS)
        return 0;
    return 1;
}

/* Import a 16 KB aligned host block as a storage buffer without a copy
   (VK_EXT_external_memory_host). bsz is the size of the buffer, asz the
   size of the imported memory; asz must be a multiple of the import
   alignment, and the block must really cover [ptr, ptr+asz). Returns
   NULL on success, else the reason for the failure, with nothing left
   allocated. */
static const char *
gpu_import_host(r128_gpu_t *g, void *ptr, VkDeviceSize bsz, VkDeviceSize asz,
                VkBuffer *buf, VkDeviceMemory *mem)
{
    PFN_vkGetMemoryHostPointerPropertiesEXT gmhpp = (PFN_vkGetMemoryHostPointerPropertiesEXT)
        vkGetDeviceProcAddr(g->dev, "vkGetMemoryHostPointerPropertiesEXT");
    VkMemoryHostPointerPropertiesEXT hpp = {
        VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT
    };
    VkExternalMemoryBufferCreateInfo embci = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO
    };
    VkBufferCreateInfo               bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements             mr;
    VkImportMemoryHostPointerInfoEXT imp = {
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT
    };
    VkMemoryAllocateInfo             mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    uint32_t                         mti = ~0u;
    VkPhysicalDeviceMemoryProperties mp;

    if (!gmhpp
        || gmhpp(g->dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                 ptr, &hpp)
            != VK_SUCCESS)
        return "host pointer import query failed";
    embci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    bci.pNext         = &embci;
    bci.size          = bsz;
    bci.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT  /* 2D fills/copies in */
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT; /* present snapshot copies out */
    if (vkCreateBuffer(g->dev, &bci, NULL, buf) != VK_SUCCESS)
        return "buffer create failed";
    vkGetBufferMemoryRequirements(g->dev, *buf, &mr);
    vkGetPhysicalDeviceMemoryProperties(g->phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((hpp.memoryTypeBits & mr.memoryTypeBits & (1u << i))
            && (mp.memoryTypes[i].propertyFlags
                & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                   | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            mti = i;
            break;
        }
    if (mti == ~0u) {
        vkDestroyBuffer(g->dev, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return "no coherent import memory type";
    }
    imp.handleType      = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    imp.pHostPointer    = ptr;
    mai.pNext           = &imp;
    mai.allocationSize  = asz;
    mai.memoryTypeIndex = mti;
    if (vkAllocateMemory(g->dev, &mai, NULL, mem) != VK_SUCCESS) {
        vkDestroyBuffer(g->dev, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return "import allocation failed";
    }
    if (vkBindBufferMemory(g->dev, *buf, *mem, 0) != VK_SUCCESS) {
        vkFreeMemory(g->dev, *mem, NULL);
        vkDestroyBuffer(g->dev, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        *mem = VK_NULL_HANDLE;
        return "bind failed";
    }
    return NULL;
}

/* clang-format off */
#define GPU_STAGE_CAP0 (1u << 25) /* initial arena import, 32 MB: the
    arena is allocated in order across draws and every wrap drains the
    ring, so a larger arena means fewer drains */
/* clang-format on */

/* Rewrite the stage-arena binding (7) of every ring-slot descriptor
   set. Caller guarantees no slot is in flight. */
static void
gpu_stage_rebind(r128_gpu_t *g)
{
    for (unsigned s = 0; s < GPU_RING_SLOTS; s++) {
        VkDescriptorBufferInfo bi = { g->stage_buf, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet   wr;

        memset(&wr, 0, sizeof(wr));
        wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet          = g->slot[s].dset;
        wr.dstBinding      = 7;
        wr.descriptorCount = 1;
        wr.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wr.pBufferInfo     = &bi;
        vkUpdateDescriptorSets(g->dev, 1, &wr, 0, NULL);
    }
}

/* Move the tex_stage arena to a new 16 KB aligned block of `cap` bytes
   and import that block. On success the old arena (imported or plain
   heap) is freed and ts->arena and ts->cap describe the new block; on
   failure nothing changes. The caller guarantees that no GPU work
   refers to the arena (at init, or after a drain). */
static const char *
gpu_stage_import(rage128_t *dev, r128_gpu_t *g, uint32_t cap)
{
    struct rage128_tex_stage *ts = &dev->tex_stage;
    uint8_t                  *nb = NULL;
    VkBuffer                  buf;
    VkDeviceMemory            mem;
    const char               *why;

    if (r128_aligned_alloc((void **) &nb, 16384, (size_t) cap + 16384))
        return "stage arena allocation failed";
    if (ts->used)
        memcpy(nb, ts->arena, ts->used);
    memset(nb + ts->used, 0, (size_t) cap + 16384 - ts->used);
    /* 4-byte guard: a 32-bit texel read at a masked address may run up
       to 3 bytes past the last staged byte, as with the VRAM import */
    why = gpu_import_host(g, nb, (VkDeviceSize) cap + 4,
                          (VkDeviceSize) cap + 16384, &buf, &mem);
    if (why) {
        r128_aligned_free(nb);
        return why;
    }
    if (g->stage_ok) {
        vkDestroyBuffer(g->dev, g->stage_buf, NULL);
        vkFreeMemory(g->dev, g->stage_mem, NULL);
    }
    /* the old block came from r128_aligned_alloc only if it was itself
       imported; before the first import it is the CPU lane's arena from
       plain realloc */
    if (g->stage_ok)
        r128_aligned_free(ts->arena);
    else
        free(ts->arena);
    ts->arena    = nb;
    ts->cap      = cap;
    g->stage_buf = buf;
    g->stage_mem = mem;
    g->stage_ok  = 1;
    return NULL;
}

/* True when no pending or submitted segment samples the stage arena, so
   the caller may reuse it without a fence. Unlocked reads of stage_ref
   are safe: only this (submit) thread sets it, and a 1 read just before
   another thread retires the slot only delays the reuse. */
int
rage128_gpu_stage_idle(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !g->stage_ok)
        return 1;
    if (g->seg_staged)
        return 0;
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++)
        if (g->slot[i].stage_ref)
            return 0;
    return 1;
}

void
rage128_gpu_stage_quiesce(rage128_t *dev, int per_draw)
{
    r128_gpu_t *g     = (r128_gpu_t *) dev->gpu;
    int         cause = per_draw ? GF_STAGE_DRAW : GF_STAGE;

    if (!g || !g->stage_ok)
        return;
    if (g->seg_staged)
        gpu_flush_cause(dev, cause);
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++)
        if (g->slot[i].stage_ref)
            gpu_slot_retire(g, &g->slot[i], cause);
}

int
rage128_gpu_stage_grow(rage128_t *dev, uint32_t need)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint32_t    ncap;

    if (!g || !g->stage_ok)
        return -1;
    ncap = dev->tex_stage.cap ? dev->tex_stage.cap : GPU_STAGE_CAP0;
    while (ncap < need)
        ncap <<= 1;
    /* Every ring slot's descriptor set names the old buffer, so no work
       at all may be pending or submitted across the new import, not
       only the slots that sample staged levels. */
    gpu_flush_cause(dev, GF_STAGE);
    gpu_drain_all(g, GF_STAGE);
    if (gpu_stage_import(dev, g, ncap))
        return 0;
    gpu_stage_rebind(g);
    return 1;
}

/* ---- Color and z render targets staged from AGP memory: the two
   span-stage arenas, moved to importable blocks and bound at 9 to 12
   (a 16-bit view and a word view of each, like the VRAM pair). The
   lifetime rules are those of the texture arena above: quiesce before
   the arena is written back to guest memory; to grow, drain, import a
   new block and rebind. */

#    define GPU_CZSTAGE_CAP0 (1u << 21) /* initial import per arena: 2MB */

static void
gpu_czstage_rebind(r128_gpu_t *g)
{
    for (unsigned s = 0; s < GPU_RING_SLOTS; s++) {
        VkDescriptorBufferInfo bi[4] = {
            { g->cstage_buf, 0, VK_WHOLE_SIZE },
            { g->cstage_buf, 0, VK_WHOLE_SIZE },
            { g->zstage_buf, 0, VK_WHOLE_SIZE },
            { g->zstage_buf, 0, VK_WHOLE_SIZE },
        };
        VkWriteDescriptorSet wr[4];

        for (int i = 0; i < 4; i++) {
            memset(&wr[i], 0, sizeof(wr[i]));
            wr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr[i].dstSet          = g->slot[s].dset;
            wr[i].dstBinding      = 9u + (uint32_t) i;
            wr[i].descriptorCount = 1;
            wr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wr[i].pBufferInfo     = &bi[i];
        }
        vkUpdateDescriptorSets(g->dev, 4, wr, 0, NULL);
    }
}

/* Move one span-stage arena to a new 16 KB aligned block of `cap` bytes
   and import that block; the staged bytes in use (st->len) move with
   it. The caller guarantees that no GPU work refers to the arena (at
   init, or after a drain). */
static const char *
gpu_czstage_import(r128_gpu_t *g, struct rage128_span_stage *st,
                   uint32_t cap, VkBuffer *bufp, VkDeviceMemory *memp,
                   int imported)
{
    uint8_t       *nb = NULL;
    VkBuffer       buf;
    VkDeviceMemory mem;
    const char    *why;
    uint32_t       keep = st->active ? st->len : 0;

    if (r128_aligned_alloc((void **) &nb, 16384, (size_t) cap + 16384))
        return "cz stage arena allocation failed";
    if (keep)
        memcpy(nb, st->arena, keep);
    memset(nb + keep, 0, (size_t) cap + 16384 - keep);
    /* 4-byte guard past the end, as for the VRAM import */
    why = gpu_import_host(g, nb, (VkDeviceSize) cap + 4,
                          (VkDeviceSize) cap + 16384, &buf, &mem);
    if (why) {
        r128_aligned_free(nb);
        return why;
    }
    if (imported) {
        vkDestroyBuffer(g->dev, *bufp, NULL);
        vkFreeMemory(g->dev, *memp, NULL);
    }
    /* the old block came from r128_aligned_alloc only if it was itself
       imported; before the first import it is the CPU lane's arena from
       plain realloc */
    if (imported)
        r128_aligned_free(st->arena);
    else
        free(st->arena);
    st->arena = nb;
    st->cap   = cap;
    *bufp     = buf;
    *memp     = mem;
    return NULL;
}

/* Undo an arena move made at init: destroy the import and copy the
   staged bytes back to a plain heap block, so the code that owns the
   arena afterwards (the grow realloc, in vid_ati_rage128_mem.c for the
   span stages and vid_ati_rage128_3d.c for the texture stage, and the
   free in rage128_close) gets memory from the allocator it expects; the
   aligned allocator is not free()-compatible on every host. If the
   heap allocation fails the arena becomes NULL with cap 0 rather than
   keeping the aligned block, and the caller clears the staged-content
   fields. */
static void
gpu_arena_rollback(r128_gpu_t *g, uint8_t **arena, uint32_t *cap,
                   uint32_t keep, VkBuffer *bufp, VkDeviceMemory *memp)
{
    uint8_t *nb = (uint8_t *) malloc(*cap);

    vkDestroyBuffer(g->dev, *bufp, NULL);
    vkFreeMemory(g->dev, *memp, NULL);
    if (nb && keep)
        memcpy(nb, *arena, keep);
    r128_aligned_free(*arena);
    *arena = nb;
    if (!nb)
        *cap = 0;
}

static void
gpu_czstage_rollback(r128_gpu_t *g, struct rage128_span_stage *st,
                     VkBuffer *bufp, VkDeviceMemory *memp)
{
    gpu_arena_rollback(g, &st->arena, &st->cap,
                       st->active ? st->len : 0, bufp, memp);
    if (!st->arena) {
        st->len    = 0;
        st->active = 0;
    }
}

void
rage128_gpu_czstage_quiesce(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !g->czstage_ok)
        return;
    if (g->seg_czstaged)
        gpu_flush_cause(dev, GF_STAGE);
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++)
        if (g->slot[i].cz_ref)
            gpu_slot_retire(g, &g->slot[i], GF_STAGE);
}

int
rage128_gpu_czstage_grow(rage128_t *dev, struct rage128_span_stage *st,
                         uint32_t need)
{
    r128_gpu_t     *g = (r128_gpu_t *) dev->gpu;
    VkBuffer       *bufp;
    VkDeviceMemory *memp;
    uint32_t        ncap;

    if (!g || !g->czstage_ok)
        return -1;
    if (st == &dev->c_stage) {
        bufp = &g->cstage_buf;
        memp = &g->cstage_mem;
    } else if (st == &dev->z_stage) {
        bufp = &g->zstage_buf;
        memp = &g->zstage_mem;
    } else {
        return -1; /* 2D window scratch arenas are not imported */
    }
    ncap = st->cap ? st->cap : GPU_CZSTAGE_CAP0;
    while (ncap < need)
        ncap <<= 1;
    /* every slot's descriptor set names the old buffer, so drain all */
    gpu_flush_cause(dev, GF_STAGE);
    gpu_drain_all(g, GF_STAGE);
    if (gpu_czstage_import(g, st, ncap, bufp, memp, 1))
        return 0;
    gpu_czstage_rebind(g);
    return 1;
}

/* Open the Vulkan entry points: MoltenVK directly when it is bundled,
   else the host loader. 0 = no library, nothing to clean up. */
static int
gpu_vk_open(void)
{
    int direct = 0;

    /* MoltenVK reads this when its dylib loads, and both the direct open
       and the loader load it after this point. Fast math must be off for
       the kernels to match the interpreter bit for bit on Metal. Without
       MoltenVK the variable has no effect, and the NoContraction
       decorations in the SPIR-V are what keep the driver from fusing a
       multiply and an add. */
    r128_setenv("MVK_CONFIG_FAST_MATH_ENABLED", "0", 0);

    /* Open MoltenVK directly as the driver when possible: that needs no
       loader and no driver manifest, so the VK_ICD_FILENAMES handling
       below is skipped. With the bundled dylib this is the normal path
       on macOS. R128_GPU_LOADER=1 forces the Vulkan loader instead, to
       diagnose or work around a problem with the direct open. */
    {
        const char *le = getenv("R128_GPU_LOADER");

        if (le && le[0] && strcmp(le, "0") != 0)
            direct = -1;
    }
    if (direct == 0 && r128_vk_load_direct()) {
        direct = 1;
    } else {
        /* An inherited VK_ICD_FILENAMES or VK_DRIVER_FILES that points
           at a deleted SDK replaces the loader's driver search, which
           then finds no driver at all. So check each: keep a value that
           names a readable file, replace a dead one with the Homebrew
           MoltenVK manifest, and clear it when neither exists so the
           loader's default search runs. */
        static const char *const envs[2] = { "VK_ICD_FILENAMES", "VK_DRIVER_FILES" };
#    ifdef __APPLE__
        /* the Homebrew MoltenVK manifest is the one replacement known to
           work; no other platform has a manifest at a path we can name */
        const char *icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
#    else
        const char *icd = NULL;
#    endif
        FILE *f;
        int   icd_ok = icd && (f = fopen(icd, "r")) != NULL;

        if (icd_ok)
            fclose(f);
        for (int i = 0; i < 2; i++) {
            const char *cur = getenv(envs[i]);

            if (cur && (f = fopen(cur, "r"))) {
                fclose(f);
                continue; /* inherited value is alive */
            }
            if (icd_ok) {
                if (cur)
                    gpu_log("RAGE128 GPU: %s pointed at a missing file, overriding with %s\n",
                            envs[i], icd);
                r128_setenv(envs[i], icd, 1);
            } else if (cur) {
                gpu_log("RAGE128 GPU: clearing dead %s (%s)\n", envs[i], cur);
                r128_unsetenv(envs[i]);
            }
        }

        if (!r128_vk_load_loader()) {
            gpu_log("RAGE128 GPU: disabled: no Vulkan library found "
                    "(CPU renderer unaffected)\n");
            return 0;
        }
    }
    gpu_log("RAGE128 GPU: %s\n",
            direct == 1 ? "MoltenVK opened directly (no loader, no ICD manifest)"
                        : "using the host Vulkan loader");
    return 1;
}

/* Allocate the backend state and read every setting and environment
   override that does not need a Vulkan object yet. NULL = out of
   memory. */
static r128_gpu_t *
gpu_alloc(rage128_t *dev)
{
    int         mode = dev->gpu_mode_cfg;
    r128_gpu_t *g;

    g = (r128_gpu_t *) calloc(1, sizeof(*g));
    if (!g)
        return NULL;
    g->verify = (mode == R128_GPU_VERIFY);
    {
        const char *ae = getenv("R128_GPU_ASYNC");

        g->async = !g->verify && (!ae || !ae[0] || strcmp(ae, "0") != 0);
    }
    {
        /* R128_GPU_FOLD=0 turns folded pipelines off, for comparison:
           every draw then runs the unfolded (uber) kernel with selectors
           read at run time, and the boot precompile is skipped. On by
           default. */
        const char *fo = getenv("R128_GPU_FOLD");

        g->fold_en = !(fo && fo[0]) || strcmp(fo, "0") != 0;
        gpu_log("RAGE128 GPU: folded pipelines %s (%s)\n",
                g->fold_en ? "enabled" : "DISABLED",
                (fo && fo[0]) ? "R128_GPU_FOLD" : "default");
    }
    /* VRAM mirror: always on; the buffer is allocated after the VRAM
       import, and a failure there logs and clears mirror_en (kernels
       then address the import directly). */
    g->mirror_en = 1;
    {
        /* 2D verify, set by R128_GPU_2D=verify: every queued fill is
           waited for and checked byte by byte against its color, and the
           CPU-resolved queue paths compare their GPU result with the CPU
           walk. Slow. */
        const char *fe = getenv("R128_GPU_2D");

        g->verify2d = fe && strcmp(fe, "verify") == 0;
    }
    g->cur_tri      = -1;
    g->cur_tuple    = -1;
    g->memo_tuple   = -1;
    g->flush_2d_tag = GPU_2D_TAGS;
    g->tuples       = (gpu_tuple_t *) calloc(GPU_TUPLE_CAP, sizeof(gpu_tuple_t));
    g->learned      = (gpu_fold_job_t *) calloc((size_t) GPU_TUPLE_CAP * GPU_KERNELS,
                                                sizeof(gpu_fold_job_t));
    if (!g->tuples || !g->learned) {
        free(g->learned);
        free(g->tuples);
        free(g);
        return NULL;
    }
    {
        const char *pe = getenv("R128_GPU_PROF");

        g->prof = (pe && pe[0] && strcmp(pe, "0") != 0)
            || dev->gpu_telemetry_en;
        g->pr_t0        = prof_now();
        g->pr_pace_hold = &dev->pace_hold_us;
        g->owner        = dev;
        dev->ftl_en     = g->prof;
    }
    /* depth governor starts at the full ring and adapts from there */
    atomic_store(&g->depth_cap, GPU_RING_SLOTS);
    gpu_log("RAGE128 GPU: segment cadence %u spans (cap %u)\n",
            (unsigned) GPU_CADENCE, GPU_SPAN_CAP);
    gpu_log("RAGE128 GPU: depth governor cap=%u hi=%d.%d%% lo=%d.%d%%\n",
            atomic_load(&g->depth_cap),
            GPU_DEPTH_HI_X / 10, GPU_DEPTH_HI_X % 10,
            GPU_DEPTH_LO_X / 10, GPU_DEPTH_LO_X % 10);
    rng_reset(g);
    return g;
}

/* Move svga.vram to a 16 KB aligned block so the host pointer can be
   imported (VK_EXT_external_memory_host). The aligned allocator is not
   free()-compatible everywhere, so the block is recorded on dev and
   freed by rage128_gpu_close (which also runs when init fails) instead
   of by svga_close. The block is twice the VRAM size. The upper half
   holds the present snapshots: scanout can reach it (vram_display_mask
   is widened here) but the guest cannot (the svga vram_mask and
   vram_max and every dev->vram_mask clamp keep the real size), so the
   CRTC can read GPU frame copies that the guest cannot change. */
static const char *
gpu_vram_reseat(rage128_t *dev, r128_gpu_t *g)
{
    uint8_t *nv  = NULL;
    size_t   vsz = (size_t) dev->vram_size * 2;
    size_t   sz  = (vsz + 16384 + 16383) & ~(size_t) 16383;

    if (r128_aligned_alloc((void **) &nv, 16384, sz))
        return "vram realign failed";
    memcpy(nv, dev->svga.vram, dev->vram_size);
    memset(nv + dev->vram_size, 0, sz - dev->vram_size);
    free(dev->svga.vram);
    dev->vram_aligned           = nv;
    dev->svga.vram              = nv;
    dev->svga.vram_display_mask = (uint32_t) vsz - 1;
    /* changedvram must span the hidden pages the renderer will
       index (+2: the render loop probes two pages ahead); calloc
       keeps svga_close's free() valid */
    free(dev->svga.changedvram);
    dev->svga.changedvram = calloc((vsz >> 12) + 2, 1);
    /* deferred bring-up runs during scanout, and the new dirty map is
       all clean, so without a full redraw the frame from before the
       move would stay on screen */
    dev->svga.fullchange = dev->svga.monitor->mon_changeframecount;
    g->snap_stride       = (dev->vram_size / GPU_SNAPS) & ~4095u;
    for (unsigned i = 0; i < GPU_SNAPS; i++)
        g->snaps[i].dst = dev->vram_size + i * g->snap_stride;
    return NULL;
}

/* Create the instance and resolve its entry points. On NULL the
   instance-level table is loaded and g->inst may be destroyed. */
static const char *
gpu_instance_create(r128_gpu_t *g)
{
    const char *iexts[] = {
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    VkApplicationInfo    ai  = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };

    ai.pApplicationName         = "86Box-r128-gpu";
    ai.apiVersion               = VK_API_VERSION_1_2;
    ici.pApplicationInfo        = &ai;
    ici.flags                   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ici.enabledExtensionCount   = 2;
    ici.ppEnabledExtensionNames = iexts;
    /* Portability enumeration is a loader concept: an ICD opened
       directly may reject the flag/extension outright, so fall back
       to a plain instance rather than treating that as no GPU. */
    if (vkCreateInstance(&ici, NULL, &g->inst) != VK_SUCCESS) {
        ici.flags                   = 0;
        ici.enabledExtensionCount   = 1;
        ici.ppEnabledExtensionNames = &iexts[1];
        if (vkCreateInstance(&ici, NULL, &g->inst) != VK_SUCCESS) {
            ici.enabledExtensionCount   = 0;
            ici.ppEnabledExtensionNames = NULL;
            if (vkCreateInstance(&ici, NULL, &g->inst) != VK_SUCCESS)
                return "vkCreateInstance failed (no usable Vulkan driver)";
        }
    }
    if (!r128_vk_load_instance(g->inst))
        return "instance entry point resolution failed";
    return NULL;
}

/* The loader's first device is not always the fastest: a laptop with
   two GPUs can list the integrated one ahead of the discrete card
   unless the vendor control panel says otherwise, and the backend would
   then run on the integrated GPU. Rank the devices by type (discrete,
   integrated, virtual, other), log every device so a telemetry file
   shows the choice, and let R128_GPU_DEVICE=<index> force one for
   comparison. */
static const char *
gpu_device_pick(r128_gpu_t *g)
{
    static const char *const tnames[] = {
        "other", "integrated", "discrete", "virtual", "cpu"
    };
    uint32_t          np    = 0;
    VkPhysicalDevice *pd    = NULL;
    int               pick  = -1;
    int               best  = -1;
    int               force = -1;
    const char       *fe    = getenv("R128_GPU_DEVICE");
    VkResult          er    = vkEnumeratePhysicalDevices(g->inst, &np, NULL);

    if (er != VK_SUCCESS || np == 0)
        return "no Vulkan physical device";
    pd = (VkPhysicalDevice *) calloc(np, sizeof(*pd));
    er = vkEnumeratePhysicalDevices(g->inst, &np, pd);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || np == 0) {
        free(pd);
        return "no Vulkan physical device";
    }
    if (fe && fe[0]) {
        force = atoi(fe);
        if (force < 0 || (uint32_t) force >= np) {
            gpu_log("RAGE128 GPU: R128_GPU_DEVICE=%s is out of range "
                    "(%u devices), ignored\n",
                    fe, np);
            force = -1;
        }
    }
    for (uint32_t i = 0; i < np; i++) {
        VkPhysicalDeviceProperties2 pp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        int                         rank;

        vkGetPhysicalDeviceProperties2(pd[i], &pp);
        /* clang-format off */
        switch (pp.properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   rank = 3; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: rank = 2; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    rank = 1; break;
            default:                                     rank = 0; break;
        }
        /* clang-format on */
        if ((int) i == force)
            rank = 100;
        if (rank > best) {
            best = rank;
            pick = (int) i;
        }
    }
    for (uint32_t i = 0; i < np; i++) {
        VkPhysicalDeviceProperties2 pp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        unsigned                    t;

        vkGetPhysicalDeviceProperties2(pd[i], &pp);
        t = (unsigned) pp.properties.deviceType;
        gpu_log("RAGE128 GPU: device %u/%u: \"%s\" vendor %04x type %s -- %s\n",
                i + 1, np, pp.properties.deviceName, pp.properties.vendorID,
                t < 5 ? tnames[t] : "unknown",
                (int) i == pick ? (force >= 0 ? "forced by R128_GPU_DEVICE" : "chosen")
                                : "passed over");
    }
    g->phys = pd[pick];
    free(pd);
    return NULL;
}

/* Read the limits the backend depends on, and log the driver and its
   float controls, the first things to check when verify mode reports
   mismatches on a new host. */
static const char *
gpu_device_query(r128_gpu_t *g)
{
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hp = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT
    };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };

    p2.pNext = &hp;
    vkGetPhysicalDeviceProperties2(g->phys, &p2);
    g->tqperiod = p2.properties.limits.timestampPeriod;
    g->max_wg_x = p2.properties.limits.maxComputeWorkGroupCount[0];
    if (hp.minImportedHostPointerAlignment > 16384)
        return "host pointer alignment requirement above 16KB";
    gpu_log("RAGE128 GPU: device \"%s\" vendor %04x api %u.%u host-ptr "
            "align %llu\n",
            p2.properties.deviceName, p2.properties.vendorID,
            VK_VERSION_MAJOR(p2.properties.apiVersion),
            VK_VERSION_MINOR(p2.properties.apiVersion),
            (unsigned long long) hp.minImportedHostPointerAlignment);
    /* Matching the interpreter's float results depends on OpFAdd,
       OpFSub and OpFMul being correctly rounded (Vulkan requires
       that), on the NoContraction decorations in the SPIR-V, and on
       fp32 denormals not being flushed to zero. The last one is up to
       the driver's float controls, so they are logged once per boot.
       This is a separate query because the structures are Vulkan 1.2
       core; on a 1.1 device it is skipped instead of being chained
       unchecked. */
    if (p2.properties.apiVersion >= VK_API_VERSION_1_2) {
        VkPhysicalDeviceDriverProperties dp = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES
        };
        VkPhysicalDeviceFloatControlsProperties fc = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES
        };
        VkPhysicalDeviceProperties2 fp2 = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
        };

        fc.pNext  = &dp;
        fp2.pNext = &fc;
        vkGetPhysicalDeviceProperties2(g->phys, &fp2);
        /* log the driver as well as the device: two releases of one
           vendor's driver report the same deviceName */
        gpu_log("RAGE128 GPU: driver \"%s\" (%s) version %08x\n",
                dp.driverName, dp.driverInfo,
                p2.properties.driverVersion);
        gpu_log("RAGE128 GPU: fp32 controls: denorm preserve=%d ftz=%d "
                "rte=%d signed-zero-inf-nan=%d\n",
                fc.shaderDenormPreserveFloat32,
                fc.shaderDenormFlushToZeroFloat32,
                fc.shaderRoundingModeRTEFloat32,
                fc.shaderSignedZeroInfNanPreserveFloat32);
    }
    return NULL;
}

static const char *
gpu_queue_pick(r128_gpu_t *g)
{
    uint32_t                 nqf = 0;
    VkQueueFamilyProperties *qfp;

    vkGetPhysicalDeviceQueueFamilyProperties(g->phys, &nqf, NULL);
    qfp = (VkQueueFamilyProperties *) calloc(nqf, sizeof(*qfp));
    vkGetPhysicalDeviceQueueFamilyProperties(g->phys, &nqf, qfp);
    g->qfam = ~0u;
    for (uint32_t i = 0; i < nqf; i++)
        if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            g->qfam   = i;
            g->tqbits = qfp[i].timestampValidBits;
            break;
        }
    free(qfp);
    if (g->qfam == ~0u)
        return "no compute queue";
    return NULL;
}

/* Extensions, mandatory features, then the logical device + queue. */
static const char *
gpu_device_create(r128_gpu_t *g)
{
    uint32_t               nde = 0;
    VkExtensionProperties *de;
    const char            *dexts[2];
    uint32_t               ndext    = 0;
    int                    have_emh = 0;

    vkEnumerateDeviceExtensionProperties(g->phys, NULL, &nde, NULL);
    de = (VkExtensionProperties *) calloc(nde, sizeof(*de));
    vkEnumerateDeviceExtensionProperties(g->phys, NULL, &nde, de);
    for (uint32_t i = 0; i < nde; i++) {
        if (!strcmp(de[i].extensionName, "VK_KHR_portability_subset"))
            dexts[ndext++] = "VK_KHR_portability_subset";
        if (!strcmp(de[i].extensionName, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME))
            have_emh = 1;
    }
    free(de);
    if (!have_emh)
        return "VK_EXT_external_memory_host unsupported";
    dexts[ndext++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;

    {
        VkPhysicalDeviceVulkan11Features f11 = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
        };
        VkPhysicalDeviceFeatures2 f2   = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        float                     prio = 1.0f;
        VkDeviceQueueCreateInfo   qci  = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        VkDeviceCreateInfo        dci  = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

        /* The SPIR-V modules declare Int64 and StorageBuffer16BitAccess,
           so both features are required. Query them first and name the
           one that is missing: a bare vkCreateDevice failure on a new
           host says nothing. */
        {
            VkPhysicalDeviceVulkan11Features h11 = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
            };
            VkPhysicalDeviceFeatures2 h2 = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
            };

            h2.pNext = &h11;
            vkGetPhysicalDeviceFeatures2(g->phys, &h2);
            if (!h2.features.shaderInt64)
                return "device lacks shaderInt64 (the kernel's 64-bit "
                       "edge/z chain)";
            if (!h11.storageBuffer16BitAccess)
                return "device lacks storageBuffer16BitAccess (the u16 "
                       "vram view)";
        }
        f11.storageBuffer16BitAccess = VK_TRUE;
        f2.pNext                     = &f11;
        f2.features.shaderInt64      = VK_TRUE;
        qci.queueFamilyIndex         = g->qfam;
        qci.queueCount               = 1;
        qci.pQueuePriorities         = &prio;
        dci.pNext                    = &f2;
        dci.queueCreateInfoCount     = 1;
        dci.pQueueCreateInfos        = &qci;
        dci.enabledExtensionCount    = ndext;
        dci.ppEnabledExtensionNames  = dexts;
        if (vkCreateDevice(g->phys, &dci, NULL, &g->dev) != VK_SUCCESS)
            return "vkCreateDevice failed";
        vkGetDeviceQueue(g->dev, g->qfam, 0, &g->queue);
    }
    return NULL;
}

/* VRAM mirror: a device-local buffer covering the guest half of the
   import plus the guard word. Every kernel address is masked, so the
   kernels cannot reach the snapshot half through it, and the copies
   never include it. A memory type that is device-local and not
   host-visible is preferred, since the point is to keep host-mapped
   memory out of the kernels' path; a device-local type that is also
   host-visible is the fallback, and the log says so. Not fatal: a
   failure clears mirror_en. */
static void
gpu_mirror_create(rage128_t *dev, r128_gpu_t *g)
{
    VkBufferCreateInfo               bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements             mr;
    VkPhysicalDeviceMemoryProperties mp;
    VkMemoryAllocateInfo             mai  = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    uint32_t                         mti  = ~0u;
    const char                      *mwhy = NULL;

    if (!g->mirror_en)
        return;
    g->mirror_cap = (VkDeviceSize) dev->vram_size + 4;
    bci.size      = g->mirror_cap;
    bci.usage     = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(g->dev, &bci, NULL, &g->mirror_buf) != VK_SUCCESS)
        mwhy = "buffer create failed";
    else {
        vkGetBufferMemoryRequirements(g->dev, g->mirror_buf, &mr);
        vkGetPhysicalDeviceMemoryProperties(g->phys, &mp);
        for (int pass = 0; pass < 2 && mti == ~0u; pass++)
            for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
                VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;

                if (!(mr.memoryTypeBits & (1u << i))
                    || !(f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                    continue;
                if (pass == 0 && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                    continue;
                mti = i;
                break;
            }
        if (mti == ~0u)
            mwhy = "no device-local memory type";
        else {
            mai.allocationSize  = mr.size;
            mai.memoryTypeIndex = mti;
            if (vkAllocateMemory(g->dev, &mai, NULL, &g->mirror_mem)
                != VK_SUCCESS)
                mwhy = "allocation failed";
            else if (vkBindBufferMemory(g->dev, g->mirror_buf,
                                        g->mirror_mem, 0)
                     != VK_SUCCESS)
                mwhy = "bind failed";
        }
    }
    if (mwhy) {
        if (g->mirror_mem)
            vkFreeMemory(g->dev, g->mirror_mem, NULL);
        if (g->mirror_buf)
            vkDestroyBuffer(g->dev, g->mirror_buf, NULL);
        g->mirror_mem = VK_NULL_HANDLE;
        g->mirror_buf = VK_NULL_HANDLE;
        g->mirror_en  = 0;
        gpu_log("RAGE128 GPU: vram mirror unavailable: %s -- kernels address "
                "the host import\n",
                mwhy);
    } else
        gpu_log("RAGE128 GPU: vram mirror: %llu bytes, memory type %u "
                "flags 0x%x -- %s\n",
                (unsigned long long) g->mirror_cap, mti,
                (unsigned) mp.memoryTypes[mti].propertyFlags,
                (mp.memoryTypes[mti].propertyFlags
                 & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                    ? "device-local but host-visible"
                    : "device-local");
}

/* Move and import the texture stage arena and the color and z span-stage
   arenas, so the kernels read texture levels staged from AGP memory and
   write render targets staged from AGP memory without a copy. Neither
   is fatal: without them stage_ok or czstage_ok stays 0 and the state
   gate keeps those draws on the CPU path. */
static void
gpu_arenas_import(rage128_t *dev, r128_gpu_t *g)
{
    {
        uint32_t    scap = GPU_STAGE_CAP0;
        const char *swhy;

        while (scap < dev->tex_stage.cap)
            scap <<= 1;
        swhy = gpu_stage_import(dev, g, scap);
        if (swhy)
            gpu_log("RAGE128 GPU: stage arena import unavailable: %s\n", swhy);
    }

    /* czstage_ok needs both arenas */
    {
        uint32_t    ccap = GPU_CZSTAGE_CAP0, zcap = GPU_CZSTAGE_CAP0;
        const char *cwhy;

        while (ccap < dev->c_stage.cap)
            ccap <<= 1;
        while (zcap < dev->z_stage.cap)
            zcap <<= 1;
        cwhy = gpu_czstage_import(g, &dev->c_stage, ccap,
                                  &g->cstage_buf, &g->cstage_mem, 0);
        if (!cwhy) {
            cwhy = gpu_czstage_import(g, &dev->z_stage, zcap,
                                      &g->zstage_buf, &g->zstage_mem, 0);
            /* only the color arena was imported: it now sits in an
               aligned block, which the CPU grow and close paths would
               pass to realloc() and free(), so move it back */
            if (cwhy)
                gpu_czstage_rollback(g, &dev->c_stage,
                                     &g->cstage_buf, &g->cstage_mem);
        }
        if (cwhy)
            gpu_log("RAGE128 GPU: cz stage arena import unavailable: %s\n",
                    cwhy);
        else
            g->czstage_ok = 1;
    }
}

/* Per-slot segment buffers (fatal) and the 2D staging ring (not). */
static const char *
gpu_slot_bufs_create(r128_gpu_t *g)
{
    /* Each slot has its own mapped inputs and ladder buffer, kept until
       its fence is retired; its descriptor set binds them with the
       GPU_NBIND layout. */
    for (unsigned i = 0; i < GPU_RING_SLOTS; i++) {
        gpu_slot_t *sl = &g->slot[i];

        if (!gpu_mk_buf(g, &sl->b_rows, GPU_SPAN_CAP * 2 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            || !gpu_mk_buf(g, &sl->b_order,
                           GPU_SPAN_CAP * GPU_BATCH_SLOTS * sizeof(uint32_t),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            || !gpu_mk_buf(g, &sl->b_spans, GPU_SPAN_CAP * sizeof(seg_span_t),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            || !gpu_mk_buf(g, &sl->b_tris, GPU_TRI_CAP * sizeof(seg_tri_t),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            || !gpu_mk_buf(g, &sl->b_lad, GPU_PX_CAP * sizeof(uint32_t),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            || !gpu_mk_buf(g, &sl->b_pal, GPU_PAL_CAP * 256 * sizeof(uint32_t),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return "segment buffer allocation failed";
    }

    /* 2D staging ring: source bytes for queued mono-expand, host-data
       and pattern copies, and the bounce target for overlapping screen
       blits, which is why it is also a transfer destination. Not fatal:
       without it those operations use the CPU walk. */
    if (gpu_mk_buf(g, &g->stage2d, GPU_2DSTAGE_SIZE,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                       | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        g->stage2d_ok = 1;
    else
        gpu_log("RAGE128 GPU: 2d staging ring unavailable\n");
    return NULL;
}

/* The segment kernels' GPU_NBIND storage-buffer set layout and the
   pipeline layout (8-byte push range) every folded pipeline shares. */
static const char *
gpu_layouts_create(r128_gpu_t *g)
{
    VkDescriptorSetLayoutBinding    db[GPU_NBIND];
    VkDescriptorSetLayoutCreateInfo dslci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    VkPushConstantRange        pcr  = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 8 };
    VkPipelineLayoutCreateInfo plci = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };

    for (int i = 0; i < GPU_NBIND; i++) {
        db[i].binding            = i;
        db[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        db[i].descriptorCount    = 1;
        db[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
        db[i].pImmutableSamplers = NULL;
    }
    dslci.bindingCount = GPU_NBIND;
    dslci.pBindings    = db;
    if (vkCreateDescriptorSetLayout(g->dev, &dslci, NULL, &g->dsl) != VK_SUCCESS)
        return "descriptor layout failed";
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &g->dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(g->dev, &plci, NULL, &g->plyt) != VK_SUCCESS)
        return "pipeline layout failed";
    return NULL;
}

/* Pipeline cache saved on disk: load the saved data if its header still
   matches this driver (vendor ID, device ID, cache UUID), else start
   empty. Bad or out-of-date data must never be fatal. */
static const char *
gpu_pcache_load(r128_gpu_t *g)
{
    VkPipelineCacheCreateInfo pcci = {
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
    };
    VkPhysicalDeviceProperties2 p2 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    };
    char  path[1088];
    FILE *f;
    void *blob = NULL;
    long  bsz  = 0;

    vkGetPhysicalDeviceProperties2(g->phys, &p2);
    gpu_cache_path(path, sizeof(path), "r128gpu.plcache");
    f = plat_fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        bsz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (bsz >= 32 && (blob = malloc((size_t) bsz))
            && fread(blob, 1, (size_t) bsz, f) == (size_t) bsz) {
            const uint32_t *h = (const uint32_t *) blob;

            if (h[0] >= 32 && h[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
                && h[2] == p2.properties.vendorID
                && h[3] == p2.properties.deviceID
                && !memcmp((const uint8_t *) blob + 16,
                           p2.properties.pipelineCacheUUID,
                           VK_UUID_SIZE)) {
                pcci.initialDataSize = (size_t) bsz;
                pcci.pInitialData    = blob;
            }
        }
        fclose(f);
    }
    if (vkCreatePipelineCache(g->dev, &pcci, NULL, &g->pcache)
        != VK_SUCCESS) {
        /* retry empty: a rejected blob should not kill the backend */
        pcci.initialDataSize = 0;
        pcci.pInitialData    = NULL;
        if (vkCreatePipelineCache(g->dev, &pcci, NULL, &g->pcache)
            != VK_SUCCESS) {
            free(blob);
            return "pipeline cache failed";
        }
    }
    free(blob);
    return NULL;
}

/* Shader modules and pipelines: the segment uber kernel (module kept
   alive for lazy folded compiles), the ladder, and the RMW and OV0
   kernels with their own two-binding layouts. */
static const char *
gpu_pipelines_create(r128_gpu_t *g)
{
    VkShaderModuleCreateInfo    smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule              sm;
    VkComputePipelineCreateInfo cpi = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
    };

    gpu_axes_index();
    smci.codeSize = sizeof(r128_gpu_seg_spv);
    smci.pCode    = (const uint32_t *) r128_gpu_seg_spv;
    /* module kept alive on g: folded pipelines compile lazily from
       it for the whole device lifetime */
    if (vkCreateShaderModule(g->dev, &smci, NULL, &g->seg_sm) != VK_SUCCESS)
        return "shader module failed";
    atomic_init(&g->boot_done, 0);
    g->boot_total = gpu_boot_total(g);
    if (gpu_uber_build(g) != VK_SUCCESS)
        return "compute pipeline failed";
    smci.codeSize = sizeof(r128_gpu_lad_spv);
    smci.pCode    = (const uint32_t *) r128_gpu_lad_spv;
    if (vkCreateShaderModule(g->dev, &smci, NULL, &sm) != VK_SUCCESS)
        return "ladder shader module failed";
    /* every field of cpi the driver reads is set here; no other code
       fills this struct */
    cpi.stage.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module              = sm;
    cpi.stage.pName               = "main";
    cpi.stage.pSpecializationInfo = NULL;
    cpi.layout                    = g->plyt;
    if (vkCreateComputePipelines(g->dev, g->pcache, 1, &cpi, NULL,
                                 &g->lad_pipe)
        != VK_SUCCESS) {
        vkDestroyShaderModule(g->dev, sm, NULL);
        return "ladder pipeline failed";
    }
    vkDestroyShaderModule(g->dev, sm, NULL);
    {
        VkDescriptorSetLayoutBinding    rdb[2];
        VkDescriptorSetLayoutCreateInfo rdslci = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
        };
        VkPushConstantRange        rpcr  = { VK_SHADER_STAGE_COMPUTE_BIT,
                                             0, 28 };
        VkPipelineLayoutCreateInfo rplci = {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };

        for (int i = 0; i < 2; i++) {
            rdb[i].binding            = i;
            rdb[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            rdb[i].descriptorCount    = 1;
            rdb[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
            rdb[i].pImmutableSamplers = NULL;
        }
        rdslci.bindingCount          = 2;
        rdslci.pBindings             = rdb;
        rplci.setLayoutCount         = 1;
        rplci.pushConstantRangeCount = 1;
        rplci.pPushConstantRanges    = &rpcr;
        if (vkCreateDescriptorSetLayout(g->dev, &rdslci, NULL,
                                        &g->rmw_dsl)
            != VK_SUCCESS)
            return "rmw descriptor layout failed";
        rplci.pSetLayouts = &g->rmw_dsl;
        if (vkCreatePipelineLayout(g->dev, &rplci, NULL, &g->rmw_plyt)
            != VK_SUCCESS)
            return "rmw pipeline layout failed";
    }
    smci.codeSize = sizeof(r128_gpu_rmw_spv);
    smci.pCode    = (const uint32_t *) r128_gpu_rmw_spv;
    if (vkCreateShaderModule(g->dev, &smci, NULL, &sm) != VK_SUCCESS)
        return "rmw shader module failed";
    cpi.stage.module = sm;
    cpi.layout       = g->rmw_plyt;
    if (vkCreateComputePipelines(g->dev, g->pcache, 1, &cpi, NULL,
                                 &g->rmw_pipe)
        != VK_SUCCESS) {
        vkDestroyShaderModule(g->dev, sm, NULL);
        return "rmw pipeline failed";
    }
    vkDestroyShaderModule(g->dev, sm, NULL);
    {
        VkDescriptorSetLayoutBinding    odb[2];
        VkDescriptorSetLayoutCreateInfo odslci = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
        };
        VkPushConstantRange        opcr  = { VK_SHADER_STAGE_COMPUTE_BIT,
                                             0, sizeof(r128_ov0_frame_t) };
        VkPipelineLayoutCreateInfo oplci = {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };

        for (int i = 0; i < 2; i++) {
            odb[i].binding            = i;
            odb[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            odb[i].descriptorCount    = 1;
            odb[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
            odb[i].pImmutableSamplers = NULL;
        }
        odslci.bindingCount          = 2;
        odslci.pBindings             = odb;
        oplci.setLayoutCount         = 1;
        oplci.pushConstantRangeCount = 1;
        oplci.pPushConstantRanges    = &opcr;
        if (vkCreateDescriptorSetLayout(g->dev, &odslci, NULL,
                                        &g->ov0_dsl)
            != VK_SUCCESS)
            return "ov0 descriptor layout failed";
        oplci.pSetLayouts = &g->ov0_dsl;
        if (vkCreatePipelineLayout(g->dev, &oplci, NULL, &g->ov0_plyt)
            != VK_SUCCESS)
            return "ov0 pipeline layout failed";
    }
    smci.codeSize = sizeof(r128_gpu_ov0_spv);
    smci.pCode    = (const uint32_t *) r128_gpu_ov0_spv;
    if (vkCreateShaderModule(g->dev, &smci, NULL, &sm) != VK_SUCCESS)
        return "ov0 shader module failed";
    cpi.stage.module = sm;
    cpi.layout       = g->ov0_plyt;
    if (vkCreateComputePipelines(g->dev, g->pcache, 1, &cpi, NULL,
                                 &g->ov0_pipe)
        != VK_SUCCESS) {
        vkDestroyShaderModule(g->dev, sm, NULL);
        return "ov0 pipeline failed";
    }
    vkDestroyShaderModule(g->dev, sm, NULL);
    return NULL;
}

/* Intern the seed tuples (r128_gpu_comb_seed, derived from the drivers'
   combine code) and then the learned list, so the boot precompile takes
   the pipeline compile stalls at boot, where they are logged, instead
   of during a game. The fold, queue and flush mutexes are initialized
   here too. */
static void
gpu_tuples_prime(r128_gpu_t *g)
{
    g->quiet_intern = 1;
    for (size_t i = 0;
         i < sizeof(r128_gpu_comb_seed) / sizeof(r128_gpu_comb_seed[0]);
         i++) {
        uint32_t sel[GPU_TUPLE_SEL];

        /* The seed table holds combine selectors only. Stencil
           variants are interned when first seen and saved through the
           learned list. Texture formats too: a seed tuple leaves the
           format words at GPU_TUPLE_FMT_ANY (read at run time), and it
           is the any_tid stand-in that serves a tuple with fixed formats
           while that tuple's own pipeline compiles. */
        for (int k = 0; k < (int) GPU_TUPLE_COMB; k++)
            sel[k] = r128_gpu_comb_seed[i].sel[k];
        sel[14] = 0;
        sel[15] = 0;
        for (int k = 16; k < 20; k++)
            sel[k] = GPU_TUPLE_FMT_ANY;
        sel[GPU_TUPLE_LIGHT] = 0; /* no driver lights a seed tuple */
        gpu_tuple_intern(g, sel);
    }
    {
        uint32_t sel[GPU_TUPLE_SEL] = { 0 };

        for (int k = 16; k < 20; k++)
            sel[k] = GPU_TUPLE_FMT_ANY;
        sel[15] = 1;
        gpu_tuple_intern(g, sel); /* dead color, stencil off */
        sel[14] = 1;
        gpu_tuple_intern(g, sel); /* shadow-volume stencil */
    }
    pthread_mutex_init(&g->fold_mtx, NULL);
    pthread_mutex_init(&g->queue_mtx, NULL);
    {
        pthread_mutexattr_t fa;

        pthread_mutexattr_init(&fa);
        pthread_mutexattr_settype(&fa, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&g->flush_mtx, &fa);
        pthread_mutexattr_destroy(&fa);
    }
    gpu_tuples_load(g);
    g->quiet_intern = 0;
    /* in list mode the folded count is nlearn, unknown until the load
       just above; until now the total held only the unfolded and
       serial pipelines (GPU_KERNELS * 2) */
    g->boot_total = gpu_boot_total(g);
}

/* Descriptor pool and the per-slot, RMW and OV0 sets, bound once here
   with vram_buf placeholders wherever an optional import is missing. */
static const char *
gpu_dsets_create(r128_gpu_t *g)
{
    VkDescriptorPoolSize dps = {
        /* + 2 = the RMW set's vram + staging bindings,
           + 2 more = the OV0 set's vram + out bindings */
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GPU_NBIND * GPU_RING_SLOTS + 4
    };
    VkDescriptorPoolCreateInfo dpci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
    };
    VkDescriptorSetAllocateInfo dsai = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
    };

    dpci.maxSets       = GPU_RING_SLOTS + 2; /* + the RMW and OV0 sets */
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &dps;
    if (vkCreateDescriptorPool(g->dev, &dpci, NULL, &g->dpool) != VK_SUCCESS)
        return "descriptor pool failed";
    {
        VkDescriptorSetLayout layouts[GPU_RING_SLOTS];
        VkDescriptorSet       sets[GPU_RING_SLOTS];

        for (unsigned s = 0; s < GPU_RING_SLOTS; s++)
            layouts[s] = g->dsl;
        dsai.descriptorPool     = g->dpool;
        dsai.descriptorSetCount = GPU_RING_SLOTS;
        dsai.pSetLayouts        = layouts;
        if (vkAllocateDescriptorSets(g->dev, &dsai, sets) != VK_SUCCESS)
            return "descriptor set failed";
        for (unsigned s = 0; s < GPU_RING_SLOTS; s++) {
            gpu_slot_t *sl = &g->slot[s];
            /* the stage binding falls back to vram_buf when the arena
               import is unavailable: it is never read then (the state
               gate refuses staged draws without stage_ok), but the set
               must stay valid */
            VkBuffer               vb            = g->mirror_en ? g->mirror_buf : g->vram_buf;
            VkDescriptorBufferInfo bi[GPU_NBIND] = {
                { vb,                                          0, VK_WHOLE_SIZE },
                { vb,                                          0, VK_WHOLE_SIZE },
                { sl->b_rows.b,                                0, VK_WHOLE_SIZE },
                { sl->b_order.b,                               0, VK_WHOLE_SIZE },
                { sl->b_spans.b,                               0, VK_WHOLE_SIZE },
                { sl->b_tris.b,                                0, VK_WHOLE_SIZE },
                { sl->b_lad.b,                                 0, VK_WHOLE_SIZE },
                { g->stage_ok ? g->stage_buf : g->vram_buf,    0, VK_WHOLE_SIZE },
                { sl->b_pal.b,                                 0, VK_WHOLE_SIZE },
                /* staged color and z arenas, a 16-bit view and a word
                   view of each; vram_buf keeps the set valid when the
                   import is unavailable (the state gate then refuses
                   staged targets, as czstage_ok is 0) */
                { g->czstage_ok ? g->cstage_buf : g->vram_buf, 0, VK_WHOLE_SIZE },
                { g->czstage_ok ? g->cstage_buf : g->vram_buf, 0, VK_WHOLE_SIZE },
                { g->czstage_ok ? g->zstage_buf : g->vram_buf, 0, VK_WHOLE_SIZE },
                { g->czstage_ok ? g->zstage_buf : g->vram_buf, 0, VK_WHOLE_SIZE },
            };
            VkWriteDescriptorSet wr[GPU_NBIND];

            sl->dset = sets[s];
            for (int i = 0; i < GPU_NBIND; i++) {
                memset(&wr[i], 0, sizeof(wr[i]));
                wr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                wr[i].dstSet          = sl->dset;
                wr[i].dstBinding      = i;
                wr[i].descriptorCount = 1;
                wr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                wr[i].pBufferInfo     = &bi[i];
            }
            vkUpdateDescriptorSets(g->dev, GPU_NBIND, wr, 0, NULL);
        }
    }
    {
        VkDescriptorSetAllocateInfo rai = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        VkDescriptorBufferInfo rbi[2] = {
            { g->vram_buf,                                0, VK_WHOLE_SIZE },
            { g->stage2d_ok ? g->stage2d.b : g->vram_buf, 0,
             VK_WHOLE_SIZE                                                 },
        };
        VkWriteDescriptorSet rwr[2];

        rai.descriptorPool     = g->dpool;
        rai.descriptorSetCount = 1;
        rai.pSetLayouts        = &g->rmw_dsl;
        if (vkAllocateDescriptorSets(g->dev, &rai, &g->rmw_dset)
            != VK_SUCCESS)
            return "rmw descriptor set failed";
        for (int i = 0; i < 2; i++) {
            memset(&rwr[i], 0, sizeof(rwr[i]));
            rwr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            rwr[i].dstSet          = g->rmw_dset;
            rwr[i].dstBinding      = (uint32_t) i;
            rwr[i].descriptorCount = 1;
            rwr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            rwr[i].pBufferInfo     = &rbi[i];
        }
        vkUpdateDescriptorSets(g->dev, 2, rwr, 0, NULL);
    }
    {
        VkDescriptorSetAllocateInfo oai = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        /* the output binding starts as vram_buf: the real buffer is
           allocated on the first compose, which rebinds it */
        VkDescriptorBufferInfo obi[2] = {
            { g->vram_buf, 0, VK_WHOLE_SIZE },
            { g->vram_buf, 0, VK_WHOLE_SIZE },
        };
        VkWriteDescriptorSet owr[2];

        oai.descriptorPool     = g->dpool;
        oai.descriptorSetCount = 1;
        oai.pSetLayouts        = &g->ov0_dsl;
        if (vkAllocateDescriptorSets(g->dev, &oai, &g->ov0_dset)
            != VK_SUCCESS)
            return "ov0 descriptor set failed";
        for (int i = 0; i < 2; i++) {
            memset(&owr[i], 0, sizeof(owr[i]));
            owr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            owr[i].dstSet          = g->ov0_dset;
            owr[i].dstBinding      = (uint32_t) i;
            owr[i].descriptorCount = 1;
            owr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            owr[i].pBufferInfo     = &obi[i];
        }
        vkUpdateDescriptorSets(g->dev, 2, owr, 0, NULL);
    }
    return NULL;
}

/* Command pools, per-slot and snapshot command buffers + fences, the
   OV0 pool on its own (the vsync thread records there), and the
   optional timestamp query pools. */
static const char *
gpu_cmd_create(r128_gpu_t *g)
{
    VkCommandPoolCreateInfo     cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo cbai = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = g->qfam;
    if (vkCreateCommandPool(g->dev, &cpci, NULL, &g->pool) != VK_SUCCESS)
        return "command pool failed";
    {
        VkCommandBuffer cbs[GPU_RING_SLOTS];

        cbai.commandPool        = g->pool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = GPU_RING_SLOTS;
        if (vkAllocateCommandBuffers(g->dev, &cbai, cbs) != VK_SUCCESS)
            return "command buffer failed";
        for (unsigned s = 0; s < GPU_RING_SLOTS; s++) {
            g->slot[s].cb = cbs[s];
            if (vkCreateFence(g->dev, &fci, NULL, &g->slot[s].fence)
                != VK_SUCCESS)
                return "fence failed";
            pthread_mutex_init(&g->slot[s].fence_mtx, NULL);
        }
    }
    {
        VkCommandBuffer scbs[GPU_SNAPS];

        cbai.commandBufferCount = GPU_SNAPS;
        if (vkAllocateCommandBuffers(g->dev, &cbai, scbs) != VK_SUCCESS)
            return "snap command buffer failed";
        for (unsigned s = 0; s < GPU_SNAPS; s++) {
            g->snaps[s].cb = scbs[s];
            if (vkCreateFence(g->dev, &fci, NULL, &g->snaps[s].fence)
                != VK_SUCCESS)
                return "snap fence failed";
            pthread_mutex_init(&g->snaps[s].mtx, NULL);
        }
    }
    {
        /* The overlay compose records on the vsync thread, and a command
           pool may be used by only one thread at a time, so it gets its
           own pool. The fence is created signaled, so the wait before
           growing the output buffer returns even before the first
           submit. */
        VkCommandPool     opool;
        VkFenceCreateInfo ofci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

        ofci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateCommandPool(g->dev, &cpci, NULL, &opool)
            != VK_SUCCESS)
            return "ov0 command pool failed";
        g->ov0_pool             = opool;
        cbai.commandPool        = opool;
        cbai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(g->dev, &cbai, &g->ov0_cb)
            != VK_SUCCESS)
            return "ov0 command buffer failed";
        if (vkCreateFence(g->dev, &ofci, NULL, &g->ov0_fence)
            != VK_SUCCESS)
            return "ov0 fence failed";
        g->ov0_ok = 1;
    }
    if (g->prof && g->tqbits) {
        VkQueryPoolCreateInfo qpci = {
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO
        };

        qpci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = 5; /* 0-2 ladder/shade, 3-4 mirror copies */
        for (unsigned s = 0; s < GPU_RING_SLOTS; s++)
            if (vkCreateQueryPool(g->dev, &qpci, NULL, &g->slot[s].tqpool)
                != VK_SUCCESS) {
                g->tqbits = 0;
                gpu_log("RAGE128 GPU: timestamp profiling unavailable\n");
                break;
            }
    }
    return NULL;
}

/* Start the live fold workers and the boot precompile thread. This runs
   last: every ring object whose creation can fail exists by now, so an
   init failure never has to tear down running threads. */
static void
gpu_threads_start(r128_gpu_t *g)
{
    /* Two live workers: enough to keep up with the new tuples a game
       produces in its first minutes, while bounding the memory of
       compiles running at once. With AMD's Windows driver one compile
       was measured at about 6.5 GB, so a third worker could add that
       much again while the game runs. */
    pthread_mutex_init(&g->lq_mtx, NULL);
    pthread_cond_init(&g->lq_cv, NULL);
    if (g->fold_en)
        for (int i = 0; i < 2; i++)
            if (pthread_create(&g->lq_thr[g->lq_nthr], NULL,
                               gpu_live_worker, g)
                == 0)
                g->lq_nthr++;

    if (pthread_create(&g->boot_thr, NULL, gpu_boot_thread, g) == 0)
        g->boot_thr_live = 1;
    else
        gpu_boot_precompile(g);
}

/* Teardown after an init failure: log why, move any imported arena back
   to a plain heap block (with no backend left, the CPU grow and close
   paths treat the arenas as heap blocks), then free the Vulkan objects
   and g. ifn_ok means the instance entry points are loaded, so
   vkDestroy* may be called. */
static void
gpu_init_abort(rage128_t *dev, r128_gpu_t *g, int ifn_ok, const char *why)
{
    gpu_log("RAGE128 GPU: disabled: %s (CPU renderer unaffected)\n",
            why ? why : "unknown");
    if (ifn_ok) {
        if (g->dev) {
            vkDeviceWaitIdle(g->dev);
            if (g->stage_ok) {
                gpu_arena_rollback(g, &dev->tex_stage.arena,
                                   &dev->tex_stage.cap, dev->tex_stage.used,
                                   &g->stage_buf, &g->stage_mem);
                if (!dev->tex_stage.arena) {
                    dev->tex_stage.used = 0;
                    dev->tex_stage.tgen++;
                    dev->tex_stage.src_lo = dev->tex_stage.src_hi = 0;
                }
            }
            if (g->czstage_ok) {
                gpu_czstage_rollback(g, &dev->c_stage,
                                     &g->cstage_buf, &g->cstage_mem);
                gpu_czstage_rollback(g, &dev->z_stage,
                                     &g->zstage_buf, &g->zstage_mem);
            }
            vkDestroyDevice(g->dev, NULL);
        }
        if (g->inst)
            vkDestroyInstance(g->inst, NULL);
    }
    free(g->learned);
    free(g->tuples);
    free(g);
}

static void
rage128_gpu_init_real(rage128_t *dev)
{
    int         mode = dev->gpu_mode_cfg;
    r128_gpu_t *g;
    const char *why    = NULL;
    int         ifn_ok = 0;

    dev->gpu = NULL;
    if (mode == R128_GPU_OFF)
        return;

    {
        const char *jenv = getenv("R128_JIT");

        if (jenv && !strcmp(jenv, "verify")) {
            gpu_log("RAGE128 GPU: disabled (R128_JIT=verify owns the jfn hook)\n");
            return;
        }
    }

    if (!gpu_vk_open())
        return;
    g = gpu_alloc(dev);
    if (!g)
        return;

    why = gpu_vram_reseat(dev, g);
    if (why)
        goto fail;
    why = gpu_instance_create(g);
    if (why)
        goto fail;
    ifn_ok = 1;
    why    = gpu_device_pick(g);
    if (why)
        goto fail;
    why = gpu_device_query(g);
    if (why)
        goto fail;
    why = gpu_queue_pick(g);
    if (why)
        goto fail;
    why = gpu_device_create(g);
    if (why)
        goto fail;

    /* Import svga.vram without a copy, both halves, so present copies
       can write the snapshot half. The buffer is 4 bytes longer than
       that: a guard for texel reads that mask the address and then read
       a word linearly, which can run up to 3 bytes past the vram_mask
       edge. Those are the same host bytes the interpreter reads past
       the mask (the start of snapshot slot 0, on both lanes), so the
       results still match. The imported size stays a multiple of the
       import alignment: gpu_device_query refused an alignment above
       16 KB, and the block from gpu_vram_reseat has 16 KB of zeroed
       slack. */
    why = gpu_import_host(g, dev->svga.vram,
                          (VkDeviceSize) dev->vram_size * 2 + 4,
                          (VkDeviceSize) dev->vram_size * 2 + 16384,
                          &g->vram_buf, &g->vram_mem);
    if (why)
        goto fail;
    gpu_mirror_create(dev, g);
    gpu_arenas_import(dev, g);
    why = gpu_slot_bufs_create(g);
    if (why)
        goto fail;
    why = gpu_layouts_create(g);
    if (why)
        goto fail;
    why = gpu_pcache_load(g);
    if (why)
        goto fail;
    why = gpu_pipelines_create(g);
    if (why)
        goto fail;
    gpu_tuples_prime(g);
    why = gpu_dsets_create(g);
    if (why)
        goto fail;
    why = gpu_cmd_create(g);
    if (why)
        goto fail;
    gpu_threads_start(g);

    dev->gpu = g;
    gpu_gate_census(dev);
    gpu_log("RAGE128 GPU: optional raster backend ON (%s, async=%s), %d kernels "
            "(%s..%s), vram imported zero-copy, %u span / %u tri / %u run "
            "segment caps\n",
            g->verify ? "verify" : "on", g->async ? "on" : "off", GPU_KERNELS,
            r128_gpu_variants[0].name, r128_gpu_variants[GPU_KERNELS - 1].name,
            GPU_SPAN_CAP, GPU_TRI_CAP, GPU_RUN_CAP);
    if (dev->synctel)
        gpu_log("RAGE128 GPU: R128_GPU_SYNC_TELEMETRY is set -- async present, "
                "snapshots and the GPU 2D queue are OFF for this run\n");
    return;

fail:
    gpu_init_abort(dev, g, ifn_ok, why);
}

/* Public init: read the config now (device config reads work only
   inside device init), then put off the Vulkan bring-up until the guest
   first writes an acceleration register. Creating the Vulkan device
   while the host window is still being set up has held up the
   application's event handling for minutes on some hosts, and nothing
   needs the device before the guest's first accelerated operation.
   R128_GPU_DEFER=0 initializes at once instead. */
void
rage128_gpu_init(rage128_t *dev)
{
    const char *de = getenv("R128_GPU_DEFER");

    dev->gpu       = NULL;
    dev->gpu_defer = 0;
    gpu_tel_open(dev);
    dev->gpu_mode_cfg = rage128_gpu_mode();
    if (dev->gpu_mode_cfg == R128_GPU_OFF) {
        /* say so in the telemetry file: a report from a user who never
           turned the backend on is the first thing to rule out */
        if (gpu_tel)
            gpu_log("RAGE128 GPU: off (gpu_raster config is Off)\n");
        return;
    }
    if (de && !strcmp(de, "0")) {
        rage128_gpu_init_real(dev);
        return;
    }
    dev->gpu_defer = 1;
    gpu_log("RAGE128 GPU: init deferred until first accel touch\n");
}

void
rage128_gpu_lazy_init(rage128_t *dev, uint32_t off)
{
    if (!dev->gpu_defer)
        return;
    dev->gpu_defer = 0;
    gpu_log("RAGE128 GPU: deferred init firing (accel reg 0x%04x)\n", off);
    rage128_gpu_init_real(dev);
}

/* Free the aligned host blocks the backend put under svga.vram and the
   staging arenas, and leave their owners NULL: the aligned allocator is
   not free()-compatible on every host, and svga_close and rage128_close
   call free() on them. Runs after the imports are destroyed. */
static void
gpu_vram_unseat(rage128_t *dev)
{
    if (!dev->vram_aligned)
        return;
    if (dev->svga.vram == dev->vram_aligned)
        dev->svga.vram = NULL;
    r128_aligned_free(dev->vram_aligned);
    dev->vram_aligned = NULL;
}

static void
gpu_arena_unseat(uint8_t **arena, uint32_t *cap)
{
    r128_aligned_free(*arena);
    *arena = NULL;
    *cap   = 0;
}

/* Stop the boot precompile and the live fold workers before anything
   they might touch goes away. */
static void
gpu_close_threads(r128_gpu_t *g)
{
    if (g->boot_thr_live) {
        atomic_store(&g->boot_abort, 1);
        pthread_join(g->boot_thr, NULL);
        g->boot_thr_live = 0;
    }
    if (g->lq_nthr) {
        /* the join waits at most for one pipeline create already in
           the driver, as with the boot abort */
        pthread_mutex_lock(&g->lq_mtx);
        g->lq_stop = 1;
        pthread_cond_broadcast(&g->lq_cv);
        pthread_mutex_unlock(&g->lq_mtx);
        for (int i = 0; i < g->lq_nthr; i++)
            pthread_join(g->lq_thr[i], NULL);
        g->lq_nthr = 0;
    }
}

/* Close-time stats, always on: the segment counters, reject shapes,
   flush causes, texture arena and the async submit ledger. */
static void
gpu_stats_core(rage128_t *dev, r128_gpu_t *g)
{
    gpu_log("RAGE128 GPU: close: segments=%llu runs=%llu spans=%llu tris=%llu fallback_draws=%llu"
            " reprice=%llu reprice-miss=%llu toobig=%llu spandrop=%llu pxdrop=%llu cz-alias=%llu rtt-alias=%llu"
            " cz-serial=%llu rtt-serial=%llu line-fb=%llu point-fb=%llu"
            " px=%llu wide-spans=%llu wide-px=%llu odd-c=%llu odd-z=%llu\n",
            (unsigned long long) g->st_segments, (unsigned long long) g->st_runs,
            (unsigned long long) g->st_spans,
            (unsigned long long) g->st_tris, (unsigned long long) g->st_fallback,
            (unsigned long long) g->st_reprice,
            (unsigned long long) g->st_reprice_miss,
            (unsigned long long) g->st_toobig, (unsigned long long) g->st_spandrop,
            (unsigned long long) g->st_pxdrop,
            (unsigned long long) g->st_alias_rej[0],
            (unsigned long long) g->st_alias_rej[1],
            (unsigned long long) g->st_alias_ser[0],
            (unsigned long long) g->st_alias_ser[1],
            (unsigned long long) g->st_fb_line,
            (unsigned long long) g->st_fb_point,
            (unsigned long long) g->st_px_total,
            (unsigned long long) g->st_wide_spans,
            (unsigned long long) g->st_wide_px,
            (unsigned long long) g->st_odd_c,
            (unsigned long long) g->st_odd_z);
    if (g->st_rej_total) {
        /* one key per failed gate predicate; a reject failing several
           predicates counts under each, so keys can sum past total */
        char  line[512];
        char *p   = line;
        char *end = line + sizeof(line);

        *p = '\0';
        for (int i = 0; i < GPU_REJ_KEYS; i++)
            if (g->st_rej[i]) {
                int n_ = snprintf(p, (size_t) (end - p), " %s=%llu",
                                  gpu_rej_name[i],
                                  (unsigned long long) g->st_rej[i]);

                p = (n_ < 0 || n_ >= end - p) ? end - 1 : p + n_;
            }
        gpu_log("RAGE128 GPU: reject shapes: total=%llu%s\n",
                (unsigned long long) g->st_rej_total, line);
    }
    {
        char  line[256];
        char *p = line;

        for (int i = 0; i < GF_CAUSES; i++)
            gpu_append(&p, line + sizeof(line), " %s=%llu", gf_name[i],
                       (unsigned long long) g->st_flush[i]);
        gpu_log("RAGE128 GPU: flush causes:%s\n", line);
    }
    gpu_log("RAGE128 GPU: tex-arena: recycles draw=%llu flush=%llu wrap=%llu "
            "grows=%llu hits=%llu kills=%llu peak=%u cap=%u\n",
            (unsigned long long) dev->tex_stage.n_rec_draw,
            (unsigned long long) dev->tex_stage.n_rec_flush,
            (unsigned long long) dev->tex_stage.n_rec_wrap,
            (unsigned long long) dev->tex_stage.n_grow,
            (unsigned long long) dev->tex_stage.n_hit,
            (unsigned long long) dev->tex_stage.n_kill,
            dev->tex_stage.peak_used, dev->tex_stage.cap);
    {
        char  line[256];
        char *p = line;

        for (int i = 0; i < GF_CAUSES; i++)
            gpu_append(&p, line + sizeof(line), " %s=%llu", gf_name[i],
                       (unsigned long long) g->st_wait_drain[i]);
        gpu_log("RAGE128 GPU: async: %s submits=%llu chained=%llu max=%u "
                "reuse-waits=%llu depth-waits=%llu cap=%u reaped=%llu "
                "drains:%s\n",
                g->async ? "on" : "off", (unsigned long long) g->st_submits,
                (unsigned long long) g->st_chained, g->st_max_inflight,
                (unsigned long long) g->st_wait_reuse,
                (unsigned long long) g->st_wait_depth,
                atomic_load(&g->depth_cap),
                (unsigned long long) g->st_reaped, line);
    }
    /* Scanout against the pending segment and the slots in flight: lines
       whose overlap submitted the segment from the scan thread (kicks),
       lines that left the submit to a busy executor (defers), submits the
       executor made for those requests (served), and lines rendered ahead
       of a GPU store that had not landed (stale). Printed on every close,
       profiling or not, so a run without the profiler still shows what
       the scan barrier did; a zero here is a result. */
    gpu_log("RAGE128 GPU: scan: kicks=%llu stale=%llu defers=%llu "
            "served=%llu\n",
            (unsigned long long) g->st_scan_kicks,
            (unsigned long long) g->st_scan_stale,
            (unsigned long long) g->st_scan_defers,
            (unsigned long long) atomic_load(&g->st_scan_served));
}

/* Profiling only: where the submit path waited, and the read-fence
   pages that cost the most. */
static void
gpu_stats_prof_waits(r128_gpu_t *g)
{
    if (g->prof) {
        char  line[256];
        char *p = line;

        for (int i = 0; i < GF_CAUSES; i++)
            gpu_append(&p, line + sizeof(line), " %s=%llu", gf_name[i],
                       (unsigned long long) (g->pr_wait_drain_cause_ns[i]
                                             / 1000000));
        gpu_log("RAGE128 GPU: wait ms: reuse=%llu depth=%llu cpu-quiesce=%llu "
                "(hits=%llu) read-fence=%llu (n=%llu) drains:%s\n",
                (unsigned long long) (g->pr_wait_reuse_ns / 1000000),
                (unsigned long long) (g->pr_wait_depth_ns / 1000000),
                (unsigned long long) (g->pr_cpu_quiesce_ns / 1000000),
                (unsigned long long) g->st_cpu_barrier_hits,
                (unsigned long long) (g->pr_read_fence_ns / 1000000),
                (unsigned long long) g->st_read_fences, line);
    }
    if (g->prof && g->pr_rf_used) {
        /* sort by total wait, not call count, since the wait is what
           the depth governor responds to; a selection sort, as the
           table is small */
        for (uint32_t n = 0; n < g->pr_rf_used && n < 16; n++) {
            uint32_t best = n;

            for (uint32_t i = n + 1; i < g->pr_rf_used; i++)
                if (g->pr_rf_ns[i] > g->pr_rf_ns[best])
                    best = i;
            if (best != n) {
                uint32_t tk = g->pr_rf_key[n];
                uint64_t tc = g->pr_rf_cnt[n], tn = g->pr_rf_ns[n];

                g->pr_rf_key[n]    = g->pr_rf_key[best];
                g->pr_rf_cnt[n]    = g->pr_rf_cnt[best];
                g->pr_rf_ns[n]     = g->pr_rf_ns[best];
                g->pr_rf_key[best] = tk;
                g->pr_rf_cnt[best] = tc;
                g->pr_rf_ns[best]  = tn;
            }
            gpu_log("RAGE128 GPU: read-fence page %08x: n=%llu ms=%llu\n",
                    g->pr_rf_key[n] << 16, (unsigned long long) g->pr_rf_cnt[n],
                    (unsigned long long) (g->pr_rf_ns[n] / 1000000));
        }
        gpu_log("RAGE128 GPU: live-scan why: backward=%llu stale=%llu "
                "other-base=%llu in-flight=%llu no-snaps=%llu\n",
                (unsigned long long) g->st_snap_why[0],
                (unsigned long long) g->st_snap_why[1],
                (unsigned long long) g->st_snap_why[2],
                (unsigned long long) g->st_snap_why[3],
                (unsigned long long) g->st_snap_why[4]);
        gpu_log("RAGE128 GPU: read-fence pages: %u tracked, %u dropped\n",
                g->pr_rf_used, g->pr_rf_dropped);
    }
}

/* Present snapshots, the 2D queue per op table, the 2D stage ring and
   OV0. Each line prints only when its counters moved. */
static void
gpu_stats_2d(r128_gpu_t *g)
{
    if (g->st_snap_copies || g->st_snap_drops || g->st_snap_shows
        || g->st_snap_real) {
        gpu_log("RAGE128 GPU: present: copies=%llu drops=%llu "
                "snap-frames=%llu real-frames=%llu reshow=%llu late=%llu\n",
                (unsigned long long) g->st_snap_copies,
                (unsigned long long) g->st_snap_drops,
                (unsigned long long) g->st_snap_shows,
                (unsigned long long) g->st_snap_real,
                (unsigned long long) g->st_snap_reshow,
                (unsigned long long) g->st_snap_late);
    }
    if (g->st_texq_spared || g->st_texq_real || g->st_texq_ovf)
        gpu_log("RAGE128 GPU: texq: 2d ops with a hull hit cleared=%llu "
                "confirmed=%llu segments fallen back to the hull=%llu "
                "(cap %u)\n",
                (unsigned long long) g->st_texq_spared,
                (unsigned long long) g->st_texq_real,
                (unsigned long long) g->st_texq_ovf, GPU_TEXQ_CAP);
    if (atomic_load(&g->st_fq_spared) || atomic_load(&g->st_fq_real)
        || atomic_load(&g->st_fqp_spared) || atomic_load(&g->st_fqp_real)
        || g->st_fq_ovf)
        gpu_log("RAGE128 GPU: fq: slot 2d-store hull hits cleared=%llu "
                "confirmed=%llu pending cleared=%llu confirmed=%llu "
                "segments fallen back to the hull=%llu (cap %u)\n",
                (unsigned long long) atomic_load(&g->st_fq_spared),
                (unsigned long long) atomic_load(&g->st_fq_real),
                (unsigned long long) atomic_load(&g->st_fqp_spared),
                (unsigned long long) atomic_load(&g->st_fqp_real),
                (unsigned long long) g->st_fq_ovf, GPU_FQ_CAP);
    if (g->prof)
        for (uint32_t tag = 0; tag < GPU_2D_TAGS; tag++)
            if (g->st_2d_requests[tag] || g->st_2d_waits[tag])
                gpu_log("RAGE128 GPU: 2d drain: op=%03x/%s requests=%llu waits=%llu "
                        "wait=%llums\n",
                        tag, gpu_2d_tag_name(tag),
                        (unsigned long long) g->st_2d_requests[tag],
                        (unsigned long long) g->st_2d_waits[tag],
                        (unsigned long long) (g->pr_2d_tag_ns[tag] / 1000000));
    if (g->st_fill_rects || g->st_fill_cpu || g->st_fill_skip[0]
        || g->st_fill_skip[1] || g->st_fill_skip[2] || g->st_fill_skip[3]
        || g->st_fill_skip[4])
        gpu_log("RAGE128 GPU: 2d fill: rects=%llu rows=%llu px=%llu tiled=%llu "
                "cpu=%llu (dom=%llu stage=%llu) nb=%llu skip: "
                "rop=%llu wmask=%llu aux=%llu pat=%llu cca=%llu bad=%llu\n",
                (unsigned long long) g->st_fill_rects,
                (unsigned long long) g->st_fill_rows,
                (unsigned long long) g->st_fill_px,
                (unsigned long long) g->st_fill_tiled,
                (unsigned long long) g->st_fill_cpu,
                (unsigned long long) g->st_fill_cpu_r[0],
                (unsigned long long) g->st_fill_cpu_r[1],
                (unsigned long long) g->st_qnb[3],
                (unsigned long long) g->st_fill_skip[0],
                (unsigned long long) g->st_fill_skip[1],
                (unsigned long long) g->st_fill_skip[2],
                (unsigned long long) g->st_fill_skip[3],
                (unsigned long long) g->st_fill_skip[4],
                (unsigned long long) g->st_fill_bad);
    /* the callers set only why bits 8, 32 and 64 for tables 0 to 2, and
       only 16, 32 and 64 for table 3, so the other columns would always
       be zero and are not printed */
    if (g->st_mono_runs || g->st_mono_orects || g->st_mono_cpu
        || g->st_qaux[0]
        || g->st_qskip[0][3] || g->st_qskip[0][5] || g->st_qskip[0][6])
        gpu_log("RAGE128 GPU: 2d mono: runs=%llu px=%llu orects=%llu "
                "obytes=%llu cpu=%llu nb=%llu auxseg=%llu vqbad=%llu skip: "
                "frag=%llu dom=%llu stage=%llu\n",
                (unsigned long long) g->st_mono_runs,
                (unsigned long long) g->st_mono_px,
                (unsigned long long) g->st_mono_orects,
                (unsigned long long) g->st_mono_obytes,
                (unsigned long long) g->st_mono_cpu,
                (unsigned long long) g->st_qnb[0],
                (unsigned long long) g->st_qaux[0],
                (unsigned long long) g->st_qbad[0],
                (unsigned long long) g->st_qskip[0][3],
                (unsigned long long) g->st_qskip[0][5],
                (unsigned long long) g->st_qskip[0][6]);
    if (g->st_copy_rects || g->st_copy_cpu || g->st_copy_bad
        || g->st_qaux[1]
        || g->st_qskip[1][3] || g->st_qskip[1][5] || g->st_qskip[1][6])
        gpu_log("RAGE128 GPU: 2d host: rects=%llu rows=%llu bytes=%llu "
                "cpu=%llu bad=%llu nb=%llu auxseg=%llu vqbad=%llu skip: "
                "frag=%llu dom=%llu stage=%llu mid=%llu mid-split=%llu\n",
                (unsigned long long) g->st_copy_rects,
                (unsigned long long) g->st_copy_rows,
                (unsigned long long) g->st_copy_bytes,
                (unsigned long long) g->st_copy_cpu,
                (unsigned long long) g->st_copy_bad,
                (unsigned long long) g->st_qnb[1],
                (unsigned long long) g->st_qaux[1],
                (unsigned long long) g->st_qbad[1],
                (unsigned long long) g->st_qskip[1][3],
                (unsigned long long) g->st_qskip[1][5],
                (unsigned long long) g->st_qskip[1][6],
                (unsigned long long) g->st_copy_mid,
                (unsigned long long) g->st_copy_mid_split);
    if (g->st_pat_copies || g->st_pat_runs || g->st_pat_cpu
        || g->st_qaux[2]
        || g->st_qskip[2][3] || g->st_qskip[2][5] || g->st_qskip[2][6])
        gpu_log("RAGE128 GPU: 2d pat: copies=%llu runs=%llu px=%llu "
                "cpu=%llu nb=%llu auxseg=%llu vqbad=%llu skip: "
                "frag=%llu dom=%llu stage=%llu\n",
                (unsigned long long) g->st_pat_copies,
                (unsigned long long) g->st_pat_runs,
                (unsigned long long) g->st_pat_px,
                (unsigned long long) g->st_pat_cpu,
                (unsigned long long) g->st_qnb[2],
                (unsigned long long) g->st_qaux[2],
                (unsigned long long) g->st_qbad[2],
                (unsigned long long) g->st_qskip[2][3],
                (unsigned long long) g->st_qskip[2][5],
                (unsigned long long) g->st_qskip[2][6]);
    if (g->st_blit_rects || g->st_blit_cpu || g->st_blit_bad
        || g->st_qaux[3]
        || g->st_qskip[3][4] || g->st_qskip[3][5] || g->st_qskip[3][6])
        gpu_log("RAGE128 GPU: 2d blit: rects=%llu rows=%llu bytes=%llu "
                "bounce=%llu cpu=%llu auxseg=%llu bad=%llu skip: "
                "cca=%llu dom=%llu stage=%llu\n",
                (unsigned long long) g->st_blit_rects,
                (unsigned long long) g->st_blit_rows,
                (unsigned long long) g->st_blit_bytes,
                (unsigned long long) g->st_blit_bounce,
                (unsigned long long) g->st_blit_cpu,
                (unsigned long long) g->st_qaux[3],
                (unsigned long long) g->st_blit_bad,
                (unsigned long long) g->st_qskip[3][4],
                (unsigned long long) g->st_qskip[3][5],
                (unsigned long long) g->st_qskip[3][6]);
    for (int t = 4; t < GPU_Q2D_TABLES; t++) {
        static const char *qn[GPU_Q2D_TABLES] = {
            NULL, NULL, NULL, NULL, "key", "stretch", "line", "grad", "rmw",
            "monopat"
        };
        int any = g->st_qdone[t] || g->st_q_runs[t] || g->st_q_cpu[t]
            || g->st_qbad[t] || g->st_qaux[t];

        for (int b = 0; b < 7; b++)
            any = any || g->st_qskip[t][b];
        if (!any)
            continue;
        gpu_log("RAGE128 GPU: 2d %s: rects=%llu runs=%llu bytes=%llu "
                "cpu=%llu auxseg=%llu bad=%llu skip: rop=%llu wmask=%llu "
                "aux=%llu "
                "frag=%llu cca=%llu dom=%llu stage=%llu\n",
                qn[t],
                (unsigned long long) g->st_qdone[t],
                (unsigned long long) g->st_q_runs[t],
                (unsigned long long) g->st_q_bytes[t],
                (unsigned long long) g->st_q_cpu[t],
                (unsigned long long) g->st_qaux[t],
                (unsigned long long) g->st_qbad[t],
                (unsigned long long) g->st_qskip[t][0],
                (unsigned long long) g->st_qskip[t][1],
                (unsigned long long) g->st_qskip[t][2],
                (unsigned long long) g->st_qskip[t][3],
                (unsigned long long) g->st_qskip[t][4],
                (unsigned long long) g->st_qskip[t][5],
                (unsigned long long) g->st_qskip[t][6]);
    }
    if (g->st_tiled_cpu)
        gpu_log("RAGE128 GPU: 2d tiled: cpu=%llu\n",
                (unsigned long long) g->st_tiled_cpu);
    if (g->st_2ds_bytes)
        gpu_log("RAGE128 GPU: 2d stage: bytes=%llu recycles=%llu full=%llu\n",
                (unsigned long long) g->st_2ds_bytes,
                (unsigned long long) g->st_2ds_recycles,
                (unsigned long long) g->st_2ds_full);
    if (g->st_ov0_frames || g->st_ov0_cpu || g->st_ov0_bad)
        gpu_log("RAGE128 GPU: ov0: frames=%llu px=%llu cpu=%llu bad=%llu\n",
                (unsigned long long) g->st_ov0_frames,
                (unsigned long long) g->st_ov0_px,
                (unsigned long long) g->st_ov0_cpu,
                (unsigned long long) g->st_ov0_bad);
}

/* Coverage: which accepted paths the run actually used. The list of
   kernels never reached matters most: a verify run is evidence only
   for the states it reached. */
static void
gpu_stats_coverage(r128_gpu_t *g)
{
    {
        char  line[512];
        char *p   = line;
        char *end = line + sizeof(line);
        int   hit = 0;

        /* snprintf returns the length the output would have had, so
           advancing p by it unchecked can move p past end and make
           (end - p) a huge size_t. Clamp. */
#    define COV_ADD(...)                                           \
        do {                                                       \
            int n_ = snprintf(p, (size_t) (end - p), __VA_ARGS__); \
            p      = (n_ < 0 || n_ >= end - p) ? end - 1 : p + n_; \
        } while (0)

        *p = '\0';
        for (int i = 0; i < 16; i++)
            if (g->st_cov_dt[i])
                COV_ADD(" dt%d=%llu", i, (unsigned long long) g->st_cov_dt[i]);
        gpu_log("RAGE128 GPU: coverage tris:%s dith-on=%llu dith-off=%llu wmask-partial=%llu"
                " spec=%llu fogv=%llu fogt=%llu ck=%llu smip=%llu cstg=%llu"
                " zstg=%llu affine=%llu"
                " affine-lod=%llu"
                " line=%llu line-cpu=%llu point=%llu point-cpu=%llu"
                " z-off=%llu z16=%llu z32lo=%llu z32hi=%llu"
                " sten-lo=%llu sten-hi=%llu sten-zoff=%llu\n",
                line, (unsigned long long) g->st_cov_dith[1],
                (unsigned long long) g->st_cov_dith[0],
                (unsigned long long) g->st_cov_wmask_partial,
                (unsigned long long) g->st_cov_spec,
                (unsigned long long) g->st_cov_fogv,
                (unsigned long long) g->st_cov_fogt,
                (unsigned long long) g->st_cov_ck,
                (unsigned long long) g->st_cov_smip,
                (unsigned long long) g->st_cov_cstg,
                (unsigned long long) g->st_cov_zstg,
                (unsigned long long) g->st_cov_affine,
                (unsigned long long) g->st_cov_affine_lod,
                (unsigned long long) g->st_cov_line[1],
                (unsigned long long) g->st_cov_line[0],
                (unsigned long long) g->st_cov_point[1],
                (unsigned long long) g->st_cov_point[0],
                (unsigned long long) g->st_cov_z[0],
                (unsigned long long) g->st_cov_z[1],
                (unsigned long long) g->st_cov_z[2],
                (unsigned long long) g->st_cov_z[3],
                (unsigned long long) g->st_cov_sten[0],
                (unsigned long long) g->st_cov_sten[1],
                (unsigned long long) g->st_cov_sten[2]);
        p  = line;
        *p = '\0';
        for (int i = 0; i < GPU_KERNELS; i++)
            if (!g->st_cov_k[i])
                COV_ADD(" %s", r128_gpu_variants[i].name);
            else
                hit++;
        if (p == line)
            snprintf(line, sizeof(line), " (none)");
        gpu_log("RAGE128 GPU: coverage kernels: %d/%d reached, unreached:%s\n",
                hit, GPU_KERNELS, line);
        p  = line;
        *p = '\0';
        for (int i = 0; i < 16; i++)
            if (g->st_cov_tex[i])
                COV_ADD(" tex%d=%llu", i, (unsigned long long) g->st_cov_tex[i]);
        for (int i = 0; i < 4; i++)
            if (g->st_cov_texs3[i])
                COV_ADD(" texs3c%d=%llu", i,
                        (unsigned long long) g->st_cov_texs3[i]);
        if (p == line)
            snprintf(line, sizeof(line), " (none)");
        gpu_log("RAGE128 GPU: coverage texfmt (per stage-use):%s\n", line);
#    undef COV_ADD
    }
    if (g->verify)
        gpu_log("RAGE128 GPU: verify: segments=%llu mismatched=%llu\n",
                (unsigned long long) g->st_verify_seg,
                (unsigned long long) g->st_verify_bad);
}

/* Profiling only: the run's time split, capture / level-sort shape,
   the guest's hottest register reads and the LFB trace. */
static void
gpu_stats_prof_run(rage128_t *dev, r128_gpu_t *g)
{
    if (g->prof)
        gpu_log("RAGE128 GPU: prof: wall=%llums capture=%llums sort=%llums "
                "record=%llums wait=%llums reuse=%llums drain=%llums "
                "px=%llu wg=%llu bat=%llu crit=%llu crit_lvl=%llu\n",
                (unsigned long long) ((prof_now() - g->pr_t0) / 1000000),
                (unsigned long long) (g->pr_capture_ns / 1000000),
                (unsigned long long) (g->pr_sort_ns / 1000000),
                (unsigned long long) (g->pr_record_ns / 1000000),
                (unsigned long long) (g->pr_wait_ns / 1000000),
                (unsigned long long) (g->pr_wait_reuse_ns / 1000000),
                (unsigned long long) (g->pr_wait_drain_ns / 1000000),
                (unsigned long long) g->pr_px,
                (unsigned long long) g->pr_wg,
                (unsigned long long) g->pr_bat,
                (unsigned long long) g->pr_crit,
                (unsigned long long) g->pr_crit_lvl);
    if (g->prof) {
        uint64_t sub = g->pr_sort_level_ns + g->pr_sort_emit_ns
            + g->pr_sort_stat_ns;

        gpu_log("RAGE128 GPU: prof: sort split: level=%llums emit=%llums "
                "profstat=%llums scatter=%llums rows=%llu pairs=%llu\n",
                (unsigned long long) (g->pr_sort_level_ns / 1000000),
                (unsigned long long) (g->pr_sort_emit_ns / 1000000),
                (unsigned long long) (g->pr_sort_stat_ns / 1000000),
                (unsigned long long) ((g->pr_sort_ns > sub
                                           ? g->pr_sort_ns - sub
                                           : 0)
                                      / 1000000),
                (unsigned long long) g->pr_sort_rows,
                (unsigned long long) g->pr_lvpairs);
        gpu_log("RAGE128 GPU: prof: capture shape: rows=%llu clipwalk=%llu "
                "bbox=%llu covered=%llu\n",
                (unsigned long long) g->pr_cap_rows,
                (unsigned long long) g->pr_cap_lo,
                (unsigned long long) g->pr_cap_bbox,
                (unsigned long long) g->st_px_total);
        gpu_log("RAGE128 GPU: prof: level rows by class: uniform=%llu "
                "clean=%llu/%llupairs mixed=%llu/%llupairs\n",
                (unsigned long long) g->pr_lv_rows_cls[0],
                (unsigned long long) g->pr_lv_rows_cls[1],
                (unsigned long long) g->pr_lv_pairs_cls[1],
                (unsigned long long) g->pr_lv_rows_cls[2],
                (unsigned long long) g->pr_lv_pairs_cls[2]);
        for (unsigned b = 0; b < GPU_LVBINS; b++)
            if (g->pr_lvbin_rows[b])
                gpu_log("RAGE128 GPU: prof: level bin %5u%c rows=%llu "
                        "pairs=%llu ns=%llums\n",
                        1u << b, (b == GPU_LVBINS - 1u) ? '+' : ' ',
                        (unsigned long long) g->pr_lvbin_rows[b],
                        (unsigned long long) g->pr_lvbin_pairs[b],
                        (unsigned long long) (g->pr_lvbin_ns[b] / 1000000));
        /* the 12 registers the guest read most over the whole run, each
           with the reads made between a flip and the next frame's first
           draw: n shows which register the guest polls, gap whether it
           polls between frames or inside one */
        for (unsigned k = 0; k < 12; k++) {
            unsigned best = 0;

            for (unsigned i = 1; i < 0x4000 >> 2; i++)
                if (dev->tel_reads[i] > dev->tel_reads[best])
                    best = i;
            if (!dev->tel_reads[best])
                break;
            gpu_log("RAGE128 GPU: prof: reg reads: reg 0x%04x n=%u gap=%u\n",
                    best << 2, dev->tel_reads[best], dev->ftl_reads[best]);
            dev->tel_reads[best] = 0;
            dev->ftl_reads[best] = 0;
        }
        gpu_lfbt_print(&dev->lfbt, NULL, "run",
                       g->pr_int_epoch ? (prof_now() - g->pr_int_epoch) / 1000000 : 0);
    }
}

/* Profiling only: packing efficiency, GPU timestamps, per-kernel fanout
   histograms, dispatch barriers, cap hits and the per-kernel pixel split. */
static void
gpu_stats_prof_pack(r128_gpu_t *g)
{
    if (g->prof) {
        char  line[512];
        char *p = line;

        gpu_log("RAGE128 GPU: pack: lvl=%llu bat=%llu slots=%llu nruns=%llu "
                "zless_px=%llu\n",
                (unsigned long long) g->pr_lvl, (unsigned long long) g->pr_bat,
                (unsigned long long) g->pr_slots, (unsigned long long) g->st_nruns,
                (unsigned long long) g->pr_px_zless);
        gpu_log("RAGE128 GPU: gpu time: ladder=%llums shade=%llums samples=%llu "
                "tqbits=%u maxwg=%u\n",
                (unsigned long long) (g->pr_gpu_lad_ns / 1000000),
                (unsigned long long) (g->pr_gpu_shade_ns / 1000000),
                (unsigned long long) g->pr_gpu_seg, g->tqbits, g->max_wg_x);
        if (g->mirror_en)
            gpu_log("RAGE128 GPU: vram mirror: copies=%llums bytes=%llu\n",
                    (unsigned long long) (g->pr_gpu_mirror_ns / 1000000),
                    (unsigned long long) g->pr_mirror_bytes);
        gpu_log("RAGE128 GPU: fanout hist bins: <48 <120 <240 <480 <960 <1920 "
                "<3840 <7680 >=7680 (pixel-weighted)\n");
        for (int v = 0; v < GPU_KERNELS; v++) {
            char  hc[256], hp[256];
            char *pc = hc, *pp = hp;

            if (!g->pr_runs_k[v])
                continue;
            for (unsigned b = 0; b < GPU_WGH_BINS; b++) {
                gpu_append(&pc, hc + sizeof(hc), " %llu",
                           (unsigned long long) g->pr_wgh_cur[v][b]);
                gpu_append(&pp, hp + sizeof(hp), " %llu",
                           (unsigned long long) g->pr_wgh_proj[v][b]);
            }
            gpu_log("RAGE128 GPU: fanout %s: runs=%llu rows=%llu/%llu "
                    "px=%llu/%llu bat=%llu/%llu wg=%llu->%llu\n",
                    r128_gpu_variants[v].name,
                    (unsigned long long) g->pr_runs_k[v],
                    (unsigned long long) g->pr_erows_k[v],
                    (unsigned long long) g->pr_rows_k[v],
                    (unsigned long long) g->pr_epx_k[v],
                    (unsigned long long) g->pr_px_k[v],
                    (unsigned long long) g->pr_ebat_k[v],
                    (unsigned long long) g->pr_bat_k[v],
                    (unsigned long long) g->pr_wg_k[v],
                    (unsigned long long) g->pr_pwg_k[v]);
            gpu_log("RAGE128 GPU: fanout hist %s: cur:%s proj:%s\n",
                    r128_gpu_variants[v].name, hc, hp);
        }
        gpu_log("RAGE128 GPU: dispatch: barriers=%llu elided=%llu texture=%llu\n",
                (unsigned long long) g->pr_dbar,
                (unsigned long long) g->pr_delide,
                (unsigned long long) g->pr_dtex);
        gpu_log("RAGE128 GPU: cap: span=%llu tri=%llu px=%llu run=%llu\n",
                (unsigned long long) g->st_cap[0], (unsigned long long) g->st_cap[1],
                (unsigned long long) g->st_cap[2], (unsigned long long) g->st_cap[3]);
        for (int v = 0; v < GPU_KERNELS; v++)
            gpu_append(&p, line + sizeof(line), " %s=%llu",
                       r128_gpu_variants[v].name,
                       (unsigned long long) g->pr_px_k[v]);
        gpu_log("RAGE128 GPU: kernel px:%s\n", line);
    }
}

/* Save the caches to disk, then destroy every Vulkan object down to the
   instance. The caller frees the host blocks under the imports
   afterwards. */
static void
gpu_close_vk(r128_gpu_t *g)
{
    vkDeviceWaitIdle(g->dev);
    gpu_persist_save(g); /* pipeline cache data and the learned list */
    for (unsigned s = 0; s < GPU_RING_SLOTS; s++) {
        if (g->slot[s].tqpool)
            vkDestroyQueryPool(g->dev, g->slot[s].tqpool, NULL);
        if (g->slot[s].fence) {
            vkDestroyFence(g->dev, g->slot[s].fence, NULL);
            pthread_mutex_destroy(&g->slot[s].fence_mtx);
        }
    }
    for (unsigned s = 0; s < GPU_SNAPS; s++)
        if (g->snaps[s].fence) {
            vkDestroyFence(g->dev, g->snaps[s].fence, NULL);
            pthread_mutex_destroy(&g->snaps[s].mtx);
        }
    vkDestroyCommandPool(g->dev, g->pool, NULL);
    vkDestroyDescriptorPool(g->dev, g->dpool, NULL);
    for (int v = 0; v < GPU_KERNELS; v++)
        vkDestroyPipeline(g->dev, g->pipes[v], NULL);
    for (int v = 0; v < GPU_KERNELS; v++)
        if (g->pipes_serial[v])
            vkDestroyPipeline(g->dev, g->pipes_serial[v], NULL);
    for (uint32_t i = 0; i < g->ntuples; i++)
        for (int v = 0; v < GPU_KERNELS; v++)
            if (g->tuples[i].pipes[v])
                vkDestroyPipeline(g->dev, g->tuples[i].pipes[v], NULL);
    vkDestroyPipeline(g->dev, g->lad_pipe, NULL);
    vkDestroyPipeline(g->dev, g->rmw_pipe, NULL);
    vkDestroyPipelineLayout(g->dev, g->rmw_plyt, NULL);
    vkDestroyDescriptorSetLayout(g->dev, g->rmw_dsl, NULL);
    vkDestroyPipeline(g->dev, g->ov0_pipe, NULL);
    vkDestroyPipelineLayout(g->dev, g->ov0_plyt, NULL);
    vkDestroyDescriptorSetLayout(g->dev, g->ov0_dsl, NULL);
    if (g->ov0_out.b) {
        vkDestroyBuffer(g->dev, g->ov0_out.b, NULL);
        vkFreeMemory(g->dev, g->ov0_out.m, NULL);
    }
    if (g->ov0_fence)
        vkDestroyFence(g->dev, g->ov0_fence, NULL);
    if (g->ov0_pool)
        vkDestroyCommandPool(g->dev, g->ov0_pool, NULL);
    pthread_mutex_destroy(&g->queue_mtx);
    pthread_mutex_destroy(&g->flush_mtx);
    vkDestroyShaderModule(g->dev, g->seg_sm, NULL);
    vkDestroyPipelineCache(g->dev, g->pcache, NULL);
    pthread_mutex_destroy(&g->fold_mtx);
    pthread_mutex_destroy(&g->lq_mtx);
    pthread_cond_destroy(&g->lq_cv);
    vkDestroyPipelineLayout(g->dev, g->plyt, NULL);
    vkDestroyDescriptorSetLayout(g->dev, g->dsl, NULL);
    for (unsigned s = 0; s < GPU_RING_SLOTS; s++) {
        gpu_slot_t     *sl      = &g->slot[s];
        r128_gpu_buf_t *bufs[6] = { &sl->b_rows, &sl->b_order, &sl->b_spans,
                                    &sl->b_tris, &sl->b_lad, &sl->b_pal };

        for (int i = 0; i < 6; i++) {
            vkDestroyBuffer(g->dev, bufs[i]->b, NULL);
            vkFreeMemory(g->dev, bufs[i]->m, NULL);
        }
    }
    if (g->stage2d_ok) {
        vkDestroyBuffer(g->dev, g->stage2d.b, NULL);
        vkFreeMemory(g->dev, g->stage2d.m, NULL);
    }
    vkDestroyBuffer(g->dev, g->vram_buf, NULL);
    vkFreeMemory(g->dev, g->vram_mem, NULL);
    if (g->mirror_buf) {
        vkDestroyBuffer(g->dev, g->mirror_buf, NULL);
        vkFreeMemory(g->dev, g->mirror_mem, NULL);
    }
    if (g->stage_ok) {
        vkDestroyBuffer(g->dev, g->stage_buf, NULL);
        vkFreeMemory(g->dev, g->stage_mem, NULL);
    }
    if (g->czstage_ok) {
        vkDestroyBuffer(g->dev, g->cstage_buf, NULL);
        vkFreeMemory(g->dev, g->cstage_mem, NULL);
        vkDestroyBuffer(g->dev, g->zstage_buf, NULL);
        vkFreeMemory(g->dev, g->zstage_mem, NULL);
    }
    vkDestroyDevice(g->dev, NULL);
    vkDestroyInstance(g->inst, NULL);
}

void
rage128_gpu_close(rage128_t *dev)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    /* Init can fail after svga.vram was moved, leaving the aligned
       block in place with no backend to tear down. With a backend the
       block may be freed only after the import is destroyed, at the
       end of this function. */
    if (!g) {
        gpu_vram_unseat(dev);
        gpu_tel_close();
        return;
    }
    dev->gpu = NULL;

    gpu_close_threads(g);
    gpu_drain_all(g, -2);
    gpu_prof_interval(g, 1);

    gpu_stats_core(dev, g);
    gpu_stats_prof_waits(g);
    gpu_stats_2d(g);
    gpu_stats_coverage(g);
    gpu_stats_prof_run(dev, g);
    /* Fold stats, always printed: the live compile count and time show
       what compiling during play costs, and unknown > 0 means tuples
       were seen that the seed list does not have. */
    gpu_log("RAGE128 GPU: fold: tuples=%u tsplit=%llu overflow=%llu unknown=%llu "
            "boot=%llu/%llums live=%llu/%llums\n",
            g->ntuples, (unsigned long long) g->st_tsplit,
            (unsigned long long) g->st_tovfl,
            (unsigned long long) g->st_tunknown,
            (unsigned long long) g->st_boot_pipes,
            (unsigned long long) (g->st_boot_ns / 1000000),
            (unsigned long long) g->st_live_pipes,
            (unsigned long long) (g->st_live_ns / 1000000));
    gpu_stats_prof_pack(g);

    gpu_close_vk(g);
    /* With the imports destroyed the aligned host blocks can be freed.
       The arenas the backend moved are freed here, not by
       rage128_close. */
    if (g->stage_ok)
        gpu_arena_unseat(&dev->tex_stage.arena, &dev->tex_stage.cap);
    if (g->czstage_ok) {
        gpu_arena_unseat(&dev->c_stage.arena, &dev->c_stage.cap);
        gpu_arena_unseat(&dev->z_stage.arena, &dev->z_stage.cap);
    }
    gpu_vram_unseat(dev);
    free(g->learned);
    free(g->tuples);
    free(g->v_states);
    free(g->v_tris);
    free(g->v_save);
    free(g);
    gpu_tel_close();
}

#else /* !R128_GPU_HAVE_VULKAN */

void
rage128_gpu_init(rage128_t *dev)
{
    dev->gpu = NULL;
    if (rage128_gpu_mode() != R128_GPU_OFF)
        pclog("RAGE128 GPU: disabled: built without Vulkan support\n");
}

void
rage128_gpu_lazy_init(rage128_t *dev, uint32_t off)
{
    (void) dev;
    (void) off;
}
void
rage128_gpu_close(rage128_t *dev)
{
    (void) dev;
}
int
rage128_gpu_census_kernel(rage128_t *dev, const rage128_raster_state_t *rs)
{
    (void) dev;
    (void) rs;
    return -2;
}
void
rage128_gpu_flush(rage128_t *dev, uint32_t tag)
{
    (void) dev;
    (void) tag;
}
void
rage128_gpu_abandon(rage128_t *dev)
{
    (void) dev;
}
void
rage128_gpu_present(rage128_t *dev)
{
    (void) dev;
}
void
rage128_gpu_ftl_flip(rage128_t *dev)
{
    (void) dev;
}
int
rage128_gpu_engine_busy(rage128_t *dev)
{
    (void) dev;
    return 0;
}
void
rage128_gpu_cpu_barrier(rage128_t *dev, uint32_t addr, uint32_t len, int writes)
{
    (void) dev;
    (void) addr;
    (void) len;
    (void) writes;
}
int
rage128_gpu_scan_barrier(rage128_t *dev, uint32_t addr, uint32_t len)
{
    (void) dev;
    (void) addr;
    (void) len;
    return 1;
}
void
rage128_gpu_scan_serve(rage128_t *dev)
{
    (void) dev;
}
void
rage128_gpu_flip_mark(rage128_t *dev)
{
    (void) dev;
}
void
rage128_gpu_present_copy(rage128_t *dev, uint32_t src, uint32_t len)
{
    (void) dev;
    (void) src;
    (void) len;
}
int
rage128_gpu_snap_pick(rage128_t *dev, uint32_t want_src, uint32_t *dst,
                      uint64_t *seq)
{
    (void) dev;
    (void) want_src;
    (void) dst;
    (void) seq;
    return 0;
}
int
rage128_gpu_scan_hazard(rage128_t *dev, uint32_t lo, uint32_t len)
{
    (void) dev;
    (void) lo;
    (void) len;
    return 0;
}
uint64_t
rage128_gpu_pace_window(rage128_t *dev, uint32_t wall_ms,
                        unsigned *cap_out)
{
    (void) dev;
    (void) wall_ms;
    *cap_out = 0;
    return 0;
}
void
rage128_gpu_present_fence(rage128_t *dev, uint32_t src, uint32_t len)
{
    (void) dev;
    (void) src;
    (void) len;
}
void
rage128_gpu_2d_fill_skip(rage128_t *dev, int why, uint32_t rop,
                         uint32_t wmask)
{
    (void) dev;
    (void) why;
    (void) rop;
    (void) wmask;
}
void
rage128_gpu_2d_barrier(rage128_t *dev, uint32_t lo, uint32_t len, int writes)
{
    (void) dev;
    (void) lo;
    (void) len;
    (void) writes;
}
int
rage128_gpu_2d_fill(rage128_t *dev, uint32_t addr, uint32_t pitch,
                    uint32_t len, uint32_t rows, uint32_t color, int bpp,
                    int route)
{
    (void) dev;
    (void) addr;
    (void) pitch;
    (void) len;
    (void) rows;
    (void) color;
    (void) bpp;
    (void) route;
    return 0;
}
int
rage128_gpu_2d_fill_tiled(rage128_t *dev, uint32_t tbase, uint32_t tpitch,
                          uint32_t xb0, uint32_t xb1, uint32_t y0,
                          uint32_t y1, uint32_t color, int bpp)
{
    (void) dev;
    (void) tbase;
    (void) tpitch;
    (void) xb0;
    (void) xb1;
    (void) y0;
    (void) y1;
    (void) color;
    (void) bpp;
    return 0;
}
void
rage128_gpu_2d_nb(rage128_t *dev, int table)
{
    (void) dev;
    (void) table;
}
void
rage128_gpu_2d_qskip(rage128_t *dev, int table, int why)
{
    (void) dev;
    (void) table;
    (void) why;
}
void
rage128_gpu_2d_tiled_skip(rage128_t *dev)
{
    (void) dev;
}
void *
rage128_gpu_2d_stage(rage128_t *dev, uint32_t bytes, uint32_t *off)
{
    (void) dev;
    (void) bytes;
    (void) off;
    return NULL;
}
int
rage128_gpu_2d_copy(rage128_t *dev, uint32_t addr, uint32_t pitch,
                    uint32_t len, uint32_t rows, uint32_t soff,
                    uint32_t spitch, int route)
{
    (void) dev;
    (void) addr;
    (void) pitch;
    (void) len;
    (void) rows;
    (void) soff;
    (void) spitch;
    (void) route;
    return 0;
}
int
rage128_gpu_2d_blit(rage128_t *dev, uint32_t addr, uint32_t pitch,
                    uint32_t src, uint32_t spitch, uint32_t len,
                    uint32_t rows)
{
    (void) dev;
    (void) addr;
    (void) pitch;
    (void) src;
    (void) spitch;
    (void) len;
    (void) rows;
    return 0;
}
int
rage128_gpu_2d_rmw(rage128_t *dev, uint32_t addr, uint32_t pitch,
                   uint32_t len, uint32_t rows, uint32_t aoff,
                   uint32_t boff, uint32_t amod)
{
    (void) dev;
    (void) addr;
    (void) pitch;
    (void) len;
    (void) rows;
    (void) aoff;
    (void) boff;
    (void) amod;
    return 0;
}
int
rage128_gpu_submit_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                       const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c)
{
    (void) dev;
    (void) rs;
    (void) a;
    (void) b;
    (void) c;
    return 0;
}
int
rage128_gpu_submit_line(rage128_t *dev, const rage128_raster_state_t *rs,
                        const r3d_vtx_t *a, const r3d_vtx_t *b)
{
    (void) dev;
    (void) rs;
    (void) a;
    (void) b;
    return 0;
}
int
rage128_gpu_submit_point(rage128_t *dev, const rage128_raster_state_t *rs,
                         const r3d_vtx_t *v)
{
    (void) dev;
    (void) rs;
    (void) v;
    return 0;
}
void
rage128_gpu_stage_quiesce(rage128_t *dev, int per_draw)
{
    (void) dev;
    (void) per_draw;
}
int
rage128_gpu_stage_idle(rage128_t *dev)
{
    (void) dev;
    return 1;
}
int
rage128_gpu_stage_grow(rage128_t *dev, uint32_t need)
{
    (void) dev;
    (void) need;
    return -1;
}
void
rage128_gpu_czstage_quiesce(rage128_t *dev)
{
    (void) dev;
}
int
rage128_gpu_czstage_grow(rage128_t *dev, struct rage128_span_stage *st,
                         uint32_t need)
{
    (void) dev;
    (void) st;
    (void) need;
    return -1;
}
int
rage128_gpu_ov0_compose(rage128_t *dev, const r128_ov0_frame_t *f)
{
    (void) dev;
    (void) f;
    return 0;
}
const uint32_t *
rage128_gpu_ov0_row(rage128_t *dev, uint32_t row)
{
    (void) dev;
    (void) row;
    return NULL;
}
int
rage128_gpu_ov0_verifying(rage128_t *dev)
{
    (void) dev;
    return 0;
}
void
rage128_gpu_ov0_bad(rage128_t *dev, uint64_t bad)
{
    (void) dev;
    (void) bad;
}
void
rage128_gpu_2d_qdone(rage128_t *dev, int table)
{
    (void) dev;
    (void) table;
}
void
rage128_gpu_2d_qaux(rage128_t *dev, int table)
{
    (void) dev;
    (void) table;
}
int
rage128_gpu_2d_verifying(rage128_t *dev)
{
    (void) dev;
    return 0;
}
void
rage128_gpu_2d_verify_bad(rage128_t *dev, int table, uint64_t bad)
{
    (void) dev;
    (void) table;
    (void) bad;
}
uint64_t
rage128_gpu_capture_span(const r128_jit_tri_t *tri, int64_t e0,
                         int64_t e1, int64_t e2, double zline,
                         uint32_t drow, uint32_t zrow, int32_t py)
{
    (void) tri;
    (void) e0;
    (void) e1;
    (void) e2;
    (void) zline;
    (void) drow;
    (void) zrow;
    (void) py;
    return 0;
}
uint64_t
rage128_gpu_count_span(const r128_jit_tri_t *tri, int64_t e0,
                       int64_t e1, int64_t e2, double zline,
                       uint32_t drow, uint32_t zrow, int32_t py)
{
    (void) tri;
    (void) e0;
    (void) e1;
    (void) e2;
    (void) zline;
    (void) drow;
    (void) zrow;
    (void) py;
    return 0;
}
int
rage128_gpu_capture_row_cover(const r128_jit_tri_t *tri, int64_t e0,
                              int64_t e1, int64_t e2, int32_t py,
                              int32_t *cx0, int32_t *cx1)
{
    (void) e0;
    (void) e1;
    (void) e2;
    (void) py;
    *cx0 = tri->x0;
    *cx1 = tri->x1;
    return 1;
}

#endif /* R128_GPU_HAVE_VULKAN */
