#include "pch.h"
#include <d3d11.h>
#include <fstream>

#include "overlay_api.h"
#include "game_types.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// imgui_impl_win32.h intentionally hides this declaration behind #if 0.
// Forward-declare it here so the call in Overlay_WndProcHandler links cleanly.
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    bool g_inited = false;
    HWND g_hwnd = nullptr;
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
}

OVERLAY_API bool Overlay_Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (g_inited) return true;
    if (!hwnd || !device || !context) return false;

    std::ofstream("D:\\Project\\overlay_log.txt", std::ios::app) << "[Overlay_Init] hwnd=" << hwnd << " device=" << device << " ctx=" << context << "\n";

    g_hwnd = hwnd;
    g_device = device;
    g_context = context;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hwnd))
        return false;
    if (!ImGui_ImplDX11_Init(device, context))
        return false;

    g_inited = true;
    return true;
}

OVERLAY_API void Overlay_NewFrame()
{
    if (!g_inited) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

OVERLAY_API void Overlay_Render()
{
    if (!g_inited) return;

    ImGui::Begin("Overlay DLL");
    ImGui::Text("Hello from DLL!");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

    ImGui::SeparatorText("Local Player");
    if (g_LocalPlayer)
    {
        ImGui::Text("World XYZ:");
        ImGui::Text("X: %.2f", g_LocalPlayerPos.x);
        ImGui::Text("Y: %.2f", g_LocalPlayerPos.y);
        ImGui::Text("Z: %.2f", g_LocalPlayerPos.z);
    }
    else
    {
        ImGui::Text("LocalPlayer not found");
    }
    ImGui::End();

    ImGui::Render();
    ImDrawData* draw = ImGui::GetDrawData();
    if (draw && draw->DisplaySize.x > 0.0f && draw->DisplaySize.y > 0.0f && draw->CmdListsCount > 0) {
        ImGui_ImplDX11_RenderDrawData(draw);
    }
}

OVERLAY_API void Overlay_EndFrame(bool renderDrawData)
{
    if (!g_inited) return;
    ImGui::Render();
    if (renderDrawData) {
        ImDrawData* draw = ImGui::GetDrawData();
        if (draw && draw->DisplaySize.x > 0.0f && draw->DisplaySize.y > 0.0f && draw->CmdListsCount > 0) {
            ImGui_ImplDX11_RenderDrawData(draw);
        }
    }
}

OVERLAY_API void Overlay_Shutdown()
{
    if (!g_inited) return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    g_inited = false;
    g_hwnd = nullptr;
    g_device = nullptr;
    g_context = nullptr;
}

OVERLAY_API LRESULT Overlay_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_inited) return 0;
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;
    return 0;
}
