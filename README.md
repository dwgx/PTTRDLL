# PTTRDLL

DX11 ImGui overlay + IL2CPP game hooks for PTTR — DLL injection, god mode, live player state tuning.

DX11 ImGui 覆盖层 + IL2CPP 游戏 Hook，针对 PTTR 的注入式修改器：无敌模式、血量/增益实时调节。

## Overview / 概述

PTTRDLL is a Windows DLL that injects into a DirectX 11 process, hooks the DXGI present/resize path, and draws a Dear ImGui overlay on top of the rendered frame. It also hooks IL2CPP game methods in `GameAssembly.dll` to read and modify player state (health, boosts, upgrades) for a Unity game referred to as "PTTR".

PTTRDLL 是一个注入到 DirectX 11 宿主进程的 Windows DLL。它 Hook DXGI 的 Present/Resize 流程，在渲染帧上方绘制 Dear ImGui 覆盖层，同时 Hook `GameAssembly.dll` 中的 IL2CPP 游戏方法，实现对 Unity 游戏 PTTR 玩家状态（血量、增益、升级）的读取与修改。

## Features / 功能

- **DX11 Hooking** — hooks `IDXGISwapChain::Present` (vtable index 8) and `ResizeBuffers` (index 13) via MinHook, plus `D3D11CreateDeviceAndSwapChain`. Uses a temporary dummy swap chain to resolve the vtable when the game swap chain is not yet available.
- **ImGui Overlay** — Win32 + DX11 ImGui backends, renders a "ClickGUI" window with dark accent-colored style, recreates the render target view on resize.
- **Overlay Toggle** — `INSERT` key shows/hides the overlay (WndProc hook).
- **Game Hooks (IL2CPP)** — hooks `PTTRPlayer` methods (`Update`, `Awake`, `OnEnable`, `OnDestroy`, `Die`, `set_health`) at fixed RVAs in `GameAssembly.dll` to capture the local player and read/write vitals.
- **God Mode** — toggle that clamps health and applies configurable health / boost / upgrade values; live values shown in the overlay.
- **Player Position Readout** — resolves `UnityEngine.Component::get_transform` and `Transform::get_position_Injected` via `il2cpp_resolve_icall`, with a `GetTargetPoint` fast path and transform fallback.
- **SEH Protection** — game reads/writes wrapped in `__try/__except` so a bad pointer logs instead of crashing the host.
- **File Logging** — append-only logging for tracing init and render stages.

---

- **DX11 Hook** — 通过 MinHook 挂钩 `IDXGISwapChain::Present`（vtable 索引 8）和 `ResizeBuffers`（索引 13），以及 `D3D11CreateDeviceAndSwapChain`。利用临时假 swap chain 获取真实 vtable。
- **ImGui 覆盖层** — 使用 Win32 + DX11 后端，绘制暗色主题的 ClickGUI 窗口，resize 时自动重建 RTV。
- **覆盖层开关** — `INSERT` 键切换显示/隐藏。
- **游戏 Hook (IL2CPP)** — 在 `GameAssembly.dll` 中按固定 RVA 挂钩 `PTTRPlayer` 的 `Update`、`Awake`、`OnEnable`、`OnDestroy`、`Die`、`set_health` 方法，捕获本地玩家并读写生命值。
- **无敌模式** — 锁血并应用可配置的 health / boost / upgrade 数值，覆盖层中实时显示。
- **玩家坐标读取** — 通过 `il2cpp_resolve_icall` 解析 `get_transform` 和 `get_position_Injected`，支持 `GetTargetPoint` 快速路径。
- **SEH 保护** — 游戏内存读写使用结构化异常处理，指针失效时记录日志而非崩溃。
- **文件日志** — 追加式日志，跟踪初始化和渲染流程。

## Tech Stack / 技术栈

