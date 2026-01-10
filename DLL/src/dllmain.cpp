#include "pch.h"
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "overlay_api.h"
#include "game_types.h"
#include "MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using D3D11CreateDeviceAndSwapChainFn = HRESULT(WINAPI*)(
    IDXGIAdapter*,
    D3D_DRIVER_TYPE,
    HMODULE,
    UINT,
    const D3D_FEATURE_LEVEL*,
    UINT,
    UINT,
    const DXGI_SWAP_CHAIN_DESC*,
    IDXGISwapChain**,
    ID3D11Device**,
    D3D_FEATURE_LEVEL*,
    ID3D11DeviceContext**);

static const char* MHStatusText(MH_STATUS st) {
    switch (st) {
    case MH_OK: return "MH_OK";
    case MH_ERROR_ALREADY_INITIALIZED: return "MH_ERROR_ALREADY_INITIALIZED";
    case MH_ERROR_NOT_INITIALIZED: return "MH_ERROR_NOT_INITIALIZED";
    case MH_ERROR_ALREADY_CREATED: return "MH_ERROR_ALREADY_CREATED";
    case MH_ERROR_NOT_CREATED: return "MH_ERROR_NOT_CREATED";
    case MH_ERROR_ENABLED: return "MH_ERROR_ENABLED";
    case MH_ERROR_DISABLED: return "MH_ERROR_DISABLED";
    case MH_ERROR_NOT_EXECUTABLE: return "MH_ERROR_NOT_EXECUTABLE";
    case MH_ERROR_UNSUPPORTED_FUNCTION: return "MH_ERROR_UNSUPPORTED_FUNCTION";
    case MH_ERROR_MEMORY_ALLOC: return "MH_ERROR_MEMORY_ALLOC";
    case MH_ERROR_MEMORY_PROTECT: return "MH_ERROR_MEMORY_PROTECT";
    case MH_ERROR_MODULE_NOT_FOUND: return "MH_ERROR_MODULE_NOT_FOUND";
    case MH_ERROR_FUNCTION_NOT_FOUND: return "MH_ERROR_FUNCTION_NOT_FOUND";
    default: return "MH_UNKNOWN";
    }
}

