# 安全编辑器实现概要

## 数据流

打开路径：`CreateFileW` → 页保护的原始字节 `SecureAllocation` → 严格编码/换行分析 → 页保护的 UTF-16 解码区域 → `SecureGapBuffer`。原始和解码区域在返回前由 RAII 清零释放。

保存路径：Gap Buffer → 页保护的连续 UTF-16 区域 → 页保护的编码输出区域 → `CreateFileW(CREATE_ALWAYS)` → 循环 `WriteFile` → `FlushFileBuffers`。编码成功后才截断目标文件，不使用临时替换策略。

复制路径：选区从 Gap Buffer 直接写入 Win32 `CF_UNICODETEXT` 所需的 `HGLOBAL`，内部 LF 转为 Windows CRLF，不建立普通堆正文字符串。`SetClipboardData` 成功后内存归 Windows 所有，超出现有安全内存模型；失败时尽力清零仍由应用持有的传输内存。

粘贴路径：Windows `CF_UNICODETEXT` → 有界 NUL 扫描与 UTF-16 校验 → CRLF/CR 规范化 → 临时 `SecureAllocation` → `SecureDocument::replace`。剪切仅在复制成功后删除选区。粘贴正文不进入普通堆，临时安全内存在返回时清零释放。

绘制路径：只把当前可见行的一部分复制到 64 KiB 安全 scratch，以 `GetTextExtentPoint32W` 测量 CJK、代理对和 Tab 的实际像素宽度，调用 `ExtTextOutW` 后立即清零已用范围。不建立完整文档字符串或离屏正文 bitmap。编辑区禁止背景擦除并按行覆盖，避免“先清空、后绘字”导致的频闪。

横向滚动使用像素坐标。布局只缓存每 8192 个 UTF-16 单元的文档偏移与累计像素宽度；命中测试在相邻检查点之间二分查找，不在普通内存中缓存正文。滚动范围、光标、选区和鼠标命中共享同一套测量结果。

## 组件

- `security/SecureAllocation`：保护页、锁页、WER 注册、崩溃清零槽位和可靠释放；
- `document/SecureGapBuffer`：单一安全分配上的 UTF-16 Gap Buffer；
- `document/SecureDocument`：路径、编码、换行、dirty 状态和大小限制；
- `encoding`：严格 UTF-8、UTF-16 LE/BE、BOM、换行规范化和反向编码；
- `io`：Win32 直接文件读写与 COM 文件选择器；
- `editor`：行偏移索引、选择模型、Unicode 字形宽度命中、Tab 显示/插入策略、输入导航和系统色 GDI 自绘；
- `app`：原生菜单、系统主题跟随、安全状态、关于窗口、Explorer 文件拖放、保存提示及锁屏/电源/会话策略。

## 固定限制

```text
输入文件       8 MiB
UTF-16 文档   16 MiB
单行           1 MiB UTF-16 code units
绘制 scratch  64 KiB
崩溃清零槽位  16
```

## 错误策略

核心接口使用 `bool`、`ErrorCode`、Windows 错误码和不含正文的偏移量。Release 使用 `-fno-exceptions -fno-rtti`。错误信息不包含正文、附近字节或当前行。安全 API 失败不降级到普通堆。
