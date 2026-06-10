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
#define VMA_IMPLEMENTATION           1
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "qt_vulkanwindowrenderer.hpp"

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QWindow>

#if QT_CONFIG(vulkan)
#    include <QVulkanWindowRenderer>
#    include <QVulkanDeviceFunctions>
#    include <array>
#    include <stdexcept>
#    include <algorithm>
#    include <limits>

#    include "qt_mainwindow.hpp"
#    include "qt_osd.hpp"

extern "C" {
#    include <86box/86box.h>
#    include <86box/path.h>
#    include <86box/plat.h>
#    include <86box/ui.h>
#    include <86box/video.h>
}

extern MainWindow *main_window;
/*
#version 450

layout(location = 0) out vec2 v_texcoord;

mat4 mvp = mat4(
vec4(1.0, 0, 0, 0),
vec4(0, 1.0, 0, 0),
vec4(0, 0, 1.0, 0),
vec4(0, 0, 0, 1.0)
);

// Y coordinate in Vulkan viewport space is flipped as compared to OpenGL.
vec4 vertCoords[4] = vec4[](
vec4(-1.0, -1.0, 0, 1),
vec4(1.0, -1.0, 0, 1),
vec4(-1.0, 1.0, 0, 1),
vec4(1.0, 1.0, 0, 1)
);

layout(push_constant) uniform texCoordBuf {
    vec2 texCoords[4];
} texCoordUniform;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    v_texcoord = texCoordUniform.texCoords[gl_VertexIndex];
    gl_Position = mvp * vertCoords[gl_VertexIndex];
}
*/
// Compiled with glslangValidator -V
static const uint8_t vertShaderCode[] = {
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x08, 0x00,
    0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x1f, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x03, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x6d, 0x76, 0x70, 0x00, 0x05, 0x00, 0x05, 0x00, 0x16, 0x00, 0x00, 0x00,
    0x76, 0x65, 0x72, 0x74, 0x43, 0x6f, 0x6f, 0x72, 0x64, 0x73, 0x00, 0x00,
    0x05, 0x00, 0x05, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x76, 0x5f, 0x74, 0x65,
    0x78, 0x63, 0x6f, 0x6f, 0x72, 0x64, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
    0x21, 0x00, 0x00, 0x00, 0x74, 0x65, 0x78, 0x43, 0x6f, 0x6f, 0x72, 0x64,
    0x42, 0x75, 0x66, 0x00, 0x06, 0x00, 0x06, 0x00, 0x21, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x74, 0x65, 0x78, 0x43, 0x6f, 0x6f, 0x72, 0x64,
    0x73, 0x00, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00, 0x23, 0x00, 0x00, 0x00,
    0x74, 0x65, 0x78, 0x43, 0x6f, 0x6f, 0x72, 0x64, 0x55, 0x6e, 0x69, 0x66,
    0x6f, 0x72, 0x6d, 0x00, 0x05, 0x00, 0x06, 0x00, 0x27, 0x00, 0x00, 0x00,
    0x67, 0x6c, 0x5f, 0x56, 0x65, 0x72, 0x74, 0x65, 0x78, 0x49, 0x6e, 0x64,
    0x65, 0x78, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00, 0x2c, 0x00, 0x00, 0x00,
    0x67, 0x6c, 0x5f, 0x50, 0x65, 0x72, 0x56, 0x65, 0x72, 0x74, 0x65, 0x78,
    0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x06, 0x00, 0x2c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50, 0x6f, 0x73, 0x69, 0x74,
    0x69, 0x6f, 0x6e, 0x00, 0x05, 0x00, 0x03, 0x00, 0x2e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x1f, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x05, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x03, 0x00,
    0x21, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x27, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x05, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x03, 0x00,
    0x2c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x18, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
    0x2b, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x07, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x2c, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x07, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x04, 0x00, 0x12, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x04, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x15, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x15, 0x00, 0x00, 0x00,
    0x16, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf,
    0x2c, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x07, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x2c, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x07, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x1c, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
    0x1a, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
    0x1d, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x1d, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x1e, 0x00, 0x00, 0x00,
    0x1f, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x04, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x03, 0x00, 0x21, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x22, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x21, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x15, 0x00, 0x04, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x2b, 0x00, 0x04, 0x00, 0x24, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x26, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x26, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x29, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x1d, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x03, 0x00, 0x2c, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x2d, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x2d, 0x00, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x31, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x35, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x3e, 0x00, 0x03, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x3e, 0x00, 0x03, 0x00, 0x16, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00,
    0x3d, 0x00, 0x04, 0x00, 0x24, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x27, 0x00, 0x00, 0x00, 0x41, 0x00, 0x06, 0x00, 0x29, 0x00, 0x00, 0x00,
    0x2a, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00,
    0x28, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x1d, 0x00, 0x00, 0x00,
    0x2b, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
    0x1f, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x3d, 0x00, 0x04, 0x00, 0x24, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x27, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00, 0x31, 0x00, 0x00, 0x00,
    0x32, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x3d, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00,
    0x32, 0x00, 0x00, 0x00, 0x91, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x34, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00,
    0x41, 0x00, 0x05, 0x00, 0x35, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
    0x2e, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
    0x36, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00,
    0x38, 0x00, 0x01, 0x00
};

