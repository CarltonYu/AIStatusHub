# AIStatusHub 设计文档

## 1. 项目定位

AIStatusHub 是一个运行在个人电脑上的本机状态中枢，用来同步和整理多个 AI 编程工具的运行状态，并将整理后的状态输出到桌面 UI、局域网服务、远程服务器、蓝牙设备或其他硬件显示终端。

它的目标不是替代 Claude Code、Codex、Kimi Code、VS Code 插件或 Hermes Agent，而是作为这些工具旁边的“状态总线”：

```text
Claude Code / Codex / Kimi Code / VS Code AI 插件 / Hermes Agent / 其他 CLI
        ↓
输入适配器 Adapters
        ↓
统一事件总线 + 状态归一化 + 本地存储
        ↓
输出路由 Outputs
        ↓
桌面 UI / Web UI / WebSocket / HTTP / MQTT / BLE / M5Stack / 其他设备
```

核心价值：

- 在一个界面里看见所有 AI 工具当前在做什么。
- 统一展示“思考中、执行工具、等待批准、完成、出错”等状态。
- 把关键事件推送到外部设备，例如 M5Stack StickC Plus、ESP32、手机、局域网屏幕。
- 为不同 AI 工具提供一致的事件日志、统计、审计和通知能力。
- 将来可以扩展成跨机器的 AI 工作台监控系统。

## 2. 设计原则

1. **工具无关**

   不把系统绑定到某一家模型或某一个客户端。每个 AI 工具通过独立 adapter 接入。

2. **输入多源，状态统一**

   不同工具的事件格式不同，进入 Hub 后统一转换成内部事件模型和任务状态模型。

3. **官方接口优先，旁路观察兜底**

   优先使用 hooks、JSON-RPC、VS Code Extension API、OpenAI-compatible proxy 等稳定接口。没有公开接口时，再考虑读取日志、监听终端输出、观察进程、窗口标题等旁路方式。

4. **输出可配置**

   同一个状态可以同时输出到多个地方，例如桌面 UI、WebSocket、HTTP webhook、MQTT topic、BLE 设备。

5. **本地优先**

   默认只在本机运行和存储，用户明确配置后才向局域网或公网发送。

6. **审批安全优先**

   如果支持外部设备批准工具调用，必须有白名单、配对、签名、超时和二次确认机制。

## 3. 典型使用场景

### 3.1 桌面状态面板

用户同时开着 Claude Code、Codex CLI、Kimi Code、VS Code 插件。AIStatusHub 展示：

- 哪些项目正在运行 AI 任务。
- 当前每个工具使用的模型。
- 是否正在调用 shell、编辑文件、等待用户审批。
- 最近完成、失败、取消的任务。
- token、耗时、工具调用次数等统计。

### 3.2 M5Stack 小屏幕显示

AIStatusHub 把聚合状态通过 BLE 发给 M5StickC Plus：

- 空闲：显示 idle。
- 思考中：显示 thinking 动画。
- 执行命令：显示工具名和简短命令。
- 等待审批：屏幕变色并显示 approve/reject。
- 完成或错误：显示摘要和提示音/震动。

### 3.3 局域网状态广播

AIStatusHub 开启 WebSocket 或 MQTT，将状态同步给：

- 另一台电脑。
- NAS 上的 dashboard。
- Home Assistant。
- 树莓派或 ESP32 显示器。
- 自建远程监控服务。

### 3.4 API 代理统计

对 Hermes Agent 或其他 OpenAI-compatible 客户端，可以让它们调用本机代理：

```text
AI Client → http://127.0.0.1:17888/v1/chat/completions → Provider API
```

这样 AIStatusHub 可以记录：

- 模型名。
- 请求开始/结束。
- streaming 状态。
- token usage。
- 错误码。
- 延迟。

## 4. 支持工具与接入方式

### 4.1 Claude Code

优先使用 Claude Code hooks。

可接入事件：

- UserPromptSubmit
- PreToolUse
- PostToolUse
- Notification
- Stop
- SubagentStop
- Permission/approval 相关事件，视当前版本能力而定

建议实现方式：

```text
Claude Code hook → 本地脚本 → AIStatusHub HTTP ingest API
```

示例：

```bash
curl -X POST http://127.0.0.1:17888/ingest/claude-code \
  -H "Content-Type: application/json" \
  -d @event.json
```

参考文档：

- https://code.claude.com/docs/en/agent-sdk/hooks

### 4.2 Codex CLI / Codex App / Codex IDE Extension

Codex 可以分两层接入。

