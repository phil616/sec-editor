# 安全编辑器

安全编辑器是一个面向 Windows 10 1703 及更高版本、Windows 11 的小型原生纯文本编辑器。它用于查看和编辑私钥、Token 与配置密钥等敏感文本；界面完全使用 Win32/GDI 自绘，不使用 EDIT、RichEdit、Scintilla 或第三方文本控件。

## 功能

- 打开、编辑、保存、另存为、关闭文档和退出；
- 命令行文件路径打开，并支持从 Windows Explorer 把文件拖入窗口；
- 未保存修改的“保存 / 放弃 / 取消”提示；
- 严格识别 ASCII、UTF-8（有/无 BOM）、UTF-16 LE/BE（有 BOM）；
- 检测 CRLF、LF、CR 和混合换行，内部规范化为 LF，保存时恢复选定格式；
- Unicode 标量安全的左右移动、Backspace 和 Delete；绘制、命中、光标和横向滚动统一使用实际像素宽度，适配超长单行、CJK、代理对与 Tab；
- 鼠标/键盘选择、Ctrl+A、Tab、翻页、水平/垂直滚动；Tab 可选择写入真实 `\t`（按四个字符宽度显示）或四个空格；
- 原生 Windows 菜单栏与系统色外观，可选择浅色、深色或跟随系统；
- 菜单栏实时显示当前文档安全状态，状态栏显示行列、编码、换行、修改状态和安全内存状态；
- “关于”对话框显示版本、作者、版权和许可证信息；
- 会话锁定、系统挂起、注销或关机时清空文档且不自动保存。

第一版故意不提供剪贴板、撤销/重做、自动保存、恢复、最近文件、日志、配置、网络、遥测和更新功能。

Tab 与主题选项仅对当前运行会话生效，不创建配置文件或注册表项。

## 安全模型

所有可能保存正文的程序自有缓冲区都由 `SecureAllocation` 管理：用 `VirtualAlloc` 预留前保护页、数据页和后保护页，只提交数据页；随后必须依次成功执行 `VirtualLock` 和 `WerRegisterExcludedMemoryBlock`。任一步失败都会清零并释放区域，文件不会继续打开。进程启动时还必须成功调用 `WerSetFlags(WER_FAULT_REPORTING_FLAG_NOHEAP)`。

正文 Gap Buffer、原始输入字节、解码 UTF-16、保存时连续 UTF-16、编码输出、64 KiB 绘制 scratch 和 UTF-16 输入暂存全部使用这种分配。扩容先建立并保护新区域，再复制，最后清零并释放旧区域。正常释放顺序是：清零、WER 注销、`VirtualUnlock`、`VirtualFree`。未处理异常处理器只尽力清零固定安全区域并终止进程，不生成 dump、日志或恢复文件。

横向布局缓存只记录行号、UTF-16 偏移和像素宽度，不保存正文。为避免在普通 GDI 位图中留下额外正文副本，编辑区不使用离屏文本位图；窗口通过禁止背景擦除、按行覆盖和局部状态栏刷新减少频闪。

> 安全编辑器不主动创建包含文档明文的缓存、临时文件、备份、日志或恢复文件。文档缓冲区使用 `VirtualLock` 锁定，并从 Windows Error Reporting 中排除。本程序无法抵御管理员、调试器、内核组件、完整系统转储、休眠转储或已经失陷的操作系统。

此外，GDI、DWM、输入法、显卡驱动及 Windows 自身可能产生应用无法控制的瞬时副本。外部管理员仍可使用 Task Manager、ProcDump、WinDbg 或 `MiniDumpWriteDump` 主动读取进程内存；系统休眠也可能把物理内存写入 `hiberfil.sys`。因此安全编辑器不宣称“绝对不落盘”。

## 文件与保存限制

- 最大输入文件：8 MiB；
- 最大内部 UTF-16 文档：16 MiB；
- 最大单行：1 MiB UTF-16 代码单元；
- 非法、截断、overlong UTF-8，代理项编码和大于 U+10FFFF 的码点会被拒绝；
- 不支持 ANSI/GBK/Shift-JIS/Big5/UTF-32 或二进制文件。

> 为避免产生额外明文临时文件，安全编辑器使用直接覆盖保存。系统崩溃或断电可能导致正在保存的文件损坏。

保存只在用户明确执行“保存”或“另存为”时使用 `CREATE_ALWAYS` 直接写目标文件，并在完成后调用 `FlushFileBuffers`。不会创建 `.tmp`、`.bak`、`filename~`、autosave 或 recovery 文件。

## 构建

Debian/Ubuntu 安装依赖：

```bash
sudo apt update
sudo apt install -y cmake ninja-build \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
  binutils-mingw-w64-x86-64 wine xvfb
```

交叉编译：

```bash
./scripts/build-windows.sh
```

生成的程序位于 `build-windows/安全编辑器.exe`，是静态链接的 x86-64 Windows GUI PE，不依赖 MinGW 或 Visual C++ 运行时 DLL。

Linux 原生单元测试：

```bash
./scripts/test-linux.sh
```

Wine 功能冒烟：

```bash
./scripts/test-wine.sh
```

Wine 只是兼容层，不能证明 Windows 安全 API 的真实语义。Wine 9 以及截至 2026-08-06 的 Wine 主分支缺少 `WerRegisterExcludedMemoryBlock` 和 `WerUnregisterExcludedMemoryBlock`；在这些版本上，正式 `安全编辑器.exe` 会按设计拒绝启动，冒烟脚本会失败而不会降低安全要求。应在实现全部三个 WER API 的 Wine 版本或真实 Windows 10/11 上完成端到端功能测试。
