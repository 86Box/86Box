#include "qt_vulkanwindowrenderer.hpp"

#include <QMessageBox>
#include <QWindow>

#if QT_CONFIG(vulkan)
#    include <QVulkanWindowRenderer>
#    include <QVulkanDeviceFunctions>
#    include <array>
#    include <stdexcept>

#    include "qt_mainwindow.hpp"
#    include "qt_vulkanrenderer.hpp"

extern "C" {
#    include <86box/86box.h>
#    include <86box/video.h>
}
#    if 0
extern MainWindow* main_window;
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

static const uint8_t fragShaderCode[]
{
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

class VulkanRendererEmu : public QVulkanWindowRenderer
{
    VulkanWindowRenderer* m_window{nullptr};
    QVulkanDeviceFunctions* m_devFuncs{nullptr};
    VmaAllocator allocator{nullptr};
    VmaAllocation allocation{nullptr};
    VkImage image{nullptr};
    VkImageView imageView{nullptr};
    VkPipeline pipeline{nullptr};
    VkPipelineLayout pipelineLayout{nullptr};
    VkDescriptorSetLayout descLayout{nullptr};
    VkDescriptorPool descPool{nullptr};
    VkDescriptorSet descSet{nullptr};
    VkSampler sampler{nullptr}, nearestSampler{nullptr};
    bool imageLayoutTransitioned{false};
    int cur_video_filter_method = -1;
private:
    void updateOptions() {
        VkResult res;
        if (cur_video_filter_method == video_filter_method) return;
        if (!descPool) {
        VkDescriptorPoolSize descSize{};
        descSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descSize.descriptorCount = 2;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &descSize;
        poolInfo.maxSets = 2;

        if ((res = m_devFuncs->vkCreateDescriptorPool(m_window->device(), &poolInfo, nullptr, &descPool)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create descriptor pool. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descLayout;

        if ((res = m_devFuncs->vkAllocateDescriptorSets(m_window->device(), &allocInfo, &descSet)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create descriptor set. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        }}
        VkDescriptorImageInfo imageDescInfo{};
        imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescInfo.imageView = imageView;
        imageDescInfo.sampler = video_filter_method == 0 ? nearestSampler : sampler;

        VkWriteDescriptorSet descWrite{};
        descWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descWrite.dstSet = descSet;
        descWrite.dstBinding = 1;
        descWrite.dstArrayElement = 0;
        descWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descWrite.descriptorCount = 1;
        descWrite.pImageInfo = &imageDescInfo;

        m_devFuncs->vkUpdateDescriptorSets(m_window->device(), 1, &descWrite, 0, nullptr);
        cur_video_filter_method = video_filter_method;
    }
public:
    void* mappedPtr = nullptr;
    uint32_t imagePitch{2048 * 4};
    VulkanRendererEmu(VulkanWindowRenderer *w) : m_window(w), m_devFuncs(nullptr) { }

    void initResources() override
    {
        VmaAllocatorCreateInfo info{};
        info.instance = m_window->vulkanInstance()->vkInstance();
        info.device = m_window->device();
        info.physicalDevice = m_window->physicalDevice();
        VmaVulkanFunctions funcs{};
        funcs.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)m_window->vulkanInstance()->getInstanceProcAddr("vkGetInstanceProcAddr");
        funcs.vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)m_window->vulkanInstance()->getInstanceProcAddr("vkGetDeviceProcAddr");
        info.pVulkanFunctions = &funcs;
        if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create Vulkan allocator. Switch to another renderer");
            return;
        }

        m_devFuncs = m_window->vulkanInstance()->deviceFunctions(m_window->device());

        VkResult res;
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
        imageInfo.extent.width = imageInfo.extent.height = 2048;
        imageInfo.extent.depth = imageInfo.arrayLayers = imageInfo.mipLevels = 1;
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

        VmaAllocationInfo allocatedInfo{};
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.pUserData = allocInfo.pool = nullptr;
        allocInfo.requiredFlags = allocInfo.preferredFlags = 0;
        allocInfo.usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if ((res = vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, &allocatedInfo)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create Vulkan image. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        };

        VkImageViewCreateInfo imageViewInfo{};
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
        imageViewInfo.image = image;
        imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewInfo.subresourceRange.baseMipLevel = 0;
        imageViewInfo.subresourceRange.levelCount = 1;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount = 1;
        imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;

        if ((res = m_devFuncs->vkCreateImageView(m_window->device(), &imageViewInfo, nullptr, &imageView)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create Vulkan image view. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        }

        // Begin Pipeline creation.
        {
            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.pVertexBindingDescriptions = nullptr;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;
            vertexInputInfo.pVertexAttributeDescriptions = nullptr;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {2048, 2048};

            VkViewport viewport{};
            viewport.x = m_window->destination.x();
            viewport.y = m_window->destination.y();
            viewport.width = (float)m_window->destination.width();
            viewport.height = (float)m_window->destination.height();
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;
            rasterizer.depthBiasConstantFactor = 0.0f;
            rasterizer.depthBiasClamp = 0.0f;
            rasterizer.depthBiasSlopeFactor = 0.0f;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisampling.minSampleShading = 1.0f;
            multisampling.pSampleMask = nullptr;
            multisampling.alphaToCoverageEnable = VK_FALSE;
            multisampling.alphaToOneEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = VK_LOGIC_OP_COPY;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;
            colorBlending.blendConstants[0] = 0.0f;
            colorBlending.blendConstants[1] = 0.0f;
            colorBlending.blendConstants[2] = 0.0f;
            colorBlending.blendConstants[3] = 0.0f;

            VkDynamicState dynState = VK_DYNAMIC_STATE_VIEWPORT;
            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 1;
            dynamicState.pDynamicStates = &dynState;

            VkPushConstantRange range{};
            range.offset = 0;
            range.size = sizeof(float) * 8;
            range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            // Sampler binding start.
            VkDescriptorSetLayoutBinding samplerLayoutBinding{};
            samplerLayoutBinding.binding = 1;
            samplerLayoutBinding.descriptorCount = 1;
            samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerLayoutBinding.pImmutableSamplers = nullptr;
            samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &samplerLayoutBinding;

            if ((res = m_devFuncs->vkCreateDescriptorSetLayout(m_window->device(), &layoutInfo, nullptr, &descLayout))) {
                QMessageBox::critical(main_window, "86Box", "Could not create descriptor set layout. Switch to another renderer. " + Vulkan_GetResultString(res));
                return;
            }
            // Sampler binding end.

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &descLayout;
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = &range;

            if ((res = m_devFuncs->vkCreatePipelineLayout(m_window->device(), &pipelineLayoutInfo, nullptr, &pipelineLayout)) != VK_SUCCESS) {
                QMessageBox::critical(main_window, "86Box", "Could not create pipeline layout. Switch to another renderer. " + Vulkan_GetResultString(res));
                return;
            }

            // Shader loading start.
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = sizeof(vertShaderCode);
            createInfo.pCode = (uint32_t*)vertShaderCode;
            VkShaderModule vertModule{nullptr}, fragModule{nullptr};
            if ((res = m_devFuncs->vkCreateShaderModule(m_window->device(), &createInfo, nullptr, &vertModule)) != VK_SUCCESS) {
                QMessageBox::critical(main_window, "86Box", "Could not create vertex shader. Switch to another renderer. " + Vulkan_GetResultString(res));
                return;
            }

            createInfo.codeSize = sizeof(fragShaderCode);
            createInfo.pCode = (uint32_t*)fragShaderCode;
            if ((res = m_devFuncs->vkCreateShaderModule(m_window->device(), &createInfo, nullptr, &fragModule)) != VK_SUCCESS) {
                QMessageBox::critical(main_window, "86Box", "Could not create fragment shader. Switch to another renderer. " + Vulkan_GetResultString(res));
                return;
            }

            VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
            vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = vertModule;
            vertShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo fragShaderStageInfo{vertShaderStageInfo};
            fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragModule;

            VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
            // Shader loading end.

            VkPipelineDepthStencilStateCreateInfo depthInfo{};
            depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthInfo.pNext = nullptr;
            depthInfo.depthTestEnable = VK_TRUE;
            depthInfo.depthWriteEnable = VK_TRUE;
            depthInfo.depthBoundsTestEnable = VK_FALSE;
            depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            depthInfo.stencilTestEnable = VK_FALSE;
            depthInfo.maxDepthBounds = 1.0f;

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthInfo;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = m_window->defaultRenderPass();
            pipelineInfo.subpass = 0;

            if ((res = m_devFuncs->vkCreateGraphicsPipelines(m_window->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)) != VK_SUCCESS) {
                m_devFuncs->vkDestroyShaderModule(m_window->device(), vertModule, nullptr);
                m_devFuncs->vkDestroyShaderModule(m_window->device(), fragModule, nullptr);
                QMessageBox::critical(main_window, "86Box", "Could not create graphics pipeline. Switch to another renderer. " + Vulkan_GetResultString(res));
                return;
            }
            m_devFuncs->vkDestroyShaderModule(m_window->device(), vertModule, nullptr);
            m_devFuncs->vkDestroyShaderModule(m_window->device(), fragModule, nullptr);
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0;

        if ((res = m_devFuncs->vkCreateSampler(m_window->device(), &samplerInfo, nullptr, &sampler)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create linear image sampler. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        }

        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        if ((res = m_devFuncs->vkCreateSampler(m_window->device(), &samplerInfo, nullptr, &nearestSampler)) != VK_SUCCESS) {
            QMessageBox::critical(main_window, "86Box", "Could not create nearest image sampler. Switch to another renderer. " + Vulkan_GetResultString(res));
            return;
        }

        updateOptions();

        VkImageSubresource resource{};
        resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        resource.arrayLayer = 0;
        resource.mipLevel = 0;
        VkSubresourceLayout layout{};
        m_devFuncs->vkGetImageSubresourceLayout(m_window->device(), image, &resource, &layout);

        imagePitch = layout.rowPitch;
        mappedPtr = (uint8_t*)allocatedInfo.pMappedData + layout.offset;
        emit m_window->rendererInitialized();
    }

    void releaseResources() override
    {
        if (pipeline) m_devFuncs->vkDestroyPipeline(m_window->device(), pipeline, nullptr);
        if (pipelineLayout) m_devFuncs->vkDestroyPipelineLayout(m_window->device(), pipelineLayout, nullptr);
        if (descSet) m_devFuncs->vkDestroyDescriptorPool(m_window->device(), descPool, nullptr);
        if (descLayout) m_devFuncs->vkDestroyDescriptorSetLayout(m_window->device(), descLayout, nullptr);
        if (imageView) m_devFuncs->vkDestroyImageView(m_window->device(), imageView, nullptr);
        if (image) vmaDestroyImage(allocator, image, allocation);
        if (sampler) m_devFuncs->vkDestroySampler(m_window->device(), sampler, nullptr);
        if (nearestSampler) m_devFuncs->vkDestroySampler(m_window->device(), nearestSampler, nullptr);
        image = nullptr;
        pipeline = nullptr;
        pipelineLayout = nullptr;
        imageView = nullptr;
        descLayout = nullptr;
        descPool = nullptr;
        descSet = nullptr;
        sampler = nullptr;
        vmaDestroyAllocator(allocator);
        allocator = nullptr;
    }

    QString Vulkan_GetResultString(VkResult result)
    {
        switch ((int)result) {
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
#        if VK_HEADER_VERSION >= 135 && VK_HEADER_VERSION < 162
            case VK_ERROR_INCOMPATIBLE_VERSION_KHR:
                return "VK_ERROR_INCOMPATIBLE_VERSION_KHR";
#        endif
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

    void startNextFrame() override
    {
        m_devFuncs->vkDeviceWaitIdle(m_window->device());
        VkClearValue values[2];
        auto cmdBufs = m_window->currentCommandBuffer();
        memset(values, 0, sizeof(values));
        values[0].depthStencil = { 1, 0 };
        values[1].depthStencil = { 1, 0 };
        VkRenderPassBeginInfo info{};
        VkSubpassDependency deps{};
        info.pClearValues = values;
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.framebuffer = m_window->currentFramebuffer();
        info.renderArea = VkRect2D{{0, 0}, {(uint32_t)m_window->swapChainImageSize().width(), (uint32_t)m_window->swapChainImageSize().height()}};
        info.clearValueCount = 2;
        info.renderPass = m_window->defaultRenderPass();

        updateOptions();
        if (!imageLayoutTransitioned) {
            VkPipelineStageFlags srcflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, dstflags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = image;
            barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            m_devFuncs->vkCmdPipelineBarrier(cmdBufs, srcflags, dstflags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            imageLayoutTransitioned = true;
        }
        vmaFlushAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
        VkViewport viewport{};
        viewport.x = m_window->destination.x();
        viewport.y = m_window->destination.y();
        viewport.width = (float)m_window->destination.width();
        viewport.height = (float)m_window->destination.height();
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        m_devFuncs->vkCmdSetViewport(cmdBufs, 0, 1, &viewport);
        m_devFuncs->vkCmdBeginRenderPass(m_window->currentCommandBuffer(), &info, VK_SUBPASS_CONTENTS_INLINE);
        m_devFuncs->vkCmdBindPipeline(cmdBufs, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        m_devFuncs->vkCmdBindDescriptorSets(cmdBufs, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSet, 0, nullptr);
        std::array<QVector2D, 4> texcoords;
        auto source = m_window->source;
        texcoords[0] = (QVector2D((float)source.x() / 2048.f, (float)(source.y()) / 2048.f));
        texcoords[2] = (QVector2D((float)source.x() / 2048.f, (float)(source.y() + source.height()) / 2048.f));
        texcoords[1] = (QVector2D((float)(source.x() + source.width()) / 2048.f, (float)(source.y()) / 2048.f));
        texcoords[3] = (QVector2D((float)(source.x() + source.width()) / 2048.f, (float)(source.y() + source.height()) / 2048.f));
        m_devFuncs->vkCmdPushConstants(cmdBufs, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(QVector2D) * 4, texcoords.data());
        m_devFuncs->vkCmdDraw(cmdBufs, 4, 1, 0, 0);
        m_devFuncs->vkCmdEndRenderPass(cmdBufs);

        m_window->frameReady();
        m_devFuncs->vkDeviceWaitIdle(m_window->device());
    }
};
#    endif

VulkanWindowRenderer::VulkanWindowRenderer(QWidget *parent)
    : QVulkanWindow(parent->windowHandle())
{
    parentWidget = parent;
    instance.setApiVersion(QVersionNumber(1, 0));
    instance.create();
    setVulkanInstance(&instance);
    setPhysicalDeviceIndex(0);
    setPreferredColorFormats({ VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_A8B8G8R8_UNORM_PACK32 });
    setFlags(Flag::PersistentResources);
    buf_usage = std::vector<std::atomic_flag>(1);
    buf_usage[0].clear();
}

QVulkanWindowRenderer *
VulkanWindowRenderer::createRenderer()
{
    renderer = new VulkanRenderer2(this);
    return renderer;
}

void
VulkanWindowRenderer::resizeEvent(QResizeEvent *event)
{
    onResize(width(), height());

    QVulkanWindow::resizeEvent(event);
}

bool
VulkanWindowRenderer::event(QEvent *event)
{
    bool res = false;
    if (!eventDelegate(event, res))
        return QVulkanWindow::event(event);
    return res;
}

void
VulkanWindowRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    auto origSource = source;
    source.setRect(x, y, w, h);
    if (isExposed())
        requestUpdate();
    buf_usage[0].clear();
    if (origSource != source)
        onResize(this->width(), this->height());
}

uint32_t
VulkanWindowRenderer::getBytesPerRow()
{
    return renderer->imagePitch;
}

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
VulkanWindowRenderer::getBuffers()
{
    return std::vector { std::make_tuple((uint8_t *) renderer->mappedPtr, &this->buf_usage[0]) };
}
#endif
