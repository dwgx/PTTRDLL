#pragma once
#include <Windows.h>
#include <d3d11.h>

#ifdef OVERLAY_EXPORTS
#define OVERLAY_API extern "C" __declspec(dllexport)
#else
#define OVERLAY_API extern "C" __declspec(dllimport)
#endif

OVERLAY_API bool Overlay_Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
OVERLAY_API void Overlay_NewFrame();
OVERLAY_API void Overlay_Render();
OVERLAY_API void Overlay_EndFrame(bool renderDrawData);
OVERLAY_API void Overlay_Shutdown();
OVERLAY_API LRESULT Overlay_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
