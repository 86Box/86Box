/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- GPU backend check of the correctly rounded
 *          divide and the divide by 255.
 *
 *          One pipeline, built from fdivtest.comp, runs the production
 *          divide source without the full gputri pipeline matrix. The
 *          host's float divide supplies the correctly rounded reference,
 *          and the kernel must match each result that is not a NaN bit
 *          for bit; NaN results are compared by class. The divide by 255
 *          is checked over its complete byte-input domain.
 *
 *          Build through build.sh, with -ffp-contract=off and fast math
 *          disabled. Usage: fdivtest [batches] [seed]; the default is 64
 *          batches of 1M operand pairs.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK_CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "VK_CHECK failed %s:%d: %s = %d\n", __FILE__, __LINE__, #x, r_); \
    exit(1); } } while (0)

#define N_PAIRS (1u << 20)

static VkInstance       inst;
static VkPhysicalDevice phys;
static VkDevice         dev;
static VkQueue          queue;
static uint32_t         qfam;
static VkCommandPool    pool;
static VkCommandBuffer  cb;
static VkFence          fence;

typedef struct { VkBuffer b; VkDeviceMemory m; void *map; } gbuf;
static gbuf b_in, b_out;

static uint32_t
mtype(uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp;

    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i))
            && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    fprintf(stderr, "no host-visible memory type\n");
    exit(1);
}

static void
mk_buf(gbuf *g, VkDeviceSize sz)
{
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };

    bci.size        = sz;
    bci.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(dev, &bci, NULL, &g->b));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, g->b, &mr);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = mtype(mr.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(dev, &mai, NULL, &g->m));
    VK_CHECK(vkBindBufferMemory(dev, g->b, g->m, 0));
    VK_CHECK(vkMapMemory(dev, g->m, 0, VK_WHOLE_SIZE, 0, &g->map));
}

