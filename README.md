# PTTRDLL

DX11 game cheat DLL for PTTR (Unity IL2CPP) — ImGui overlay + god mode + player state manipulation.

针对 PTTR（Unity IL2CPP 游戏）的 DX11 作弊 DLL —— ImGui 覆盖层 + 无敌模式 + 玩家状态修改。

## Overview / 概述

PTTRDLL is a Windows DLL that injects into a DirectX 11 game process, hooks the DXGI present/resize path, and draws a Dear ImGui overlay on top of the rendered frame. It hooks IL2CPP game methods to read and modify player state — health, boosts, upgrades, position — for the Unity game "PTTR".

PTTRDLL 是一个注入 DirectX 11 游戏进程的 Windows DLL，Hook DXGI 的 Present/Resize 流程，在渲染帧之上绘制 Dear ImGui 覆盖层，并 Hook IL2CPP 游戏方法来读取和修改玩家状态（血量、增益、升级、坐标）。

## Features / 功能

- **DX11 hooking** — hooks `IDXGISwapChain::Present` (vtable index 8) and `ResizeBuffers` (index 13) via MinHook, plus `D3D11CreateDeviceAndSwapChain`. Uses a temporary dummy swap chain to resolve the vtable when the game swap chain is not yet available.
- **ImGui overlay** — Win32 + DX11 ImGui backends, renders a ClickGUI window with dark accent-colored style, recreates render target view on resize.
- **Overlay toggle** — `INSERT` key shows/hides the overlay via WndProc hook.
- **Game hooks (IL2CPP)** — hooks `PTTRPlayer` methods (`Update`, `Awake`, `OnEnable`, `OnDestroy`, `Die`, `set_health`) at fixed RVAs in `GameAssembly.dll` to capture the local player and read/write vitals.
- **God mode** — toggle that clamps health and applies configurable health / boost / upgrade values, with live readout in the overlay.
- **Player position readout** — resolves `UnityEngine.Component::get_transform` and `Transform::get_position_Injected` via `il2cpp_resolve_icall`.
- **SEH protection** — game reads/writes wrapped in `__try/__except` so bad pointers log instead of crashing the host.
- **File logging** — append-only log for tracing init and render stages during injection sessions.

---

- **DX11 Hook** — 通过 MinHook 挂钩 `IDXGISwapChain::Present`（vtable 8）和 `ResizeBuffers`（13），以及 `D3D11CreateDeviceAndSwapChain`。用临时 dummy swap chain 解析 vtable。
- **ImGui 覆盖层** — Win32 + DX11 后端，渲染暗色风格 ClickGUI 窗口，resize 时自动重建 RTV。
- **覆盖层开关** — `INSERT` 键切换显示/隐藏。
- **游戏 Hook（IL2CPP）** — 挂钩 `PTTRPlayer` 的 `Update`、`Awake`、`OnEnable`、`OnDestroy`、`Die`、`set_health` 方法（固定 RVA），捕获本地玩家并读写状态。
- **无敌模式** — 锁血 + 可调血量/增益/升级数值，覆盖层实时显示。
- **玩家坐标读取** — 通过 `il2cpp_resolve_icall` 解析 Transform 获取坐标。
- **SEH 保护** — 游戏读写用 `__try/__except` 包裹，野指针只记日志不崩进程。
- **文件日志** — 追加写入，用于跟踪注入会话中的初始化和渲染阶段。

## Tech Stack / 技术栈

| Component | Detail |
|-----------|--------|
| Language | C++20, Windows DLL |
| Toolchain | MSVC / Visual Studio, platform toolset `v145`, Windows SDK `10.0` |
| Graphics | Direct3D 11 / DXGI |
| ImGui | Vendored Dear ImGui (Win32 + DX11 backends) |
| Hooking | Vendored MinHook |
| Solution | `DLL.slnx` + `DLL/DLL.vcxproj` |

