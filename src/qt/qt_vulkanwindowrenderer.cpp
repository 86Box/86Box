/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Vulkan renderer, adapted from various examples, with librashader support.
 *
 * Authors: Teemu Korhonen
 *          Sascha Willems
 *          Cacodemon345
 *
 *          Copyright 2016-2025 Sascha Willems
 *          Copyright 2021 Teemu Korhonen
 *          Copyright 2026 Cacodemon345.
 */
#define VMA_IMPLEMENTATION           1
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "qt_vulkanwindowrenderer.hpp"
#include "qt_vulkanshadermanagerdialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QWindow>

extern "C"
{
    char vk_shader_file[20][512] = {};
}

#if QT_CONFIG(vulkan)
#    include <QVulkanWindowRenderer>
#    include <QVulkanDeviceFunctions>
#    include <array>
#    include <stdexcept>
#    include <algorithm>
#    include <limits>
#    include <cmath>

#    include "qt_mainwindow.hpp"
#    include "qt_osd.hpp"
#    include "qt_slangp.hpp"

extern "C" {
#    include <86box/86box.h>
#    include <86box/path.h>
#    include <86box/plat.h>
#    include <86box/ui.h>
#    include <86box/video.h>
}

extern MainWindow *main_window;

QString
Vulkan_GetResultString(VkResult result)
{
    switch ((int) result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
#    if VK_HEADER_VERSION >= 135 && VK_HEADER_VERSION < 162
        case VK_ERROR_INCOMPATIBLE_VERSION_KHR:
            return "VK_ERROR_INCOMPATIBLE_VERSION_KHR";
#    endif /* VK_HEADER_VERSION >= 135 && VK_HEADER_VERSION < 162 */
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
            return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_NOT_PERMITTED_EXT:
            return "VK_ERROR_NOT_PERMITTED_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_THREAD_IDLE_KHR:
            return "VK_THREAD_IDLE_KHR";
        case VK_THREAD_DONE_KHR:
            return "VK_THREAD_DONE_KHR";
        case VK_OPERATION_DEFERRED_KHR:
            return "VK_OPERATION_DEFERRED_KHR";
        case VK_OPERATION_NOT_DEFERRED_KHR:
            return "VK_OPERATION_NOT_DEFERRED_KHR";
        case VK_PIPELINE_COMPILE_REQUIRED_EXT:
            return "VK_PIPELINE_COMPILE_REQUIRED_EXT";
        default:
            break;
    }
    if (result < 0) {
        return "VK_ERROR_<Unknown>";
    }
    return "VK_<Unknown>";
}

VulkanWindowRenderer::VulkanWindowRenderer(QWidget *parent)
    : QWindow((QWindow *) NULL)
    , renderTimer(new QTimer(this))
    , osdRenderTimer(new QTimer(this))
{
    connect(renderTimer, &QTimer::timeout, this, [this]() { this->render(); });
    connect(osdRenderTimer, &QTimer::timeout, this, [this]() {
        if (video_framerate == -1 && dopause && qt_osd_is_visible())
            this->render();

        if (video_framerate == -1 && !qt_osd_is_visible() && was_osd_visible)
            this->render();

        was_osd_visible = qt_osd_is_visible();
    });
    parentWidget = parent;
    instance.setApiVersion(QVersionNumber(1, 3));
    if (instance.supportedExtensions().contains("VK_KHR_get_physical_device_properties2")) {
        auto list = instance.extensions();
        list.push_back("VK_KHR_get_physical_device_properties2");
        instance.setExtensions(list);
    }
    if (!instance.create()) {
        throw vulkan_init_error(tr("Failed to create Vulkan 1.3 instance"));
    }
    setSurfaceType(QSurface::VulkanSurface);
    setVulkanInstance(&instance);
    buf_usage = std::vector<std::atomic_flag>(1);
    source.setRect(0, 0, 640, 480);
    buf_usage[0].clear();
}

#ifdef LIBRA_RUNTIME_VULKAN
void
VulkanWindowRenderer::cleanupShaderSrcImages()
{
    if (isFinalized || !isInitialized)
        return;
    m_devFuncs->vkDeviceWaitIdle(logi_device);
    for (unsigned int i = 0; i < shaderFilterChains.size(); i++) {
        for (unsigned int j = 0; j < shaderFilterChains[i].size(); j++) {
            if (j != (shaderFilterChains[i].size() - 1)) {
                vmaDestroyImage(allocator, shaderFilterChains[i][j].next_image_chain, shaderFilterChains[i][j].next_image_alloc);
            }
            shaderFilterChains[i][j].chain = nullptr;
        }
        vmaDestroyImage(allocator, shaderSrcImages[i], shaderSrcImageAllocations[i]);
    }
    shaderSrcImageTransitioned.clear();
    shaderFilterChains.clear();
    shaderSrcImages.clear();
    shaderSrcImageAllocations.clear();
}
#endif

QDialog *
VulkanWindowRenderer::getOptions(QWidget *parent)
{
    std::vector<std::string> device_names;
    uint32_t physicalDevices;
    instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, nullptr);
    std::vector<VkPhysicalDevice> phys_devices;
    phys_devices.resize(physicalDevices);
    if (VK_SUCCESS == instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, phys_devices.data())) {
        for (auto& phys_dev: phys_devices) {
            VkPhysicalDeviceProperties phys_dev_prop{};
            instance.functions()->vkGetPhysicalDeviceProperties(phys_dev, &phys_dev_prop);
            device_names.push_back(phys_dev_prop.deviceName);
        }
    }

    return new VulkanShaderManagerDialog(parent, device_names);
}

#ifdef LIBRA_RUNTIME_VULKAN
void
VulkanWindowRenderer::recreateShaderSrcImages()
{
    if (isFinalized || !isInitialized)
        return;
    m_devFuncs->vkDeviceWaitIdle(logi_device);
    cleanupShaderSrcImages();

    shaderSrcImageTransitioned.resize(swapchainImageViews.size());
    shaderSrcImages.resize(swapchainImageViews.size());
    shaderSrcImageAllocations.resize(swapchainImageViews.size());
    shaderFilterChains.resize(swapchainImageViews.size());

    for (unsigned int i = 0; i < swapchainImageViews.size(); i++) {
        VkImageCreateInfo img_info = { };
        img_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType         = VK_IMAGE_TYPE_2D;
        img_info.extent.width      = source.width();
        img_info.extent.height     = source.height();
        img_info.extent.depth      = 1;
        img_info.mipLevels         = 1;
        img_info.arrayLayers       = 1;
        img_info.format            = VK_FORMAT_B8G8R8A8_UNORM;
        img_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage             = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        img_info.samples           = VK_SAMPLE_COUNT_1_BIT;
        img_info.initialLayout     = VK_IMAGE_LAYOUT_PREINITIALIZED;
        img_info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
        img_info.flags             = 0;

        VmaAllocationCreateInfo allocInfo { };
        VmaAllocationInfo       allocInfo2 { };
        allocInfo.pUserData = allocInfo.pool = nullptr;
        allocInfo.requiredFlags = allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags                                    = 0;

        if (vmaCreateImage(allocator, &img_info, &allocInfo, &shaderSrcImages[i], &shaderSrcImageAllocations[i], &allocInfo2) != VK_SUCCESS) {
            throw vulkan_init_error("Failed to create shader source images");
            return;
        }

        img_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        img_info.extent.width      = curExtent.width;
        img_info.extent.height     = curExtent.height;

        for (std::vector<_filter_chain_vk*>::size_type j = 0;
             j < shaderLibraFilterChains.size(); j++) {
            VulkanShaderChain vk_shader_chain { };
            vk_shader_chain.chain = shaderLibraFilterChains[j];
            if (vk_shader_chain.chain) {
                vk_shader_chain.next_image_alloc = nullptr;
                vk_shader_chain.next_image_chain = swapchainImages[i];
            } else {
                continue;
            }
            if (shaderFilterChains[i].size()) {
                auto &last_chain            = shaderFilterChains[i][shaderFilterChains[i].size() - 1];
                last_chain.next_image_chain = nullptr;

                allocInfo           = { };
                allocInfo2          = { };
                allocInfo.pUserData = allocInfo.pool = nullptr;
                allocInfo.requiredFlags = allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                allocInfo.flags                                    = 0;
                if (vmaCreateImage(allocator, &img_info, &allocInfo, &last_chain.next_image_chain, &last_chain.next_image_alloc, &allocInfo2) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create shader image chain images");
                    return;
                }
            }
            shaderFilterChains[i].push_back(vk_shader_chain);
        }
    }
}
#endif