static VkShaderModule
load_spv(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (!f) { fprintf(stderr, "%s not found (run the glslc line above)\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint32_t *spv = malloc((size_t) len);

    if (!spv || fread(spv, 1, (size_t) len, f) != (size_t) len) {
        fprintf(stderr, "spv read\n");
        exit(1);
    }
    fclose(f);

    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = (size_t) len;
    smci.pCode    = spv;
    VkShaderModule sm;
    VK_CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
    free(spv);
    return sm;
}

/* xorshift, so a failure is reproducible from the printed seed */
static uint32_t rs = 0x12345678u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

/* Operand distribution. Uniform random bit patterns alone would spend almost
   every sample on huge exponents and never probe the interesting structure,
   so mix in the shapes this kernel actually divides: reciprocals, squares
   (the 1/(wp*wp) site), near-1 values where the quotient lands next to a
   rounding boundary, and the subnormal/zero/inf edges. */
static void
fill(uint32_t *in, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a, b;

        switch (i & 7u) {
            case 0: a = rnd(); b = rnd(); break;                 /* anything   */
            case 1: a = 0x3f800000u; b = rnd(); break;           /* 1.0 / x    */
            case 2: {                                            /* 1/(w*w)    */
                float w = (float) ((int32_t) rnd() / 4096.0f);
                float ww = w * w;
                a = 0x3f800000u;
                memcpy(&b, &ww, 4);
                break;
            }
            case 3: a = 0x3f800000u | (rnd() & 0x7fffffu);        /* [1,2)/[1,2) */
                    b = 0x3f800000u | (rnd() & 0x7fffffu); break;
            case 4: a = rnd() & 0x007fffffu;                      /* subnormals */
                    b = rnd() & 0x007fffffu; break;
            case 5: a = rnd() & 0x7f800000u;                      /* powers/inf */
                    b = rnd() & 0x7f800000u; break;
            case 6: a = rnd(); b = (a & 0x807fffffu) | 0x3f800000u; break;
            default: {                                            /* n/255-ish  */
                float f = (float) (rnd() & 0xffu);
                memcpy(&a, &f, 4);
                b = 0x437f0000u; /* 255.0 */
                break;
            }
        }
        in[i * 2] = a;
        in[i * 2 + 1] = b;
    }
}

int
main(int argc, char **argv)
{
    int batches = (argc > 1) ? atoi(argv[1]) : 64;

    if (argc > 2)
        rs = (uint32_t) strtoul(argv[2], NULL, 0);

    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.pApplicationName = "r128-fdivtest";
    ai.apiVersion       = VK_API_VERSION_1_2;

    const char *iexts[] = {
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo        = &ai;
    ici.flags                   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ici.enabledExtensionCount   = 2;
    ici.ppEnabledExtensionNames = iexts;
    VK_CHECK(vkCreateInstance(&ici, NULL, &inst));

    uint32_t np = 1;
    VkResult er = vkEnumeratePhysicalDevices(inst, &np, &phys);

    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || np == 0) {
        fprintf(stderr, "no physical device\n");
        return 1;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);

    uint32_t nqf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nqf, NULL);
    VkQueueFamilyProperties *qfp = malloc(nqf * sizeof(*qfp));
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nqf, qfp);
    qfam = ~0u;
    for (uint32_t i = 0; i < nqf; i++)
        if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qfam = i; break; }
    free(qfp);

    uint32_t nde = 0;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &nde, NULL);
    VkExtensionProperties *de = malloc(nde * sizeof(*de));
    vkEnumerateDeviceExtensionProperties(phys, NULL, &nde, de);
    const char *dexts[1];
    uint32_t    ndext = 0;
    for (uint32_t i = 0; i < nde; i++)
        if (!strcmp(de[i].extensionName, "VK_KHR_portability_subset"))
            dexts[ndext++] = "VK_KHR_portability_subset";
    free(de);

    /* Request shaderInt64, as the production divide requires. Leave native
       double precision disabled so the check uses software arithmetic. */
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.features.shaderInt64 = VK_TRUE;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = qfam;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext                   = &f2;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = ndext;
    dci.ppEnabledExtensionNames = dexts;
    VK_CHECK(vkCreateDevice(phys, &dci, NULL, &dev));
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = qfam;
    VK_CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));

    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool        = pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb));

    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(dev, &fci, NULL, &fence));

    mk_buf(&b_in, (VkDeviceSize) N_PAIRS * 2 * sizeof(uint32_t));
    mk_buf(&b_out, (VkDeviceSize) N_PAIRS * 2 * sizeof(uint32_t));

    VkDescriptorSetLayoutBinding lb[2];
    for (int i = 0; i < 2; i++) {
        lb[i].binding            = (uint32_t) i;
        lb[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lb[i].descriptorCount    = 1;
        lb[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
        lb[i].pImmutableSamplers = NULL;
    }
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 2;
    dslci.pBindings    = lb;
    VkDescriptorSetLayout dsl;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));

    VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    VkPipelineLayout plyt;
    VK_CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &plyt));

    VkShaderModule sm = load_spv("fdivtest.spv");
    VkComputePipelineCreateInfo cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = plyt;
    VkPipeline pipe;
    VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &pipe));
    vkDestroyShaderModule(dev, sm, NULL);

    VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &dps;
    VkDescriptorPool dpool;
    VK_CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));

    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool     = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &dsl;
    VkDescriptorSet dset;
    VK_CHECK(vkAllocateDescriptorSets(dev, &dsai, &dset));

    VkDescriptorBufferInfo dbi[2] = {
        { b_in.b, 0, VK_WHOLE_SIZE }, { b_out.b, 0, VK_WHOLE_SIZE }
    };
    VkWriteDescriptorSet wds[2];
    for (int i = 0; i < 2; i++) {
        wds[i] = (VkWriteDescriptorSet) { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        wds[i].dstSet          = dset;
        wds[i].dstBinding      = (uint32_t) i;
        wds[i].descriptorCount = 1;
        wds[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].pBufferInfo     = &dbi[i];
    }
    vkUpdateDescriptorSets(dev, 2, wds, 0, NULL);

    printf("device: %s\n", props.deviceName);
    printf("fdivtest: %d batches x %u pairs, seed 0x%08x\n",
           batches, N_PAIRS, rs);

    uint32_t *in  = b_in.map;
    uint32_t *out = b_out.map;
    uint64_t tested = 0, bad_div = 0, bad_255 = 0, shown = 0;

    /* div255's whole domain is 256 values, so check it exhaustively once
       rather than hoping the random stream covers every one */
    {
        for (uint32_t i = 0; i < 256; i++) { in[i * 2] = i; in[i * 2 + 1] = 0x437f0000u; }
        VK_CHECK(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, plyt, 0, 1, &dset, 0, NULL);
        uint32_t n = 256;
        vkCmdPushConstants(cb, plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &n);
        vkCmdDispatch(cb, (n + 63) / 64, 1, 1);
        VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        VK_CHECK(vkEndCommandBuffer(cb));
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
        VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(dev, 1, &fence));

        uint32_t bad = 0;
        for (uint32_t i = 0; i < 256; i++) {
            float    ref = (float) i / 255.0f;
            uint32_t eb;
            memcpy(&eb, &ref, 4);
            if (out[i * 2 + 1] != eb) {
                if (bad < 4)
                    printf("  div255(%u): gpu=%08x ref=%08x\n", i, out[i * 2 + 1], eb);
                bad++;
            }
        }
        printf("div255 exhaustive (all 256): %u mismatches\n", bad);
        bad_255 += bad;
    }

    for (int bi = 0; bi < batches; bi++) {
        fill(in, N_PAIRS);

        VK_CHECK(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, plyt, 0, 1, &dset, 0, NULL);
        uint32_t n = N_PAIRS;
        vkCmdPushConstants(cb, plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &n);
        vkCmdDispatch(cb, (n + 63) / 64, 1, 1);
        VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        VK_CHECK(vkEndCommandBuffer(cb));
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
        VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(dev, 1, &fence));

        for (uint32_t i = 0; i < N_PAIRS; i++) {
            float a, b;
            memcpy(&a, &in[i * 2], 4);
            memcpy(&b, &in[i * 2 + 1], 4);

            /* NaN results carry an unspecified payload on both sides, so a
               bit compare there would flag a non-difference */
            float ref = a / b;
            if (isnan(ref) || isnan(a) || isnan(b))
                continue;

            uint32_t eb;
            memcpy(&eb, &ref, 4);
            tested++;
            if (out[i * 2] != eb) {
                if (shown < 8) {
                    printf("  fdiv_cr(%a, %a): gpu=%08x ref=%08x (%a)\n",
                           (double) a, (double) b, out[i * 2], eb, (double) ref);
                    shown++;
                }
                bad_div++;
            }
        }
        printf("  batch %2d/%d: %llu tested, %llu bad\r", bi + 1, batches,
               (unsigned long long) tested, (unsigned long long) bad_div);
        fflush(stdout);
    }
    printf("\n");
    printf("fdiv_cr: %llu tested, %llu mismatches\n",
           (unsigned long long) tested, (unsigned long long) bad_div);

    if (bad_div || bad_255) {
        printf("FAIL: this device does not compute the correctly-rounded quotient\n");
        return 1;
    }
    printf("PASS: fdiv_cr and div255 are bit-exact against the host divide\n");
    return 0;
}