<!-- Note: GitHub reports the primary language as C# because of the large IL2CPP dump under Data/ (dump.cs). The buildable project itself is C++. -->

## Project Structure / 项目结构

```
PTTRDLL/
├─ DLL.slnx                 # Visual Studio solution
├─ DLL/
│  ├─ DLL.vcxproj           # MSVC project (x86/x64, Debug/Release)
│  ├─ include/              # Config, game type layouts, overlay API, secure types
│  ├─ src/
│  │  ├─ dllmain.cpp        # DllMain: hook thread spawn, install/uninstall
│  │  ├─ dx_hooks.cpp       # DXGI Present/Resize hooking + RTV management
│  │  ├─ game_hooks.cpp     # IL2CPP PTTRPlayer hooks + god-mode logic
│  │  ├─ overlay.cpp        # ImGui overlay init/render/shutdown
│  │  └─ logging.cpp        # Append-only file logging
│  └─ third_party/
│     ├─ imgui/             # Dear ImGui + backends
│     └─ minhook/           # MinHook
└─ Data/                    # Reverse-engineering artifacts (IL2CPP dump.cs, etc.)
```

## Getting Started / 快速开始

### Prerequisites / 前置条件

- Windows + Visual Studio (toolset `v145`, C++20 support)
- Windows 10 SDK (provides D3D11/DXGI libs)

### Build / 构建

1. Open `DLL.slnx` (or `DLL/DLL.vcxproj`) in Visual Studio.
2. Select platform (x86 / x64) and configuration (Debug / Release).
3. Build. Output: `bin/<Platform>/<Configuration>/DLL.dll`.

用 VS 打开 `DLL.slnx`，选平台和配置，编译即可。产物在 `bin/<Platform>/<Configuration>/DLL.dll`。

No external package manager dependencies — ImGui and MinHook are vendored and compiled as part of the project.

### Injection / 注入

The build produces a DLL. Inject it into the running PTTR game process (any DLL injector will work). On `DLL_PROCESS_ATTACH` it starts a background thread that initializes MinHook and installs DX + game hooks.

编译产物是 DLL，用任意注入器注入到运行中的 PTTR 游戏进程。`DLL_PROCESS_ATTACH` 时自动启动后台线程挂钩。

<!-- TODO: confirm the intended injection method / target process. The repo does not include an injector. -->

## Usage / 使用方法

Once injected and DX hooks are live:

- Press **`INSERT`** to toggle the overlay.
- The overlay window exposes:
  - **God Mode** checkbox
  - **God Health** / **Health Boost** / **Health Upgrade** sliders
  - Live **Local** readout: health, boosts, upgrade, XYZ position

注入成功后按 `INSERT` 打开覆盖层，可切换无敌模式、调节血量/增益/升级数值，实时显示本地玩家状态和坐标。

Defaults (`DLL/include/config.h`): god mode ON, health `10000`, health boost `9999`, health upgrade `99`.

## Configuration / 配置

Settings are compile-time defaults in `DLL/include/config.h` (`Config::Settings`), adjustable live in the overlay at runtime. No config files or environment variables.

配置项为编译期默认值（`config.h`），运行时可通过覆盖层实时调整。无外部配置文件。

Game hooks use fixed RVAs into `GameAssembly.dll` (defined as `RVA_*` macros in `game_hooks.cpp`) — these are tied to a specific game build. Update them for other versions.

游戏 Hook 使用固定 RVA 偏移（`game_hooks.cpp` 中的 `RVA_*` 宏），绑定特定游戏版本，换版本需要更新偏移。

## Status / 状态

Personal project, WIP. Kept around as a DX11 hooking and IL2CPP reversing reference.

个人项目，留着当逆向和图形 Hook 的参考。

## License / 许可证

MIT — see [LICENSE.txt](./LICENSE.txt).

<!-- TODO: the MIT LICENSE.txt still has placeholder [year] and [fullname] fields; fill these in. -->