/*
#version 440

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

void main()
{
    fragColor = texture(tex, v_texcoord);
}
*/

static const uint8_t fragShaderCode[] {
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x08, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x10, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x02, 0x00, 0x00, 0x00, 0xb8, 0x01, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x05, 0x00, 0x09, 0x00, 0x00, 0x00, 0x66, 0x72, 0x61, 0x67,
    0x43, 0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00, 0x05, 0x00, 0x03, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x74, 0x65, 0x78, 0x00, 0x05, 0x00, 0x05, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x76, 0x5f, 0x74, 0x65, 0x78, 0x63, 0x6f, 0x6f,
    0x72, 0x64, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x3b, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x2b, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3f, 0x15, 0x00, 0x04, 0x00, 0x15, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
    0x3d, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x57, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x3e, 0x00, 0x03, 0x00, 0x09, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x41, 0x00, 0x05, 0x00, 0x17, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00,
    0x38, 0x00, 0x01, 0x00
};

VulkanWindowRenderer::VulkanWindowRenderer(QWidget *parent)
    : QWindow((QWindow *) NULL)
{
    parentWidget = parent;
    instance.setApiVersion(QVersionNumber(1, 0));
    if (instance.supportedExtensions().contains("VK_KHR_get_physical_device_properties2")) {
        auto list = instance.extensions();
        list.push_back("VK_KHR_get_physical_device_properties2");
        instance.setExtensions(list);
    }
    instance.create();
    setSurfaceType(QSurface::VulkanSurface);
    setVulkanInstance(&instance);
    buf_usage = std::vector<std::atomic_flag>(1);
    buf_usage[0].clear();
}