| Component | Detail |
|-----------|--------|
| Language | C++20 (`stdcpp20`), Windows DLL |
| Toolchain | MSVC / Visual Studio, platform toolset `v145`, Windows SDK `10.0` |
| Graphics | Direct3D 11 / DXGI (`d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`) |
| ImGui | Vendored Dear ImGui with Win32 + DX11 backends |
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
│  │  ├─ dllmain.cpp        # DllMain: hook thread, install/uninstall hooks
│  │  ├─ dx_hooks.cpp       # DXGI Present/Resize hooking + RTV management
│  │  ├─ game_hooks.cpp     # IL2CPP PTTRPlayer hooks + god-mode logic
│  │  ├─ overlay.cpp        # ImGui overlay init/render/shutdown
│  │  └─ logging.cpp        # Append-only file logging
│  └─ third_party/
│     ├─ imgui/             # Dear ImGui + Win32/DX11 backends
│     └─ minhook/           # MinHook
├─ Data/                    # RE artifacts (IL2CPP dump.cs, function.json, ...)
└─ LICENSE.txt              # MIT
```

## Getting Started / 快速开始

### Prerequisites / 前置条件

- Windows + Visual Studio (toolset `v145`, C++20 support)
- Windows 10 SDK (provides D3D11/DXGI libs)

### Build / 构建

1. Open `DLL.slnx` (or `DLL/DLL.vcxproj`) in Visual Studio.
2. Select platform (x86 / x64) and configuration (Debug / Release).
3. Build. Output: `bin/<Platform>/<Configuration>/DLL.dll`.

用 Visual Studio 打开 `DLL.slnx`，选平台和配置，编译即可。产物在 `bin/<Platform>/<Configuration>/DLL.dll`。

Vendored ImGui and MinHook are compiled as part of the project — no external package manager needed.

### Injection / 注入

Build produces a DLL. Load it into the target DX11 process; on `DLL_PROCESS_ATTACH` it spawns a background thread that initializes MinHook and installs DX + game hooks.

编译产物为 DLL，注入到目标 DX11 进程后，`DLL_PROCESS_ATTACH` 时自动启动后台线程安装所有 Hook。

<!-- TODO: confirm the intended injection method / target process. The repo does not include an injector. -->

## Usage / 使用方法

Once loaded and hooks are live:

- Press **`INSERT`** to toggle the overlay.
- The overlay window exposes:
  - **God Mode** checkbox
  - **God Health**, **Health Boost**, **Health Upgrade** sliders
  - Live **Local** readout: health, boosts, upgrade, XYZ position (when a local player is captured)

注入成功后：

- 按 **`INSERT`** 切换覆盖层显隐
- 覆盖层提供：
  - **无敌模式** 开关
  - **血量 / 增益 / 升级** 滑块调节
  - **本地玩家** 实时数据：血量、增益、升级值、XYZ 坐标

Defaults (`DLL/include/config.h`): god mode enabled, health `10000`, health boost `9999`, health upgrade `99`.

默认值（`DLL/include/config.h`）：无敌模式开启，血量 `10000`，增益 `9999`，升级 `99`。

## Configuration / 配置

Settings are compile-time defaults in `DLL/include/config.h` (`Config::Settings`), adjustable live in the overlay. No environment variables.

配置在 `DLL/include/config.h` 中以编译期默认值定义，可在覆盖层中实时修改，无需环境变量。

Game hooks rely on fixed RVAs into `GameAssembly.dll` (defined as `RVA_*` macros in `game_hooks.cpp`). These are specific to a particular game build — update them for other versions.

游戏 Hook 依赖 `GameAssembly.dll` 中的固定 RVA（`game_hooks.cpp` 中的 `RVA_*` 宏），针对特定游戏版本，其他版本需自行更新偏移。

## Status / 状态

Personal project, WIP. Kept around as a DX11 hooking + IL2CPP reverse engineering study.

个人项目，持续折腾中。留着当 DX11 Hook 和 IL2CPP 逆向的练手记录。

## License / 许可证

MIT — see [LICENSE.txt](./LICENSE.txt).

<!-- TODO: the MIT LICENSE.txt still has placeholder [year] and [fullname] fields; fill these in. -->
