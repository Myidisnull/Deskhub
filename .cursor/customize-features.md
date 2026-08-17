# develop 客制化功能清单

权威说明：本文件记录 **`develop` 相对 `main` 的产品客制**。同步上游时不得破坏下列功能、配置、布局与 UI（见 `.cursor/rules/develop-main-parallel.mdc`）。  
版本锚点：`VERSION` = **4.0.2** · 品牌文件 `brand/Brand.json` · 对照 tip 时以本文件 + `customize-main-base.md` 为准。

上次整理：2026-08-17

---

## 1. 产品定位

| 项 | develop（客制） | main（上游） |
| --- | --- | --- |
| 形态 | 白标远程桌面 / 投屏主机+观看端 | Deskhub 完整能力面 |
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

### 2.3 会话加密

- 设置项：`encryptSession`、`escrowSessionKey`、`sessionKeyLifetime`、`sessionKeyHex`、`hostStaticSkHex`
- 出现在：Windows 设置、macOS Settings、Linux 设置、iOS Sharing、Android 连接相关字段
- 连接流程可携带 / 校验 session key（相对 main 的配对信任模型）

### 2.4 后台运行与托盘

- 设置：`runInBackground`、`runInBackgroundChoiceMade`、`hideTrayIcon`
- Windows：`BackgroundPrompt`、`QuitBusyPrompt`
- macOS：`AppLifecycle`、`BackgroundPromptSheet`、`TrayController`
- Linux：后台开关 + `ApplyTrayMode` / 托盘

### 2.5 日志策略 UI

- `logMaxFileMb` / `logCompressAfterDays` / `logDeleteAfterDays` / `logDir`
- 设置页「日志」区块（各桌面端）

### 2.6 Windows 主题与体验

- `client/windows/win32/AppTheme.h`（明暗主题，替代已删除的 `WxUi.h`）
- 观看到 GPU/打开失败等客制提示文案

### 2.7 工程向强化（非独立产品页，但属本分支习惯）

- 多端 `AgentLoop`、Windows 采集 / NVENC·MF 编码、HostEngine / StallLog 等稳定性改动
- 协议侧已按需吸收：`HostCaps`（`terminal` 在本分支恒为 false）、InputApplier 接管仍放键

### 2.8 会话期间防休眠（keepAwake）

- 设置：`UiSettings.keepAwake`（默认开），键名 `keep_awake`
- 平台：`deskhubp/system/KeepAwake.h` + Win/Mac/Linux/None
- 引擎：`HostEngine` / `ClientEngine` 在会话起止时 Acquire/Release
- 各端设置开关；Android / iOS 观看页按设置控制屏幕常亮
- 与品牌/加密/托盘/三页导航无冲突；相对 main 为误砍后补回

---

## 3. 相对 main 明确不提供的能力（客制边界）

同步 `main` 时默认 **不要恢复**：

| 能力 | 说明 |
| --- | --- |
| 远程终端 / PTY / Stop & Attach | 各端 Terminal*、`TerminalFfi`、TermGrid 等已删 |
| 配对 / 信任设备 UI | 无 TrustPrompt、配对弹窗、已配对设备管理页 |
| Devices 导航 | 侧栏无 Devices；无独立 Devices 页（Client 内近期设备保留） |
| main 专属设置 | 无 `clientShell`、`clientDesktop`、`startHidden`、`allowNewPairings`（`keepAwake` 已补回） |
| QUIC 缺失产品文案 | 无 `kShareNoQuicLibrary` 一类提示；不以恢复 quiche 脚本为默认 |

口令：本分支以 **4 位分享口令** 为主叙事；不要改回 main 的「可选配对口令 + 信任」主流程。

---

## 4. 配置清单（`UiSettings`）

**本分支使用：**

- 共享：`fps`、`bitrateMbps`、`maxDim`、`port`、`allowInput`、`clientControl`、`passcode`、`deviceName`、`bindIp`、`autostart`、`autoShare`、`clipboardSync`、`keepAwake`
- 客制：`language`、`encryptSession` 及密钥相关、`runInBackground*`、`hideTrayIcon`、日志四字段

**不要从 main 加回（除非产品明确要求）：**  
`clientShell`、`clientDesktop`、`startHidden`、`allowNewPairings`  
（`keepAwake` 已是本分支能力）

权威头文件：`core/include/deskhub/ui/UiSettings.h`

---

## 5. 布局 / UI 结构（客制）

### 导航（桌面）

- **三页**：Host（共享）· Client（连接）· Settings  
- **不要**恢复 Devices 第四页或 Terminal 路由

### Windows（`client/windows/win32`）

- 保留：`MainFrame` 三页导航、`AppTheme`、`BackgroundPrompt`、`QuitBusyPrompt`
- 禁止默认加回：`TerminalWindow`、Devices/配对列表、`WxUi.h`

### macOS（`client/macos/app/swift`）

- 保留：托盘与生命周期、设置中的语言/加密/后台/日志、Connect 内联侧栏
- Host 表：**无**终端行；无配对告警主路径
- 禁止默认加回：`TerminalModel`/`TerminalScreen`、`DevicesPage`、`MainMenuSidebar`（已内联）、`.terminal` 路由

### Linux GTK

- 保留：语言、加密、后台+托盘、近期设备/LAN 列表
- 禁止默认加回：`TerminalWindow`、配对/Devices 流程

### Android / iOS

- 保留：System Runtime 品牌、语言、口令 + session key 连接/分享设置
- 禁止默认加回：Terminal Activity / NativeTerminal、iOS Devices 页、TrustPrompt、Terminal 屏

---

## 6. 关键文件索引

| 用途 | 路径 |
| --- | --- |
| 品牌 | `brand/Brand.json`、`core/include/deskhub/ui/Brand.h` |
| 设置 | `core/include/deskhub/ui/UiSettings.h` |
| 语言 | `core/include/deskhub/ui/Locale.h`、`core/src/ui/Locale*.cpp` |
| 文案 | `core/include/deskhub/ui/Strings.h` |
| 上游 pin | `.cursor/customize-main-base.md` |
| 分支规则 | `.cursor/rules/develop-main-parallel.mdc`、`customize-from-main.mdc`、`main-sync-pin.mdc` |

---

## 7. 维护约定

1. 新增客制功能或砍掉能力时：**同步更新本文件**（同一变更集）。  
2. 从 `main` 选择性移植前：对照第 2–5 节，冲突则以本清单为准。  
3. 不要用 merge 把 `main` 的 UI/终端/配对面冲进 `develop`。