#define RVA_PTTRPLAYER_UPDATE  0x71F8B0   // PTTRPlayer::Update
#define RVA_GET_FEET_POSITION  0x731E70   // PTTRPlayer::GetFeetPosition
#define RVA_PTTRPLAYER_AWAKE   0x71B870   // PTTRPlayer::Awake
#define RVA_PTTRPLAYER_ONENABLE 0x71E610  // PTTRPlayer::OnEnable
#define RVA_PTTRPLAYER_ONDESTROY 0x71F440 // PTTRPlayer::OnDestroy
#define RVA_PTTRPLAYER_DIE     0x72F860   // PTTRPlayer::Die
using GetFeetPosition_t = void(__fastcall*)(Vector3* ret, PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Update_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Awake_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_OnEnable_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_OnDestroy_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Die_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using NetworkManager_get_Instance_t = NetworkManager_o* (__cdecl*)();
using NetworkManager_get_PlayerID_t = uint64_t(__cdecl*)();
using CharacterManager_GetEnemiesAfterPlayer_t = Il2CppList<Enemy_o>* (__cdecl*)(uint64_t playerID);
using Component_get_transform_t = void* (__fastcall*)(void* __this, void* method);
using Transform_get_position_Injected_t = void(__fastcall*)(Vector3* ret, void* __this, void* method);
using il2cpp_resolve_icall_t = void* (__cdecl*)(const char* name);

static GetFeetPosition_t       GetFeetPosition = nullptr;
static PTTRPlayer_Update_t     o_Update = nullptr;
static PTTRPlayer_Awake_t      o_Awake = nullptr;
static PTTRPlayer_OnEnable_t   o_OnEnable = nullptr;
static PTTRPlayer_OnDestroy_t  o_OnDestroy = nullptr;
static PTTRPlayer_Die_t        o_Die = nullptr;
static NetworkManager_get_Instance_t NetworkManager_get_Instance = nullptr;
static NetworkManager_get_PlayerID_t NetworkManager_get_PlayerID = nullptr;
static CharacterManager_GetEnemiesAfterPlayer_t CharacterManager_GetEnemiesAfterPlayer = nullptr;
static Component_get_transform_t Component_get_transform = nullptr;
static Transform_get_position_Injected_t Transform_get_position_Injected = nullptr;
static il2cpp_resolve_icall_t    il2cpp_resolve_icall_fn = nullptr;
PTTRPlayer_o*                  g_LocalPlayer = nullptr;
static PTTRPlayer_o*           g_LocalPlayerCandidate = nullptr;
Vector3                        g_LocalPlayerPos{};
static HMODULE                 g_gameAsm = nullptr;
static std::atomic_int         g_feetExceptionCount{ 0 };
static std::atomic_bool        g_dxHooksReady{ false };
RemotePlayer                   g_RemotePlayers[16]{};
int                            g_RemotePlayerCount = 0;
EnemyInfo                      g_Enemies[300]{};
int                            g_EnemyCount = 0;

static PresentFn  oPresent = nullptr;
static ResizeFn   oResize = nullptr;
static D3D11CreateDeviceAndSwapChainFn oD3D11CreateDeviceAndSwapChain = nullptr;

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND                    g_hwnd = nullptr;
static WNDPROC                 g_origWndProc = nullptr;
static std::atomic_bool        g_imguiReady{ false };
static std::atomic_bool        g_gameHookReady{ false };
static std::atomic_bool        g_overlayVisible{ true };

// Lightweight file append to avoid C++ iostreams inside SEH regions.
static void AppendLog(const char* msg)
{
    HANDLE h = CreateFileA("D:\\Project\\overlay_log.txt", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    const size_t len = strlen(msg);
    if (len > 0)
        WriteFile(h, msg, static_cast<DWORD>(len), &written, nullptr);
    CloseHandle(h);
}

static void AppendLogFmt(const char* fmt, ...)
{
    char buf[512]{};
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    AppendLog(buf);
}

// Reset tracked data (position, counters) without touching g_LocalPlayer pointer.
static inline void ResetLocalPlayerState()
{
    std::memset(&g_LocalPlayerPos, 0, sizeof(g_LocalPlayerPos));
    g_feetExceptionCount.store(0, std::memory_order_relaxed);
}

static void UpdateRemotePlayers()
{
    g_RemotePlayerCount = 0;
    if (!NetworkManager_get_Instance || !Component_get_transform || !Transform_get_position_Injected)
        return;
    NetworkManager_o* mgr = NetworkManager_get_Instance();
    if (!mgr || !mgr->connectedPlayers || !mgr->connectedPlayers->_items)
        return;
    auto arr = mgr->connectedPlayers->_items;
    const int count = mgr->connectedPlayers->_size;
    const int maxCount = static_cast<int>(sizeof(g_RemotePlayers) / sizeof(g_RemotePlayers[0]));
    for (int i = 0; i < count && g_RemotePlayerCount < maxCount; ++i) {
        auto np = arr->m_Items[i];
        if (!np || !np->netPlayerModel)
            continue;
        NetPlayerModel_o* model = np->netPlayerModel;
        void* tr = Component_get_transform(model, nullptr);
        if (!tr)
            continue;
        Vector3 pos{};
        __try {
            Transform_get_position_Injected(&pos, tr, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        g_RemotePlayers[g_RemotePlayerCount].model = model;
        g_RemotePlayers[g_RemotePlayerCount].pos = pos;
        g_RemotePlayers[g_RemotePlayerCount].isSelf = np->self;
        ++g_RemotePlayerCount;
    }
}

static void UpdateEnemies()
{
    g_EnemyCount = 0;
    if (!CharacterManager_GetEnemiesAfterPlayer || !Component_get_transform || !Transform_get_position_Injected)
        return;

    uint64_t pid = 0;
    if (NetworkManager_get_PlayerID) {
        __try { pid = NetworkManager_get_PlayerID(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { pid = 0; }
    }

    auto list = CharacterManager_GetEnemiesAfterPlayer(pid);
    if ((!list || !list->_items) && pid != 0) {
        list = CharacterManager_GetEnemiesAfterPlayer(0); // fallback to all
    }
    if (!list || !list->_items)
        return;
    auto arr = list->_items;
    const int count = list->_size;
    const int maxCount = static_cast<int>(sizeof(g_Enemies) / sizeof(g_Enemies[0]));
    for (int i = 0; i < count && g_EnemyCount < maxCount; ++i) {
        Enemy_o* e = arr->m_Items[i];
        if (!e)
            continue;
        void* tr = Component_get_transform(e, nullptr);
        if (!tr)
            continue;
        Vector3 pos{};
        __try {
            Transform_get_position_Injected(&pos, tr, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        g_Enemies[g_EnemyCount].obj = e;
        g_Enemies[g_EnemyCount].pos = pos;
        g_Enemies[g_EnemyCount].faction = e->faction;
        ++g_EnemyCount;
    }
}

static bool CreateAndEnableHook(void* target, void* detour, void** original, const char* name)
{
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        AppendLogFmt("[InstallGameHooks] CreateHook %s fail: %d (%s)\n", name, st, MHStatusText(st));
        return false;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        AppendLogFmt("[InstallGameHooks] EnableHook %s fail: %d (%s)\n", name, st, MHStatusText(st));
        return false;
    }
    return true;
}

// PTTRPlayer::Update hook: capture local player and feet position each frame.
static void __fastcall hk_PTTRPlayer_Update(PTTRPlayer_o* __this, void* method) {
    __try {
        if (!g_LocalPlayer && __this) {
            g_LocalPlayer = __this;
            g_LocalPlayerCandidate = nullptr;
            ResetLocalPlayerState();
            AppendLogFmt("[hk_PTTRPlayer_Update] Captured local player (raw) this=%p fpController=%p\n",
                __this, __this->fields.fpCharacterController);
        } else if (!g_LocalPlayer && g_LocalPlayerCandidate == __this && __this->fields.fpCharacterController) {
            g_LocalPlayer = __this;
            g_LocalPlayerCandidate = nullptr;
            ResetLocalPlayerState();
            AppendLogFmt("[hk_PTTRPlayer_Update] Captured local player (delayed) this=%p fpController=%p\n",
                __this, __this->fields.fpCharacterController);
        }

        if (__this == g_LocalPlayer && __this->fields.fpCharacterController && GetFeetPosition) {
            __try {
                GetFeetPosition(&g_LocalPlayerPos, __this, nullptr);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                const int count = g_feetExceptionCount.fetch_add(1) + 1;
                if (count <= 5) {
                    AppendLogFmt("[hk_PTTRPlayer_Update] GetFeetPosition crashed code=0x%08X this=%p local=%p retBuf=%p attempt=%d\n",
                        GetExceptionCode(), __this, g_LocalPlayer, &g_LocalPlayerPos, count);
                }
                if (count >= 3) {
                    GetFeetPosition = nullptr;
                }
            }
        }

        // Fallback: if feet function unavailable or we want earliest position, query Transform directly.
        if (__this == g_LocalPlayer && __this->fields.fpCharacterController && Component_get_transform && Transform_get_position_Injected) {
            void* controller = static_cast<void*>(__this->fields.fpCharacterController);
            void* tr = Component_get_transform(controller, nullptr);
            if (tr) {
                Vector3 pos{};
                __try {
                    Transform_get_position_Injected(&pos, tr, nullptr);
                    g_LocalPlayerPos = pos;
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    static std::atomic_int transformExCount{ 0 };
                    const int tcount = transformExCount.fetch_add(1) + 1;
                    if (tcount <= 5) {
                        AppendLogFmt("[hk_PTTRPlayer_Update] Transform_get_position crashed code=0x%08X this=%p ctrl=%p tr=%p attempt=%d\n",
                            GetExceptionCode(), __this, controller, tr, tcount);
                    }
                    if (tcount >= 3) {
                        Transform_get_position_Injected = nullptr; // disable fallback after repeated crashes
                    }
                }
            }
        }

        if (o_Update) {
            o_Update(__this, method);
        }

        // Update remote player list for ESP usage.
        UpdateRemotePlayers();
        UpdateEnemies();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static std::atomic_int updateExCount{ 0 };
        const int ucount = updateExCount.fetch_add(1) + 1;
        if (ucount <= 5) {
            AppendLogFmt("[hk_PTTRPlayer_Update] SEH caught code=0x%08X this=%p local=%p attempt=%d\n",
                GetExceptionCode(), __this, g_LocalPlayer, ucount);
        }
        // keep functions enabled; crashes likely due to null controller before ready
    }
}

static void __fastcall hk_PTTRPlayer_Awake(PTTRPlayer_o* __this, void* method)
{
    if (o_Awake) o_Awake(__this, method);
    if (__this && __this->fields.fpCharacterController && !g_LocalPlayer) {
        g_LocalPlayer = __this;
        g_feetExceptionCount.store(0, std::memory_order_relaxed);
        std::memset(&g_LocalPlayerPos, 0, sizeof(g_LocalPlayerPos));
        AppendLogFmt("[hk_PTTRPlayer_Awake] Spawn this=%p fpController=%p\n", __this, __this->fields.fpCharacterController);
    }
}

static void __fastcall hk_PTTRPlayer_OnEnable(PTTRPlayer_o* __this, void* method)
{
    if (o_OnEnable) o_OnEnable(__this, method);
    if (__this && __this->fields.fpCharacterController && !g_LocalPlayer) {
        g_LocalPlayer = __this;
        g_feetExceptionCount.store(0, std::memory_order_relaxed);
        std::memset(&g_LocalPlayerPos, 0, sizeof(g_LocalPlayerPos));
        AppendLogFmt("[hk_PTTRPlayer_OnEnable] Spawn/enable this=%p fpController=%p\n",
            __this, __this->fields.fpCharacterController);
    }
}

static void __fastcall hk_PTTRPlayer_OnDestroy(PTTRPlayer_o* __this, void* method)
{
    if (__this == g_LocalPlayer) {
        AppendLogFmt("[hk_PTTRPlayer_OnDestroy] Despawn local player this=%p\n", __this);
        ResetLocalPlayerState();
        g_LocalPlayer = nullptr;
    }
    else if (__this) {
        AppendLogFmt("[hk_PTTRPlayer_OnDestroy] Destroy other player this=%p local=%p\n", __this, g_LocalPlayer);
    }
    if (o_OnDestroy) o_OnDestroy(__this, method);
}

static void __fastcall hk_PTTRPlayer_Die(PTTRPlayer_o* __this, void* method)
{
    if (__this == g_LocalPlayer) {
        AppendLogFmt("[hk_PTTRPlayer_Die] Local player died this=%p\n", __this);
        ResetLocalPlayerState();
        g_LocalPlayer = nullptr;
    }
    else if (__this) {
        AppendLogFmt("[hk_PTTRPlayer_Die] Other player died this=%p local=%p\n", __this, g_LocalPlayer);
    }
    if (o_Die) o_Die(__this, method);
}

static void CreateRTV(IDXGISwapChain* swap) {
    if (g_rtv) return;
    ID3D11Texture2D* backbuf = nullptr;
    if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&backbuf)))) {
        g_device->CreateRenderTargetView(backbuf, nullptr, &g_rtv);
        backbuf->Release();
    }
}

static void CleanupRTV() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_INSERT) {
        const bool newState = !g_overlayVisible.load(std::memory_order_relaxed);
        g_overlayVisible.store(newState, std::memory_order_release);
        AppendLogFmt("[WndProc] Overlay toggle %s\n", newState ? "ON" : "OFF");
        return 0;
    }

    if (g_imguiReady.load(std::memory_order_acquire)) {
        if (Overlay_WndProcHandler(hWnd, msg, wParam, lParam))
            return 1;
    }
    if (g_origWndProc)
        return CallWindowProc(g_origWndProc, hWnd, msg, wParam, lParam);
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void HookWndProc(HWND hwnd) {
    if (!hwnd || g_origWndProc)
        return;
    g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProcHook)));
}

