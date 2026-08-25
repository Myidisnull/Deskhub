# develop 客制化功能清单

权威说明：本文件记录 **`develop` 相对 `main` 的产品客制**。同步上游时不得破坏下列功能、配置、布局与 UI（见 `.cursor/rules/develop-main-parallel.mdc`）。
版本锚点：`VERSION` = **4.0.2** · 品牌文件 `brand/Brand.json` · 对照 tip 时以本文件 + `customize-main-base.md` 为准。

上次整理：2026-08-25

---

## 1. 产品定位

| 项 | develop（客制） | main（上游） |
| --- | --- | --- |
| 显示名 | **System Runtime** | Deskhub |
| 版本号 | 4.0.2 | 5.x |
| 数据目录 | `.system-runtime` | Deskhub 默认路径 |
| 内部 code_name | 仍可为 Deskhub（日志/工程） | — |

---

## 2. 客制新增 / 强化的功能

### 2.1 品牌与文案

- `brand/Brand.json`：`product_name`、`windows_service_name`、`data_dir_name`、`log_file_prefix`、自启任务名、广播扩展名等
- `core/include/deskhub/ui/Brand.h` + 文案 `{app}` / `{service}` 占位
- 各端商店/应用标题走 System Runtime（Android / iOS metadata 等）

### 2.2 多语言（i18n）

- `core`：`Locale.h` / `Locale.cpp` / `LocaleCatalog.cpp`，`UiSettings.language`
- UI 语言选项（含跟随系统）：Windows / macOS / Linux / iOS / Android
- Apple 共享：`client/apple/swift/AppLanguage.swift`
- 字符串以 `LStr` + 翻译目录为主，而非 main 硬编码英文为主的终端/配对文案集

### 2.3 会话加密（不局限实现方式，设置）

- 设置项：`encryptSession`、`escrowSessionKey`、`sessionKeyLifetime`、`sessionKeyHex`、`hostStaticSkHex`
- 出现在：Windows 设置、macOS Settings、Linux 设置、iOS Sharing、Android 连接相关字段
- 连接流程可携带 / 校验 session key（相对 main 的配对信任模型）

### 2.4 后台运行与托盘

- 设置：`runInBackground`、`runInBackgroundChoiceMade`、`hideTrayIcon`
- Windows：`BackgroundPrompt`、`QuitBusyPrompt`
- macOS：`AppLifecycle`、`BackgroundPromptSheet`、`TrayController`
- Linux：后台开关、`hideTrayIcon`、首次关窗 `BackgroundPrompt` + `ApplyTrayMode` / 托盘

### 2.5 日志策略 UI

- `logMaxFileMb` / `logCompressAfterDays` / `logDeleteAfterDays` / `logDir`
- 设置页「日志」区块：各桌面端完整（含目录/浏览/查看）；Android / iOS 提供保留三字段（目录浏览与日志查看仍为桌面向）

### 2.6 Windows 主题与体验

- `client/windows/win32/AppTheme.h`（明暗主题，替代已删除的 `WxUi.h`）
- 观看到 GPU/打开失败等客制提示文案

## 3. 维护约定

1. 新增客制功能或砍掉能力时：**同步更新本文件**（同一变更集）。
2. 从 `main` 选择性移植前：冲突则以本清单为准。
3. 不要用 merge 把 `main` 的 UI/终端/配对面冲进 `develop`。
