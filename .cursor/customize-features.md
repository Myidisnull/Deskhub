# develop 客制化功能清单

权威说明：本文件记录 **`develop` 相对 `main` 的产品客制**。同步上游时不得破坏下列功能、配置、布局与 UI（见 `.cursor/rules/develop-main-parallel.mdc`）。
版本锚点：`VERSION` = **5.2.0**（与 main 对齐版本号；**品牌仍为 System Runtime**）· 品牌文件 `brand/Brand.json` · 对照 tip 时以本文件 + `customize-main-base.md` 为准。

上次整理：2026-08-26（P0–P3 + `deskhub-cli` 全量 Adapt 收尾）

---

## 1. 产品定位

| 项 | develop（客制） | main（上游） |
| --- | --- | --- |
| 显示名 | **System Runtime** | Deskhub |
| 版本号 | **5.2.0**（跟主站号；文案不跟 Deskhub） | 5.2.0 |
| 数据目录 | `.system-runtime` | Deskhub 默认路径 |
| 内部 code_name | 仍可为 Deskhub（日志/工程） | — |

---

## 2. 客制新增 / 强化的功能

### 2.1 品牌与文案

- `brand/Brand.json`：`product_name`、`windows_service_name`、`data_dir_name`、`log_file_prefix`、自启任务名、广播扩展名等
- `core/include/deskhub/ui/Brand.h` + 文案 `{app}` / `{service}` 占位
- 各端商店/应用标题走 System Runtime（Android / iOS metadata 等）
- **VERSION 可与 main 同号；商店标题 / README 品牌不得改回 Deskhub**

### 2.2 多语言（i18n）

- `core`：`Locale.h` / `Locale.cpp` / `LocaleCatalog.cpp`，`UiSettings.language`
- UI 语言选项（含跟随系统）：Windows / macOS / Linux / iOS / Android
- Apple 共享：`client/apple/swift/AppLanguage.swift`
- 字符串以 `LStr` + 翻译目录为主

### 2.3 会话加密（不局限实现方式，设置）

- 设置项：`encryptSession`、`escrowSessionKey`、`sessionKeyLifetime`、`sessionKeyHex`、`hostStaticSkHex`
- 出现在：Windows 设置、macOS Settings、Linux 设置、iOS Sharing、Android 连接相关字段
- 连接流程可携带 / 校验 session key
- **配对 UX 为增量 Adapt**：可增加 Devices / Trust 面，**不得删除**本会话加密主路径

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
3. 不要用 merge 把 `main` 整树冲进 `develop`；点名主题用 **Adapt**。

## 4. 用户已点名 · Adapt 队列（2026-08-25）

下列主题用户已明确要求加入 develop。仍 **禁止** `main`↔`develop` merge / 整包 HostLink·Deskhub 品牌替换。

| 主题 | Adapt 方式（develop 形） | 状态 |
| --- | --- | --- |
| Shell / 终端 | 自建 **UDP** 终端通道 + `core/terminal` + 各端 UI；connect-once 可加 Shell 钮；**不**依赖 HostLink/QUIC | **已做**：全平台主机 `shareTerminal` + Apple/Android/Win/Linux 客户端；有桌面源时 Desktop/Shell 选择（Win/Linux/Apple） |
| 配对 UX | 移植 TrustStore / Devices / PairingAsks 为**可选面**；与 §2.3 会话加密**并存**；默认连接仍可用口令+密钥 | **已做（develop 形）**：`PairingHello`/`PairingResult` UDP + `HostPairingBroker`；Settings 内配对列表 + `allowNewPairings`（Win/Linux/macOS/iOS/Android）；口令路径仍可用 |
| HostLink | **不**迁入 `HostLink` 树；把「一次 dial、多 surface」意图继续落在 UDP connect-once（Desktop/Files/Shell） | **已做（develop 形）**：Desktop/Files/Shell 均走 UDP connect-once；Android Connected 面也有 Open shell |
| VERSION 5.x | `VERSION` → **5.2.0**；保留 System Runtime / `.system-runtime` / 商店标题 | **已做**：Android `versionName` 读 `VERSION`；Apple `MARKETING_VERSION` = 5.2.0 |
| fuzz 噪声 | Port `core/fuzz/seeds`（及已有目标的 regressions）；`FuzzTerm` 随终端 core 已挂 | **已做**（与 `origin/main` tip 对齐） |
| deskhub-cli | `client/cli` 接 UDP（`AgentLoop`/`TerminalViewer`/`FileTransferClient`）；`make build-cli`；无 HostLink/QUIC | **已做（develop 形）**：Win `deskhub_win_view`；Linux `deskhub_linux_core` + X11/`ClientEngine`；`cli-smoke` |
| core_perf CI | Port `core/perf`；CI release 跑 `core_perf`；**不**引入 quiche / `platform_perf` | **已做（develop 形）** |

仍默认 Skip（未点名）：main 专属 `clientShell`/`allowNewPairings` 设置名原样替换客制设置键、Deskhub 商店文案/图标整包、`platform_perf`/QUIC。
