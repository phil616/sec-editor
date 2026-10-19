# 安全编辑器

安全编辑器是一个面向 Windows 10 1703 及更高版本、Windows 11 的小型原生纯文本编辑器。它用于查看和编辑私钥、Token 与配置密钥等敏感文本；界面完全使用 Win32/GDI 自绘，不使用 EDIT、RichEdit、Scintilla 或第三方文本控件。

## 功能

- 打开、编辑、保存、另存为、关闭文档和退出；
- 支持 `mempad.exe [filename]` 命令行启动，并支持从 Windows Explorer 把文件拖入窗口；
- 可从“文件”菜单按当前用户安装或卸载“使用 mempad 打开”右键菜单；支持 `.txt`、`.text`、`.md`、`.csv`、`.pem`、`.env`、`.ini`、`.yaml`、`.yml`、`.key`、`.json`、`.crt`、`.cer`、`.sig`、`.csr`、`.req`、`.conf`、`.inf`；
- 未保存修改的“保存 / 放弃 / 取消”提示；
- 严格识别 ASCII、UTF-8（有/无 BOM）、UTF-16 LE/BE（有 BOM）；
- 检测 CRLF、LF、CR 和混合换行，内部规范化为 LF，保存时恢复选定格式；
- Unicode 标量安全的左右移动、Backspace 和 Delete；绘制、命中、光标和横向滚动统一使用实际像素宽度，适配超长单行、CJK、代理对与 Tab；
- 鼠标/键盘选择、Ctrl+A 全选、Ctrl+C 复制选中、Ctrl+X 剪切选中、Ctrl+V 粘贴、Tab、翻页、水平/垂直滚动；Tab 可选择写入真实 `\t`（按四个字符宽度显示）或四个空格；
- Ctrl+F 打开自绘查找栏，支持区分大小写的实时字面量查找、匹配计数、Enter / F3 下一个、Shift+Enter / Shift+F3 上一个、首尾循环和 Esc 关闭；可从当前单行选区填入关键词，查找栏内支持选择、复制、剪切和粘贴，Tab 返回正文；
- 双击选择词块，并可继续拖动按词扩展选区。例如 `/etc/ssh/sshd_config` 中双击 `ssh` 只选择 `ssh`，双击 `sshd_config` 选择完整标识符；
- 原生 Windows 菜单栏与系统色外观，可选择浅色、深色或跟随系统；
- “格式”菜单提供字体切换（默认 Consolas + 等线，中文按等线/黑体渲染）与颜色渲染开关；
- 颜色渲染默认关闭；开启后对 `.ini` / `.env` 文件进行简单语法分析并着色（注释、节名、键、操作符、字符串、数字），其他文件保持纯文本；
- “安全状态”菜单可按需启用 Windows 屏幕捕获保护；该选项默认关闭、仅在本次运行中生效且不写入配置；
- 菜单栏实时显示当前文档安全状态，状态栏显示行列、编码、换行、修改状态和安全内存状态；
- “关于”对话框显示版本、作者、版权和许可证信息；
- 会话锁定、系统挂起、注销或关机时清空文档且不自动保存。

## 查找与双击选词

查找按原文精确匹配（区分大小写，不使用正则表达式），当前结果以正文选区高亮并自动滚动到可见位置。支持重叠匹配；没有结果时显示“未找到匹配项”。关键词限单行、1024 个 UTF-16 单元，关闭查找栏会清除关键词，不保留查找历史。

选词参考 [Scintilla 的字符分类策略](https://scintilla.org/ScintillaDoc.html#SCI_SETWORDCHARS)：连续 Unicode 字母、组合标记、数字和连接标点（含 `_`）组成词块；空白、标点/符号分别成块，换行永远作为边界。`/`、`\`、`.` 和 `-` 都会分隔标识符。使用 Unicode 17.0 静态类别表，按 Unicode 标量扩展，不拆开 UTF-16 代理对；连续中文按同类字符成块，不做词典分词，也不宣称实现完整 UAX #29 自然语言分词。双击命中实际字符，普通单击仍定位到最近光标位置。Unicode 类别数据使用 [Unicode License V3](UNICODE-LICENSE.txt)。

## 命令行启动

构建产物为 `mempad.exe`，只接受以下形式（方括号表示文件名可省略）：

```text
mempad.exe [filename]
```

不带文件名时打开空白文档；提供一个文件名时直接打开该文件。项目不负责把程序加入 `PATH` 或安装到系统目录，额外参数会被拒绝。

## Windows 右键菜单

在“文件”菜单中点击“安装打开方式拓展”，即可为当前 Windows 用户注册 mempad；不需要管理员权限，也不会改变任何后缀的默认打开程序。Windows 11 的现代菜单可通过“打开方式 → mempad”打开文件；点击“显示更多选项”后，也可直接选择“使用 mempad 打开”。Windows 10 的旧式右键菜单会直接显示“使用 mempad 打开”。

安装和卸载均为幂等操作，可以重复执行。安装操作会将已有的 mempad 菜单项更新为当前正在运行的 `mempad.exe` 路径，因此移动程序后只需再次点击安装。卸载只移除 mempad 自己的菜单项，不删除或修改其他程序的关联。`.crt` / `.cer` 等文件如果实际包含不受支持的二进制编码，编辑器会沿用普通文件打开流程并显示错误提示。

实现遵循 Microsoft 的 [Application Registration](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration)、[Open With Registration](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-include-an-application-on-the-open-with-dialog-box)、[File Types](https://learn.microsoft.com/en-us/windows/win32/shell/fa-file-types) 和 [Verb Selection Model](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-employ-the-verb-selection-model) 文档：应用、ProgID、支持类型和 OpenWithProgids 均写入当前用户范围；静态 verb 使用单选模型，并在变更后通知 Windows Shell 刷新关联缓存。

## 测试

Linux 核心测试：运行 `scripts/test-linux.sh`，或使用 CMake 构建 `mempad_tests` 并运行 CTest。

Windows 原生交互测试：CMake 配置时添加 `-DMEMPAD_BUILD_EDITOR_TESTS=ON`，构建后运行 `mempad_editor_tests.exe`。测试窗口保持隐藏，不发送全局键盘输入或操作系统剪贴板；覆盖查找导航、正文/查询焦点、Unicode 输入、双击字符边缘、按词拖选、查找栏绘制和文档清空。

## Credits

GreenShadeCapital Sec Team
