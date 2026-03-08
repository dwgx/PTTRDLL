# PTTRDLL

## Compliance Notice / 合规声明

- 本仓库仅用于学习、逆向研究、调试分析和兼容性研究。
- 禁止用于未授权访问、破坏服务、隐私侵害、在线作弊、恶意传播等行为。
- 使用者需自行遵守当地法律、平台条款与相关 EULA。
- 完整声明见 [DISCLAIMER.md](./DISCLAIMER.md)

---

PTTRDLL 是一个 C++ DLL 项目，包含 DX11 Hook 与简易 Overlay 相关代码。

一句话：
这是一个用于图形管线研究与调试的实验性项目，不是通用成品。

## 主要内容

- DX11 Present / Resize / WndProc 相关 Hook
- 基于 ImGui 的调试层显示
- 运行日志记录（用于定位问题）

## 项目结构

- `DLL/`：核心工程
- `Data/`：相关数据
- `DLL.slnx`：解决方案入口

## 构建方式

1. 用 Visual Studio 打开 `DLL.slnx` 或 `DLL/DLL.vcxproj`
2. 选择平台（x86/x64）和配置（Debug/Release）
3. 编译得到 DLL

## 调试建议

- 先在测试环境验证
- 通过日志排查 Hook 初始化和渲染阶段问题
- 出现异常时优先看最后几条日志

## 免责声明

本项目不提供任何违规用途支持。
