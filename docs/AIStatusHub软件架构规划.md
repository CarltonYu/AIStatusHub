# AIStatusHub 软件架构规划

本文档基于 [AIStatusHub 设计文档](./AIStatusHub设计.md)，将实现路线收敛为 Rust 单二进制方案。新的核心目标是：

- 开销尽可能小。
- 编译为 Windows/macOS/Linux 可执行程序。
- 一个 `config.toml` 即可启动和接入。
- 默认只保留当前状态，不保存完整事件日志。
- 支持多个 AIStatusHub 汇聚到一个 AIStatusHub，并保留多层来源链。

## 1. 产品形态

AIStatusHub 先实现为单个常驻进程：

```text
aistatushub
  ├─ HTTP ingest API
  ├─ WebSocket state stream
  ├─ in-memory current state store
  ├─ optional compact state snapshot
  ├─ Hub-to-Hub receiver
  ├─ Hub-to-Hub forwarder
  ├─ stdout print output
  ├─ report subcommand
  ├─ OpenAI-compatible proxy
  └─ minimal built-in web page
```

默认启动方式：

```bash
aistatushub --config ./config.toml
```

默认监听：

```text
http://127.0.0.1:17888
```

## 2. 非目标

第一阶段暂不实现：

- 完整桌面壳。
- 复杂前端应用。
- SQLite 事件库。
- 全量 prompt / response / raw payload 保存。
- 多用户账号系统。
- BLE / M5Stack 双向审批。
- VS Code 插件。

这些能力可以后续添加，但不能破坏“低开销、单配置、当前状态优先”的主线。

## 3. 低开销原则

默认行为：

- 所有事件只在内存流水线中短暂存在。
- 只维护每个任务的当前 `TaskState`。
- Activity 使用固定大小 ring buffer，默认 300 条。
- 不持久化完整事件日志。
- 不持久化 raw payload。
- 不保存完整 prompt / response。
- 状态快照使用 debounce 写入，默认关闭或低频写入。
- Hub-to-Hub 只同步当前状态、状态删除、短活动摘要和 heartbeat。

推荐默认内存结构：

```text
HashMap<TaskKey, TaskState>
VecDeque<ActivityItem>
HashMap<HubId, HubPeer>
```

## 4. 技术栈

MVP 使用 Rust：

- HTTP/WebSocket：`axum`
- async runtime：`tokio`
- JSON/TOML：`serde`、`serde_json`、`toml`
- 配置路径：`directories`
- 日志：`tracing`、`tracing-subscriber`
- ID：`uuid`
- 时间：`time`
- 签名：`hmac`、`sha2`
- 错误处理：`thiserror`、`anyhow`

暂不使用数据库。当前状态可选落地为一个小型 JSON 快照文件：

```text
state.snapshot.json
```

后续如果确实需要复杂查询，再引入 SQLite；默认路径不依赖 SQLite。

## 5. 仓库结构

```text
AIStatusHub/
  Cargo.toml
  README.md
  config.example.toml
  docs/
    AIStatusHub设计.md
    AIStatusHub软件架构规划.md
  src/
    main.rs
    config.rs
    model.rs
    state.rs
    ingest.rs
    hub.rs
    api.rs
    snapshot.rs
    security.rs
    web.rs
  tests/
```

## 6. 配置文件

示例：

```toml
[server]
host = "127.0.0.1"
port = 17888
auth_token = "dev-local-token"

[hub]
hub_id = "hub_auto_generated"
display_name = "My Laptop"
accept_remote_hubs = false
max_hops = 5

[privacy]
store_raw_events = false
store_prompt_text = false
hash_workspace = true
max_preview_length = 180

[state]
activity_ring_size = 300
snapshot_enabled = true
snapshot_path = "state.snapshot.json"
snapshot_debounce_ms = 1000
remote_state_ttl_ms = 120000

[proxy]
enabled = false
source = "hermes-agent"
anthropic_source = "vscode-claude-code"
kimi_source = "vscode-kimi-code"
source_header = "x-aistatushub-source"
upstream_base_url_header = "x-aistatushub-upstream-base-url"
upstream_base_url = "https://api.example.com/v1"
anthropic_upstream_base_url = "https://api.anthropic.com"
kimi_upstream_base_url = "https://api.kimi.com/coding/v1"
api_key = ""
timeout_secs = 120

[[trusted_hubs]]
hub_id = "hub_desktop_01"
display_name = "Desktop"
token = "shared-secret"
allow_relay = true

[[outputs]]
id = "console"
type = "stdout"
enabled = true
format = "json"

[[outputs]]
id = "duo-face"
type = "irisoled-udp"
enabled = false
url = "udp://192.168.42.1:25250"
busy_expression = "angry"
idle_expression = "normal"
include_remote_states = false

[[outputs]]
id = "home-server"
type = "hub-forward"
enabled = false
url = "http://192.168.1.10:17888/api/v1/hub/state"
target_hub_id = "hub_home_server_01"
token = "shared-secret"
send_snapshot_on_start = true
include_remote_states = false
```

配置原则：