void
VulkanWindowRenderer::cleanupSwapchain()
{
    if (isFinalized || !isInitialized)
        return;
    m_devFuncs->vkDeviceWaitIdle(logi_device);
#ifdef LIBRA_RUNTIME_VULKAN
    cleanupShaderSrcImages();
#endif

    for (unsigned int i = 0; i < swapchainImageViews.size(); i++) {
        m_devFuncs->vkDestroyImageView(logi_device, swapchainImageViews[i], nullptr);
        m_devFuncs->vkDestroySemaphore(logi_device, renderFinishedSemaphores[i], nullptr);
        m_devFuncs->vkDestroySemaphore(logi_device, presentSemaphores[i], nullptr);
        m_devFuncs->vkDestroyFence(logi_device, presentFences[i], nullptr);

        vmaDestroyImage(allocator, swapchainImageScreenshots[i], swapchainImageScreenshotAllocations[i]);
    }
    if (cmdBuffers.size()) {
        m_devFuncs->vkDestroyCommandPool(logi_device, this->commandPool, nullptr);
    }
    swapchainImageScreenshots.clear();
    swapchainImageScreenshotAllocations.clear();
    swapchainImageScreenshotMappedPtrs.clear();

    swapchainImageViews.clear();
    swapchainImageTransitioned.clear();
    renderFinishedSemaphores.clear();
    presentSemaphores.clear();
    cmdBuffers.clear();

    if (this->dev_swapchain)
        fn_vkDestroySwapchainKHR(logi_device, this->dev_swapchain, nullptr);
    this->dev_swapchain = nullptr;
}

void
VulkanWindowRenderer::recreateSwapchain()
{
    if (isFinalized || !isInitialized)
        return;
    VkSurfaceCapabilitiesKHR surfaceCaps;
    if (fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) {
        fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, instance.surfaceForWindow(this), &surfaceCaps);
    } else {
        throw vulkan_init_error("Failed to get surface capabilities");
    }

    uint32_t format_count = 0;

    fn_vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, instance.surfaceForWindow(this), &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
    fn_vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, instance.surfaceForWindow(this), &format_count, surface_formats.data());
#if defined __unix__ && !defined __HAIKU__
    bool passthrough_found = false;

    for (auto& surface_format : surface_formats) {
        if (surface_format.format == VK_FORMAT_B8G8R8A8_UNORM && surface_format.colorSpace == VK_COLOR_SPACE_PASS_THROUGH_EXT) {
            passthrough_found = true;
        }
    }
#endif

    cleanupSwapchain();

    curExtent = surfaceCaps.currentExtent;
    if (curExtent.width == 0 || curExtent.width == ~0u) curExtent.width = width() * devicePixelRatio();
    if (curExtent.height == 0 || curExtent.height == ~0u) curExtent.height = height() * devicePixelRatio();

    if (width() == 0) {
        curExtent.width = 640;
    }

    if (height() == 0) {
        curExtent.height = 480;
    }

    VkSwapchainCreateInfoKHR swapchain_creation = { };
    swapchain_creation.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_creation.surface                  = instance.surfaceForWindow(this);
    swapchain_creation.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_creation.presentMode              = video_vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    swapchain_creation.imageFormat              = VK_FORMAT_B8G8R8A8_UNORM;
#if defined __unix__ && !defined __HAIKU__
    // We don't want trouble on Wayland at all.
    swapchain_creation.imageColorSpace          = passthrough_found ? VK_COLOR_SPACE_PASS_THROUGH_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
#else
    swapchain_creation.imageColorSpace          = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
