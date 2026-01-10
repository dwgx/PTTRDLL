#include "pch.h"
#include <atomic>

#include "game_types.h"
#include "logging.h"
#include "MinHook.h"

#define RVA_PTTRPLAYER_UPDATE        0x71F8B0   // PTTRPlayer::Update
#define RVA_PTTRPLAYER_AWAKE         0x71B870   // PTTRPlayer::Awake
#define RVA_PTTRPLAYER_ONENABLE      0x71E610   // PTTRPlayer::OnEnable
#define RVA_PTTRPLAYER_ONDESTROY     0x71F440   // PTTRPlayer::OnDestroy
#define RVA_PTTRPLAYER_DIE           0x72F860   // PTTRPlayer::Die
#define RVA_PTTRPLAYER_GETTARGETPOINT 0x71B620  // PTTRPlayer::GetTargetPoint

using GetTargetPoint_t = void(__fastcall*)(Vector3* ret, PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Update_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Awake_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_OnEnable_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_OnDestroy_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Die_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using Component_get_transform_t = void* (__fastcall*)(void* __this, void* method);
using Transform_get_position_Injected_t = void(__fastcall*)(Vector3* ret, void* __this, void* method);
using il2cpp_resolve_icall_t = void* (__cdecl*)(const char* name);

static GetTargetPoint_t        GetTargetPoint = nullptr;
static PTTRPlayer_Update_t     o_Update = nullptr;
static PTTRPlayer_Awake_t      o_Awake = nullptr;
static PTTRPlayer_OnEnable_t   o_OnEnable = nullptr;
static PTTRPlayer_OnDestroy_t  o_OnDestroy = nullptr;
static PTTRPlayer_Die_t        o_Die = nullptr;
static Component_get_transform_t Component_get_transform = nullptr;
static Transform_get_position_Injected_t Transform_get_position_Injected = nullptr;
static il2cpp_resolve_icall_t    il2cpp_resolve_icall_fn = nullptr;
static HMODULE                 g_gameAsm = nullptr;
static std::atomic_int         g_targetPointExCount{ 0 };
static std::atomic_int         g_transformExCount{ 0 };

// Exported globals declared in game_types.h
PTTRPlayer_o*                  g_LocalPlayer = nullptr;
Vector3                        g_LocalPlayerPos{};

static inline void ResetLocalPlayerState()
{
    std::memset(&g_LocalPlayerPos, 0, sizeof(g_LocalPlayerPos));
    g_targetPointExCount.store(0, std::memory_order_relaxed);
    g_transformExCount.store(0, std::memory_order_relaxed);
}

// PTTRPlayer::Update hook: capture local player and read position each frame.
static void __fastcall hk_PTTRPlayer_Update(PTTRPlayer_o* __this, void* method) {
    static bool loggedFirstUpdate = false;

    __try {
        if (!loggedFirstUpdate) {
            loggedFirstUpdate = true;
            AppendLogFmt("[hk_PTTRPlayer_Update] first call this=%p fpController=%p\n",
                __this, __this ? __this->fields.fpCharacterController : nullptr);
        }

        // Capture immediately
        if (!g_LocalPlayer && __this) {
            g_LocalPlayer = __this;
            ResetLocalPlayerState();
            AppendLogFmt("[hk_PTTRPlayer_Update] Captured local player this=%p fpController=%p\n",
                __this, __this->fields.fpCharacterController);
        }

        if (o_Update) {
            o_Update(__this, method);
        }

        if (__this != g_LocalPlayer)
            return;

        // Prefer GetTargetPoint
        if (GetTargetPoint) {
            __try {
                GetTargetPoint(&g_LocalPlayerPos, __this, nullptr);
                g_targetPointExCount.store(0, std::memory_order_relaxed);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                const int tcount = g_targetPointExCount.fetch_add(1) + 1;
                if (tcount <= 5) {
                    AppendLogFmt("[hk_PTTRPlayer_Update] GetTargetPoint crashed code=0x%08X this=%p attempt=%d\n",
                        GetExceptionCode(), __this, tcount);
                }
            }
        }

        // Transform fallback (self -> controller)
        if (Component_get_transform && Transform_get_position_Injected) {
            auto tryRead = [&](void* tr) -> bool {
                if (!tr) return false;
                Vector3 pos{};
                __try {
                    Transform_get_position_Injected(&pos, tr, nullptr);
                    g_LocalPlayerPos = pos;
                    return true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    const int tcount = g_transformExCount.fetch_add(1) + 1;
                    if (tcount <= 5) {
                        AppendLogFmt("[hk_PTTRPlayer_Update] Transform_get_position crashed code=0x%08X this=%p tr=%p attempt=%d\n",
                            GetExceptionCode(), __this, tr, tcount);
                    }
                    return false;
                }
            };

            void* trSelf = nullptr;
            __try { trSelf = Component_get_transform(__this, nullptr); }
            __except (EXCEPTION_EXECUTE_HANDLER) { trSelf = nullptr; }

            bool got = tryRead(trSelf);

            if (!got && __this->fields.fpCharacterController) {
                void* trCtrl = nullptr;
                __try { trCtrl = Component_get_transform(static_cast<void*>(__this->fields.fpCharacterController), nullptr); }
                __except (EXCEPTION_EXECUTE_HANDLER) { trCtrl = nullptr; }
                tryRead(trCtrl);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static std::atomic_int updateExCount{ 0 };
        const int ucount = updateExCount.fetch_add(1) + 1;
        if (ucount <= 5) {
            AppendLogFmt("[hk_PTTRPlayer_Update] SEH caught code=0x%08X this=%p local=%p attempt=%d\n",
                GetExceptionCode(), __this, g_LocalPlayer, ucount);
        }
    }
}

static void __fastcall hk_PTTRPlayer_Awake(PTTRPlayer_o* __this, void* method)
{
    if (o_Awake) o_Awake(__this, method);
    if (__this && __this->fields.fpCharacterController && !g_LocalPlayer) {
        g_LocalPlayer = __this;
        ResetLocalPlayerState();
        AppendLogFmt("[hk_PTTRPlayer_Awake] Spawn this=%p fpController=%p\n", __this, __this->fields.fpCharacterController);
    }
}

static void __fastcall hk_PTTRPlayer_OnEnable(PTTRPlayer_o* __this, void* method)
{
    if (o_OnEnable) o_OnEnable(__this, method);
    if (__this && __this->fields.fpCharacterController && !g_LocalPlayer) {
        g_LocalPlayer = __this;
        ResetLocalPlayerState();
        AppendLogFmt("[hk_PTTRPlayer_OnEnable] Spawn/enable this=%p fpController=%p\n",
            __this, __this->fields.fpCharacterController);
    }
}

static void __fastcall hk_PTTRPlayer_OnDestroy(PTTRPlayer_o* __this, void* method)
{
    if (__this == g_LocalPlayer) {
        AppendLogFmt("[hk_PTTRPlayer_OnDestroy] Despawn local player this=%p\n", __this);
        g_LocalPlayer = nullptr;
        ResetLocalPlayerState();
    }
    if (o_OnDestroy) o_OnDestroy(__this, method);
}

static void __fastcall hk_PTTRPlayer_Die(PTTRPlayer_o* __this, void* method)
{
    if (__this == g_LocalPlayer) {
        AppendLogFmt("[hk_PTTRPlayer_Die] Local player died this=%p\n", __this);
        g_LocalPlayer = nullptr;
        ResetLocalPlayerState();
    }
    if (o_Die) o_Die(__this, method);
}

static bool CreateAndEnableHook(void* target, void* detour, void** original, const char* name)
{
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        AppendLogFmt("[InstallGameHooks] CreateHook %s fail: %d\n", name, st);
        return false;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        AppendLogFmt("[InstallGameHooks] EnableHook %s fail: %d\n", name, st);
        return false;
    }
    return true;
}

bool InstallGameHooks() {
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
    }

    auto addrUpdate = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_UPDATE;
    auto addrAwake = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_AWAKE;
    auto addrOnEnable = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_ONENABLE;
    auto addrOnDestroy = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_ONDESTROY;
    auto addrDie = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_DIE;
    GetTargetPoint = reinterpret_cast<GetTargetPoint_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_GETTARGETPOINT);

    bool okUpdate = CreateAndEnableHook(addrUpdate, &hk_PTTRPlayer_Update, reinterpret_cast<void**>(&o_Update), "PTTRPlayer::Update");
    bool okAwake = CreateAndEnableHook(addrAwake, &hk_PTTRPlayer_Awake, reinterpret_cast<void**>(&o_Awake), "PTTRPlayer::Awake");
    bool okOnEnable = CreateAndEnableHook(addrOnEnable, &hk_PTTRPlayer_OnEnable, reinterpret_cast<void**>(&o_OnEnable), "PTTRPlayer::OnEnable");
    bool okOnDestroy = CreateAndEnableHook(addrOnDestroy, &hk_PTTRPlayer_OnDestroy, reinterpret_cast<void**>(&o_OnDestroy), "PTTRPlayer::OnDestroy");
    bool okDie = CreateAndEnableHook(addrDie, &hk_PTTRPlayer_Die, reinterpret_cast<void**>(&o_Die), "PTTRPlayer::Die");

    const bool allOk = okUpdate && okAwake && okOnEnable && okOnDestroy && okDie;
    if (allOk) {
        AppendLogFmt("[InstallGameHooks] SUCCESS update=%p targetPoint=%p awake=%p onEnable=%p onDestroy=%p die=%p\n",
            addrUpdate, GetTargetPoint, addrAwake, addrOnEnable, addrOnDestroy, addrDie);
        return true;
    }

    AppendLogFmt("[InstallGameHooks] Pending hooks update=%d awake=%d onEnable=%d onDestroy=%d die=%d\n",
        okUpdate, okAwake, okOnEnable, okOnDestroy, okDie);
    return false;
}

void UninstallGameHooks() {
    MH_DisableHook(MH_ALL_HOOKS);
    g_LocalPlayer = nullptr;
    ResetLocalPlayerState();
}