static void UnhookWndProc() {
    if (g_hwnd && g_origWndProc) {
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
        g_origWndProc = nullptr;
    }
}

static bool InstallGameHooks() {
    if (g_gameHookReady.load(std::memory_order_acquire))
        return true;

    if (!g_gameAsm) {
        g_gameAsm = GetModuleHandleW(L"GameAssembly.dll");
        if (!g_gameAsm) {
            AppendLog("[InstallGameHooks] GameAssembly.dll not loaded yet\n");
            return false;
        }
    }

    if (!il2cpp_resolve_icall_fn) {
        il2cpp_resolve_icall_fn = reinterpret_cast<il2cpp_resolve_icall_t>(
            GetProcAddress(g_gameAsm, "il2cpp_resolve_icall"));
        if (!il2cpp_resolve_icall_fn) {
            AppendLog("[InstallGameHooks] il2cpp_resolve_icall not found; transform fallback disabled\n");
        }
    }
    if (il2cpp_resolve_icall_fn) {
        Component_get_transform = reinterpret_cast<Component_get_transform_t>(
            il2cpp_resolve_icall_fn("UnityEngine.Component::get_transform()"));
        Transform_get_position_Injected = reinterpret_cast<Transform_get_position_Injected_t>(
            il2cpp_resolve_icall_fn("UnityEngine.Transform::get_position_Injected(UnityEngine.Vector3&)"));
        if (!Component_get_transform || !Transform_get_position_Injected) {
            AppendLog("[InstallGameHooks] Failed to resolve transform icalls; fallback disabled\n");
            Component_get_transform = nullptr;
            Transform_get_position_Injected = nullptr;
        }
    }
    // NetworkManager::get_Instance for remote player enumeration.
    NetworkManager_get_Instance = reinterpret_cast<NetworkManager_get_Instance_t>(
        reinterpret_cast<uint8_t*>(g_gameAsm) + 0x6C0AD0);
    NetworkManager_get_PlayerID = reinterpret_cast<NetworkManager_get_PlayerID_t>(
        reinterpret_cast<uint8_t*>(g_gameAsm) + 0x6C09E0);
    CharacterManager_GetEnemiesAfterPlayer = reinterpret_cast<CharacterManager_GetEnemiesAfterPlayer_t>(
        reinterpret_cast<uint8_t*>(g_gameAsm) + 0x3F0D90);

    auto addrUpdate = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_UPDATE;
    auto addrFeet = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_GET_FEET_POSITION;
    auto addrAwake = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_AWAKE;
    auto addrOnEnable = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_ONENABLE;
    auto addrOnDestroy = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_ONDESTROY;
    auto addrDie = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_DIE;
    GetFeetPosition = reinterpret_cast<GetFeetPosition_t>(addrFeet);

    auto target = reinterpret_cast<void*>(addrUpdate);

    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(target, &mbi, sizeof(mbi));
    AppendLogFmt("[InstallGameHooks] base=%p target=%p protect=0x%X state=0x%X type=0x%X\n",
        g_gameAsm, target, mbi.Protect, mbi.State, mbi.Type);

    bool okUpdate = CreateAndEnableHook(target, &hk_PTTRPlayer_Update, reinterpret_cast<void**>(&o_Update), "PTTRPlayer::Update");
    bool okAwake = CreateAndEnableHook(addrAwake, &hk_PTTRPlayer_Awake, reinterpret_cast<void**>(&o_Awake), "PTTRPlayer::Awake");
    bool okOnEnable = CreateAndEnableHook(addrOnEnable, &hk_PTTRPlayer_OnEnable, reinterpret_cast<void**>(&o_OnEnable), "PTTRPlayer::OnEnable");
    bool okOnDestroy = CreateAndEnableHook(addrOnDestroy, &hk_PTTRPlayer_OnDestroy, reinterpret_cast<void**>(&o_OnDestroy), "PTTRPlayer::OnDestroy");
    bool okDie = CreateAndEnableHook(addrDie, &hk_PTTRPlayer_Die, reinterpret_cast<void**>(&o_Die), "PTTRPlayer::Die");

    const bool allOk = okUpdate && okAwake && okOnEnable && okOnDestroy && okDie;
    if (allOk) {
        AppendLogFmt("[InstallGameHooks] SUCCESS update=%p feet=%p awake=%p onEnable=%p onDestroy=%p die=%p\n",
            target, static_cast<void*>(addrFeet), addrAwake, addrOnEnable, addrOnDestroy, addrDie);
        g_gameHookReady.store(true, std::memory_order_release);
        return true;
    }

    AppendLogFmt("[InstallGameHooks] Pending hooks update=%d awake=%d onEnable=%d onDestroy=%d die=%d\n",
        okUpdate, okAwake, okOnEnable, okOnDestroy, okDie);
    return false;
}