#endif
    swapchain_creation.imageArrayLayers         = 1;
    swapchain_creation.imageUsage               = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_creation.minImageCount            = std::clamp(surfaceCaps.minImageCount + 1u, surfaceCaps.minImageCount, surfaceCaps.maxImageCount ? surfaceCaps.maxImageCount : std::numeric_limits<uint32_t>::max());
    swapchain_creation.imageExtent              = curExtent;
    swapchain_creation.preTransform             = surfaceCaps.currentTransform;
    swapchain_creation.clipped                  = VK_TRUE;
    swapchain_creation.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_creation.oldSwapchain             = NULL;
    auto res                                    = fn_vkCreateSwapchainKHR(logi_device, &swapchain_creation, nullptr, &dev_swapchain);
    if (res != VK_SUCCESS) {
        printf("Failed to create swapchain (0x%X, %d)\n", res, res);
        throw vulkan_init_error("Failed to create swapchain");
    }

    uint32_t swapchainImagesCount = 0;
    fn_vkGetSwapchainImagesKHR(logi_device, dev_swapchain, &swapchainImagesCount, nullptr);

    swapchainImageScreenshots.resize(swapchainImagesCount);
    swapchainImageScreenshotAllocations.resize(swapchainImagesCount);
    swapchainImageScreenshotMappedPtrs.resize(swapchainImagesCount);

    this->swapchainImages.resize(swapchainImagesCount);
    this->swapchainImageViews.resize(swapchainImagesCount);
    this->swapchainImageTransitioned.resize(swapchainImagesCount);
    this->renderFinishedSemaphores.resize(swapchainImagesCount);
    this->presentSemaphores.resize(swapchainImagesCount);
    this->presentFences.resize(swapchainImagesCount);
    this->cmdBuffers.resize(swapchainImagesCount);
    res = fn_vkGetSwapchainImagesKHR(logi_device, dev_swapchain, &swapchainImagesCount, swapchainImages.data());
    if (res != VK_SUCCESS) {
        throw vulkan_init_error("Failed to get swapchain images");
    }

    VkCommandPoolCreateInfo cmdpollcreate { };
    cmdpollcreate.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdpollcreate.pNext            = nullptr;
    cmdpollcreate.queueFamilyIndex = gfx_queue;
    cmdpollcreate.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if ((res = m_devFuncs->vkCreateCommandPool(logi_device, &cmdpollcreate, nullptr, &commandPool)) != VK_SUCCESS) {
        throw vulkan_init_error("Could not create command pool. Switch to another renderer.");
    }

    VkCommandBufferAllocateInfo cmdbufferallocate { };
    cmdbufferallocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdbufferallocate.commandPool        = commandPool;
    cmdbufferallocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdbufferallocate.commandBufferCount = swapchainImagesCount;

    m_devFuncs->vkAllocateCommandBuffers(logi_device, &cmdbufferallocate, cmdBuffers.data());

    qt_osd_vulkan_set_min_image(cmdBuffers.size());
    init_info.MinImageCount = cmdBuffers.size();

    for (uint32_t i = 0; i < swapchainImagesCount; i++) {
        VkSemaphoreCreateInfo semaphore_create = { };
        semaphore_create.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create.pNext                 = nullptr;
        semaphore_create.flags                 = 0;

        VkFenceCreateInfo fence_create = { };
        fence_create.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create.pNext             = nullptr;
        fence_create.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

        res = m_devFuncs->vkCreateSemaphore(logi_device, &semaphore_create, nullptr, &renderFinishedSemaphores[i]);

        if (res != VK_SUCCESS) {
            throw vulkan_init_error("Failed to create semaphores");
        }

        res = m_devFuncs->vkCreateSemaphore(logi_device, &semaphore_create, nullptr, &presentSemaphores[i]);

        if (res != VK_SUCCESS) {
            throw vulkan_init_error("Failed to create semaphores");
        }

        res = m_devFuncs->vkCreateFence(logi_device, &fence_create, nullptr, &presentFences[i]);

        if (res != VK_SUCCESS) {
            throw vulkan_init_error("Failed to create fences");
        }

        VkImageViewCreateInfo image_view_info           = { };
        image_view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_info.format                          = VK_FORMAT_B8G8R8A8_UNORM;
        image_view_info.image                           = swapchainImages[i];
        image_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        image_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_info.subresourceRange.baseMipLevel   = 0;
        image_view_info.subresourceRange.levelCount     = 1;
        image_view_info.subresourceRange.baseArrayLayer = 0;
        image_view_info.subresourceRange.layerCount     = 1;
        image_view_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
        image_view_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
        image_view_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
        image_view_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
        res = m_devFuncs->vkCreateImageView(logi_device, &image_view_info, nullptr, &swapchainImageViews[i]);
        if (res != VK_SUCCESS) {
            throw vulkan_init_error("Failed to create image views of swapchains");
        }

        // Create screenshot images.
        {
            VkImageCreateInfo img_info = { };
            img_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            img_info.imageType         = VK_IMAGE_TYPE_2D;
            img_info.extent.width      = curExtent.width;
            img_info.extent.height     = curExtent.height;
            img_info.extent.depth      = 1;
            img_info.mipLevels         = 1;
            img_info.arrayLayers       = 1;
            img_info.format            = VK_FORMAT_B8G8R8A8_UNORM;
            img_info.tiling            = VK_IMAGE_TILING_LINEAR;
            img_info.usage             = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            img_info.samples           = VK_SAMPLE_COUNT_1_BIT;
            img_info.initialLayout     = VK_IMAGE_LAYOUT_PREINITIALIZED;
            img_info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
            img_info.flags             = 0;

            VmaAllocationCreateInfo allocInfo { };
            VmaAllocationInfo       allocInfo2 { };
            allocInfo.pUserData = allocInfo.pool = nullptr;
            allocInfo.requiredFlags = allocInfo.preferredFlags = 0;
            allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags                                    = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            if (vmaCreateImage(allocator, &img_info, &allocInfo, &swapchainImageScreenshots[i], &swapchainImageScreenshotAllocations[i], &allocInfo2) != VK_SUCCESS) {
                throw vulkan_init_error("Failed to create screenshot images");
            }

            VkImageSubresource resource { };
            resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            resource.arrayLayer = 0;
            resource.mipLevel   = 0;
            VkSubresourceLayout layout { };
            m_devFuncs->vkGetImageSubresourceLayout(logi_device, swapchainImageScreenshots[i], &resource, &layout);

            swapchainImageScreenshotMappedPtrs[i] = std::pair<void *, uint32_t>((void *) ((uint8_t *) allocInfo2.pMappedData + layout.offset), layout.rowPitch);
        }
    }
#ifdef LIBRA_RUNTIME_VULKAN
    recreateShaderSrcImages();
#endif
}

VulkanWindowRenderer::~VulkanWindowRenderer()
{
    finalize();
}

void
VulkanWindowRenderer::finalize()
{
    if (isFinalized)
        return;

    /* Mark all buffers as in use */
    for (auto &flag : buf_usage)
        flag.test_and_set();
    qt_osd_shutdown();
    cleanupSwapchain();
#ifdef LIBRA_RUNTIME_VULKAN
#ifndef LIBRASHADER_STATIC
    if (!ensure_librashader_instance())
        goto clean_up_rest;
#endif
    for (unsigned int i = 0; i < shaderFilterChains.size(); i++)
#ifndef LIBRASHADER_STATIC
        librashader_inst.vk_filter_chain_free(&shaderLibraFilterChains[i]);
#else
        libra_vk_filter_chain_free(&shaderLibraFilterChains[i]);
#endif

    shaderLibraFilterChains.clear();
#endif

#ifndef LIBRASHADER_STATIC
clean_up_rest:
#endif
    m_devFuncs->vkDestroyImageView(logi_device, src_image_view, nullptr);
    vmaDestroyImage(allocator, src_image, img_allocation);
    vmaDestroyAllocator(allocator);
    m_devFuncs->vkDestroyDevice(logi_device, nullptr);
    m_devFuncs = nullptr;
    isFinalized = true;
    isInitialized = false;
}

void
VulkanWindowRenderer::updateOptions()
{
    cur_video_filter_method = video_filter_method;
}

