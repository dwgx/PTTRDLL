# DLL Overlay/Hook

轻量 DX11 + MinHook DLL，用于挂接游戏渲染与 PTTRPlayer 逻辑，采集坐标并绘制 ImGui 叠加层。

## 功能概览
- DX11 Present/Resize 钩子 + Win32 WndProc 捕获 `Insert` 热键开关 UI。
- PTTRPlayer 生命周期钩子（Awake/OnEnable/Update/OnDestroy/Die），自动捕获本地玩家并读取足部位置或 Transform 坐标。
- 异常防护：对 Feet/Transform 调用和 Update 入口添加 SEH，崩溃会写入 `D:\Project\overlay_log.txt` 并自动降级。
- ImGui 渲染安全检查，空绘制数据时跳过，避免断言。

## 构建
1. 打开 `DLL/DLL.vcxproj`（目标名已设为 `DLL.dll`）。
2. 选择需要的配置/平台（Debug/Release, x86/x64）。
3. 生成后产物位于 `bin/<arch>/<config>/DLL.dll`；调试符号 `DLL.pdb`。

## 使用
- 将 `DLL.dll` 注入目标进程（需匹配架构）。
- 默认自动挂接游戏创建的 swapchain；若游戏启动前已创建 swapchain，也会通过 dummy swapchain 获取 DX vtable。
- 日志写入 `D:\Project\overlay_log.txt`，包含钩子状态、崩溃信息、玩家捕获/死亡等。
- 按 `Insert` 显示/隐藏 ImGui 窗口；窗口内展示 FPS 与本地玩家 XYZ。

## 注意事项
- 若 Feet 读取崩溃超过 3 次，会自动停用 Feet 函数并仅用 Transform 备选路径。
- Transform icall 解析失败时会记录日志并禁用该路径，避免非法调用。
- 如遇崩溃或无坐标，请先查看 `overlay_log.txt` 末尾的 SEH/Transform/Feet 日志并反馈。\