#### 4.2.1 轻量接入：Codex hooks

适合获取生命周期事件：

- SessionStart
- UserPromptSubmit
- PreToolUse
- PermissionRequest
- PostToolUse
- Stop
- SubagentStart
- SubagentStop
- PreCompact
- PostCompact

建议实现方式：

```text
Codex hook → 本地脚本 → AIStatusHub HTTP ingest API
```

Codex hooks 可以放在：

- `~/.codex/hooks.json`
- `~/.codex/config.toml`
- 项目内 `.codex/hooks.json`
- 项目内 `.codex/config.toml`

参考文档：

- https://developers.openai.com/codex/hooks

#### 4.2.2 深度接入：Codex app-server

适合做更完整的客户端级集成：

- 读取 thread/turn 生命周期。
- 获取 streamed agent events。
- 观察 item started/completed。
- 处理 approvals。
- 读取会话和任务结构。

建议实现方式：

```text
AIStatusHub Codex adapter → codex app-server JSON-RPC/WebSocket/stdio
```

参考文档：

- https://developers.openai.com/codex/app-server

### 4.3 Kimi Code CLI

优先使用 Kimi Code hooks。

建议实现方式：

```text
Kimi Code hook → 本地脚本 → AIStatusHub HTTP ingest API
```

配置位置通常为：

- `~/.kimi-code/config.toml`

参考文档：

- https://moonshotai.github.io/kimi-code/en/customization/hooks

注意：

- hooks 适合做通知、记录、轻量拦截。
- 不要把 hooks 当成唯一安全边界。
- 对敏感工具调用审批，Hub 内部还应有自己的安全策略。

### 4.4 VS Code + AI 插件

VS Code 插件生态没有统一的 AI 状态协议。建议做一个 AIStatusHub Companion Extension。

可采集内容：

- 当前 workspace。
- 活动文件。
- 活动 terminal。
- task/debug 状态。
- 通过 VS Code commands API 调用公开命令。
- 读取用户授权范围内的日志。
- 接收其他插件主动发来的事件。

建议不要做：

- 强行读取其他插件的私有 storage。
- 依赖未公开的内部 API。
- Hook 其他插件网络请求，除非用户明确配置代理。

参考文档：

- https://code.visualstudio.com/api/extension-guides/command

### 4.5 Hermes Agent / Mimo API Key / OpenAI-compatible 客户端

对于走 OpenAI-compatible API 的工具，推荐实现 API Proxy adapter。

流程：

```text
Hermes Agent
  ↓
AIStatusHub Local API Proxy
  ↓
Mimo / OpenAI-compatible Provider
```

Hub 可记录：

- provider。
- base_url。
- model。
- request id。
- stream start/end。
- usage。
- latency。
- error。

为了避免泄露密钥：

- API key 默认只保存在本机。
- 日志中永远不记录 Authorization header。
- 请求/响应正文默认只记录元数据，不记录完整 prompt。
- 用户可以显式开启完整日志，但需要醒目的风险提示。

参考：

- https://hermes-ai.net/en/docs/faq/

## 5. 系统架构

### 5.1 进程组成

推荐先做成一个本机服务 + 一个 UI：

```text
ai-status-hub-daemon
  - ingest API
  - adapter manager
  - event bus
  - state reducer
  - storage
  - output router

ai-status-hub-ui
  - desktop UI or web UI
  - settings
  - dashboard
  - event timeline
  - output/device config
```

MVP 阶段可以合并为一个 Tauri/Electron 应用，内部启动本地 HTTP/WebSocket 服务。

### 5.2 推荐技术栈

方案 A：TypeScript 优先

- Runtime: Node.js
- UI: Tauri + React/Vue/Svelte，或 Electron + React
- API: Fastify / Hono / Express
- Storage: SQLite
- Realtime: WebSocket
- BLE: noble 或平台原生桥接
- Config: YAML/TOML/JSON

方案 B：Rust 优先

- Runtime: Rust
- UI: Tauri
- API: axum
- Storage: SQLite/sqlx
- Realtime: WebSocket
- BLE: btleplug
- Config: TOML

推荐：

- MVP 用 TypeScript 更快。
- 后期如果对常驻稳定性、低资源占用、BLE 跨平台更重视，可以迁移核心 daemon 到 Rust。

## 6. 内部数据模型

### 6.1 统一事件 AIEvent