void
VulkanWindowRenderer::render()
{
    if (!isInitialized || isFinalized)
        return;
    auto fence_res = m_devFuncs->vkWaitForFences(logi_device, 1, &presentFences[current_frame], VK_TRUE, UINT64_MAX);
    if (fence_res == VK_ERROR_DEVICE_LOST) {
        QMessageBox::critical(main_window, tr("Error"), tr("Device lost"));
        finalize();
        return;
    }
    m_devFuncs->vkResetFences(logi_device, 1, &presentFences[current_frame]);
    auto         cmdBufs = this->cmdBuffers[current_frame];

    if (prev_source != source) {
#ifdef LIBRA_RUNTIME_VULKAN
        try {
            recreateShaderSrcImages();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            finalize();
            return;
        }
#endif
        prev_source = source;
    }

    VkCommandBufferBeginInfo beginInfo { };
    beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags            = 0;       // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    updateOptions();

    auto res = fn_vkAcquireNextImageKHR(logi_device, dev_swapchain, (uint64_t) 3 * 1000 * 1000 * 1000, presentSemaphores[current_frame], VK_NULL_HANDLE, &swapchain_image_index);
    if (res == VK_TIMEOUT) {
        pclog("Vulkan: Present image timeout\n");
        return;
    }
    if (res == VK_ERROR_DEVICE_LOST) {
        QMessageBox::critical(main_window, tr("Error"), tr("Device lost"));
        finalize();
        return;
    }
    if (res == VK_ERROR_SURFACE_LOST_KHR) {
        QMessageBox::critical(main_window, tr("Error"), tr("Surface lost"));
        finalize();
        return;
    }
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        try {
            recreateSwapchain();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
        return;
    }
    if (res < 0) {
        QMessageBox::critical(main_window, tr("Error"), QString("vkAcquireNextImageKHR: ") + Vulkan_GetResultString(res));
        finalize();
        return;
    }
#ifndef LIBRA_RUNTIME_VULKAN
    constexpr bool noshadersloaded = true;
#else
    bool noshadersloaded = shaderFilterChains[swapchain_image_index].size() == 0;
#endif
    m_devFuncs->vkBeginCommandBuffer(cmdBufs, &beginInfo);

    const VkImageMemoryBarrier image_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
#ifndef LIBRA_RUNTIME_VULKAN
        .image            = swapchainImages[swapchain_image_index],
#else
        .image            = !noshadersloaded ? shaderSrcImages[swapchain_image_index] : swapchainImages[swapchain_image_index],
#endif
        .subresourceRange = {
                             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1,
                             }
    };
    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
    if (!imageLayoutTransitioned) {
        VkPipelineStageFlags srcflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, dstflags = VK_PIPELINE_STAGE_TRANSFER_BIT;

        VkImageMemoryBarrier barrier { };
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = src_image;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_PREINITIALIZED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, srcflags, dstflags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        imageLayoutTransitioned = true;
    } else {
        VkPipelineStageFlags srcflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, dstflags = VK_PIPELINE_STAGE_TRANSFER_BIT;

        VkImageMemoryBarrier barrier { };
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = src_image;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, srcflags, dstflags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    vmaFlushAllocation(allocator, img_allocation, 0, VK_WHOLE_SIZE);

    if (shaderSrcImageTransitioned[swapchain_image_index] && !noshadersloaded) {
        const VkImageMemoryBarrier image3_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask    = 0,
            .oldLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = swapchainImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, 0, 1, &image3_memory_barrier);
    }

    VkClearColorValue clr_val = {};
    clr_val.float32[0] = 0;
    clr_val.float32[1] = 0;
    clr_val.float32[2] = 0;
    clr_val.float32[3] = 1;

    VkImageBlit bregion{};
    bregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bregion.srcSubresource.mipLevel = 0;
    bregion.srcSubresource.baseArrayLayer = 0;
    bregion.srcSubresource.layerCount = 1;
    bregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bregion.dstSubresource.mipLevel = 0;
    bregion.dstSubresource.baseArrayLayer = 0;
    bregion.dstSubresource.layerCount = 1;
    bregion.srcOffsets[0].x = source.x();
    bregion.srcOffsets[0].y = source.y();
    bregion.srcOffsets[0].z = 0;
    bregion.srcOffsets[1].x = source.x() + source.width();
    bregion.srcOffsets[1].y = source.y() + source.height();
    bregion.srcOffsets[1].z = 1;
    bregion.dstOffsets[0].x = std::clamp((uint32_t)destination.x(), 0u, (uint32_t)curExtent.width);
    bregion.dstOffsets[0].y = std::clamp((uint32_t)destination.y(), 0u, (uint32_t)curExtent.height);
    bregion.dstOffsets[0].z = 0;
    bregion.dstOffsets[1].x = std::clamp((uint32_t)destination.x() + destination.width(), 0u, (uint32_t)curExtent.width);
    bregion.dstOffsets[1].y = std::clamp((uint32_t)destination.y() + destination.height(), 0u, (uint32_t)curExtent.height);
    bregion.dstOffsets[1].z = 1;

    VkImageCopy cregion{};
    cregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cregion.srcSubresource.mipLevel = 0;
    cregion.srcSubresource.baseArrayLayer = 0;
    cregion.srcSubresource.layerCount = 1;
    cregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cregion.dstSubresource.mipLevel = 0;
    cregion.dstSubresource.baseArrayLayer = 0;
    cregion.dstSubresource.layerCount = 1;
    cregion.srcOffset.x = source.x();
    cregion.srcOffset.y = source.y();
    cregion.srcOffset.z = 0;
    cregion.dstOffset.x = 0;
    cregion.dstOffset.y = 0;
    cregion.dstOffset.z = 0;
    cregion.extent.width = source.width();
    cregion.extent.height = source.height();
    cregion.extent.depth = 1;

    VkImageSubresourceRange clr_range;
    clr_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clr_range.baseMipLevel = 0;
    clr_range.baseArrayLayer = 0;
    clr_range.levelCount = 1;
    clr_range.layerCount = 1;

#ifndef LIBRA_RUNTIME_VULKAN
    m_devFuncs->vkCmdClearColorImage(cmdBufs, swapchainImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clr_val, 1, &clr_range);
#else
    m_devFuncs->vkCmdClearColorImage(cmdBufs, noshadersloaded ? swapchainImages[swapchain_image_index] : shaderSrcImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clr_val, 1, &clr_range);
#endif

    const VkImageMemoryBarrier clear_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
#ifdef LIBRA_RUNTIME_VULKAN
        .image            = noshadersloaded ? swapchainImages[swapchain_image_index] : shaderSrcImages[swapchain_image_index],
#else
        .image            = swapchainImages[swapchain_image_index],
#endif
        .subresourceRange = {
                             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1,
                             }
    };

    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &clear_memory_barrier);

    if (noshadersloaded) {
        m_devFuncs->vkCmdBlitImage(cmdBufs, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bregion, cur_video_filter_method ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    } else {
#ifdef LIBRA_RUNTIME_VULKAN
        m_devFuncs->vkCmdCopyImage(cmdBufs, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shaderSrcImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cregion);
#endif
    }

#   ifndef LIBRA_RUNTIME_VULKAN
    const VkImageMemoryBarrier image2_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask    = (VkAccessFlags)(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT),
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image            = swapchainImages[swapchain_image_index],
        .subresourceRange = {
                             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1,
                             }
    };
    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, 0, 1, &image2_memory_barrier);
