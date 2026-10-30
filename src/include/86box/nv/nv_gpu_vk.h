/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA GPU lanes -- the Vulkan plumbing the RIVA 128's
 *          (nv3_render_d3d_gpu.c) and the RIVA TNT's
 *          (nv4_pgraph_3d_gpu.c) compute lanes share.
 *
 *          Vulkan is opened at run time (no link), and one compute
 *          pipeline is made from a lane's SPIR-V: six storage buffers --
 *          a device-local mirror of VRAM, the texture arena, then a
 *          slot's four record buffers -- and an 8-byte push constant.
 *          Each slot (a batch in flight; nslots of them, NV_GPU_SLOTS at
 *          most) has its records (device-local, with host-visible
 *          staging), its descriptor set, command buffer and fence, and a
 *          timestamp query pool when asked for.
 *
 *          VRAM rows go in and out through a host-visible buffer the size
 *          of VRAM. With VK_EXT_external_memory_host that buffer is VRAM
 *          itself: svga.vram moves to a block aligned for the import, so
 *          the GPU copies rows straight from and into it (vram_import)
 *          and the lanes copy nothing on the CPU.
 *
 *          nv_gpu_log() is pclog that also goes to a telemetry file in
 *          the VM folder (nv-gpu-telemetry.log) when NV_GPU_TELEMETRY is
 *          set: the host, every Vulkan device, what the lane got, its
 *          statistics and why batches went to the CPU (NV_GPU_FB_*), so
 *          a tester's report needs no log switches. Only built with
 *          Vulkan headers (NV_GPU_HAVE_VULKAN).
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#ifndef NV_GPU_VK_H
#define NV_GPU_VK_H

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define NV_GPU_FNS(X)                           \
    X(vkEnumeratePhysicalDevices)               \
    X(vkGetPhysicalDeviceProperties)            \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties)      \
    X(vkCreateDevice)                           \
    X(vkGetDeviceQueue)                         \
    X(vkCreateBuffer)                           \
    X(vkGetBufferMemoryRequirements)            \
    X(vkAllocateMemory)                         \
    X(vkBindBufferMemory)                       \
    X(vkMapMemory)                              \
    X(vkCreateDescriptorSetLayout)              \
    X(vkCreatePipelineLayout)                   \
    X(vkCreateShaderModule)                     \
    X(vkCreateComputePipelines)                 \
    X(vkDestroyShaderModule)                    \
    X(vkCreateDescriptorPool)                   \
    X(vkAllocateDescriptorSets)                 \
    X(vkUpdateDescriptorSets)                   \
    X(vkCreateCommandPool)                      \
    X(vkAllocateCommandBuffers)                 \
    X(vkCreateFence)                            \
    X(vkBeginCommandBuffer)                     \
    X(vkCmdBindPipeline)                        \
    X(vkCmdBindDescriptorSets)                  \
    X(vkCmdPushConstants)                       \
    X(vkCmdDispatch)                            \
    X(vkCmdPipelineBarrier)                     \
    X(vkCmdCopyBuffer)                          \
    X(vkEndCommandBuffer)                       \
    X(vkQueueSubmit)                            \
    X(vkWaitForFences)                          \
    X(vkResetFences)                            \
    X(vkResetCommandBuffer)                     \
    X(vkDeviceWaitIdle)                         \
    X(vkDestroyFence)                           \
    X(vkDestroyCommandPool)                     \
    X(vkDestroyDescriptorPool)                  \
    X(vkDestroyPipeline)                        \
    X(vkDestroyPipelineLayout)                  \
    X(vkDestroyDescriptorSetLayout)             \
    X(vkDestroyBuffer)                          \
    X(vkFreeMemory)                             \
    X(vkDestroyDevice)                          \
    X(vkDestroyInstance)                        \
    X(vkCreateQueryPool)                        \
    X(vkDestroyQueryPool)                       \
    X(vkCmdResetQueryPool)                      \
    X(vkCmdWriteTimestamp)                      \
    X(vkGetQueryPoolResults)                        X(vkEnumerateDeviceExtensionProperties)         X(vkGetPhysicalDeviceProperties2)               X(vkGetDeviceProcAddr)

typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    void          *map; /* host-visible ones */
    VkDeviceSize   size;
} nv_gpu_buf_t;

enum {
    NV_GPU_B_VRAM,   /* bindings 0-1, device-local: a mirror of VRAM */
    NV_GPU_B_TEX,    /* the texture arena */
    NV_GPU_B_VSTAGE, /* host-visible: VRAM rows in and out (VRAM itself with vram_import) */
    NV_GPU_B_TSTAGE, /* textures in */
    NV_GPU_B_COUNT
};

/* A batch's records, bindings 2-5: states, triangles, spans, rows */
enum {
    NV_GPU_R_STATES,
    NV_GPU_R_TRIS,
    NV_GPU_R_SPANS,
    NV_GPU_R_ROWS,
    NV_GPU_R_COUNT
};

