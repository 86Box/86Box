/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA GPU lanes -- the Vulkan plumbing of the RIVA 128's
 *          compute lane (nv_gpu_vk.h), kept apart from the lane itself
 *          so other NVIDIA cards can use it: run-time loading, instance
 *          and device (a discrete GPU first), buffers, VRAM imported as
 *          host memory, the compute pipeline, the slots' descriptor
 *          sets, command buffers, fences and timestamp queries.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/version.h>
#include <86box/nv/nv_gpu_vk.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

/* ---- telemetry ---- */
#if defined(_WIN32)
#    define GPU_TEL_OS "windows"
#elif defined(__APPLE__)
#    define GPU_TEL_OS "macos"
#elif defined(__linux__)
#    define GPU_TEL_OS "linux"
#else
#    define GPU_TEL_OS "other"
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#    define GPU_TEL_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
#    define GPU_TEL_ARCH "x86_64"
#else
#    define GPU_TEL_ARCH "other"
#endif

static FILE *gpu_tel;

void
nv_gpu_log(const char *fmt, ...)
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

/* NV_GPU_TELEMETRY: the file is made new by a launch's first lane and
   added to by any later one (a hard reset makes the card again) */
static void
gpu_tel_open(const char *tag)
{
    static int  opened;
    const char *te = getenv("NV_GPU_TELEMETRY");
    char        path[1024 + 32];
    char        stamp[32] = "unknown";
    time_t      now = time(NULL);
    struct tm   tm;

    if (!te || !te[0] || !strcmp(te, "0") || gpu_tel)
        return;
    path_append_filename(path, usr_path, "nv-gpu-telemetry.log");
    gpu_tel = plat_fopen(path, opened ? "a" : "w");
    opened  = 1;
    if (!gpu_tel) {
        pclog("%s: telemetry: cannot open %s\n", tag, path);
        return;
    }
#ifdef _WIN32
    if (localtime_s(&tm, &now) == 0)
#else
    if (localtime_r(&now, &tm))
#endif
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    nv_gpu_log("%s: telemetry: %s %s, host %s/%s, %s\n", tag, EMU_NAME, EMU_VERSION_FULL, GPU_TEL_OS, GPU_TEL_ARCH,
               stamp);
}

static void
gpu_tel_close(void)
{
    if (gpu_tel) {
        fclose(gpu_tel);
        gpu_tel = NULL;
    }
}

static const char *const gpu_fb_name[NV_GPU_FB_COUNT] = {
    "no rows", "rows past VRAM", "arena full", "no spans", "submit failed", "odd texture address"
};

void
nv_gpu_vk_fallback(nv_gpu_vk_t *vk, int why)
{
    vk->fb[why]++;
    vk->fb_total[why]++;
}

void
nv_gpu_vk_fallbacks(nv_gpu_vk_t *vk, int total)
{
    char  buf[768];
    char *p = buf;

    buf[0] = 0;
    for (int i = 0; i < NV_GPU_FB_COUNT; i++) {
        uint64_t n = total ? vk->fb_total[i] : vk->fb[i];

        if (n && (p < buf + sizeof(buf) - 64))
            p += snprintf(p, buf + sizeof(buf) - p, "%s%llu %s", (p == buf) ? "" : ", ", (unsigned long long) n, gpu_fb_name[i]);
        if (!total)
            vk->fb[i] = 0;
    }
    if (buf[0])
        nv_gpu_log("%s: left to the CPU%s: %s\n", vk->tag, total ? " in all" : "", buf);
}