```json
{
  "id": "evt_01H...",
  "source": "claude-code",
  "surface": "cli",
  "workspace": "C:/projects/demo",
  "session_id": "session_123",
  "turn_id": "turn_456",
  "event_type": "tool.started",
  "status": "tool_running",
  "model": "claude-opus",
  "tool": {
    "name": "Bash",
    "input_preview": "npm test"
  },
  "message": "Running npm test",
  "metrics": {
    "tokens_in": 1200,
    "tokens_out": 300,
    "latency_ms": 4200
  },
  "severity": "info",
  "raw": {},
  "created_at": "2026-06-04T12:00:00+08:00"
}
```

### 6.2 统一任务状态 AITaskState

```json
{
  "key": "claude-code:C:/projects/demo:session_123",
  "source": "claude-code",
  "workspace": "C:/projects/demo",
  "session_id": "session_123",
  "turn_id": "turn_456",
  "title": "Refactor auth module",
  "model": "claude-opus",
  "status": "waiting_approval",
  "active_tool": "Bash",
  "last_message": "Approve command: npm test",
  "started_at": "2026-06-04T11:58:00+08:00",
  "updated_at": "2026-06-04T12:00:00+08:00",
  "counters": {
    "tool_calls": 4,
    "errors": 0,
    "approval_requests": 1
  }
}
```

### 6.3 状态枚举

基础状态：

- `idle`
- `starting`
- `thinking`
- `streaming`
- `tool_running`
- `waiting_approval`
- `blocked`
- `completed`
- `error`
- `cancelled`
- `compacting`
- `unknown`

事件类型：

- `session.started`
- `session.resumed`
- `prompt.submitted`
- `turn.started`
- `message.delta`
- `message.completed`
- `tool.started`
- `tool.completed`
- `approval.requested`
- `approval.approved`
- `approval.rejected`
- `turn.completed`
- `turn.failed`
- `turn.cancelled`
- `notification`
- `metrics.updated`

## 7. Ingest API 设计

### 7.1 单事件写入

```http
POST /api/v1/ingest/event
Content-Type: application/json
Authorization: Bearer <local-token>
```

Body:

```json
{
  "source": "codex",
  "event_type": "tool.started",
  "workspace": "C:/projects/demo",
  "session_id": "abc",
  "tool": {
    "name": "Bash",
    "input_preview": "npm test"
  },
  "raw": {}
}
```

### 7.2 批量写入

```http
POST /api/v1/ingest/batch
```

### 7.3 当前状态查询

```http
GET /api/v1/state
GET /api/v1/state?source=codex
GET /api/v1/state?workspace=C:/projects/demo
```

### 7.4 事件流

```http
GET /api/v1/events/stream
```

可以用：

- WebSocket
- Server-Sent Events

### 7.5 输出设备控制

```http
GET /api/v1/outputs
POST /api/v1/outputs/:id/test
PATCH /api/v1/outputs/:id
```

## 8. 配置文件设计

建议配置文件：

- Windows: `%APPDATA%/AIStatusHub/config.toml`
- macOS: `~/Library/Application Support/AIStatusHub/config.toml`
- Linux: `~/.config/aistatushub/config.toml`

示例：

```toml
[server]
host = "127.0.0.1"
port = 17888
auth_token_env = "AI_STATUS_HUB_TOKEN"

[storage]
sqlite_path = "aistatushub.sqlite3"
retain_days = 30
store_raw_events = true
store_prompt_text = false

[[adapters]]
id = "claude-code"
type = "hook-http"
enabled = true

[[adapters]]
id = "codex-hooks"
type = "hook-http"
enabled = true

[[adapters]]
id = "codex-app-server"
type = "codex-app-server"
enabled = false
listen = "stdio"

[[adapters]]
id = "openai-compatible-proxy"
type = "api-proxy"
enabled = true
listen_port = 17889
upstream_base_url = "https://api.example.com/v1"
api_key_env = "MIMO_API_KEY"

[[outputs]]
id = "desktop-ui"
type = "ui"
enabled = true

[[outputs]]
id = "lan-websocket"
type = "websocket"
enabled = false
bind = "0.0.0.0"
port = 17890

[[outputs]]
id = "m5stickc-plus"
type = "ble"
enabled = false
device_name = "AIStatusBuddy"
service_uuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
characteristic_uuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

[[routes]]
match_status = ["waiting_approval", "error", "completed"]
outputs = ["desktop-ui", "m5stickc-plus"]
```

## 9. 输出系统设计

### 9.1 桌面 UI

核心页面：

- Overview：所有 AI 工具状态总览。
- Sessions：按 workspace/session 展示任务。
- Timeline：事件时间线。
- Approvals：等待审批队列。
- Devices：输出设备配置。
- Settings：adapter、token、隐私、安全配置。