static HRESULT __stdcall hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    static bool loggedDeviceInitOk = false;
    static bool loggedDeviceInitFail = false;
    static bool loggedOverlayInitOk = false;
    static bool loggedOverlayInitFail = false;
    static int gameHookRetryCounter = 0;

    if (!g_device) {
        HRESULT hrDev = swap->GetDevice(__uuidof(ID3D11Device), (void**)&g_device);
        if (FAILED(hrDev)) {
            if (!loggedDeviceInitFail) {
                AppendLogFmt("[hkPresent] GetDevice failed hr=0x%08X\n", hrDev);
                loggedDeviceInitFail = true;
            }
            return oPresent(swap, sync, flags);
        }
        if (!loggedDeviceInitOk) {
            AppendLogFmt("[hkPresent] GetDevice OK device=%p\n", g_device);
            loggedDeviceInitOk = true;
        }
        g_device->GetImmediateContext(&g_ctx);
        DXGI_SWAP_CHAIN_DESC desc{};
        swap->GetDesc(&desc);
        g_hwnd = desc.OutputWindow;
        HookWndProc(g_hwnd);
        CreateRTV(swap);
        if (Overlay_Init != nullptr) {
            const bool ok = Overlay_Init(g_hwnd, g_device, g_ctx);
            if (ok) {
                g_imguiReady.store(true, std::memory_order_release);
            }
            if (ok && !loggedOverlayInitOk) {
                AppendLogFmt("[hkPresent] Overlay_Init succeeded hwnd=%p dev=%p ctx=%p\n", g_hwnd, g_device, g_ctx);
                loggedOverlayInitOk = true;
            }
            if (!ok && !loggedOverlayInitFail) {
                AppendLogFmt("[hkPresent] Overlay_Init FAILED hwnd=%p dev=%p ctx=%p\n", g_hwnd, g_device, g_ctx);
                loggedOverlayInitFail = true;
            }
        }
        else if (!loggedOverlayInitFail) {
            AppendLog("[hkPresent] Overlay_Init pointer is null\n");
            loggedOverlayInitFail = true;
        }
    }
    else {
        if (!g_rtv) CreateRTV(swap);
    }

    if (!g_imguiReady.load(std::memory_order_acquire) && Overlay_Init != nullptr && g_device && g_ctx && g_hwnd) {
        const bool ok = Overlay_Init(g_hwnd, g_device, g_ctx);
        if (ok) {
            g_imguiReady.store(true, std::memory_order_release);
            if (!loggedOverlayInitOk) {
                AppendLogFmt("[hkPresent] Overlay_Init retry succeeded hwnd=%p dev=%p ctx=%p\n", g_hwnd, g_device, g_ctx);
                loggedOverlayInitOk = true;
            }
        }
        else if (!loggedOverlayInitFail) {
            AppendLogFmt("[hkPresent] Overlay_Init retry FAILED hwnd=%p dev=%p ctx=%p\n", g_hwnd, g_device, g_ctx);
            loggedOverlayInitFail = true;
        }
    }

    if (!g_gameHookReady.load(std::memory_order_acquire)) {
        if ((gameHookRetryCounter++ % 120) == 0) {
            InstallGameHooks();
        }
    }

    if (g_imguiReady.load(std::memory_order_acquire) && g_rtv) {
        Overlay_NewFrame();
        if (g_overlayVisible.load(std::memory_order_acquire)) {
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            Overlay_Render();
        } else {
            Overlay_EndFrame(false); // finalize frame so ImGui state stays in sync even when hidden
        }
    }
    return oPresent(swap, sync, flags);
}

