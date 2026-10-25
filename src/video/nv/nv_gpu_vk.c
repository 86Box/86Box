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
 *          so other NVIDIA cards can use it: run-time loading, instance and device (a discrete GPU first), buffers,
 *          the compute pipeline, the slots' descriptor sets, command
 *          buffers, fences and timestamp queries.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/nv/nv_gpu_vk.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

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

    create_instance = (PFN_vkCreateInstance) vk->gipa(NULL, "vkCreateInstance");
    if (!create_instance)
        return 0;
    app.pApplicationName = d->app_name;
    app.apiVersion       = VK_API_VERSION_1_1;
    ici.pApplicationInfo = &app;
    if (create_instance(&ici, NULL, &vk->inst) != VK_SUCCESS) {
        pclog("%s: vkCreateInstance failed\n", vk->tag);
        return 0;
    }
#define GPU_LOAD(name)                                     \
    vk->name = (PFN_##name) vk->gipa(vk->inst, #name);     \
    if (!vk->name) {                                       \
        pclog("%s: no " #name "\n", vk->tag);              \
        return 0;                                          \
    }
    NV_GPU_FNS(GPU_LOAD)
#undef GPU_LOAD

    if ((vk->vkEnumeratePhysicalDevices(vk->inst, &npd, pds) < 0) || !npd) {
        pclog("%s: no Vulkan device\n", vk->tag);
        return 0;
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
    qci.queueFamilyIndex     = vk->qfam;
    qci.queueCount           = 1;
    qci.pQueuePriorities     = &prio;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &qci;
    if (vk->vkCreateDevice(vk->pd, &dci, NULL, &vk->dev) != VK_SUCCESS) {
        pclog("%s: vkCreateDevice failed\n", vk->tag);
        return 0;
    }
    vk->vkGetDeviceQueue(vk->dev, vk->qfam, 0, &vk->queue);
    vk->ts_period = props.limits.timestampPeriod;
    /* timestamps, when this queue has them */
    for (int i = 0; d->queries && qf[vk->qfam].timestampValidBits && (i < NV_GPU_SLOTS); i++) {
        VkQueryPoolCreateInfo qpi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };

        qpi.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qpi.queryCount = d->queries;
        if (vk->vkCreateQueryPool(vk->dev, &qpi, NULL, &vk->slot[i].qpool) != VK_SUCCESS)
            vk->slot[i].qpool = VK_NULL_HANDLE;
    }
    pclog("%s: using %s\n", vk->tag, props.deviceName);

    if (!gpu_buf(vk, &vk->b[NV_GPU_B_VRAM], d->vram_size, 0) || !gpu_buf(vk, &vk->b[NV_GPU_B_TEX], d->tex_arena, 0)
        || !gpu_buf(vk, &vk->b[NV_GPU_B_VSTAGE], d->vram_size, 1) || !gpu_buf(vk, &vk->b[NV_GPU_B_TSTAGE], d->tex_arena, 1)) {
        pclog("%s: buffer allocation failed\n", vk->tag);
        return 0;
    }
    for (int i = 0; i < NV_GPU_SLOTS; i++)
        for (int r = 0; r < NV_GPU_R_COUNT; r++)
            if (!gpu_buf(vk, &vk->slot[i].rec[r], d->rec_size[r], 0) || !gpu_buf(vk, &vk->slot[i].rech[r], d->rec_size[r], 1)) {
                pclog("%s: buffer allocation failed\n", vk->tag);
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
            pclog("%s: pipeline creation failed (%d)\n", vk->tag, res);
            return 0;
        }
        dpi.maxSets       = NV_GPU_SLOTS;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes    = &ps;
        if (vk->vkCreateDescriptorPool(vk->dev, &dpi, NULL, &vk->dpool) != VK_SUCCESS)
            return 0;
        for (int sl = 0; sl < NV_GPU_SLOTS; sl++) {
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
        for (int i = 0; i < NV_GPU_SLOTS; i++) {
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
    memset(vk, 0, sizeof(*vk));
    vk->tag = d->tag;
    if (!gpu_load(vk)) {
        pclog("%s: no Vulkan library, 3D stays on the CPU\n", vk->tag);
        nv_gpu_vk_close(vk);
        return 0;
    }
    if (!gpu_create(vk, d)) {
        pclog("%s: Vulkan setup failed, 3D stays on the CPU\n", vk->tag);
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
        for (int i = 0; i < NV_GPU_SLOTS; i++) {
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
    memset(vk, 0, sizeof(*vk));
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