- 没有配置文件时可以生成最小默认配置。
- `hub_id` 首次运行自动生成并写回配置或本地 identity 文件。
- 局域网监听和远端 Hub 接收默认关闭。
- 敏感 token 后续可支持从环境变量读取；MVP 先支持直接配置。

## 7. 数据模型

### 7.1 来源概念

AIStatusHub 必须区分两类来源：

- `source`：AI 工具来源，例如 `codex`、`claude-code`、`kimi-code`。
- `provenance`：Hub 来源链，表示状态由哪个 AIStatusHub 首次观测，以及经过哪些 Hub 转发。

### 7.2 TaskState

`TaskState` 是默认唯一需要长期保留的业务状态。

关键字段：

```text
key
locality
source
origin_hub_id
origin_hub_name
received_from_hub_id
hub_path
workspace
workspace_hash
session_id
turn_id
title
model
status
active_tool
last_message
counters
started_at
updated_at
last_seen_at
expires_at
```

`key` 生成规则：

```text
{origin_hub_id}:{source}:{workspace_hash}:{session_id_or_fallback}
```

`origin_hub_id` 必须进入 key，避免多个 Hub 上的相同 workspace/session 冲突。

### 7.3 状态枚举

```text
idle
starting
thinking
streaming
tool_running
waiting_approval
blocked
completed
error
cancelled
compacting
unknown
stale
```

### 7.4 Hub 来源链

```text
HubProvenance
  origin_hub_id
  origin_hub_name
  received_from_hub_id
  hub_path[]
  hop_count
```

规则：

- 本机观测状态：`origin_hub_id = current_hub_id`。
- Hub 转发状态：只能在 `hub_path` 尾部追加自己。
- 接收远端状态：如果 `hub_path` 已包含当前 `hub_id`，必须拒绝，避免循环。
- 默认 `max_hops = 5`。

## 8. Hub-to-Hub 协议

Hub-to-Hub 不转发完整事件，只转发状态 envelope：

```json
{
  "schema_version": 1,
  "message_id": "hubmsg_...",
  "message_type": "state.upsert",
  "target_hub_id": "hub_home_server_01",
  "sent_at": "2026-06-04T12:00:00Z",
  "sender_hub": {
    "hub_id": "hub_laptop_01",
    "display_name": "Laptop"
  },
  "provenance": {
    "origin_hub_id": "hub_laptop_01",
    "origin_hub_name": "Laptop",
    "hub_path": [
      { "hub_id": "hub_laptop_01", "display_name": "Laptop" }
    ],
    "hop_count": 1
  },
  "state": {
    "key": "hub_laptop_01:codex:ws_abcd:session_123",
    "locality": "local",
    "source": "codex",
    "origin_hub_id": "hub_laptop_01",
    "workspace_hash": "ws_abcd",
    "session_id": "session_123",
    "status": "tool_running",
    "last_message": "Running tests",
    "updated_at": "2026-06-04T12:00:00Z"
  }
}
```

消息类型：

- `state.upsert`
- `state.delete`
- `state.snapshot`
- `activity.compact`
- `heartbeat`

安全要求：

- Hub-to-Hub 接收默认关闭。
- 只接受 `trusted_hubs` 白名单。
- 校验 bearer token。
- 校验 `target_hub_id`。
- 校验 `hub_path` 防环。
- 后续可加 HMAC timestamp 签名。

## 9. API

### 9.1 本机事件写入

```http
POST /api/v1/ingest/event
Authorization: Bearer <token>
Content-Type: application/json
```

Body：

```json
{
  "source": "codex",
  "event_type": "tool.started",
  "workspace": "D:/code/demo",
  "session_id": "session_123",
  "tool": {
    "name": "Shell",
    "input_preview": "cargo test"
  },
  "message": "Running tests"
}
```

### 9.2 查询

```http
GET /api/v1/state
GET /api/v1/state/{key}
GET /api/v1/activity
GET /api/v1/hubs
GET /health
```

### 9.3 实时流

```http
GET /api/v1/events/stream
```

使用 WebSocket。

### 9.4 Hub-to-Hub

```http
POST /api/v1/hub/state
POST /api/v1/hub/snapshot
```

### 9.5 OpenAI-Compatible Proxy

用于 Hermes Agent / Mimo 等 OpenAI-compatible 客户端：

```http
POST /v1/chat/completions
GET /v1/models
ANY /v1/{path}
```

代理行为：

- 请求开始时写入 `source = "hermes-agent"`、`status = thinking`。
- 如果请求带 `x-aistatushub-source`，使用该值覆盖默认 `source`，用于区分 `vscode-codex`、`hermes-agent` 等来源。
- 如果请求带 `x-aistatushub-upstream-base-url`，使用该值作为本次请求上游；这两个内部头不会继续透传到上游。
- 如果请求体 `stream = true`，额外写入 `status = streaming`，并对上游流式响应做直通转发。
- 非流式 upstream 返回后写入 `completed` 或 `error`；流式响应结束后写入 `completed`，读取失败则写入 `error`。
- `/v1/{path}` 用于兼容 Hermes Agent 可能调用的其他 OpenAI-compatible 路径，默认只转发，不记录正文。
- 默认不保存 prompt 和 response 正文，只记录模型名、状态、延迟和 HTTP 结果摘要。