#else
    const VkImageMemoryBarrier image2_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask    = noshadersloaded ? (VkAccessFlags)(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout        = noshadersloaded ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image            = noshadersloaded ? swapchainImages[swapchain_image_index] : shaderSrcImages[swapchain_image_index],
        .subresourceRange = {
                             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1,
                             }
    };
    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | (noshadersloaded ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : 0) | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, 0, 1, &image2_memory_barrier);
    if (!shaderSrcImageTransitioned[swapchain_image_index] && !noshadersloaded) {
        for (unsigned int i = 0; i < shaderFilterChains[swapchain_image_index].size(); i++) {
            const VkImageMemoryBarrier image_shader_memory_barrier {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask    = 0,
                .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image            = shaderFilterChains[swapchain_image_index][i].next_image_chain,
                .subresourceRange = {
                                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel   = 0,
                                    .levelCount     = 1,
                                    .baseArrayLayer = 0,
                                    .layerCount     = 1,
                                    }
            };
            m_devFuncs->vkCmdPipelineBarrier(cmdBufs, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_shader_memory_barrier);
        }
        shaderSrcImageTransitioned[swapchain_image_index] = 1;
    }

    for (unsigned int i = 0; i < shaderFilterChains[swapchain_image_index].size(); i++) {
        const VkImageMemoryBarrier image_shader_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image            = shaderFilterChains.at(swapchain_image_index).at(i).next_image_chain,
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_shader_memory_barrier);

        auto shader_img_src = (i == 0) ? shaderSrcImages[swapchain_image_index] : shaderFilterChains[swapchain_image_index][i - 1].next_image_chain;
        auto shader_img_dst = shaderFilterChains[swapchain_image_index][i].next_image_chain;

        libra_viewport_t vport{};
        vport.width = curExtent.width;
        vport.height = curExtent.height;
        libra_error_t error = nullptr;

        if (i == 0) {
            vport.x = destination.x();
            vport.y = destination.y();
            vport.width = destination.width();
            vport.height= destination.height();
#ifndef LIBRASHADER_STATIC
            error = librashader_inst.vk_filter_chain_frame(&shaderFilterChains[swapchain_image_index][i].chain, cmdBufs, current_frame_shader, { shader_img_src, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)source.width(), (unsigned int)source.height()}, { shader_img_dst, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)curExtent.width, (unsigned int)curExtent.height }, &vport, nullptr, nullptr);
#else
            error = libra_vk_filter_chain_frame(&shaderFilterChains[swapchain_image_index][i].chain, cmdBufs, current_frame_shader, { shader_img_src, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)source.width(), (unsigned int)source.height()}, { shader_img_dst, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)curExtent.width, (unsigned int)curExtent.height }, &vport, nullptr, nullptr);
#endif
        } else {
            vport.x = 0;
            vport.y = 0;
            vport.width = curExtent.width;
            vport.height = curExtent.height;
#ifndef LIBRASHADER_STATIC
            error = librashader_inst.vk_filter_chain_frame(&shaderFilterChains[swapchain_image_index][i].chain, cmdBufs, current_frame_shader, { shader_img_src, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)curExtent.width, (unsigned int)curExtent.height}, { shader_img_dst, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)curExtent.width, (unsigned int)curExtent.height }, &vport, nullptr, nullptr);
#else
            error = libra_vk_filter_chain_frame(&shaderFilterChains[swapchain_image_index][i].chain, cmdBufs, current_frame_shader, { shader_img_src, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)curExtent.width, (unsigned int)curExtent.height}, { shader_img_dst, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)curExtent.width, (unsigned int)curExtent.height }, &vport, nullptr, nullptr);
#endif
        }

        if (error) {
#ifndef LIBRASHADER_STATIC
            librashader_inst.error_print(error);
#else
            libra_error_print(error);
#endif
        }

        if (i == (shaderFilterChains[swapchain_image_index].size() - 1))
            break;

        const VkImageMemoryBarrier image_shader_memory_barrier_2 {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = shaderFilterChains.at(swapchain_image_index).at(i).next_image_chain,
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_shader_memory_barrier_2);
    }
#endif

    VkRenderingAttachmentInfo render_info = {};
    render_info.clearValue = {};
    render_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    render_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    render_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    render_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    render_info.resolveMode = VK_RESOLVE_MODE_NONE;
    render_info.imageView = swapchainImageViews[swapchain_image_index];
    VkRenderingInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    info.flags = 0;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &render_info;
    info.renderArea = { { 0, 0 }, curExtent };
    info.viewMask = 0;
    info.layerCount = 1;
    fn_vkCmdBeginRendering(cmdBufs, &info);
    
    {
        qt_osd_set_layout_scale_hint(osdLayoutScaleHint());
        qt_osd_render(width(), height(), devicePixelRatio(), (void*)cmdBufs);
    }

    fn_vkCmdEndRendering(cmdBufs);

    if (monitors[r_monitor_index].mon_screenshots || monitors[r_monitor_index].mon_screenshots_clipboard) {
        const VkImageMemoryBarrier image3_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image            = swapchainImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        const VkImageMemoryBarrier image3_scrshot_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = swapchainImageScreenshots[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image3_scrshot_memory_barrier);
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image3_memory_barrier);

        VkImageCopy cregion = {};
        cregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cregion.srcSubresource.mipLevel = 0;
        cregion.srcSubresource.baseArrayLayer = 0;
        cregion.srcSubresource.layerCount = 1;
        cregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cregion.dstSubresource.mipLevel = 0;
        cregion.dstSubresource.baseArrayLayer = 0;
        cregion.dstSubresource.layerCount = 1;
        cregion.srcOffset.x = 0;
        cregion.srcOffset.y = 0;
        cregion.srcOffset.z = 0;
        cregion.dstOffset.x = 0;
        cregion.dstOffset.y = 0;
        cregion.dstOffset.z = 0;
        cregion.extent.width = curExtent.width;
        cregion.extent.height = curExtent.height;
        cregion.extent.depth = 1;

        m_devFuncs->vkCmdCopyImage(cmdBufs, swapchainImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImageScreenshots[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cregion);
        const VkImageMemoryBarrier image4_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image            = swapchainImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, 0, 1, &image4_memory_barrier);

        const VkImageMemoryBarrier image4_scrshot_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask    = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_HOST_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_GENERAL,
            .image            = swapchainImageScreenshots[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image3_scrshot_memory_barrier);
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image4_scrshot_memory_barrier);
    } else {
        const VkImageMemoryBarrier image3_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image            = swapchainImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, 0, 1, &image3_memory_barrier);
    }

    m_devFuncs->vkEndCommandBuffer(cmdBufs);
    // The submit info structure specifies a command buffer queue submission batch
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo         submitInfo { };
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask  = &waitStageMask; // Pointer to the list of pipeline stages that the semaphore waits will occur at
    submitInfo.pCommandBuffers    = &cmdBufs;       // Command buffers(s) to execute in this batch (submission)
    submitInfo.commandBufferCount = 1;              // We submit a single command buff

    // Semaphore to wait upon before the submitted command buffer starts executing
    submitInfo.pWaitSemaphores    = &presentSemaphores[current_frame];
    submitInfo.waitSemaphoreCount = 1;
    // Semaphore to be signaled when command buffers have completed
    submitInfo.pSignalSemaphores    = &renderFinishedSemaphores[swapchain_image_index];
    submitInfo.signalSemaphoreCount = 1;

    // Submit to the graphics queue passing a wait fence
    auto submit_res = m_devFuncs->vkQueueSubmit(gfx_queue_o, 1, &submitInfo, presentFences[current_frame]);
    if (submit_res != VK_SUCCESS) {
        QMessageBox::critical(main_window, tr("Error"), Vulkan_GetResultString(submit_res));
        finalize();
        return;
    }

    if (monitors[r_monitor_index].mon_screenshots || monitors[r_monitor_index].mon_screenshots_clipboard) {
        // Wait for fence to be signalled.
        m_devFuncs->vkWaitForFences(logi_device, 1, &presentFences[current_frame], VK_FALSE, (uint64_t)-1);

        auto scrShotImagePitch = swapchainImageScreenshotMappedPtrs[swapchain_image_index].second;
        uint8_t* scrShotImageMapPtr = (uint8_t*)swapchainImageScreenshotMappedPtrs[swapchain_image_index].first;

        if (monitors[r_monitor_index].mon_screenshots) {
            int  width = curExtent.width, height = curExtent.height;
            char path[1024];
            char fn[256];

            memset(fn, 0, sizeof(fn));
            memset(path, 0, sizeof(path));

            path_append_filename(path, usr_path, SCREENSHOT_PATH);

            if (!plat_dir_check(path))
                plat_dir_create(path);

            path_slash(path);
            strcat(path, "Monitor_");
            snprintf(&path[strlen(path)], 42, "%d_", r_monitor_index + 1);

            plat_tempfile(fn, NULL, (char *) ".png");
            strcat(path, fn);

            unsigned char *rgb = scrShotImageMapPtr;

            QImage image((uchar*)rgb, width, height, scrShotImagePitch, QImage::Format_RGBA8888);
            image.rgbSwapped().save(path, "png");
            monitors[r_monitor_index].mon_screenshots--;
        }
        if (monitors[r_monitor_index].mon_screenshots_clipboard) {
            int  width = curExtent.width, height = curExtent.height;

            unsigned char *rgb = scrShotImageMapPtr;

            QImage image((uchar*)rgb, width, height, scrShotImagePitch, QImage::Format_RGBA8888);
            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setImage(image.rgbSwapped(), QClipboard::Clipboard);
            monitors[r_monitor_index].mon_screenshots_clipboard--;
        }
    }

    // Present the current frame buffer to the swap chain
    // Pass the semaphore signaled by the command buffer submission from the submit info as the wait semaphore for swap chain presentation
    // This ensures that the image is not presented to the windowing system until all commands have been submitted

    VkPresentInfoKHR presentInfo { };
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinishedSemaphores[swapchain_image_index];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &dev_swapchain;
    presentInfo.pImageIndices      = &swapchain_image_index;
    instance.presentAboutToBeQueued(this);
    auto result                    = fn_vkQueuePresentKHR(gfx_queue_o, &presentInfo);
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
        QMessageBox::critical(main_window, tr("Error"), tr("Surface lost"));
        finalize();
        return;
    }
    if (result == VK_ERROR_DEVICE_LOST) {
        QMessageBox::critical(main_window, tr("Error"), tr("Device lost"));
        finalize();
        return;
    }
    if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        try {
            recreateSwapchain();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
    } else if (result != VK_SUCCESS) {
        QMessageBox::critical(main_window, tr("Error"), Vulkan_GetResultString(res));
        finalize();
        return;
    }
    instance.presentQueued(this);

    current_frame = (current_frame + 1) % swapchainImageViews.size();
    current_frame_shader++;
}

