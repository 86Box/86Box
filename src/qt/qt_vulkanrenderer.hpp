#pragma once
/****************************************************************************
**
** Copyright (C) 2022 Cacodemon345
** Copyright (C) 2017 The Qt Company Ltd.
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
****************************************************************************/
#ifndef VULKANRENDERER_HPP
#define VULKANRENDERER_HPP

#include <QVulkanWindow>
#include <QImage>

#if QT_CONFIG(vulkan)
#    include "qt_vulkanwindowrenderer.hpp"

class VulkanRenderer2 : public QVulkanWindowRenderer {
public:
    void  *mappedPtr  = nullptr;
    size_t imagePitch = 2048 * 4;
    VulkanRenderer2(QVulkanWindow *w);

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

    void startNextFrame() override;

private:
    VkShaderModule createShader(const QString &name);
    bool           createTexture();
    bool           createTextureImage(const QSize &size, VkImage *image, VkDeviceMemory *mem,
                                      VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memIndex);
    bool           writeLinearImage(const QImage &img, VkImage image, VkDeviceMemory memory);
    void           ensureTexture();
    void           updateSamplers();

    QVulkanWindow          *m_window;
    QVulkanDeviceFunctions *m_devFuncs;

    VkDeviceMemory         m_bufMem = VK_NULL_HANDLE;
    VkBuffer               m_buf    = VK_NULL_HANDLE;
    VkDescriptorBufferInfo m_uniformBufInfo[QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT];

    VkDescriptorPool      m_descPool      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_descSet[QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT];

    VkPipelineCache  m_pipelineCache  = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;

    VkSampler      m_sampler                  = VK_NULL_HANDLE;
    VkSampler      m_linearSampler            = VK_NULL_HANDLE;
    VkImage        m_texImage                 = VK_NULL_HANDLE;
    VkDeviceMemory m_texMem                   = VK_NULL_HANDLE;
    bool           m_texLayoutPending         = false;
    VkImageView    m_texView                  = VK_NULL_HANDLE;
    VkImage        m_texStaging               = VK_NULL_HANDLE;
    VkDeviceMemory m_texStagingMem            = VK_NULL_HANDLE;
    bool           m_texStagingPending        = false;
    bool           m_texStagingTransferLayout = false;
    QSize          m_texSize;
    VkFormat       m_texFormat;

    QMatrix4x4 m_proj;
};
#endif // QT_CONFIG(vulkan)

#endif // VULKANRENDERER_HPP