void
VulkanWindowRenderer::cleanupSwapchain()
{
    instance.deviceFunctions(logi_device)->vkDeviceWaitIdle(logi_device);

    for (int i = 0; i < swapchainImageViews.size(); i++) {
        instance.deviceFunctions(logi_device)->vkDestroyImageView(logi_device, swapchainImageViews[i], nullptr);
        m_devFuncs->vkDestroySemaphore(logi_device, renderFinishedSemaphores[i], nullptr);
        m_devFuncs->vkDestroySemaphore(logi_device, presentSemaphores[i], nullptr);
        m_devFuncs->vkDestroyFence(logi_device, presentFences[i], nullptr);
    }
    if (cmdBuffers.size()) {
        m_devFuncs->vkDestroyCommandPool(logi_device, this->commandPool, nullptr);
    }
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
    cleanupSwapchain();
    VkSurfaceCapabilitiesKHR surfaceCaps;
    if (fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) {
        fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, instance.surfaceForWindow(this), &surfaceCaps);
    } else {
        // throw vulkan_init_error("Failed to get surface capabilities");
    }

    curExtent = surfaceCaps.currentExtent;

    VkSwapchainCreateInfoKHR swapchain_creation = { };
    swapchain_creation.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_creation.surface                  = instance.surfaceForWindow(this);
    swapchain_creation.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_creation.presentMode              = VK_PRESENT_MODE_IMMEDIATE_KHR;
    swapchain_creation.imageFormat              = VK_FORMAT_R8G8B8A8_UNORM;
    swapchain_creation.imageColorSpace          = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_creation.imageArrayLayers         = 1;
    swapchain_creation.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapchain_creation.minImageCount            = std::clamp(surfaceCaps.minImageCount + 1u, surfaceCaps.minImageCount, surfaceCaps.maxImageCount ? surfaceCaps.maxImageCount : std::numeric_limits<uint32_t>::max());
    swapchain_creation.imageExtent              = surfaceCaps.currentExtent;
    swapchain_creation.preTransform             = surfaceCaps.currentTransform;
    swapchain_creation.clipped                  = VK_TRUE;
    swapchain_creation.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_creation.oldSwapchain             = NULL;
    auto res                                    = fn_vkCreateSwapchainKHR(logi_device, &swapchain_creation, nullptr, &dev_swapchain);
    if (res != VK_SUCCESS) {
        // throw vulkan_init_error("Failed to create swapchain");
    }

    uint32_t swapchainImagesCount = 0;
    fn_vkGetSwapchainImagesKHR(logi_device, dev_swapchain, &swapchainImagesCount, nullptr);

    this->swapchainImages.resize(swapchainImagesCount);
    this->swapchainImageViews.resize(swapchainImagesCount);
    this->swapchainImageTransitioned.resize(swapchainImagesCount);
    this->renderFinishedSemaphores.resize(swapchainImagesCount);
    this->presentSemaphores.resize(swapchainImagesCount);
    this->presentFences.resize(swapchainImagesCount);
    this->cmdBuffers.resize(swapchainImagesCount);
    fn_vkGetSwapchainImagesKHR(logi_device, dev_swapchain, &swapchainImagesCount, swapchainImages.data());

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

    for (int i = 0; i < swapchainImagesCount; i++) {
        VkSemaphoreCreateInfo semaphore_create = { };
        semaphore_create.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create.pNext                 = nullptr;
        semaphore_create.flags                 = 0;

        VkFenceCreateInfo fence_create = { };
        fence_create.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create.pNext             = nullptr;
        fence_create.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

        m_devFuncs->vkCreateSemaphore(logi_device, &semaphore_create, nullptr, &renderFinishedSemaphores[i]);
        m_devFuncs->vkCreateSemaphore(logi_device, &semaphore_create, nullptr, &presentSemaphores[i]);
        m_devFuncs->vkCreateFence(logi_device, &fence_create, nullptr, &presentFences[i]);

        VkImageViewCreateInfo image_view_info           = { };
        image_view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
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
        instance.deviceFunctions(logi_device)->vkCreateImageView(logi_device, &image_view_info, nullptr, &swapchainImageViews[i]);
    }
}

void
VulkanWindowRenderer::finalize()
{
    qt_osd_shutdown();
}

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

