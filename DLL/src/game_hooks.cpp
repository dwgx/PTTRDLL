#include "pch.h"
#include <atomic>
#include <algorithm>

#include "game_types.h"
#include "config.h"
#include "logging.h"
#include "MinHook.h"

#define RVA_PTTRPLAYER_UPDATE        0x71F8B0   // PTTRPlayer::Update
#define RVA_PTTRPLAYER_AWAKE         0x71B870   // PTTRPlayer::Awake
#define RVA_PTTRPLAYER_ONENABLE      0x71E610   // PTTRPlayer::OnEnable
#define RVA_PTTRPLAYER_ONDESTROY     0x71F440   // PTTRPlayer::OnDestroy
#define RVA_PTTRPLAYER_DIE           0x72F860   // PTTRPlayer::Die
#define RVA_PTTRPLAYER_GETTARGETPOINT 0x71B620  // PTTRPlayer::GetTargetPoint
#define RVA_PTTRPLAYER_GET_HEALTH    0x718EE0   // PTTRPlayer::get_health
#define RVA_PTTRPLAYER_SET_HEALTH    0x718F40   // PTTRPlayer::set_health
#define RVA_PTTRPLAYER_GET_HEALTHBOOST 0x7192A0 // PTTRPlayer::get_healthBoost
#define RVA_PTTRPLAYER_SET_HEALTHBOOST 0x719320 // PTTRPlayer::set_healthBoost
#define RVA_PTTRPLAYER_GET_HEALTHUPGRADE 0x719730 // PTTRPlayer::get_healthUpgrade
#define RVA_PTTRPLAYER_SET_HEALTHUPGRADE 0x7197B0 // PTTRPlayer::set_healthUpgrade