static HRESULT __stdcall hkResize(IDXGISwapChain* swap, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT swapFlags) {
    CleanupRTV();
    if (g_imguiReady.load(std::memory_order_acquire)) {
        Overlay_Shutdown();
        g_imguiReady.store(false, std::memory_order_release);
    }
    auto hr = oResize(swap, count, w, h, fmt, swapFlags);
    return hr;
}

static bool InstallDXHooksFromSwap(IDXGISwapChain* swap) {
    if (g_dxHooksReady.load(std::memory_order_acquire) || !swap)
        return g_dxHooksReady.load(std::memory_order_acquire);

    void** vtbl = *reinterpret_cast<void***>(swap);
    void* presentAddr = vtbl[8];
    void* resizeAddr = vtbl[13];

    MH_STATUS st1 = MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent));
    MH_STATUS st2 = MH_CreateHook(resizeAddr, &hkResize, reinterpret_cast<void**>(&oResize));
    if (st1 != MH_OK && st1 != MH_ERROR_ALREADY_CREATED)
        AppendLogFmt("[InstallHooks] CreateHook Present fail: %d (%s)\n", st1, MHStatusText(st1));
    if (st2 != MH_OK && st2 != MH_ERROR_ALREADY_CREATED)
        AppendLogFmt("[InstallHooks] CreateHook Resize fail: %d (%s)\n", st2, MHStatusText(st2));
    MH_STATUS en = MH_EnableHook(MH_ALL_HOOKS);
    if (en != MH_OK && en != MH_ERROR_ENABLED)
        AppendLogFmt("[InstallHooks] EnableHook fail: %d (%s)\n", en, MHStatusText(en));
    AppendLogFmt("[InstallHooks] DX hooks ready, present=%p resize=%p\n", presentAddr, resizeAddr);
    g_dxHooksReady.store(true, std::memory_order_release);
    return true;
}