/* Why a lane left a batch to the CPU */
enum {
    NV_GPU_FB_NOREGION,     /* no rows to draw into */
    NV_GPU_FB_PASTVRAM,     /* rows past the end of VRAM */
    NV_GPU_FB_ARENA,        /* its textures do not fit the arena */
    NV_GPU_FB_NOROWS,       /* no spans */
    NV_GPU_FB_SUBMIT,       /* vkQueueSubmit failed */
    NV_GPU_FB_TEXODD,       /* (NV3) a texture at an odd address */
    NV_GPU_FB_ALIAS_WAIT,   /* (NV4) rows alias, the serial kernel not compiled yet */
    NV_GPU_FB_ALIAS_PIECES, /* (NV4) rows alias over too many pieces or past VRAM */
    NV_GPU_FB_ALIAS_TEX,    /* (NV4) rows alias and a texture reads bytes the batch writes */
    NV_GPU_FB_ALIAS_ZCOL,   /* (NV4) rows alias and a state's color and depth bytes meet */
    NV_GPU_FB_COUNT
};

/* Batches in flight: at most NV_GPU_SLOTS, NV_GPU_SLOTS_DEFAULT unless
   the lane or NV_GPU_SLOTS in the environment says otherwise */
#define NV_GPU_SLOTS         8
#define NV_GPU_SLOTS_DEFAULT 4

typedef struct {
    nv_gpu_buf_t    rec[NV_GPU_R_COUNT];  /* device-local */
    nv_gpu_buf_t    rech[NV_GPU_R_COUNT]; /* host-visible staging */
    VkDescriptorSet dset;
    VkCommandBuffer cb;
    VkFence         fence;
    VkQueryPool     qpool; /* timestamps (VK_NULL_HANDLE unless asked for and supported) */
} nv_gpu_vk_slot_t;

typedef struct {
    const char     *tag;      /* for the log: "NV4 GPU" */
    const char     *app_name; /* the Vulkan instance's application name */
    uint32_t        vram_size;
    uint32_t        tex_arena;
    VkDeviceSize    rec_size[NV_GPU_R_COUNT];
    const uint32_t *spv;
    size_t          spv_size;
    uint32_t        queries;    /* timestamps per slot, 0 for none */
    int             slots;      /* batches in flight, 0 for the default */
    uint32_t        push_size;  /* push constant bytes, 0 for 8 */
    uint8_t       **vram;       /* &svga.vram: moved and imported when it can be (NULL: never) */
    size_t          vram_alloc; /* the bytes svga allocated there */
} nv_gpu_vk_desc_t;

typedef struct {
    const char               *tag;
    void                     *lib;
    PFN_vkGetInstanceProcAddr gipa;
#define NV_GPU_DECL(name) PFN_##name name;
    NV_GPU_FNS(NV_GPU_DECL)
#undef NV_GPU_DECL

    VkInstance            inst;
    VkPhysicalDevice      pd;
    VkDevice              dev;
    VkQueue               queue;
    uint32_t              qfam;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout      pl;
    VkPipeline            pipe;   /* the shader's defaults */
    VkShaderModule        module; /* kept for nv_gpu_vk_pipeline */
    VkDescriptorPool      dpool;
    VkCommandPool         cpool;
    nv_gpu_buf_t          b[NV_GPU_B_COUNT];
    nv_gpu_vk_slot_t      slot[NV_GPU_SLOTS];
    int                   nslots;
    float                 ts_period; /* ns per tick */
    int                   vram_import; /* b[NV_GPU_B_VSTAGE] is svga.vram */
    uint8_t             **vram_ref;    /* the moved svga.vram, given back at close */
    uint8_t              *vram_block;
    size_t                vram_alloc;
    uint64_t              fb[NV_GPU_FB_COUNT];       /* since the last nv_gpu_vk_fallbacks(.., 0) */
    uint64_t              fb_total[NV_GPU_FB_COUNT]; /* since the start */
} nv_gpu_vk_t;

/* Open Vulkan and make everything <d> describes. 0 (logged, and <vk>
   closed) when the lane cannot run. */
extern int  nv_gpu_vk_open(nv_gpu_vk_t *vk, const nv_gpu_vk_desc_t *d);
extern void nv_gpu_vk_close(nv_gpu_vk_t *vk);
/* Another pipeline of the lane's shader, its specialization constants
   0..n-1 set to <spec> (VK_NULL_HANDLE on failure). Any thread may make
   one; the lane destroys it with vkDestroyPipeline before closing. */
extern VkPipeline nv_gpu_vk_pipeline(nv_gpu_vk_t *vk, const int32_t *spec, uint32_t n);
/* pclog, and the telemetry file (NV_GPU_TELEMETRY) */
extern void nv_gpu_log(const char *fmt, ...);
/* A batch left to the CPU, and why (NV_GPU_FB_*) */
extern void nv_gpu_vk_fallback(nv_gpu_vk_t *vk, int why);
/* Log the reasons batches went to the CPU, <total>: since the start,
   else since the last call (which it resets) */
extern void nv_gpu_vk_fallbacks(nv_gpu_vk_t *vk, int total);
extern void nv_gpu_vk_barrier(nv_gpu_vk_t *vk, VkCommandBuffer cb, VkPipelineStageFlags src, VkAccessFlags sa,
                              VkPipelineStageFlags dst, VkAccessFlags da);
extern uint64_t nv_gpu_now_us(void);

#endif