uint64_t
nv_gpu_now_us(void)
{
    struct timespec ts;

    timespec_get(&ts, TIME_UTC);
    return (uint64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void *
gpu_dl_open(const char *name)
{
#ifdef _WIN32
    return (void *) LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *
gpu_dl_sym(void *lib, const char *name)
{
#ifdef _WIN32
    return (void *) GetProcAddress((HMODULE) lib, name);
#else
    return dlsym(lib, name);
#endif
}

static void
gpu_dl_close(void *lib)
{
#ifdef _WIN32
    FreeLibrary((HMODULE) lib);
#else
    dlclose(lib);
#endif
}

/* The block svga.vram moves to for the import: aligned to
   GPU_VRAM_ALIGN, which covers the import alignments seen in practice
   (4 KB to 64 KB). */
#define GPU_VRAM_ALIGN 0x10000

static void gpu_buf_free(nv_gpu_vk_t *vk, nv_gpu_buf_t *b);

static void *
gpu_aligned_alloc(size_t size)
{
#ifdef _WIN32
    return _aligned_malloc(size, GPU_VRAM_ALIGN);
#else
    void *p = NULL;

    return posix_memalign(&p, GPU_VRAM_ALIGN, size) ? NULL : p;
#endif
}

static void
gpu_aligned_free(void *p)
{
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

static int
gpu_load(nv_gpu_vk_t *vk)
{
    static const char *const cands[] = {
#if defined(_WIN32)
        "vulkan-1.dll",
#elif defined(__APPLE__)
        "libvulkan.1.dylib",
        "/opt/homebrew/lib/libvulkan.1.dylib",
        "/usr/local/lib/libvulkan.1.dylib",
        "libMoltenVK.dylib",
#else
        "libvulkan.so.1",
        "libvulkan.so",
#endif
    };

    for (size_t i = 0; !vk->lib && (i < sizeof(cands) / sizeof(cands[0])); i++)
        vk->lib = gpu_dl_open(cands[i]);
    if (!vk->lib)
        return 0;
    vk->gipa = (PFN_vkGetInstanceProcAddr) gpu_dl_sym(vk->lib, "vkGetInstanceProcAddr");
    return vk->gipa != NULL;
}

static uint32_t
gpu_mem_type(nv_gpu_vk_t *vk, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp;

    vk->vkGetPhysicalDeviceMemoryProperties(vk->pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && ((mp.memoryTypes[i].propertyFlags & want) == want))
            return i;
    return UINT32_MAX;
}

static int
gpu_buf(nv_gpu_vk_t *vk, nv_gpu_buf_t *b, VkDeviceSize size, int host)
{
    VkBufferCreateInfo   bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements mr;

    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vk->vkCreateBuffer(vk->dev, &bi, NULL, &b->buf) != VK_SUCCESS)
        return 0;
    vk->vkGetBufferMemoryRequirements(vk->dev, b->buf, &mr);
    ai.allocationSize = mr.size;
    if (host) {
        /* cached when there is such a type: the CPU reads rows back */
        ai.memoryTypeIndex = gpu_mem_type(vk, mr.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                              | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX)
            ai.memoryTypeIndex = gpu_mem_type(vk, mr.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    } else
        ai.memoryTypeIndex = gpu_mem_type(vk, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX)
        return 0;
    if (vk->vkAllocateMemory(vk->dev, &ai, NULL, &b->mem) != VK_SUCCESS)
        return 0;
    if (vk->vkBindBufferMemory(vk->dev, b->buf, b->mem, 0) != VK_SUCCESS)
        return 0;
    if (host && (vk->vkMapMemory(vk->dev, b->mem, 0, VK_WHOLE_SIZE, 0, &b->map) != VK_SUCCESS))
        return 0;
    b->size = size;
    return 1;
}

/* Move svga.vram (<d->vram>, d->vram_alloc bytes from svga_init's
   calloc) into an aligned block and import the block as the VRAM
   staging buffer, b[NV_GPU_B_VSTAGE]. svga_close frees svga.vram with
   free(), which must not reach the aligned block: nv_gpu_vk_close gives
   back a calloc'd copy. 0 when the import fails (the block, if made,
   stays until close): the lane then gets a staging buffer of its own. */
static int
gpu_vram_import(nv_gpu_vk_t *vk, const nv_gpu_vk_desc_t *d, VkDeviceSize align)
{
    PFN_vkGetMemoryHostPointerPropertiesEXT get_props;
    VkMemoryHostPointerPropertiesEXT        hpp   = { VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
    VkExternalMemoryBufferCreateInfo        embci = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
    VkBufferCreateInfo                      bi    = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkImportMemoryHostPointerInfoEXT        imp   = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT };
    VkMemoryAllocateInfo                    ai    = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkPhysicalDeviceMemoryProperties        mp;
    VkMemoryRequirements                    mr;
    nv_gpu_buf_t                           *b    = &vk->b[NV_GPU_B_VSTAGE];
    size_t                                  size = (d->vram_alloc + GPU_VRAM_ALIGN - 1) & ~(size_t) (GPU_VRAM_ALIGN - 1);
    uint8_t                                *blk;

    get_props = (PFN_vkGetMemoryHostPointerPropertiesEXT) vk->vkGetDeviceProcAddr(vk->dev, "vkGetMemoryHostPointerPropertiesEXT");
    if (!get_props || !align || (align > GPU_VRAM_ALIGN) || !(blk = gpu_aligned_alloc(size)))
        return 0;
    memcpy(blk, *d->vram, d->vram_alloc);
    memset(blk + d->vram_alloc, 0, size - d->vram_alloc);
    free(*d->vram);
    *d->vram       = blk;
    vk->vram_ref   = d->vram;
    vk->vram_block = blk;
    vk->vram_alloc = d->vram_alloc;

    if (get_props(vk->dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, blk, &hpp) != VK_SUCCESS)
        return 0;
    embci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    bi.pNext          = &embci;
    bi.size           = d->vram_size;
    bi.usage          = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
    if (vk->vkCreateBuffer(vk->dev, &bi, NULL, &b->buf) != VK_SUCCESS)
        return 0;
    vk->vkGetBufferMemoryRequirements(vk->dev, b->buf, &mr);
    vk->vkGetPhysicalDeviceMemoryProperties(vk->pd, &mp);
    ai.memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        if ((hpp.memoryTypeBits & mr.memoryTypeBits & (1u << i)) && ((mp.memoryTypes[i].propertyFlags & want) == want)) {
            ai.memoryTypeIndex = i;
            break;
        }
    }
    imp.handleType    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    imp.pHostPointer  = blk;
    ai.pNext          = &imp;
    ai.allocationSize = (size / align) * align;
    if ((ai.memoryTypeIndex == UINT32_MAX) || (ai.allocationSize < mr.size)
        || (vk->vkAllocateMemory(vk->dev, &ai, NULL, &b->mem) != VK_SUCCESS)
        || (vk->vkBindBufferMemory(vk->dev, b->buf, b->mem, 0) != VK_SUCCESS)) {
        gpu_buf_free(vk, b);
        memset(b, 0, sizeof(*b));
        return 0;
    }
    b->map          = blk;
    b->size         = d->vram_size;
    vk->vram_import = 1;
    return 1;
}

static void
gpu_buf_free(nv_gpu_vk_t *vk, nv_gpu_buf_t *b)
{
    if (b->buf)
        vk->vkDestroyBuffer(vk->dev, b->buf, NULL);
    if (b->mem)
        vk->vkFreeMemory(vk->dev, b->mem, NULL);
}

static int
gpu_create(nv_gpu_vk_t *vk, const nv_gpu_vk_desc_t *d)
{
    PFN_vkCreateInstance       create_instance;
    VkApplicationInfo          app  = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo       ici  = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkPhysicalDevice           pds[16];
    uint32_t                   npd  = 16;
    VkPhysicalDeviceProperties props;
    float                      prio = 1.0f;
    VkDeviceQueueCreateInfo    qci  = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo         dci  = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkQueueFamilyProperties    qf[16];
    uint32_t                   nqf  = 16;
    int                        best = -1;
    VkExtensionProperties     *ext  = NULL;
    uint32_t                   next = 0;
    const char                *host_ext   = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    int                        have_host  = 0;
    VkDeviceSize               host_align = 0;

    create_instance = (PFN_vkCreateInstance) vk->gipa(NULL, "vkCreateInstance");
    if (!create_instance)
        return 0;
    app.pApplicationName = d->app_name;
    app.apiVersion       = VK_API_VERSION_1_1;
    ici.pApplicationInfo = &app;
    if (create_instance(&ici, NULL, &vk->inst) != VK_SUCCESS) {
        nv_gpu_log("%s: vkCreateInstance failed\n", vk->tag);
        return 0;
    }
#define GPU_LOAD(name)                                     \
    vk->name = (PFN_##name) vk->gipa(vk->inst, #name);     \
    if (!vk->name) {                                       \
        nv_gpu_log("%s: no " #name "\n", vk->tag);              \
        return 0;                                          \
    }
    NV_GPU_FNS(GPU_LOAD)
#undef GPU_LOAD

    if ((vk->vkEnumeratePhysicalDevices(vk->inst, &npd, pds) < 0) || !npd) {
        nv_gpu_log("%s: no Vulkan device\n", vk->tag);
        return 0;
    }
    for (uint32_t i = 0; i < npd; i++) {
        static const char *const types[] = { "other", "integrated", "discrete", "virtual", "CPU" };

        vk->vkGetPhysicalDeviceProperties(pds[i], &props);
        nv_gpu_log("%s: device %u: %s (%s), vendor %04x device %04x, driver %08x, Vulkan %u.%u.%u\n", vk->tag, i,
                   props.deviceName, (props.deviceType <= 4) ? types[props.deviceType] : "?", props.vendorID, props.deviceID,
                   props.driverVersion, VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                   VK_VERSION_PATCH(props.apiVersion));
    }
    /* a discrete GPU first, then an integrated one, then anything */
    for (int pass = 0; (pass < 3) && (best < 0); pass++)
        for (uint32_t i = 0; (i < npd) && (best < 0); i++) {
            vk->vkGetPhysicalDeviceProperties(pds[i], &props);
            if ((pass == 0) && (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
                continue;
            if ((pass == 1) && (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU))
                continue;
            best = i;
        }
    vk->pd = pds[best];
    vk->vkGetPhysicalDeviceProperties(vk->pd, &props);
    vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->pd, &nqf, qf);
    vk->qfam = UINT32_MAX;
    for (uint32_t i = 0; i < nqf; i++)
        if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            vk->qfam = i;
            break;
        }
    if (vk->qfam == UINT32_MAX)
        return 0;
    /* host memory import, for VRAM rows without CPU copies */
    if (d->vram && !getenv("NV_GPU_NO_IMPORT") && (vk->vkEnumerateDeviceExtensionProperties(vk->pd, NULL, &next, NULL) == VK_SUCCESS)
        && next && (ext = calloc(next, sizeof(*ext)))) {
        if (vk->vkEnumerateDeviceExtensionProperties(vk->pd, NULL, &next, ext) == VK_SUCCESS)
            for (uint32_t i = 0; i < next; i++)
                if (!strcmp(ext[i].extensionName, host_ext))
                    have_host = 1;
        free(ext);
    }
    if (have_host) {
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT hp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT };
        VkPhysicalDeviceProperties2                     p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };

        p2.pNext = &hp;
        vk->vkGetPhysicalDeviceProperties2(vk->pd, &p2);
        host_align                  = hp.minImportedHostPointerAlignment;
        dci.enabledExtensionCount   = 1;
        dci.ppEnabledExtensionNames = &host_ext;
    }
    qci.queueFamilyIndex     = vk->qfam;
    qci.queueCount           = 1;
    qci.pQueuePriorities     = &prio;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &qci;
    if (vk->vkCreateDevice(vk->pd, &dci, NULL, &vk->dev) != VK_SUCCESS) {
        nv_gpu_log("%s: vkCreateDevice failed\n", vk->tag);
        return 0;
    }
    vk->vkGetDeviceQueue(vk->dev, vk->qfam, 0, &vk->queue);
    vk->ts_period = props.limits.timestampPeriod;
    /* timestamps, when this queue has them */
    for (int i = 0; d->queries && qf[vk->qfam].timestampValidBits && (i < vk->nslots); i++) {
        VkQueryPoolCreateInfo qpi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };

        qpi.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qpi.queryCount = d->queries;
        if (vk->vkCreateQueryPool(vk->dev, &qpi, NULL, &vk->slot[i].qpool) != VK_SUCCESS)
            vk->slot[i].qpool = VK_NULL_HANDLE;
    }
    if (have_host && !gpu_vram_import(vk, d, host_align))
        nv_gpu_log("%s: VRAM import failed (alignment %llu)\n", vk->tag, (unsigned long long) host_align);
    else if (d->vram && !have_host)
        nv_gpu_log("%s: no %s%s\n", vk->tag, host_ext, getenv("NV_GPU_NO_IMPORT") ? " (NV_GPU_NO_IMPORT)" : "");
    nv_gpu_log("%s: using %s, %d batches in flight, VRAM rows %s\n", vk->tag, props.deviceName, vk->nslots,
          vk->vram_import ? "imported" : "staged");

    if (!gpu_buf(vk, &vk->b[NV_GPU_B_VRAM], d->vram_size, 0) || !gpu_buf(vk, &vk->b[NV_GPU_B_TEX], d->tex_arena, 0)
        || (!vk->vram_import && !gpu_buf(vk, &vk->b[NV_GPU_B_VSTAGE], d->vram_size, 1))
        || !gpu_buf(vk, &vk->b[NV_GPU_B_TSTAGE], d->tex_arena, 1)) {
        nv_gpu_log("%s: buffer allocation failed\n", vk->tag);
        return 0;
    }
    for (int i = 0; i < vk->nslots; i++)
        for (int r = 0; r < NV_GPU_R_COUNT; r++)
            if (!gpu_buf(vk, &vk->slot[i].rec[r], d->rec_size[r], 0) || !gpu_buf(vk, &vk->slot[i].rech[r], d->rec_size[r], 1)) {
                nv_gpu_log("%s: buffer allocation failed\n", vk->tag);
                return 0;
            }

    /* pipeline */
    {
        VkDescriptorSetLayoutBinding    lb[6];
        VkDescriptorSetLayoutCreateInfo lci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        VkPushConstantRange             pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 8 };
        VkPipelineLayoutCreateInfo      pci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkShaderModuleCreateInfo        sci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        VkComputePipelineCreateInfo     cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        VkShaderModule                  sm;
        VkDescriptorPoolSize            ps  = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 * NV_GPU_SLOTS };
        VkDescriptorPoolCreateInfo      dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        VkDescriptorSetAllocateInfo     dai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        VkDescriptorBufferInfo          dbi[6];
        VkWriteDescriptorSet            wr[6];
        VkResult                        res;

        for (int i = 0; i < 6; i++) {
            memset(&lb[i], 0, sizeof(lb[i]));
            lb[i].binding         = i;
            lb[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lb[i].descriptorCount = 1;
            lb[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        lci.bindingCount = 6;
        lci.pBindings    = lb;
        if (vk->vkCreateDescriptorSetLayout(vk->dev, &lci, NULL, &vk->dsl) != VK_SUCCESS)
            return 0;
        pci.setLayoutCount         = 1;
        pci.pSetLayouts            = &vk->dsl;
        pci.pushConstantRangeCount = 1;
        pci.pPushConstantRanges    = &pcr;
        if (vk->vkCreatePipelineLayout(vk->dev, &pci, NULL, &vk->pl) != VK_SUCCESS)
            return 0;
        sci.codeSize = d->spv_size;
        sci.pCode    = d->spv;
        if (vk->vkCreateShaderModule(vk->dev, &sci, NULL, &sm) != VK_SUCCESS)
            return 0;
        cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = sm;
        cpi.stage.pName  = "main";
        cpi.layout       = vk->pl;
        res              = vk->vkCreateComputePipelines(vk->dev, VK_NULL_HANDLE, 1, &cpi, NULL, &vk->pipe);
        vk->vkDestroyShaderModule(vk->dev, sm, NULL);
        if (res != VK_SUCCESS) {
            nv_gpu_log("%s: pipeline creation failed (%d)\n", vk->tag, res);
            return 0;
        }
        dpi.maxSets       = NV_GPU_SLOTS;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes    = &ps;
        if (vk->vkCreateDescriptorPool(vk->dev, &dpi, NULL, &vk->dpool) != VK_SUCCESS)
            return 0;
        for (int sl = 0; sl < vk->nslots; sl++) {
            dai.descriptorPool     = vk->dpool;
            dai.descriptorSetCount = 1;
            dai.pSetLayouts        = &vk->dsl;
            if (vk->vkAllocateDescriptorSets(vk->dev, &dai, &vk->slot[sl].dset) != VK_SUCCESS)
                return 0;
            for (int i = 0; i < 6; i++) {
                /* VRAM, the arena, then the slot's records */
                dbi[i].buffer = (i < 2) ? vk->b[i].buf : vk->slot[sl].rec[i - 2].buf;
                dbi[i].offset = 0;
                dbi[i].range  = VK_WHOLE_SIZE;
                memset(&wr[i], 0, sizeof(wr[i]));
                wr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                wr[i].dstSet          = vk->slot[sl].dset;
                wr[i].dstBinding      = i;
                wr[i].descriptorCount = 1;
                wr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                wr[i].pBufferInfo     = &dbi[i];
            }
            vk->vkUpdateDescriptorSets(vk->dev, 6, wr, 0, NULL);
        }
    }
    {
        VkCommandPoolCreateInfo     cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        VkFenceCreateInfo           fci  = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

        cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = vk->qfam;
        if (vk->vkCreateCommandPool(vk->dev, &cpci, NULL, &vk->cpool) != VK_SUCCESS)
            return 0;
        cbai.commandPool        = vk->cpool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        for (int i = 0; i < vk->nslots; i++) {
            if (vk->vkAllocateCommandBuffers(vk->dev, &cbai, &vk->slot[i].cb) != VK_SUCCESS)
                return 0;
            if (vk->vkCreateFence(vk->dev, &fci, NULL, &vk->slot[i].fence) != VK_SUCCESS)
                return 0;
        }
    }
    return 1;
}

int
nv_gpu_vk_open(nv_gpu_vk_t *vk, const nv_gpu_vk_desc_t *d)
{
    const char *env = getenv("NV_GPU_SLOTS");

    memset(vk, 0, sizeof(*vk));
    vk->tag    = d->tag;
    gpu_tel_open(d->tag);
    vk->nslots = (env && *env) ? atoi(env) : (d->slots ? d->slots : NV_GPU_SLOTS_DEFAULT);
    if (vk->nslots < 1)
        vk->nslots = 1;
    if (vk->nslots > NV_GPU_SLOTS)
        vk->nslots = NV_GPU_SLOTS;
    if (!gpu_load(vk)) {
        nv_gpu_log("%s: no Vulkan library, 3D stays on the CPU\n", vk->tag);
        nv_gpu_vk_close(vk);
        return 0;
    }
    if (!gpu_create(vk, d)) {
        nv_gpu_log("%s: Vulkan setup failed, 3D stays on the CPU\n", vk->tag);
        nv_gpu_vk_close(vk);
        return 0;
    }
    return 1;
}

void
nv_gpu_vk_close(nv_gpu_vk_t *vk)
{
    if (vk->dev) {
        vk->vkDeviceWaitIdle(vk->dev);
        for (int i = 0; i < vk->nslots; i++) {
            nv_gpu_vk_slot_t *sl = &vk->slot[i];

            if (sl->fence)
                vk->vkDestroyFence(vk->dev, sl->fence, NULL);
            if (sl->qpool)
                vk->vkDestroyQueryPool(vk->dev, sl->qpool, NULL);
            for (int r = 0; r < NV_GPU_R_COUNT; r++) {
                gpu_buf_free(vk, &sl->rec[r]);
                gpu_buf_free(vk, &sl->rech[r]);
            }
        }
        if (vk->cpool)
            vk->vkDestroyCommandPool(vk->dev, vk->cpool, NULL);
        if (vk->dpool)
            vk->vkDestroyDescriptorPool(vk->dev, vk->dpool, NULL);
        if (vk->pipe)
            vk->vkDestroyPipeline(vk->dev, vk->pipe, NULL);
        if (vk->pl)
            vk->vkDestroyPipelineLayout(vk->dev, vk->pl, NULL);
        if (vk->dsl)
            vk->vkDestroyDescriptorSetLayout(vk->dev, vk->dsl, NULL);
        for (int i = 0; i < NV_GPU_B_COUNT; i++)
            gpu_buf_free(vk, &vk->b[i]);
        vk->vkDestroyDevice(vk->dev, NULL);
    }
    if (vk->inst && vk->vkDestroyInstance)
        vk->vkDestroyInstance(vk->inst, NULL);
    if (vk->lib)
        gpu_dl_close(vk->lib);
    /* svga.vram back in a block svga_close can free() */
    if (vk->vram_block) {
        uint8_t *v = calloc(vk->vram_alloc, 1);

        if (!v)
            fatal("%s: out of memory giving VRAM back\n", vk->tag);
        memcpy(v, vk->vram_block, vk->vram_alloc);
        *vk->vram_ref = v;
        gpu_aligned_free(vk->vram_block);
    }
    memset(vk, 0, sizeof(*vk));
    gpu_tel_close();
}

void
nv_gpu_vk_barrier(nv_gpu_vk_t *vk, VkCommandBuffer cb, VkPipelineStageFlags src, VkAccessFlags sa, VkPipelineStageFlags dst,
                  VkAccessFlags da)
{
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

    mb.srcAccessMask = sa;
    mb.dstAccessMask = da;
    vk->vkCmdPipelineBarrier(cb, src, dst, 0, 1, &mb, 0, NULL, 0, NULL);
}