static bool InstallDXHooksWithDummySwap()
{
    if (g_dxHooksReady.load(std::memory_order_acquire))
        return true;

    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "DXDummyWndClass";
    RegisterClassA(&wc);
    HWND dummy = CreateWindowExA(0, wc.lpszClassName, "dx_dummy", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 128, 128, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dummy) {
        AppendLog("[InstallHooks] Create dummy window failed\n");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = dummy;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* tmpDev = nullptr;
    ID3D11DeviceContext* tmpCtx = nullptr;
    IDXGISwapChain* tmpSwap = nullptr;
    D3D_FEATURE_LEVEL fl;

    const D3D_DRIVER_TYPE drivers[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = E_FAIL;
    for (auto drv : drivers) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, drv, nullptr, 0, levels, _countof(levels),
            D3D11_SDK_VERSION, &sd, &tmpSwap, &tmpDev, &fl, &tmpCtx);
        if (SUCCEEDED(hr)) break;
        AppendLogFmt("[InstallHooks] Dummy SwapChain failed (driver %d) hr=0x%08X\n", drv, hr);
    }
    if (FAILED(hr)) {
        AppendLogFmt("[InstallHooks] Dummy SwapChain final failure hr=0x%08X\n", hr);
        DestroyWindow(dummy);
        return false;
    }

    InstallDXHooksFromSwap(tmpSwap);

    tmpSwap->Release();
    tmpCtx->Release();
    tmpDev->Release();
    DestroyWindow(dummy);
    return g_dxHooksReady.load(std::memory_order_acquire);
}

