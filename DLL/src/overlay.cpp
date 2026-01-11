#include "pch.h"
#include <d3d11.h>
#include <algorithm>

#include "overlay_api.h"
#include "game_types.h"
#include "config.h"
#include "logging.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// imgui_impl_win32.h intentionally hides this declaration behind #if 0.
// Forward-declare it here so the call in Overlay_WndProcHandler links cleanly.
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    bool g_inited = false;
    bool g_frameBegun = false;
    HWND g_hwnd = nullptr;
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
}

static ImDrawData* FinalizeFrame()
{
    ImGui::Render();
    g_frameBegun = false;
    return ImGui::GetDrawData();
}

static bool CanRenderDrawData(ImDrawData* draw)
{
    if (!draw)
        return false;
    if (draw->DisplaySize.x <= 0.0f || draw->DisplaySize.y <= 0.0f)
        return false;
    if (draw->CmdListsCount <= 0)
        return false;
    if (draw->CmdListsCount != draw->CmdLists.Size)
    {
        static int warnCount = 0;
        if (warnCount++ < 5)
            AppendLogFmt("[Overlay] DrawData mismatch CmdListsCount=%d Size=%d (skipping frame)\n",
                draw->CmdListsCount, draw->CmdLists.Size);
        return false;
    }
    for (int i = 0; i < draw->CmdListsCount; ++i)
    {
        if (draw->CmdLists[i] == nullptr)
        {
            static int warnNull = 0;
            if (warnNull++ < 5)
                AppendLogFmt("[Overlay] DrawData null cmd list at %d (skipping frame)\n", i);
            return false;
        }
    }
    return true;
}

OVERLAY_API bool Overlay_Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (g_inited) return true;
    if (!hwnd || !device || !context) return false;

    AppendLogFmt("[Overlay_Init] hwnd=%p device=%p ctx=%p\n", hwnd, device, context);

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
    g_frameBegun = true;
}

OVERLAY_API void Overlay_Render()
{
    if (!g_inited || !g_frameBegun) return;

    static bool styled = false;
    if (!styled) {
        styled = true;
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        ImVec4 accent = ImVec4(0.95f, 0.38f, 0.22f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.07f, 0.92f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.65f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.85f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(accent.x, accent.y, accent.z, 0.75f);
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("DLL", nullptr, flags))
    {
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.22f, 1.0f), "ClickGUI");
        ImGui::Separator();
        Config::Settings& cfg = Config::Mutable();

        ImGui::Checkbox("God Mode", &cfg.godModeEnabled);
        ImGui::SameLine();
        ImGui::TextDisabled("(toggle)");

        ImGui::SeparatorText("Tuning");
        ImGui::DragFloat("God Health", &cfg.godModeHealth, 50.0f, 0.0f, 100000.0f, "%.0f");
        ImGui::DragInt("Health Boost", &cfg.godModeHealthBoost, 10, 0, 20000);
        ImGui::DragInt("Health Upgrade", &cfg.godModeHealthUpgrade, 1, 0, 200);
        cfg.godModeHealth = std::max(0.0f, cfg.godModeHealth);
        cfg.godModeHealthBoost = std::max(0, cfg.godModeHealthBoost);
        cfg.godModeHealthUpgrade = std::max(0, cfg.godModeHealthUpgrade);

        ImGui::SeparatorText("Local");
        if (g_LocalPlayer)
        {
            ImGui::Text("Health: %.1f", g_LocalPlayerHealth);
            ImGui::Text("Health Boost: %d", g_LocalPlayerHealthBoost);
            ImGui::Text("Health Upgrade: %d", g_LocalPlayerHealthUpgrade);
            ImGui::Text("XYZ: %.2f, %.2f, %.2f", g_LocalPlayerPos.x, g_LocalPlayerPos.y, g_LocalPlayerPos.z);
        }
        else
        {
            ImGui::Text("LocalPlayer not found");
        }
    }
    ImGui::End();

    ImDrawData* draw = FinalizeFrame();
    if (CanRenderDrawData(draw)) {
        ImGui_ImplDX11_RenderDrawData(draw);
    }
}

OVERLAY_API void Overlay_EndFrame(bool renderDrawData)
{
    if (!g_inited || !g_frameBegun) return;
    ImDrawData* draw = FinalizeFrame();
    if (renderDrawData) {
        if (CanRenderDrawData(draw)) {
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
    g_frameBegun = false;
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
