use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Status {
    Idle,
    Starting,
    Thinking,
    Streaming,
    ToolRunning,
    WaitingApproval,
    Blocked,
    Completed,
    Error,
    Cancelled,
    Compacting,
    Unknown,
    Stale,
}

impl Default for Status {
    fn default() -> Self {
        Self::Unknown
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum EventType {
    #[serde(rename = "session.started")]
    SessionStarted,
    #[serde(rename = "session.resumed")]
    SessionResumed,
    #[serde(rename = "prompt.submitted")]
    PromptSubmitted,
    #[serde(rename = "turn.started")]
    TurnStarted,
    #[serde(rename = "message.delta")]
    MessageDelta,
    #[serde(rename = "message.completed")]
    MessageCompleted,
    #[serde(rename = "tool.started")]
    ToolStarted,
    #[serde(rename = "tool.completed")]
    ToolCompleted,
    #[serde(rename = "approval.requested")]
    ApprovalRequested,
    #[serde(rename = "approval.approved")]
    ApprovalApproved,
    #[serde(rename = "approval.rejected")]
    ApprovalRejected,
    #[serde(rename = "turn.completed")]
    TurnCompleted,
    #[serde(rename = "turn.failed")]
    TurnFailed,
    #[serde(rename = "turn.cancelled")]
    TurnCancelled,
    #[serde(rename = "notification")]
    Notification,
    #[serde(rename = "metrics.updated")]
    MetricsUpdated,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Severity {
    Debug,
    Info,
    Warning,
    Error,
}

impl Default for Severity {
    fn default() -> Self {
        Self::Info
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Locality {
    Local,
    Remote,
    Relayed,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct HubIdentity {
    pub hub_id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub display_name: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct HubHop {
    pub hub_id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub display_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub received_at: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub forwarded_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct HubProvenance {
    pub origin_hub_id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub origin_hub_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub received_from_hub_id: Option<String>,
    pub hub_path: Vec<HubHop>,
    pub hop_count: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct ToolSummary {
    pub name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub input_preview: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct Metrics {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tokens_in: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tokens_out: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub latency_ms: Option<u64>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct Counters {
    pub tool_calls: u64,
    pub errors: u64,
    pub approval_requests: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IngestEventRequest {
    pub source: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub surface: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub workspace: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub workspace_hash: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub turn_id: Option<String>,
    pub event_type: EventType,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status: Option<Status>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tool: Option<ToolSummary>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    #[serde(default)]
    pub metrics: Metrics,
    #[serde(default)]
    pub severity: Severity,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub created_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AiEvent {
    pub id: String,
    pub schema_version: u32,
    pub source: String,
    pub surface: String,
    pub provenance: HubProvenance,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub workspace: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub workspace_hash: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub turn_id: Option<String>,
    pub event_type: EventType,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status: Option<Status>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tool: Option<ToolSummary>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    #[serde(default)]
    pub metrics: Metrics,
    #[serde(default)]
    pub severity: Severity,
    pub created_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct TaskState {
    pub key: String,
    pub locality: Locality,
    pub source: String,
    pub origin_hub_id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub origin_hub_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub received_from_hub_id: Option<String>,
    pub hub_path: Vec<HubHop>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub workspace: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub workspace_hash: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub turn_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    pub status: Status,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub active_tool: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_message: Option<String>,
    #[serde(default)]
    pub counters: Counters,
    pub started_at: String,
    pub updated_at: String,
    pub last_seen_at: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub expires_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ActivityItem {
    pub id: String,
    pub kind: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub origin_hub_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub state_key: Option<String>,
    pub created_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HubStateEnvelope {
    pub schema_version: u32,
    pub message_id: String,
    pub message_type: HubMessageType,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub target_hub_id: Option<String>,
    pub sent_at: String,
    pub sender_hub: HubIdentity,
    pub provenance: HubProvenance,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub state: Option<TaskState>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub states: Vec<TaskState>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub activity: Option<ActivityItem>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum HubMessageType {
    #[serde(rename = "state.upsert")]
    StateUpsert,
    #[serde(rename = "state.delete")]
    StateDelete,
    #[serde(rename = "state.snapshot")]
    StateSnapshot,
    #[serde(rename = "activity.compact")]
    ActivityCompact,
    #[serde(rename = "heartbeat")]
    Heartbeat,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HubPeer {
    pub hub_id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub display_name: Option<String>,
    pub direction: String,
    pub trusted: bool,
    pub allow_relay: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_seen_at: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_error: Option<String>,
    pub updated_at: String,
}