static HRESULT WINAPI hkD3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    HRESULT hr = oD3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
        SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        InstallDXHooksFromSwap(*ppSwapChain);
    }
    return hr;
}

static bool InstallHooks() {
    HMODULE d3d = GetModuleHandleW(L"d3d11.dll");
    if (!d3d) {
        d3d = LoadLibraryW(L"d3d11.dll");
    }
    if (!d3d) {
        AppendLog("[InstallHooks] d3d11.dll not found\n");
        return false;
    }

    oD3D11CreateDeviceAndSwapChain = reinterpret_cast<D3D11CreateDeviceAndSwapChainFn>(
        GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain"));
    if (!oD3D11CreateDeviceAndSwapChain) {
        AppendLog("[InstallHooks] GetProcAddress D3D11CreateDeviceAndSwapChain failed\n");
        return false;
    }

    MH_STATUS st = MH_CreateHook(oD3D11CreateDeviceAndSwapChain, &hkD3D11CreateDeviceAndSwapChain,
        reinterpret_cast<void**>(&oD3D11CreateDeviceAndSwapChain));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED) {
        AppendLogFmt("[InstallHooks] Hook D3D11CreateDeviceAndSwapChain fail: %d (%s)\n", st, MHStatusText(st));
        return false;
    }
    MH_EnableHook(oD3D11CreateDeviceAndSwapChain);
    AppendLog("[InstallHooks] Waiting for game swapchain via D3D11CreateDeviceAndSwapChain\n");

    InstallDXHooksWithDummySwap();

    return true;
}

static void UninstallHooks() {
    CleanupRTV();
    if (g_imguiReady.load(std::memory_order_acquire)) {
        Overlay_Shutdown();
        g_imguiReady.store(false, std::memory_order_release);
    }
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    UnhookWndProc();
    AppendLog("[UninstallHooks] cleanup\n");
    if (g_ctx) {
        g_ctx->Release();
        g_ctx = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
    g_hwnd = nullptr;
}

static DWORD WINAPI HookThread(LPVOID) {
    MH_STATUS initSt = MH_Initialize();
    if (initSt != MH_OK && initSt != MH_ERROR_ALREADY_INITIALIZED) {
        AppendLogFmt("[HookThread] MH_Initialize failed: %d\n", initSt);
        return 0;
    }

    InstallHooks();
    InstallGameHooks();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        UninstallHooks();
    }
    return TRUE;
}
