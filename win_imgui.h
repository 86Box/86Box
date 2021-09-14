#ifndef WIN_IMGUI_H
#define WIN_IMGUI_H

#include <stdbool.h>

extern void HandleSizeChange();
extern void RenderImGui();
extern void DeinitializeImGuiSDLRenderer();
extern bool ImGuiWantsKeyboardCapture();
extern bool ImGuiWantsMouseCapture();
extern void InitImGui();

#endif