UI 关键元素：

- 每个任务一个状态行。
- 用颜色和图标区分状态。
- 支持搜索 workspace、模型、工具名。
- 对等待审批的任务高亮。
- 展示最近错误和最近完成任务。

### 9.2 Web UI

本机访问：

```text
http://127.0.0.1:17888
```

如果用户开启局域网访问：

```text
http://0.0.0.0:17888
```

局域网访问必须要求 token 或登录。

### 9.3 WebSocket 输出

用于其他程序订阅：

```json
{
  "type": "state.updated",
  "data": {
    "source": "codex",
    "status": "tool_running",
    "workspace": "C:/projects/demo",
    "message": "Running tests"
  }
}
```

### 9.4 HTTP Webhook

配置示例：

```toml
[[outputs]]
id = "remote-server"
type = "webhook"
enabled = true
url = "https://example.com/aistatushub/events"
secret_env = "AI_STATUS_WEBHOOK_SECRET"
```

签名建议：

```text
X-AIStatusHub-Timestamp: 2026-06-04T12:00:00+08:00
X-AIStatusHub-Signature: sha256=<hmac>
```

### 9.5 MQTT

Topic 设计：

```text
aistatushub/state/all
aistatushub/state/{source}
aistatushub/workspace/{workspace_hash}
aistatushub/approval
```

适合接入 Home Assistant、Node-RED、ESP32 网关。

### 9.6 BLE / M5Stack

推荐协议：

- 电脑作为 BLE Central。
- M5Stack/ESP32 作为 BLE Peripheral。
- 使用 Nordic UART Service 或自定义 GATT。
- Hub 发送简短 JSON 或二进制 frame。

示例 payload：

```json
{
  "v": 1,
  "status": "waiting_approval",
  "source": "codex",
  "title": "Bash approval",
  "text": "npm test",
  "color": "#F59E0B"
}
```

M5Stack 端建议只负责显示和按键输入，不保存敏感内容。

### 9.7 外部设备回传

M5Stack 按键可以回传：

```json
{
  "v": 1,
  "device_id": "m5stickc-plus-001",
  "action": "approve",
  "target": "approval_123",
  "nonce": "..."
}
```

安全要求：

- 设备需要配对。
- 每个 approval 有短期 nonce。
- approval 超时后失效。
- 高风险命令必须在电脑 UI 二次确认。
- 可以配置哪些工具允许硬件审批。

## 10. 安全与隐私

### 10.1 默认隐私策略

默认只记录：

- source。
- workspace 路径，可选择 hash 化。
- session/turn id。
- 状态。
- 工具名。
- 命令或文件路径的短 preview。
- token usage。
- latency。
- error summary。

默认不记录：

- 完整 prompt。
- 完整模型回复。
- API key。
- Authorization header。
- 完整文件内容。
- 完整 shell 输出。

### 10.2 本机服务安全

- 默认绑定 `127.0.0.1`。
- 局域网绑定需要用户显式开启。
- 所有写入 API 使用本地 token。
- Webhook 使用 HMAC 签名。
- 配置页显示敏感项时默认打码。

### 10.3 审批安全

审批功能分级：

1. `display_only`：只展示，不允许外部批准。
2. `low_risk_approve`：允许批准白名单命令。
3. `confirm_on_desktop`：外部设备只能发起批准意图，电脑端二次确认。
4. `disabled`：完全禁用外部审批。

默认使用 `display_only`。

## 11. MVP 范围

第一版建议只做这些：

1. 本机 daemon。
2. SQLite 存储。
3. HTTP ingest API。
4. WebSocket event stream。
5. Web UI dashboard。
6. Claude Code hook adapter。
7. Codex hook adapter。
8. Kimi Code hook adapter。
9. 简单 webhook output。
10. 配置文件和启动脚本。

暂不做：

- 深度 VS Code 插件。
- Codex app-server 完整客户端。
- BLE 双向审批。
- 多用户权限系统。
- 云端账号同步。

这样可以很快形成一个 GitHub 可运行项目。

## 12. 版本路线图

### v0.1 本机状态 Hub

- 本地服务。
- Web UI。
- HTTP ingest。
- SQLite。
- Claude/Codex/Kimi hook 示例。
- 状态归一化。

### v0.2 输出路由

- WebSocket。
- Webhook。
- MQTT。
- 输出规则配置。
- 通知过滤。

### v0.3 API Proxy

