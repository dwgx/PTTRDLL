#include "pch.h"
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>

#include "overlay_api.h"
#include "logging.h"
#include "game_hooks.h"
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

static PresentFn  oPresent = nullptr;
static ResizeFn   oResize = nullptr;
static D3D11CreateDeviceAndSwapChainFn oD3D11CreateDeviceAndSwapChain = nullptr;

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND                    g_hwnd = nullptr;
static WNDPROC                 g_origWndProc = nullptr;
static std::atomic_bool        g_imguiReady{ false };
static std::atomic_bool        g_dxHooksReady{ false };
static std::atomic_bool        g_overlayVisible{ true };

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

    if ((gameHookRetryCounter++ % 120) == 0) {
        InstallGameHooks();
    }

    if (g_imguiReady.load(std::memory_order_acquire) && g_rtv) {
        Overlay_NewFrame();
        if (g_overlayVisible.load(std::memory_order_acquire)) {
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            Overlay_Render();
        } else {
            Overlay_EndFrame(false);
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

bool InstallDXHooks() {
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

void UninstallDXHooks() {
    CleanupRTV();
    if (g_imguiReady.load(std::memory_order_acquire)) {
        Overlay_Shutdown();
        g_imguiReady.store(false, std::memory_order_release);
    }
    MH_DisableHook(MH_ALL_HOOKS);
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
