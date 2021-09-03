#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_sdl.h"
#include <SDL.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/hdc.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/ui.h>

#include <string>
#include <vector>

extern "C" SDL_Window* sdl_win;
extern "C" SDL_Renderer	*sdl_render;
extern "C" float menubarheight;
static bool imrendererinit = false;
static bool firstrender = true;

struct CartMenu
{
    int cartid;
    void RenderImGuiMenu()
    {
        std::string str = "Cartridge ";
        str += std::to_string(cartid);
        str += " ";
        str += strlen(cart_fns[cartid]) == 0 ? "(empty)" : cart_fns[cartid];
        if (ImGui::BeginMenu(str.c_str()))
        {
            if (ImGui::MenuItem("Image..."))
            {
                FILE* output;
                char* res;
                int origpause = dopause;
                plat_pause(1);
                output = popen("zenity --file-selection", "r");
                if (output)
                {
                    if (getline(&res, NULL, output) != -1)
                    {
                        if (pclose(output) == 0)
                        {
                            cartridge_mount(cartid, res, 0);
                            free(res);
                        }
                    }
                }
                plat_pause(origpause);
            }
            if (ImGui::MenuItem("Image... (write-protected)"))
            {
                FILE* output;
                char* res;
                int origpause = dopause;
                plat_pause(1);
                output = popen("zenity --file-selection", "r");
                if (output)
                {
                    if (getline(&res, NULL, output) != -1)
                    {
                        if (pclose(output) == 0)
                        {
                            cartridge_mount(cartid, res, 1);
                            free(res);
                        }
                    }
                }
                plat_pause(origpause);
            }
            if (ImGui::MenuItem("Eject"))
            {
                cartridge_eject(cartid);
            }
        }
    }
};
std::vector<CartMenu> cmenu;

extern "C" void InitImGui()
{
    ImGui::CreateContext(NULL);
    ImGui_ImplSDL2_InitForOpenGL(sdl_win, NULL);
}

extern "C" void HandleSizeChange()
{
    int w, h;
    if (!ImGui::GetCurrentContext()) ImGui::CreateContext(NULL);
    SDL_GetRendererOutputSize(sdl_render, &w, &h);
    ImGuiSDL::Initialize(sdl_render, w, h);
    imrendererinit = true;
}
extern "C" void plat_resize(int w, int h);
extern "C" bool ImGuiWantsMouseCapture()
{
    return ImGui::GetIO().WantCaptureMouse;
}
extern "C" bool ImGuiWantsKeyboardCapture()
{
    return ImGui::GetIO().WantCaptureKeyboard;
}
extern "C" void RenderImGui()
{
    if (!imrendererinit) HandleSizeChange();
    ImGui_ImplSDL2_NewFrame(sdl_win);
    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar())
    {
        menubarheight = ImGui::GetFrameHeight();
        if (firstrender)
        {
            firstrender = false;
            plat_resize(640, 480 + menubarheight);
        }
        if (ImGui::BeginMenu("Action"))
        {
            if (ImGui::MenuItem("Keyboard requires capture", NULL, (bool)kbd_req_capture))
                kbd_req_capture ^= 1;
            if (ImGui::MenuItem("Right CTRL is left ALT", NULL, (bool)rctrl_is_lalt))
                rctrl_is_lalt ^= 1;
            ImGui::Separator();
            if (ImGui::MenuItem("Hard Reset", NULL))
            {
                pc_reset_hard();
            }
            if (ImGui::MenuItem("Ctrl+Alt+Del", "Ctrl+F12"))
            {
                pc_send_cad();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Ctrl+Alt+Esc", NULL))
            {
                pc_send_cae();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Pause", NULL, (bool)dopause))
            {
                plat_pause(dopause ^ 1);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
            {
                SDL_Event event;
                event.type = SDL_QUIT;
                SDL_PushEvent(&event);
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
    if (cmenu.size() != 0 && ImGui::MenuItem("Media"))
    {
        for (auto &cartMenu : cmenu)
        {
            cartMenu.RenderImGuiMenu();
        }
    }
    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());
}