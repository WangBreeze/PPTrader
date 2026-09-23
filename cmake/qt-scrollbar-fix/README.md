# APP-001 独立 Qt 滚动条修复构建

这是 PPTrader 针对 **Qt 6.4.3 与 Qt 6.11.2** 的受控依赖补丁。原始安装保持只读，供应商源码只复制到对应版本的构建目录 `vendor/quicktemplates2`。业务代码不屏蔽 Windows 无障碍，也不跳过销毁。每个 Qt 补丁版本必须独立生成二进制和清单，严禁跨版本复制。

补丁只将 `QQuickScrollBar::accessibilityActiveChanged` 中两个成员函数连接改为 `Qt::UniqueConnection`，保留关闭无障碍时的精准断开行为。重复激活前后的单次 increase/decrease 动作必须始终只有一个步长；该用法适用于成员函数连接，不适用于 lambda 去重。

## 构建

使用和应用完全相同补丁版本的 Qt SDK 及 QtDeclarative 源码，在仓库根目录执行；下列 `<version>` 与尖括号参数替换为本机值，不写入公共 preset。Qt 6.11.2 的模块目标名是 `PPTraderQuickTemplates2`，Qt 6.4.3 保留 `QuickTemplates2`：

```powershell
cmake -S cmake/qt-scrollbar-fix -B out/build/qt-scrollbar-fix-<version> -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt-SDK> -DPPTRADER_QT_DECLARATIVE_SOURCE=<QtDeclarative-source> -DHOST_PERL=<perl.exe> -DCMAKE_INSTALL_PREFIX=<workspace-local-install>
cmake --build out/build/qt-scrollbar-fix-<version> --config Debug --target <QuickTemplates2-or-PPTraderQuickTemplates2> --parallel 4
cmake --build out/build/qt-scrollbar-fix-<version> --config RelWithDebInfo --target <QuickTemplates2-or-PPTraderQuickTemplates2> --parallel 4
```

不要对全局 Qt 目录执行安装或复制。输出 `bin/Qt6QuickTemplates2d.dll` 仅用于 Debug，`bin/Qt6QuickTemplates2.dll` 仅用于 Release 系列；禁止混用。构建时保留官方模块的导出接口，使用相同版本官方 QML 插件；兼容性仍必须由实际加载和回归验证，不以文件名相同作为证明。

## 应用构建保护

Windows + Qt 6.4.3/6.11.2 默认启用 `PPTRADER_REQUIRE_QT_SCROLLBAR_FIX`。`PPTRADER_QT_SCROLLBAR_FIX_DIR` 默认指向相同 Qt 版本构建目录的 `bin`，也可在本机 preset 中指定其他受控位置。应用和相关 GUI 测试构建前校验补丁身份、Qt版本、Debug/Release系列与DLL的SHA-256，然后复制到应用生成目录；缺库或清单不符直接失败，不静默带回旧库。

不要关闭该选项来绕过退出验收。外部打包工具可能覆盖本地DLL，因此最终打包后也必须执行 `DeployRuntime.cmake` 并重跑退出回归；不能只测试构建目录后就保证外部安装包相同。Qt6.8升级必须独立重新验收，不复用本二进制补丁。

`application_exit` 的总测试预算覆盖三个独立子进程及启动阶段；每个子进程在真实窗口接受关闭后的上限仍是20秒，不因总测试预算增加而放宽。

## 验收与升级边界

- `accessible_scrollbar_lifecycle`：重复激活后单个辅助技术动作不放大。
- `application_exit`：真实 Main 的向导在重复激活后正常关闭，20秒内实际回收并返回0；超时清理明确记为失败。
- Qt 6.11.2 已于 2026-09-10 在独立 SDK/构建目录重跑相同测试；对应结果见 `docs/qt611-adaptation-2026-09-10.md`。任何其他 Qt 6.8.x/6.11.x 补丁版本仍须先核对源码并独立构建。此配方故意不接受任意版本，不能把 6.4.3 或 6.11.2 二进制复制进其他版本运行目录。
- 上游版本已满足这些回归后移除本补丁。测试超时不能用禁用无障碍、延长预算或跳过析构解决。

供应商源文件保留原版权/SPDX标记；修改位置与生成方式在此仓库公开，生成 DLL 和复制的源代码不纳入版本库。分发修复后的 Qt 时仍须按所采用的 Qt 许可提供对应源代码及修改、许可文本和其他要求，不因构建于本工作区而免除。