- OpenAI-compatible proxy。
- token/latency/usage 统计。
- provider 配置。
- Hermes Agent/Mimo 接入示例。

### v0.4 VS Code Companion Extension

- workspace 状态。
- active file 状态。
- terminal/task 状态。
- 和 Hub 的 WebSocket 双向通信。

### v0.5 BLE/M5Stack

- BLE 输出。
- M5StickC Plus 固件。
- 状态动画。
- 按键回传。
- 安全审批策略。

### v1.0 稳定版

- 插件化 adapter/output。
- 配置 UI。
- 安全审计。
- 安装包。
- Windows/macOS/Linux 支持。

## 13. 建议仓库结构

```text
AIStatusHub/
  README.md
  docs/
    design.md
    adapters.md
    outputs.md
    security.md
    m5stack.md
  apps/
    desktop/
    web/
  packages/
    core/
    daemon/
    adapters/
      claude-code/
      codex-hooks/
      kimi-code/
      openai-proxy/
    outputs/
      websocket/
      webhook/
      mqtt/
      ble/
    shared/
  examples/
    claude-code-hooks/
    codex-hooks/
    kimi-code-hooks/
    hermes-proxy/
    m5stickc-plus-firmware/
  scripts/
  tests/
```

## 14. Hook 脚本示例

### 14.1 通用 Node.js 上报脚本

```js
#!/usr/bin/env node

const chunks = [];

process.stdin.on("data", (chunk) => chunks.push(chunk));
process.stdin.on("end", async () => {
  const rawText = Buffer.concat(chunks).toString("utf8");
  let raw = {};

  try {
    raw = rawText ? JSON.parse(rawText) : {};
  } catch {
    raw = { rawText };
  }

  const event = {
    source: process.env.AI_STATUS_SOURCE || "unknown",
    event_type: process.env.AI_STATUS_EVENT || "hook.event",
    workspace: process.cwd(),
    raw,
    created_at: new Date().toISOString()
  };

  await fetch("http://127.0.0.1:17888/api/v1/ingest/event", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "Authorization": `Bearer ${process.env.AI_STATUS_HUB_TOKEN || ""}`
    },
    body: JSON.stringify(event)
  });
});
```

### 14.2 Codex hooks.json 示例

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "node C:/ai/aistatushub/hooks/report.js",
            "timeout": 10,
            "statusMessage": "Reporting AI status"
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "node C:/ai/aistatushub/hooks/report.js",
            "timeout": 10
          }
        ]
      }
    ],
    "PermissionRequest": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "node C:/ai/aistatushub/hooks/report.js",
            "timeout": 10,
            "statusMessage": "Reporting approval request"
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "node C:/ai/aistatushub/hooks/report.js",
            "timeout": 10
          }
        ]
      }
    ]
  }
}
```

实际实现时应给不同事件传入不同环境变量，或者为每种事件生成单独 wrapper。

## 15. 风险与限制

1. **不同工具的状态粒度不同**

   Claude Code、Codex、Kimi Code 能通过 hooks 拿到较清晰的生命周期事件；VS Code 插件则取决于插件是否开放接口。

2. **旁路观察不稳定**

   读取日志、终端输出、窗口标题等方法可能随版本变化而失效，只能作为补充。

3. **BLE 跨平台复杂**

   Windows、macOS、Linux 的 BLE API 差异明显。MVP 可以先做 WebSocket/MQTT，再做 BLE。

4. **审批能力要克制**

   外部硬件批准 AI 工具调用很酷，但风险也高。默认只展示状态，不直接批准。

5. **完整 prompt 日志有隐私风险**

   默认不要保存完整 prompt/response。需要用户主动开启。

## 16. 下一步建议

创建 GitHub 项目后，第一批 issue 可以这样拆：

1. 初始化 monorepo。
2. 实现 daemon HTTP ingest API。
3. 定义 AIEvent 和 AITaskState schema。
4. 实现 SQLite 存储。
5. 实现 WebSocket state stream。
6. 实现 Web UI Overview 页面。
7. 添加 Codex hook 示例。
8. 添加 Claude Code hook 示例。
9. 添加 Kimi Code hook 示例。
10. 添加 webhook output。
11. 编写安全与隐私文档。
12. 设计 M5Stack BLE payload。

推荐先完成一个非常小但能跑通的闭环：

```text
Codex hook → AIStatusHub ingest API → Web UI 实时显示 → WebSocket 输出
```

闭环跑通后，再逐个增加 Claude Code、Kimi Code、Hermes proxy、M5Stack。