PFN_vkVoidFunction vk_function_ret_callback(const char *function_name, void *user_data)
{
    QVulkanInstance* inst = (QVulkanInstance*)user_data;

    return inst->getInstanceProcAddr(function_name);
}

bool
VulkanWindowRenderer::isPhysicalDeviceUsable(VkPhysicalDevice &phys_dev)
{
    VkFormatProperties format_prop { };

    instance.functions()->vkGetPhysicalDeviceFormatProperties(phys_dev, VK_FORMAT_B8G8R8A8_UNORM, &format_prop);
    if (!(format_prop.optimalTilingFeatures & VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        return false;
    }
    if (!(format_prop.linearTilingFeatures & VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
        return false;
    }

    if (!(format_prop.optimalTilingFeatures & VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
        return false;
    }
    if (!(format_prop.linearTilingFeatures & VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
        return false;
    }
    bool                       dynamicRenderingSupported = false;
    VkPhysicalDeviceProperties phys_dev_prop { };

    instance.functions()->vkGetPhysicalDeviceProperties(phys_dev, &phys_dev_prop);
    int minor = VK_API_VERSION_MINOR(phys_dev_prop.apiVersion);
    int major = VK_API_VERSION_MAJOR(phys_dev_prop.apiVersion);
    if (major > 1 || (major == 1 && minor >= 3)) {
        VkPhysicalDeviceVulkan13Features vk13_features { };
        vk13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2KHR vk_features_2 { };
        vk_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        vk_features_2.pNext = &vk13_features;
        fn_vkGetPhysicalDeviceFeatures2KHR(phys_dev, &vk_features_2);
        dynamicRenderingSupported = vk13_features.dynamicRendering;
    } else {
        return false;
    }
    if (!dynamicRenderingSupported) {
        return false;
    }
    uint32_t queue_count;
    instance.functions()->vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_info(queue_count);
    instance.functions()->vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &queue_count, queue_info.data());
    for (unsigned int i = 0; i < queue_count; i++) {
        if (instance.supportsPresent(phys_dev, i, this) && (queue_info[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            present_queue = gfx_queue = i;
            break;
        }
    }
    if (present_queue == -1) {
        for (unsigned int i = 0; i < queue_count; i++) {
            if (instance.supportsPresent(phys_dev, i, this)) {
                present_queue = i;
                break;
            }
        }
    }
    if (gfx_queue == -1) {
        for (unsigned int i = 0; i < queue_count; i++) {
            if (queue_info[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                present_queue = i;
                break;
            }
        }
    }

    if (present_queue != gfx_queue) {
        return false;
    }

    if (gfx_queue == -1 || present_queue == -1) {
        return false;
    }

    return true;
}

class raii_set_value
{
private:
    bool& dest_val;
public:   
    raii_set_value(bool& val)
    : dest_val(val)
    {

    }

    ~raii_set_value() {
        dest_val = false;
    }
};

void
VulkanWindowRenderer::initialize()
{
    if (isFinalized || isInitialized)
        return;
    if (initialization_in_progress)
        return;
    initialization_in_progress = true;
    raii_set_value value_cleanup(initialization_in_progress);
    try {
#ifdef LIBRA_RUNTIME_VULKAN
#ifndef LIBRASHADER_STATIC
        static bool not_found_msg_disp = false;
        if (!ensure_librashader_instance()) {
            if (!not_found_msg_disp) {
                auto msgBox = new QMessageBox(QMessageBox::Critical, tr("Error"), tr("librashader not found. Shaders will not be available"), QMessageBox::Ok);
                msgBox->setAttribute(Qt::WA_DeleteOnClose);
                msgBox->show();
            }
            not_found_msg_disp = true;
        }
#endif
#endif
        window_surface = instance.surfaceForWindow(this);
        if (!window_surface) {
            throw vulkan_init_error("Failed to get VkSurfaceKHR from window.");
        }
        fn_vkGetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR) instance.getInstanceProcAddr("vkGetPhysicalDeviceFeatures2");
        uint32_t physicalDevices = 0;

        instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, nullptr);
        if (physicalDevices != 0) {
            std::vector<VkPhysicalDevice> phys_devices;
            phys_devices.resize(physicalDevices);
            if (VK_SUCCESS == instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, phys_devices.data())) {
                phys_device       = phys_devices[std::clamp((uint32_t)video_vk_device, 0u, physicalDevices - 1u)];

                if (!isPhysicalDeviceUsable(phys_device)) {
                    for (uint32_t i = 0; i < phys_devices.size(); i++) {
                        if (isPhysicalDeviceUsable(phys_devices[i])) {
                            phys_device = phys_devices[i];
                            break;
                        }
                    }
                    throw vulkan_init_error(tr("No usable Vulkan physical devices found."));
                }

                std::vector<std::string>  extList;
                std::vector<const char *> extListC;

                extList.push_back("VK_KHR_swapchain");

                for (auto &ext : extList) {
                    extListC.push_back(ext.c_str());
                }

                std::vector<VkDeviceQueueCreateInfo> addQueueInfo(1);
                const float                          prio[] = { 0 };

                addQueueInfo[0].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                addQueueInfo[0].queueFamilyIndex = gfx_queue;
                addQueueInfo[0].queueCount       = 1;
                addQueueInfo[0].pQueuePriorities = prio;
                addQueueInfo[0].flags            = 0;
                addQueueInfo[0].pNext            = nullptr;

                if (gfx_queue != present_queue) {
                    addQueueInfo.resize(addQueueInfo.size() + 1);
                    addQueueInfo[addQueueInfo.size() - 1].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    addQueueInfo[addQueueInfo.size() - 1].queueFamilyIndex = present_queue;
                    addQueueInfo[addQueueInfo.size() - 1].queueCount       = 1;
                    addQueueInfo[addQueueInfo.size() - 1].pQueuePriorities = prio;
                    addQueueInfo[addQueueInfo.size() - 1].flags            = 0;
                    addQueueInfo[addQueueInfo.size() - 1].pNext            = nullptr;
                }

                VkDeviceCreateInfo dev_create_info      = { };
                dev_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                dev_create_info.queueCreateInfoCount    = addQueueInfo.size();
                dev_create_info.pQueueCreateInfos       = addQueueInfo.data();
                dev_create_info.enabledExtensionCount   = extListC.size();
                dev_create_info.ppEnabledExtensionNames = extListC.data();

                
                VkPhysicalDeviceVulkan13Features vk13_features{};
                vk13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

                VkPhysicalDeviceFeatures2 features = { };
                features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
                features.pNext = &vk13_features;
                fn_vkGetPhysicalDeviceFeatures2KHR(phys_device, &features);
                dev_create_info.pEnabledFeatures = nullptr;
                dev_create_info.pNext            = &features;
                auto res                         = instance.functions()->vkCreateDevice(phys_device, &dev_create_info, nullptr, &logi_device);
                if (res != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create logical device");
                }
                fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) instance.getInstanceProcAddr("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
                fn_vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR) instance.getInstanceProcAddr("vkGetPhysicalDeviceSurfaceFormatsKHR");

                instance.deviceFunctions(logi_device)->vkGetDeviceQueue(logi_device, gfx_queue, 0, &gfx_queue_o);
                m_devFuncs = instance.deviceFunctions(logi_device);

                VmaAllocatorCreateInfo vma_info { };
                VmaVulkanFunctions     vma_funcs { };

                fn_vkCreateSwapchainKHR         = (PFN_vkCreateSwapchainKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCreateSwapchainKHR"));
                fn_vkDestroySwapchainKHR        = (PFN_vkDestroySwapchainKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkDestroySwapchainKHR"));
                fn_vkGetSwapchainImagesKHR      = (PFN_vkGetSwapchainImagesKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkGetSwapchainImagesKHR"));
                fn_vkAcquireNextImageKHR        = (PFN_vkAcquireNextImageKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkAcquireNextImageKHR"));
                fn_vkQueuePresentKHR            = (PFN_vkQueuePresentKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkQueuePresentKHR"));
                vma_funcs.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) instance.getInstanceProcAddr("vkGetInstanceProcAddr");
                vma_funcs.vkGetDeviceProcAddr   = (PFN_vkGetDeviceProcAddr) instance.getInstanceProcAddr("vkGetDeviceProcAddr");

                vma_info.device         = logi_device;
                vma_info.physicalDevice = phys_device;
                vma_info.instance       = instance.vkInstance();

                VkImageCreateInfo img_info = { };
                vma_info.pVulkanFunctions  = &vma_funcs;
                if (vmaCreateAllocator(&vma_info, &allocator) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create VMA allocator");
                }
                img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                img_info.imageType     = VK_IMAGE_TYPE_2D;
                img_info.extent.width  = 2048;
                img_info.extent.height = 2048;
                img_info.extent.depth  = 1;
                img_info.mipLevels     = 1;
                img_info.arrayLayers   = 1;
                img_info.format        = VK_FORMAT_B8G8R8A8_UNORM;
                img_info.tiling        = VK_IMAGE_TILING_LINEAR;
                img_info.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
                img_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
                img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
                img_info.flags         = 0;

                VmaAllocationInfo       allocatedInfo { };
                VmaAllocationCreateInfo allocInfo { };
                allocInfo.pUserData = allocInfo.pool = nullptr;
                allocInfo.requiredFlags = allocInfo.preferredFlags = 0;
                allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
                allocInfo.flags                                    = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

                if (vmaCreateImage(allocator, &img_info, &allocInfo, &src_image, &img_allocation, &allocatedInfo) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create source image");
                }

                VkImageViewCreateInfo imageViewInfo { };
                imageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
                imageViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                imageViewInfo.format                          = VK_FORMAT_B8G8R8A8_UNORM;
                imageViewInfo.image                           = src_image;
                imageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                imageViewInfo.subresourceRange.baseMipLevel   = 0;
                imageViewInfo.subresourceRange.levelCount     = 1;
                imageViewInfo.subresourceRange.baseArrayLayer = 0;
                imageViewInfo.subresourceRange.layerCount     = 1;
                imageViewInfo.components.r                    = VK_COMPONENT_SWIZZLE_R;
                imageViewInfo.components.g                    = VK_COMPONENT_SWIZZLE_G;
                imageViewInfo.components.b                    = VK_COMPONENT_SWIZZLE_B;
                imageViewInfo.components.a                    = VK_COMPONENT_SWIZZLE_A;

                if (m_devFuncs->vkCreateImageView(logi_device, &imageViewInfo, nullptr, &src_image_view) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create source image view");
                }

                updateOptions();

                VkImageSubresource resource { };
                resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                resource.arrayLayer = 0;
                resource.mipLevel   = 0;
                VkSubresourceLayout layout { };
                m_devFuncs->vkGetImageSubresourceLayout(logi_device, src_image, &resource, &layout);

                imagePitch = layout.rowPitch;
                mappedPtr  = (uint8_t *) allocatedInfo.pMappedData + layout.offset;
                isInitialized = true;
                isFinalized = false;
                recreateSwapchain();

#ifdef LIBRA_RUNTIME_VULKAN
                int num_shaders = 0;
#ifndef LIBRASHADER_STATIC
                if (!ensure_librashader_instance())
                    goto skip_shaders;
#endif
                for (int i = 0; i < 20; ++i) {
                    if (strlen(vk_shader_file[i]))
                        ++num_shaders;
                    else
                        break;
                }

                libra_device_vk_t vk_dev;
                vk_dev.entry           = (PFN_vkGetInstanceProcAddr) instance.getInstanceProcAddr("vkGetInstanceProcAddr");
                vk_dev.instance        = instance.vkInstance();
                vk_dev.device          = logi_device;
                vk_dev.physical_device = phys_device;
                vk_dev.queue           = gfx_queue_o;

                for (int j = 0; j < num_shaders; j++) {
                    if (vk_shader_file[j][0] != 0) {
                        std::vector<std::pair<std::string, double>> parameter_values;
                        auto shader = slangp_parse(vk_shader_file[j]);
                        if (shader) {
                            for (unsigned int l = 0; l < shader->param_list.length; l++) {
                                parameter_values.push_back(std::pair<std::string, double>(shader->param_list.parameters[l].name, shader->param_values[l]));
                            }
                            libra_vk_filter_chain_t filter_chain = nullptr;
#ifndef LIBRASHADER_STATIC
                            librashader_inst.preset_free_runtime_params(shader->param_list);
#else
                            libra_preset_free_runtime_params(shader->param_list);
#endif
                            shader->param_list.parameters = 0;
                            filter_chain_vk_opt_t vk{};
                            vk.use_dynamic_rendering = 1;
#ifndef LIBRASHADER_STATIC
                            auto err = librashader_inst.vk_filter_chain_create(&shader->shader_preset, vk_dev, &vk, &filter_chain);
#else
                            auto err = libra_vk_filter_chain_create(&shader->shader_preset, vk_dev, &vk, &filter_chain);
#endif
                            delete shader;
                            if (filter_chain) {
                                for (auto &curPair : parameter_values) {
#ifndef LIBRASHADER_STATIC
                                    librashader_inst.vk_filter_chain_set_param(&filter_chain, curPair.first.c_str(), curPair.second);
#else
                                    libra_vk_filter_chain_set_param(&filter_chain, curPair.first.c_str(), curPair.second);
#endif
                                }
                                shaderLibraFilterChains.push_back(filter_chain);
                            } else {
#ifndef LIBRASHADER_STATIC
                                char* errmsg = nullptr;
                                librashader_inst.error_write(err, &errmsg);
                                QMessageBox::critical(main_window, tr("Error"), QString::fromUtf8(vk_shader_file[j]) + "\n\n" + errmsg);
                                librashader_inst.error_free_string(&errmsg);
                                librashader_inst.error_free(&err);
#else
                                char* errmsg = nullptr;
                                libra_error_write(err, &errmsg);
                                QMessageBox::critical(main_window, tr("Error"), QString::fromUtf8(vk_shader_file[j]) + "\n\n" + errmsg);
                                libra_error_free_string(&errmsg);
                                libra_error_free(&err);
#endif
                            }
                        }
                    }
                }
#endif
#ifndef LIBRASHADER_STATIC
skip_shaders:
#endif
                init_info = {};
                init_info.ApiVersion = VK_VERSION_1_0;
                init_info.Instance = instance.vkInstance();
                init_info.PhysicalDevice = phys_device;
                init_info.Device = logi_device;
                init_info.QueueFamily = gfx_queue;
                init_info.Queue = gfx_queue_o;

                init_info.DescriptorPoolSize = 16;
                init_info.MinImageCount = 2;
                init_info.ImageCount = 64;

                init_info.PipelineInfoMain.RenderPass = 0;
                init_info.PipelineInfoMain.Subpass = 0;
                init_info.UseDynamicRendering = 1;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {};
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pNext = nullptr;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorAttachmentFormat;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.viewMask = 0;

                qt_osd_start_vulkan(vk_function_ret_callback, &instance, &init_info);

                if (video_framerate != -1) {
                    renderTimer->setTimerType(Qt::PreciseTimer);
                    renderTimer->start(ceilf(1000.f / (float) video_framerate));
                }

                osdRenderTimer->start(16);

                fn_vkCmdBeginRendering          = (PFN_vkCmdBeginRenderingKHR) instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCmdBeginRendering");
                fn_vkCmdEndRendering            = (PFN_vkCmdEndRenderingKHR) instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCmdEndRendering");
                render();
                emit rendererInitialized();
            }
        } else {
            throw vulkan_init_error("No Vulkan-capable physical devices found.");
        }
    } catch (const vulkan_init_error &e) {
        /* Mark all buffers as in use */
        for (auto &flag : buf_usage)
            flag.test_and_set();

        isFinalized   = true;
        isInitialized = true;

        main_window->showMessage(MBX_ERROR, QString(), tr("Error initializing Vulkan.") + QStringLiteral("\n") + tr(e.what()) + QStringLiteral("\n") + tr("Falling back to software rendering."), false);

        emit errorInitializing();
    }
}

void
VulkanWindowRenderer::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);
    this->pixelRatio = devicePixelRatio();
    onResize(size().width(), size().height());
    QWindow::exposeEvent(event);

    if (!isInitialized && isExposed()) {
        initialize();
        return;
    }

    if (isInitialized && isExposed()) {
        try {
            recreateSwapchain();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
    }
}

void
VulkanWindowRenderer::resizeEvent(QResizeEvent *event)
{
    this->pixelRatio = devicePixelRatio();
    QWindow::resizeEvent(event);
    onResize(width(), height());

    if (isInitialized) {
        try {
            recreateSwapchain();
            if (video_framerate == -1)
                render();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
    }
}

bool
VulkanWindowRenderer::event(QEvent *event)
{
    if (qt_osd_is_visible()) {
        switch (event->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseMove:
            case QEvent::MouseButtonRelease: {
                auto *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                const QPointF pos = me->position();
#else
                const QPointF pos(me->x(), me->y());
#endif
                qt_osd_mouse_pos((float) pos.x(), (float) pos.y());
                if (event->type() == QEvent::MouseButtonPress)
                    qt_osd_mouse_button(me->button(), true);
                else if (event->type() == QEvent::MouseButtonRelease)
                    qt_osd_mouse_button(me->button(), false);
                return true;
            }
            case QEvent::Wheel: {
                auto *we = static_cast<QWheelEvent *>(event);
                qt_osd_mouse_wheel((float) we->angleDelta().x() / 120.0f,
                                   (float) we->angleDelta().y() / 120.0f);
                return true;
            }
            default:
                break;
        }
    }

    bool res = false;
    if (!eventDelegate(event, res))
        return QWindow::event(event);
    return res;
}

extern void take_screenshot_clipboard_monitor(int sx, int sy, int sw, int sh, int i);

void
VulkanWindowRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    auto origSource = source;
    source.setRect(x, y, w, h);
    if (origSource != source) {
        this->pixelRatio = devicePixelRatio();
        onResize(this->width(), this->height());
        try {
            if (isInitialized && isExposed())
                recreateSwapchain();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
    }
    if (isExposed() && video_framerate == -1) {
        // requestUpdate();
        render();
    }
    buf_usage[0].clear();
    if (monitors[r_monitor_index].mon_screenshots_raw_clipboard) {
        take_screenshot_clipboard_monitor(x, y, w, h, r_monitor_index);
    }
}

uint32_t
VulkanWindowRenderer::getBytesPerRow()
{
    return imagePitch;
}

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
VulkanWindowRenderer::getBuffers()
{
    return std::vector { std::make_tuple((uint8_t *) mappedPtr, &this->buf_usage[0]) };
}
#endif /* QT_CONFIG(vulkan) */
