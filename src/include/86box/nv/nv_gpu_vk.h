/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA GPU lanes -- the Vulkan plumbing of the RIVA 128's
 *          compute lane (nv3_render_d3d_gpu.c), kept apart from it so
 *          other NVIDIA cards can use it.
 *
 *          Vulkan is opened at run time (no link), and one compute
 *          pipeline is made from a lane's SPIR-V: six storage buffers --
 *          a device-local mirror of VRAM, the texture arena, then a
 *          slot's four record buffers -- and an 8-byte push constant.
 *          Each of the NV_GPU_SLOTS slots (a batch in flight) has its
 *          records (device-local, with host-visible staging), its
 *          descriptor set, command buffer and fence, and a timestamp
 *          query pool when asked for. Only built with Vulkan headers
 *          (NV_GPU_HAVE_VULKAN).
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
    X(vkGetQueryPoolResults)

typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    void          *map; /* host-visible ones */
    VkDeviceSize   size;
} nv_gpu_buf_t;

enum {
    NV_GPU_B_VRAM,   /* bindings 0-1, device-local: a mirror of VRAM */
    NV_GPU_B_TEX,    /* the texture arena */
    NV_GPU_B_VSTAGE, /* host-visible: VRAM rows in and out */
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

/* Two batches can be in flight */
#define NV_GPU_SLOTS 2

typedef struct {
    nv_gpu_buf_t    rec[NV_GPU_R_COUNT];  /* device-local */
    nv_gpu_buf_t    rech[NV_GPU_R_COUNT]; /* host-visible staging */
    VkDescriptorSet dset;
    VkCommandBuffer cb;
    VkFence         fence;
    VkQueryPool     qpool; /* timestamps (VK_NULL_HANDLE unless asked for and supported) */
} nv_gpu_vk_slot_t;

typedef struct {
    const char     *tag;      /* for the log: "NV3 GPU" */
    const char     *app_name; /* the Vulkan instance's application name */
    uint32_t        vram_size;
    uint32_t        tex_arena;
    VkDeviceSize    rec_size[NV_GPU_R_COUNT];
    const uint32_t *spv;
    size_t          spv_size;
    uint32_t        queries; /* timestamps per slot, 0 for none */
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
    VkPipeline            pipe;
    VkDescriptorPool      dpool;
    VkCommandPool         cpool;
    nv_gpu_buf_t          b[NV_GPU_B_COUNT];
    nv_gpu_vk_slot_t      slot[NV_GPU_SLOTS];
    float                 ts_period; /* ns per tick */
} nv_gpu_vk_t;

/* Open Vulkan and make everything <d> describes. 0 (logged, and <vk>
   closed) when the lane cannot run. */
extern int  nv_gpu_vk_open(nv_gpu_vk_t *vk, const nv_gpu_vk_desc_t *d);
extern void nv_gpu_vk_close(nv_gpu_vk_t *vk);
extern void nv_gpu_vk_barrier(nv_gpu_vk_t *vk, VkCommandBuffer cb, VkPipelineStageFlags src, VkAccessFlags sa,
                              VkPipelineStageFlags dst, VkAccessFlags da);
extern uint64_t nv_gpu_now_us(void);

#endif