void
VulkanWindowRenderer::updateOptions()
{
    VkResult res;
    if (cur_video_filter_method == video_filter_method)
        return;
    if (!descPool) {
        VkDescriptorPoolSize descSize { };
        descSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descSize.descriptorCount = 2;

        VkDescriptorPoolCreateInfo poolInfo { };
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &descSize;
        poolInfo.maxSets       = 2;

        if ((res = m_devFuncs->vkCreateDescriptorPool(logi_device, &poolInfo, nullptr, &descPool)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create descriptor pool. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        }

        VkDescriptorSetAllocateInfo allocInfo { };
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = descPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &descLayout;

        if ((res = m_devFuncs->vkAllocateDescriptorSets(logi_device, &allocInfo, &descSet)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create descriptor set. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        }
    }
    VkDescriptorImageInfo imageDescInfo { };
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescInfo.imageView   = src_image_view;
    imageDescInfo.sampler     = video_filter_method == 0 ? nearestSampler : sampler;

    VkWriteDescriptorSet descWrite { };
    descWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite.dstSet          = descSet;
    descWrite.dstBinding      = 1;
    descWrite.dstArrayElement = 0;
    descWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descWrite.descriptorCount = 1;
    descWrite.pImageInfo      = &imageDescInfo;

    m_devFuncs->vkUpdateDescriptorSets(logi_device, 1, &descWrite, 0, nullptr);
    cur_video_filter_method = video_filter_method;
}

void
VulkanWindowRenderer::render()
{
    m_devFuncs->vkWaitForFences(logi_device, 1, &presentFences[current_frame], VK_TRUE, UINT64_MAX);
    m_devFuncs->vkResetFences(logi_device, 1, &presentFences[current_frame]);
    VkClearValue values[2];
    auto         cmdBufs = this->cmdBuffers[current_frame];
    memset(values, 0, sizeof(values));
    values[0].depthStencil = { 1, 0 };
    values[1].depthStencil = { 1, 0 };
    VkRenderingInfoKHR  info { };
    VkSubpassDependency deps { };
    info.sType      = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    info.renderArea = VkRect2D {
        { 0, 0 },
        curExtent
    };
    const VkRenderingAttachmentInfoKHR color_attachment_info {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView   = swapchainImageViews[swapchain_image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { },
    };
    info.layerCount           = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments    = &color_attachment_info;

    VkCommandBufferBeginInfo beginInfo { };
    beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags            = 0;       // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    updateOptions();

    auto res = fn_vkAcquireNextImageKHR(logi_device, dev_swapchain, (uint64_t) -1, presentSemaphores[current_frame], VK_NULL_HANDLE, &swapchain_image_index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
        return;
    }

    m_devFuncs->vkBeginCommandBuffer(cmdBufs, &beginInfo);

    const VkImageMemoryBarrier image_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
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
    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
    if (!imageLayoutTransitioned) {
        VkPipelineStageFlags srcflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, dstflags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        VkImageMemoryBarrier barrier { };
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = src_image;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_PREINITIALIZED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, srcflags, dstflags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        imageLayoutTransitioned = true;
    } else {
        VkPipelineStageFlags srcflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, dstflags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        VkImageMemoryBarrier barrier { };
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = src_image;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, srcflags, dstflags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    vmaFlushAllocation(allocator, img_allocation, 0, VK_WHOLE_SIZE);
    VkViewport viewport { };
    viewport.x        = destination.x();
    viewport.y        = destination.y();
    viewport.width    = (float) destination.width();
    viewport.height   = (float) destination.height();
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    m_devFuncs->vkCmdSetViewport(cmdBufs, 0, 1, &viewport);
    // m_devFuncs->vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    fn_vkCmdBeginRendering(cmdBufs, &info);
    m_devFuncs->vkCmdBindPipeline(cmdBufs, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    m_devFuncs->vkCmdBindDescriptorSets(cmdBufs, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSet, 0, nullptr);
    std::array<QVector2D, 4> texcoords;
    texcoords[0] = (QVector2D((float) source.x() / 2048.f, (float) (source.y()) / 2048.f));
    texcoords[2] = (QVector2D((float) source.x() / 2048.f, (float) (source.y() + source.height()) / 2048.f));
    texcoords[1] = (QVector2D((float) (source.x() + source.width()) / 2048.f, (float) (source.y()) / 2048.f));
    texcoords[3] = (QVector2D((float) (source.x() + source.width()) / 2048.f, (float) (source.y() + source.height()) / 2048.f));
    m_devFuncs->vkCmdPushConstants(cmdBufs, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(QVector2D) * 4, texcoords.data());
    m_devFuncs->vkCmdDraw(cmdBufs, 4, 1, 0, 0);
    fn_vkCmdEndRendering(cmdBufs);

    VkImageMemoryBarrier barrier { };
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image                           = swapchainImages[swapchain_image_index];
    barrier.oldLayout                       = swapchainImageTransitioned[swapchain_image_index] ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    barrier.srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_devFuncs->vkEndCommandBuffer(cmdBufs);
    // The submit info structure specifies a command buffer queue submission batch
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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
    m_devFuncs->vkQueueSubmit(gfx_queue_o, 1, &submitInfo, presentFences[current_frame]);

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
    auto result                    = fn_vkQueuePresentKHR(gfx_queue_o, &presentInfo);
    if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        recreateSwapchain();
    } else {
        // QMessageBox::critical()
    }

    current_frame = (current_frame + 1) % swapchainImageViews.size();
}

void
VulkanWindowRenderer::initialize()
{
    try {
        window_surface = instance.surfaceForWindow(this);
        if (!window_surface) {
            throw vulkan_init_error("Failed to get VkSurfaceKHR from window.");
        }
        uint32_t physicalDevices = 0;

        instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, nullptr);
        if (physicalDevices != 0) {
            std::vector<VkPhysicalDevice> phys_devices;
            phys_devices.resize(physicalDevices);
            if (VK_SUCCESS == instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, phys_devices.data())) {
                phys_device       = phys_devices[0];
                uint32_t extCount = 0;
                instance.functions()->vkEnumerateDeviceExtensionProperties(phys_device, nullptr, &extCount, nullptr);
                std::vector<VkExtensionProperties> device_extensions(extCount);
                instance.functions()->vkEnumerateDeviceExtensionProperties(phys_device, nullptr, &extCount, device_extensions.data());
                bool dynamicRenderingSupported = false;
                for (uint32_t i = 0; i < extCount; i++) {
                    if (strcmp(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, device_extensions[i].extensionName) == 0) {
                        dynamicRenderingSupported = true;
                        break;
                    }
                }
                if (!dynamicRenderingSupported) {
                    throw vulkan_init_error("VK_KHR_dynamic_rendering not supported.");
                }
                uint32_t queue_count;
                instance.functions()->vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_count, nullptr);
                std::vector<VkQueueFamilyProperties> queue_info(queue_count);
                instance.functions()->vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_count, queue_info.data());
                for (int i = 0; i < queue_count; i++) {
                    if (instance.supportsPresent(phys_device, i, this) && (queue_info[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                        present_queue = gfx_queue = i;
                        break;
                    }
                }
                if (present_queue == -1) {
                    for (int i = 0; i < queue_count; i++) {
                        if (instance.supportsPresent(phys_device, i, this)) {
                            present_queue = i;
                            break;
                        }
                    }
                }
                if (gfx_queue == -1) {
                    for (int i = 0; i < queue_count; i++) {
                        if (queue_info[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                            present_queue = i;
                            break;
                        }
                    }
                }

                if (gfx_queue == -1 || present_queue == -1) {
                    throw vulkan_init_error("Failed to find suitable queue families for present and graphics.");
                }

                // TODO: support seperate graphics and present queues.
                if (gfx_queue != present_queue) {
                    throw vulkan_init_error("Graphics queue can't present");
                }

                std::vector<std::string>  extList;
                std::vector<const char *> extListC;

                // We need VK_KHR_swapchain and VK_KHR_dynamic_rendering.
                extList.push_back("VK_KHR_swapchain");
                extList.push_back("VK_KHR_create_renderpass2");
                extList.push_back("VK_KHR_multiview");
                extList.push_back("VK_KHR_maintenance2");
                extList.push_back("VK_KHR_depth_stencil_resolve");
                extList.push_back("VK_KHR_dynamic_rendering");

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

                constexpr VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_feature {
                    .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
                    .dynamicRendering = VK_TRUE,
                };

                VkDeviceCreateInfo dev_create_info      = { };
                dev_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                dev_create_info.queueCreateInfoCount    = addQueueInfo.size();
                dev_create_info.pQueueCreateInfos       = addQueueInfo.data();
                dev_create_info.enabledExtensionCount   = extListC.size();
                dev_create_info.ppEnabledExtensionNames = extListC.data();

                VkPhysicalDeviceFeatures features = { };
                instance.functions()->vkGetPhysicalDeviceFeatures(phys_device, &features);
                dev_create_info.pEnabledFeatures = &features;
                dev_create_info.pNext            = &dynamic_rendering_feature;
                auto res                         = instance.functions()->vkCreateDevice(phys_device, &dev_create_info, nullptr, &logi_device);
                if (res != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create logical device");
                }
                fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) instance.getInstanceProcAddr("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
                VkSurfaceCapabilitiesKHR surfaceCaps;
                if (fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) {
                    fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, instance.surfaceForWindow(this), &surfaceCaps);
                } else {
                    throw vulkan_init_error("Failed to get surface capabilities");
                }
                // instance.deviceFunctions(logi_device)->vkGetDeviceQueue(logi_device, )
                // instance.deviceFunctions(logi_device)->vkGetPhysicalDeviceSurfaceCapabilitiesKHR()
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
                fn_vkCmdBeginRendering          = (PFN_vkCmdBeginRenderingKHR) instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCmdBeginRenderingKHR");
                fn_vkCmdEndRendering            = (PFN_vkCmdEndRenderingKHR) instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCmdEndRenderingKHR");

                vma_info.device         = logi_device;
                vma_info.physicalDevice = phys_device;
                vma_info.instance       = instance.vkInstance();

                recreateSwapchain();

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
                img_info.format        = VK_FORMAT_R8G8B8A8_UNORM;
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
                imageViewInfo.format                          = VK_FORMAT_R8G8B8A8_UNORM;
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

                if (instance.deviceFunctions(logi_device)->vkCreateImageView(logi_device, &imageViewInfo, nullptr, &src_image_view) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create source image view");
                }

                // Begin Pipeline creation.
                {
                    VkPipelineVertexInputStateCreateInfo vertexInputInfo { };
                    vertexInputInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                    vertexInputInfo.vertexBindingDescriptionCount   = 0;
                    vertexInputInfo.pVertexBindingDescriptions      = nullptr;
                    vertexInputInfo.vertexAttributeDescriptionCount = 0;
                    vertexInputInfo.pVertexAttributeDescriptions    = nullptr;

                    VkPipelineInputAssemblyStateCreateInfo inputAssembly { };
                    inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                    inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    inputAssembly.primitiveRestartEnable = VK_FALSE;

                    VkRect2D scissor { };
                    scissor.offset = { 0, 0 };
                    scissor.extent = { 2048, 2048 };

                    VkViewport viewport { };
                    viewport.x        = destination.x();
                    viewport.y        = destination.y();
                    viewport.width    = (float) destination.width();
                    viewport.height   = (float) destination.height();
                    viewport.minDepth = 0.0f;
                    viewport.maxDepth = 1.0f;

                    VkPipelineViewportStateCreateInfo viewportState { };
                    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                    viewportState.viewportCount = 1;
                    viewportState.pViewports    = &viewport;
                    viewportState.scissorCount  = 1;
                    viewportState.pScissors     = &scissor;

                    VkPipelineRasterizationStateCreateInfo rasterizer { };
                    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                    rasterizer.depthClampEnable        = VK_FALSE;
                    rasterizer.rasterizerDiscardEnable = VK_FALSE;
                    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
                    rasterizer.lineWidth               = 1.0f;
                    rasterizer.cullMode                = VK_CULL_MODE_BACK_BIT;
                    rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;
                    rasterizer.depthBiasEnable         = VK_FALSE;
                    rasterizer.depthBiasConstantFactor = 0.0f;
                    rasterizer.depthBiasClamp          = 0.0f;
                    rasterizer.depthBiasSlopeFactor    = 0.0f;

                    VkPipelineMultisampleStateCreateInfo multisampling { };
                    multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                    multisampling.sampleShadingEnable   = VK_FALSE;
                    multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
                    multisampling.minSampleShading      = 1.0f;
                    multisampling.pSampleMask           = nullptr;
                    multisampling.alphaToCoverageEnable = VK_FALSE;
                    multisampling.alphaToOneEnable      = VK_FALSE;

                    VkPipelineColorBlendAttachmentState colorBlendAttachment { };
                    colorBlendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                    colorBlendAttachment.blendEnable         = VK_TRUE;
                    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
                    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;

                    VkPipelineColorBlendStateCreateInfo colorBlending { };
                    colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                    colorBlending.logicOpEnable     = VK_FALSE;
                    colorBlending.logicOp           = VK_LOGIC_OP_COPY;
                    colorBlending.attachmentCount   = 1;
                    colorBlending.pAttachments      = &colorBlendAttachment;
                    colorBlending.blendConstants[0] = 0.0f;
                    colorBlending.blendConstants[1] = 0.0f;
                    colorBlending.blendConstants[2] = 0.0f;
                    colorBlending.blendConstants[3] = 0.0f;

                    VkDynamicState                   dynState = VK_DYNAMIC_STATE_VIEWPORT;
                    VkPipelineDynamicStateCreateInfo dynamicState { };
                    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                    dynamicState.dynamicStateCount = 1;
                    dynamicState.pDynamicStates    = &dynState;

                    VkPushConstantRange range { };
                    range.offset     = 0;
                    range.size       = sizeof(float) * 8;
                    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

                    // Sampler binding start.
                    VkDescriptorSetLayoutBinding samplerLayoutBinding { };
                    samplerLayoutBinding.binding            = 1;
                    samplerLayoutBinding.descriptorCount    = 1;
                    samplerLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    samplerLayoutBinding.pImmutableSamplers = nullptr;
                    samplerLayoutBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

                    VkDescriptorSetLayoutCreateInfo layoutInfo { };
                    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    layoutInfo.bindingCount = 1;
                    layoutInfo.pBindings    = &samplerLayoutBinding;

                    if ((res = m_devFuncs->vkCreateDescriptorSetLayout(logi_device, &layoutInfo, nullptr, &descLayout))) {
                        throw vulkan_init_error("Could not create descriptor set layout. Switch to another renderer. " + Vulkan_GetResultString(res));
                        return;
                    }
                    // Sampler binding end.

                    VkPipelineLayoutCreateInfo pipelineLayoutInfo { };
                    pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                    pipelineLayoutInfo.setLayoutCount         = 1;
                    pipelineLayoutInfo.pSetLayouts            = &descLayout;
                    pipelineLayoutInfo.pushConstantRangeCount = 1;
                    pipelineLayoutInfo.pPushConstantRanges    = &range;

                    if ((res = m_devFuncs->vkCreatePipelineLayout(logi_device, &pipelineLayoutInfo, nullptr, &pipelineLayout)) != VK_SUCCESS) {
                        throw vulkan_init_error("Could not create pipeline layout. Switch to another renderer. " + Vulkan_GetResultString(res));
                        return;
                    }

                    // Shader loading start.
                    VkShaderModuleCreateInfo createInfo { };
                    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                    createInfo.codeSize = sizeof(vertShaderCode);
                    createInfo.pCode    = (uint32_t *) vertShaderCode;
                    VkShaderModule vertModule { nullptr }, fragModule { nullptr };
                    if ((res = m_devFuncs->vkCreateShaderModule(logi_device, &createInfo, nullptr, &vertModule)) != VK_SUCCESS) {
                        throw vulkan_init_error("Could not create vertex shader. Switch to another renderer. " + Vulkan_GetResultString(res));
                        return;
                    }

                    createInfo.codeSize = sizeof(fragShaderCode);
                    createInfo.pCode    = (uint32_t *) fragShaderCode;
                    if ((res = m_devFuncs->vkCreateShaderModule(logi_device, &createInfo, nullptr, &fragModule)) != VK_SUCCESS) {
                        throw vulkan_init_error("Could not create fragment shader. Switch to another renderer. " + Vulkan_GetResultString(res));
                        return;
                    }

                    VkPipelineShaderStageCreateInfo vertShaderStageInfo { };
                    vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                    vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
                    vertShaderStageInfo.module = vertModule;
                    vertShaderStageInfo.pName  = "main";

                    VkPipelineShaderStageCreateInfo fragShaderStageInfo { vertShaderStageInfo };
                    fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
                    fragShaderStageInfo.module = fragModule;

                    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };
                    // Shader loading end.

                    VkPipelineDepthStencilStateCreateInfo depthInfo { };
                    depthInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                    depthInfo.pNext                 = nullptr;
                    depthInfo.depthTestEnable       = VK_TRUE;
                    depthInfo.depthWriteEnable      = VK_TRUE;
                    depthInfo.depthBoundsTestEnable = VK_FALSE;
                    depthInfo.depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
                    depthInfo.stencilTestEnable     = VK_FALSE;
                    depthInfo.maxDepthBounds        = 1.0f;

                    VkGraphicsPipelineCreateInfo pipelineInfo { };
                    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                    pipelineInfo.stageCount          = 2;
                    pipelineInfo.pStages             = shaderStages;
                    pipelineInfo.pVertexInputState   = &vertexInputInfo;
                    pipelineInfo.pInputAssemblyState = &inputAssembly;
                    pipelineInfo.pViewportState      = &viewportState;
                    pipelineInfo.pRasterizationState = &rasterizer;
                    pipelineInfo.pMultisampleState   = &multisampling;
                    pipelineInfo.pDepthStencilState  = &depthInfo;
                    pipelineInfo.pColorBlendState    = &colorBlending;
                    pipelineInfo.pDynamicState       = &dynamicState;
                    pipelineInfo.layout              = pipelineLayout;
                    pipelineInfo.renderPass          = nullptr; // we use dynamic rendering.
                    pipelineInfo.subpass             = 0;

                    VkFormat                      color_format  = VK_FORMAT_R8G8B8A8_UNORM;
                    VkPipelineRenderingCreateInfo renderer_info = { };
                    renderer_info.pNext                         = nullptr;
                    renderer_info.sType                         = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                    renderer_info.viewMask                      = 0;
                    renderer_info.colorAttachmentCount          = 1;
                    renderer_info.pColorAttachmentFormats       = &color_format;

                    pipelineInfo.pNext = (void *) &renderer_info;

                    if ((res = m_devFuncs->vkCreateGraphicsPipelines(logi_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)) != VK_SUCCESS) {
                        m_devFuncs->vkDestroyShaderModule(logi_device, vertModule, nullptr);
                        m_devFuncs->vkDestroyShaderModule(logi_device, fragModule, nullptr);
                        throw vulkan_init_error("Could not create graphics pipeline. Switch to another renderer. " + Vulkan_GetResultString(res));
                        return;
                    }
                    m_devFuncs->vkDestroyShaderModule(logi_device, vertModule, nullptr);
                    m_devFuncs->vkDestroyShaderModule(logi_device, fragModule, nullptr);
                }

                VkSamplerCreateInfo samplerInfo { };
                samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter               = VK_FILTER_LINEAR;
                samplerInfo.minFilter               = VK_FILTER_LINEAR;
                samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.anisotropyEnable        = VK_FALSE;
                samplerInfo.unnormalizedCoordinates = VK_FALSE;
                samplerInfo.compareEnable           = VK_FALSE;
                samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
                samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerInfo.mipLodBias              = 0.0f;
                samplerInfo.minLod                  = 0.0f;
                samplerInfo.maxLod                  = 0.0;

                if ((res = m_devFuncs->vkCreateSampler(logi_device, &samplerInfo, nullptr, &sampler)) != VK_SUCCESS) {
                    throw vulkan_init_error("Could not create linear image sampler. Switch to another renderer. " + Vulkan_GetResultString(res));
                }

                samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
                samplerInfo.mipmapMode                        = VK_SAMPLER_MIPMAP_MODE_NEAREST;

                if ((res = m_devFuncs->vkCreateSampler(logi_device, &samplerInfo, nullptr, &nearestSampler)) != VK_SUCCESS) {
                    throw vulkan_init_error("Could not create nearest image sampler. Switch to another renderer. " + Vulkan_GetResultString(res));
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
                printf("Vulkan initialized\n");
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

        main_window->showMessage(MBX_ERROR, QString(), tr("Error initializing Vulkan.") + QStringLiteral("\n") + e.what() + QStringLiteral("\n") + tr("Falling back to software rendering."), false);

        isFinalized   = true;
        isInitialized = true;

        emit errorInitializing();
    }
}

void
VulkanWindowRenderer::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);

    if (!isInitialized && isExposed())
        initialize();

    this->pixelRatio = devicePixelRatio();
    onResize(size().width(), size().height());
}

void
VulkanWindowRenderer::resizeEvent(QResizeEvent *event)
{
    this->pixelRatio = devicePixelRatio();
    onResize(width(), height());

    QWindow::resizeEvent(event);
    if (isInitialized)
        recreateSwapchain();
}

bool
VulkanWindowRenderer::event(QEvent *event)
{
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
    buf_usage[0].clear();
    if (origSource != source) {
        this->pixelRatio = devicePixelRatio();
        onResize(this->width(), this->height());
    }
    if (isExposed()) {
        // requestUpdate();
        render();
    }
#    if 0
    if (monitors[r_monitor_index].mon_screenshots) {
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

        QImage image = this->grab();
#        if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && !defined(Q_OS_WINDOWS)
        image.save(path, "png");
#        else
        image.rgbSwapped().save(path, "png");
#        endif
        monitors[r_monitor_index].mon_screenshots--;
    }
    if (monitors[r_monitor_index].mon_screenshots_clipboard) {
        QImage image = this->grab();
#        if QT_VERSION < QT_VERSION_CHECK(6, 0, 0) || defined(Q_OS_WINDOWS)
        image = image.rgbSwapped();
#        endif
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setImage(image, QClipboard::Clipboard);
        monitors[r_monitor_index].mon_screenshots_clipboard--;
    }
#    endif
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
