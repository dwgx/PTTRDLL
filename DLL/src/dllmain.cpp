#include "pch.h"
#include <windows.h>

#include "logging.h"
#include "dx_hooks.h"
#include "game_hooks.h"
#include "MinHook.h"

static DWORD WINAPI HookThread(LPVOID) {
    MH_STATUS initSt = MH_Initialize();
    if (initSt != MH_OK && initSt != MH_ERROR_ALREADY_INITIALIZED) {
        AppendLogFmt("[HookThread] MH_Initialize failed: %d\n", initSt);
        return 0;
    }

    InstallDXHooks();
    InstallGameHooks();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        UninstallDXHooks();
        UninstallGameHooks();
    }
    return TRUE;
}