using GetTargetPoint_t = void(__fastcall*)(Vector3* ret, PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Update_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Awake_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_OnEnable_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_OnDestroy_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_Die_t = void(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_get_health_t = float(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_set_health_t = void(__fastcall*)(PTTRPlayer_o* __this, float value, void* method);
using PTTRPlayer_get_healthBoost_t = int(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_set_healthBoost_t = void(__fastcall*)(PTTRPlayer_o* __this, int value, void* method);
using PTTRPlayer_get_healthUpgrade_t = int(__fastcall*)(PTTRPlayer_o* __this, void* method);
using PTTRPlayer_set_healthUpgrade_t = void(__fastcall*)(PTTRPlayer_o* __this, int value, void* method);
using Component_get_transform_t = void* (__fastcall*)(void* __this, void* method);
using Transform_get_position_Injected_t = void(__fastcall*)(Vector3* ret, void* __this, void* method);
using il2cpp_resolve_icall_t = void* (__cdecl*)(const char* name);

static GetTargetPoint_t        GetTargetPoint = nullptr;
static PTTRPlayer_Update_t     o_Update = nullptr;
static PTTRPlayer_Awake_t      o_Awake = nullptr;
static PTTRPlayer_OnEnable_t   o_OnEnable = nullptr;
static PTTRPlayer_OnDestroy_t  o_OnDestroy = nullptr;
static PTTRPlayer_Die_t        o_Die = nullptr;
static PTTRPlayer_get_health_t PTTRPlayer_get_health = nullptr;
static PTTRPlayer_set_health_t PTTRPlayer_set_health = nullptr;
static PTTRPlayer_set_health_t o_set_health = nullptr;
static PTTRPlayer_get_healthBoost_t PTTRPlayer_get_healthBoost = nullptr;
static PTTRPlayer_set_healthBoost_t PTTRPlayer_set_healthBoost = nullptr;
static PTTRPlayer_get_healthUpgrade_t PTTRPlayer_get_healthUpgrade = nullptr;
static PTTRPlayer_set_healthUpgrade_t PTTRPlayer_set_healthUpgrade = nullptr;
static Component_get_transform_t Component_get_transform = nullptr;
static Transform_get_position_Injected_t Transform_get_position_Injected = nullptr;
static il2cpp_resolve_icall_t    il2cpp_resolve_icall_fn = nullptr;
static HMODULE                 g_gameAsm = nullptr;
static std::atomic_int         g_targetPointExCount{ 0 };
static std::atomic_int         g_transformExCount{ 0 };

// Exported globals declared in game_types.h
PTTRPlayer_o*                  g_LocalPlayer = nullptr;
Vector3                        g_LocalPlayerPos{};
float                          g_LocalPlayerHealth = 0.0f;
int                            g_LocalPlayerHealthBoost = 0;
int                            g_LocalPlayerHealthUpgrade = 0;

static inline void ResetLocalPlayerState()
{
    std::memset(&g_LocalPlayerPos, 0, sizeof(g_LocalPlayerPos));
    g_LocalPlayerHealth = 0.0f;
    g_LocalPlayerHealthBoost = 0;
    g_LocalPlayerHealthUpgrade = 0;
    g_targetPointExCount.store(0, std::memory_order_relaxed);
    g_transformExCount.store(0, std::memory_order_relaxed);
}

static void RefreshLocalPlayerVitals(PTTRPlayer_o* player)
{
    if (!player)
        return;

    if (PTTRPlayer_get_health) {
        __try { g_LocalPlayerHealth = PTTRPlayer_get_health(player, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (PTTRPlayer_get_healthBoost) {
        __try { g_LocalPlayerHealthBoost = PTTRPlayer_get_healthBoost(player, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (PTTRPlayer_get_healthUpgrade) {
        __try { g_LocalPlayerHealthUpgrade = PTTRPlayer_get_healthUpgrade(player, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static void ApplyGodMode(PTTRPlayer_o* player)
{
    const auto& cfg = Config::Get();
    if (!cfg.godModeEnabled || !player)
        return;

    PTTRPlayer_set_health_t setHealth = o_set_health ? o_set_health : PTTRPlayer_set_health;
    if (setHealth) {
        __try { setHealth(player, cfg.godModeHealth, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (PTTRPlayer_set_healthBoost) {
        __try { PTTRPlayer_set_healthBoost(player, cfg.godModeHealthBoost, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (PTTRPlayer_set_healthUpgrade) {
        __try { PTTRPlayer_set_healthUpgrade(player, cfg.godModeHealthUpgrade, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    g_LocalPlayerHealth = cfg.godModeHealth;
    g_LocalPlayerHealthBoost = cfg.godModeHealthBoost;
    g_LocalPlayerHealthUpgrade = cfg.godModeHealthUpgrade;
}

static void __fastcall hk_PTTRPlayer_set_health(PTTRPlayer_o* __this, float value, void* method)
{
    const auto& cfg = Config::Get();
    if (cfg.godModeEnabled && __this) {
        if (!g_LocalPlayer)
            g_LocalPlayer = __this;
        if (__this == g_LocalPlayer) {
            value = std::max(value, cfg.godModeHealth);
            g_LocalPlayerHealth = value;
        }
    }
    else if (__this == g_LocalPlayer) {
        g_LocalPlayerHealth = value;
    }

    if (o_set_health) {
        o_set_health(__this, value, method);
    }
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

        RefreshLocalPlayerVitals(__this);
        ApplyGodMode(__this);

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

    PTTRPlayer_get_health = reinterpret_cast<PTTRPlayer_get_health_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_GET_HEALTH);
    PTTRPlayer_set_health = reinterpret_cast<PTTRPlayer_set_health_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_SET_HEALTH);
    PTTRPlayer_get_healthBoost = reinterpret_cast<PTTRPlayer_get_healthBoost_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_GET_HEALTHBOOST);
    PTTRPlayer_set_healthBoost = reinterpret_cast<PTTRPlayer_set_healthBoost_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_SET_HEALTHBOOST);
    PTTRPlayer_get_healthUpgrade = reinterpret_cast<PTTRPlayer_get_healthUpgrade_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_GET_HEALTHUPGRADE);
    PTTRPlayer_set_healthUpgrade = reinterpret_cast<PTTRPlayer_set_healthUpgrade_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_SET_HEALTHUPGRADE);

    auto addrUpdate = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_UPDATE;
    auto addrAwake = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_AWAKE;
    auto addrOnEnable = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_ONENABLE;
    auto addrOnDestroy = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_ONDESTROY;
    auto addrDie = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_DIE;
    auto addrSetHealth = reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_SET_HEALTH;
    GetTargetPoint = reinterpret_cast<GetTargetPoint_t>(reinterpret_cast<uint8_t*>(g_gameAsm) + RVA_PTTRPLAYER_GETTARGETPOINT);

    bool okUpdate = CreateAndEnableHook(addrUpdate, &hk_PTTRPlayer_Update, reinterpret_cast<void**>(&o_Update), "PTTRPlayer::Update");
    bool okAwake = CreateAndEnableHook(addrAwake, &hk_PTTRPlayer_Awake, reinterpret_cast<void**>(&o_Awake), "PTTRPlayer::Awake");
    bool okOnEnable = CreateAndEnableHook(addrOnEnable, &hk_PTTRPlayer_OnEnable, reinterpret_cast<void**>(&o_OnEnable), "PTTRPlayer::OnEnable");
    bool okOnDestroy = CreateAndEnableHook(addrOnDestroy, &hk_PTTRPlayer_OnDestroy, reinterpret_cast<void**>(&o_OnDestroy), "PTTRPlayer::OnDestroy");
    bool okDie = CreateAndEnableHook(addrDie, &hk_PTTRPlayer_Die, reinterpret_cast<void**>(&o_Die), "PTTRPlayer::Die");
    bool okSetHealth = CreateAndEnableHook(addrSetHealth, &hk_PTTRPlayer_set_health, reinterpret_cast<void**>(&o_set_health), "PTTRPlayer::set_health");

    const bool allOk = okUpdate && okAwake && okOnEnable && okOnDestroy && okDie && okSetHealth;
    if (allOk) {
        AppendLogFmt("[InstallGameHooks] SUCCESS update=%p targetPoint=%p awake=%p onEnable=%p onDestroy=%p die=%p setHealth=%p\n",
            addrUpdate, GetTargetPoint, addrAwake, addrOnEnable, addrOnDestroy, addrDie, addrSetHealth);
        return true;
    }

    AppendLogFmt("[InstallGameHooks] Pending hooks update=%d awake=%d onEnable=%d onDestroy=%d die=%d setHealth=%d\n",
        okUpdate, okAwake, okOnEnable, okOnDestroy, okDie, okSetHealth);
    return false;
}

void UninstallGameHooks() {
    MH_DisableHook(MH_ALL_HOOKS);
    g_LocalPlayer = nullptr;
    ResetLocalPlayerState();
}