### 9.6 VSCode 插件接入

当前测试机使用的 VSCode AI 入口不是 Continue，而是：

- ClaudeCode 插件调用 Opus：配置 `claudeCode.environmentVariables` 中的 `ANTHROPIC_BASE_URL = "http://127.0.0.1:17888/anthropic"`。AIStatusHub 转发 `POST /anthropic/v1/messages` 到 `proxy.anthropic_upstream_base_url`，状态来源为 `proxy.anthropic_source`。
- VSCode Codex 插件调用 GPT：使用项目级 `.codex/config.toml` hooks 自动上报。插件在本仓库内启动 Codex 时复用 Codex hook 机制。
- Kimi Code 插件调用 Kimi：配置 `kimi.environmentVariables.KIMI_BASE_URL = "http://127.0.0.1:17888/kimi/v1"`。AIStatusHub 转发 `/kimi/v1/chat/completions` 到 `proxy.kimi_upstream_base_url`，状态来源为 `proxy.kimi_source`。

VSCode 插件代理路径：

```http
POST /anthropic/v1/messages
ANY /anthropic/{path}

POST /kimi/v1/chat/completions
GET /kimi/v1/models
ANY /kimi/v1/{path}
```

其中 `/anthropic/v1/messages` 和 `/kimi/v1/chat/completions` 会记录状态；通用透传路径默认只负责兼容插件启动或辅助请求。

### 9.7 Reporter 子命令

用于 Codex hooks、VSCode tasks 和手动测试：

```bash
aistatushub report --source vscode-claude-code --event-type prompt.submitted --status thinking
aistatushub report --source vscode-codex --event-type prompt.submitted --status thinking
aistatushub report --source vscode-kimi-code --event-type prompt.submitted --status thinking
aistatushub report --codex-hook --quiet
```

`--codex-hook` 从 stdin 读取 Codex hook JSON，并映射为 AIStatusHub 事件。

## 10. 实施顺序

### Milestone 0：Rust 项目骨架

- 创建 `Cargo.toml`。
- 创建 `config.example.toml`。
- 创建核心模块文件。
- 实现配置加载。

### Milestone 1：核心模型和状态归约

- 实现 `TaskState`、`AIEvent`、`HubStateEnvelope`。
- 实现 task key 生成。
- 实现 event → state reducer。
- 实现内存 state store 和 ring buffer。

### Milestone 2：HTTP API

- 实现 `GET /health`。
- 实现 `POST /api/v1/ingest/event`。
- 实现 `GET /api/v1/state`。
- 实现 `GET /api/v1/activity`。
- 实现 `stdout` output，状态更新时打印 compact JSON。
- 实现 `report` 子命令，便于 Codex/VSCode 测试。
- 实现 `/v1/chat/completions`、`/v1/models` 和 `/v1/{path}` proxy，便于 Hermes/Mimo 测试。
- 实现 `/anthropic/v1/messages` proxy，便于 VSCode ClaudeCode/Opus 测试。
- 实现 `/kimi/v1/chat/completions`、`/kimi/v1/models` 和 `/kimi/v1/{path}` proxy，便于 VSCode Kimi Code 测试。

### Milestone 3：Hub-to-Hub 接收

- 实现 trusted hubs。
- 实现 `POST /api/v1/hub/state`。
- 实现 `POST /api/v1/hub/snapshot`。
- 实现 hop 防环、max hop、target hub 校验。

### Milestone 4：快照与低开销持久化

- 实现 `state.snapshot.json`。
- 写入 debounce。
- 启动时恢复快照。

### Milestone 5：Hub-to-Hub 转发

- 实现 `hub-forward` output。
- 启动时发送 snapshot。
- 状态变化时发送 upsert。

### Milestone 6：极简 Web UI

- 内嵌一个轻量 HTML 页面。
- 展示当前状态、Activity、Hub peers。
- 通过 WebSocket 实时更新。

## 11. 架构决策

### ADR-001：使用 Rust 单二进制

原因：

- 更低常驻内存。
- 更容易发布多平台可执行程序。
- 不要求用户安装 Node.js。
- 更适合长期后台运行。

后果：

- 开发速度比 TypeScript 慢一些。
- 前端体验第一版保持克制。
- BLE/MQTT 等后续能力可以继续在 Rust 内实现。

### ADR-002：默认不使用 SQLite

原因：

- 用户只需要当前状态。
- JSON 快照足够恢复状态。
- 减少依赖、文件锁、迁移和查询层成本。

后果：

- 默认不支持复杂历史查询。
- 审计日志需要后续显式 opt-in。

### ADR-003：Hub-to-Hub 转发当前状态，不转发完整事件

原因：

- 多 Hub 汇聚时只需要中心 Hub 看到当前状态。
- 事件转发会放大网络、磁盘和隐私成本。
- 来源链可以通过 `origin_hub_id` 和 `hub_path` 保留。

后果：

- 远端状态过期后标记 stale。
- 多层 relay 必须做 hop 防环。
- `source` 只表示 AI 工具，不表示 Hub。
