use sha2::{Digest, Sha256};
use uuid::Uuid;

use crate::{
    config::AppConfig,
    model::{
        ActivityItem, AiEvent, EventType, HubHop, HubProvenance, IngestEventRequest,
        Locality, Severity, Status, TaskState,
    },
};

pub fn now_iso() -> String {
    time::OffsetDateTime::now_utc()
        .format(&time::format_description::well_known::Rfc3339)
        .unwrap_or_else(|_| "1970-01-01T00:00:00Z".to_string())
}

pub fn normalize_event(input: IngestEventRequest, config: &AppConfig) -> AiEvent {
    let now = input.created_at.clone().unwrap_or_else(now_iso);
    let workspace_hash = input
        .workspace_hash
        .clone()
        .or_else(|| input.workspace.as_ref().map(|workspace| hash_workspace(workspace)));

    AiEvent {
        id: format!("evt_{}", Uuid::new_v4()),
        schema_version: 1,
        source: input.source,
        surface: input.surface.unwrap_or_else(|| "cli".to_string()),
        provenance: local_provenance(config, &now),
        workspace: input.workspace,
        workspace_hash,
        session_id: input.session_id,
        turn_id: input.turn_id,
        event_type: input.event_type,
        status: input.status,
        model: input.model,
        tool: input.tool,
        message: input.message.map(|value| preview(&value, config.privacy.max_preview_length)),
        metrics: input.metrics,
        severity: input.severity,
        created_at: now,
    }
}

pub fn reduce_event(previous: Option<TaskState>, event: &AiEvent) -> TaskState {
    let status = event
        .status
        .clone()
        .unwrap_or_else(|| status_for_event(&event.event_type, previous.as_ref()));
    let mut counters = previous
        .as_ref()
        .map(|state| state.counters.clone())
        .unwrap_or_default();

    match event.event_type {
        EventType::ToolStarted => counters.tool_calls += 1,
        EventType::ApprovalRequested => counters.approval_requests += 1,
        EventType::TurnFailed => counters.errors += 1,
        _ => {}
    }

    if event.severity == Severity::Error {
        counters.errors += 1;
    }

    let active_tool = match event.event_type {
        EventType::ToolStarted | EventType::ApprovalRequested => event
            .tool
            .as_ref()
            .map(|tool| tool.name.clone())
            .or_else(|| previous.as_ref().and_then(|state| state.active_tool.clone())),
        EventType::ToolCompleted | EventType::TurnCompleted => None,
        _ => previous.as_ref().and_then(|state| state.active_tool.clone()),
    };

    let key = task_key(
        &event.provenance.origin_hub_id,
        &event.source,
        event.workspace_hash.as_deref(),
        event.session_id.as_deref().or(event.turn_id.as_deref()).or(Some(&event.id)),
    );

    TaskState {
        key,
        locality: Locality::Local,
        source: event.source.clone(),
        origin_hub_id: event.provenance.origin_hub_id.clone(),
        origin_hub_name: event.provenance.origin_hub_name.clone(),
        received_from_hub_id: None,
        hub_path: event.provenance.hub_path.clone(),
        workspace: event.workspace.clone(),
        workspace_hash: event.workspace_hash.clone(),
        session_id: event.session_id.clone(),
        turn_id: event.turn_id.clone(),
        title: previous
            .as_ref()
            .and_then(|state| state.title.clone())
            .or_else(|| event.message.clone()),
        model: event
            .model
            .clone()
            .or_else(|| previous.as_ref().and_then(|state| state.model.clone())),
        status,
        active_tool,
        last_message: event
            .message
            .clone()
            .or_else(|| previous.as_ref().and_then(|state| state.last_message.clone())),
        counters,
        started_at: previous
            .as_ref()
            .map(|state| state.started_at.clone())
            .unwrap_or_else(|| event.created_at.clone()),
        updated_at: event.created_at.clone(),
        last_seen_at: event.created_at.clone(),
        expires_at: None,
    }
}

pub fn task_key(origin_hub_id: &str, source: &str, workspace_hash: Option<&str>, id: Option<&str>) -> String {
    format!(
        "{}:{}:{}:{}",
        sanitize(origin_hub_id),
        sanitize(source),
        sanitize(workspace_hash.unwrap_or("workspace_unknown")),
        sanitize(id.unwrap_or("session_unknown"))
    )
}

pub fn activity_from_state(kind: &str, state: &TaskState, message: Option<String>) -> ActivityItem {
    ActivityItem {
        id: format!("act_{}", Uuid::new_v4()),
        kind: kind.to_string(),
        message,
        source: Some(state.source.clone()),
        origin_hub_id: Some(state.origin_hub_id.clone()),
        state_key: Some(state.key.clone()),
        created_at: now_iso(),
    }
}

fn local_provenance(config: &AppConfig, now: &str) -> HubProvenance {
    HubProvenance {
        origin_hub_id: config.hub.hub_id.clone(),
        origin_hub_name: Some(config.hub.display_name.clone()),
        received_from_hub_id: None,
        hub_path: vec![HubHop {
            hub_id: config.hub.hub_id.clone(),
            display_name: Some(config.hub.display_name.clone()),
            received_at: Some(now.to_string()),
            forwarded_at: None,
        }],
        hop_count: 1,
    }
}

fn status_for_event(event_type: &EventType, previous: Option<&TaskState>) -> Status {
    match event_type {
        EventType::SessionStarted => Status::Starting,
        EventType::SessionResumed
        | EventType::PromptSubmitted
        | EventType::TurnStarted
        | EventType::MessageCompleted
        | EventType::ToolCompleted => Status::Thinking,
        EventType::MessageDelta => Status::Streaming,
        EventType::ToolStarted => Status::ToolRunning,
        EventType::ApprovalRequested => Status::WaitingApproval,
        EventType::ApprovalApproved => Status::ToolRunning,
        EventType::ApprovalRejected => Status::Blocked,
        EventType::TurnCompleted => Status::Completed,
        EventType::TurnFailed => Status::Error,
        EventType::TurnCancelled => Status::Cancelled,
        EventType::Notification | EventType::MetricsUpdated => previous
            .map(|state| state.status.clone())
            .unwrap_or(Status::Unknown),
    }
}

fn hash_workspace(workspace: &str) -> String {
    let digest = Sha256::digest(workspace.as_bytes());
    format!("ws_{}", &hex::encode(digest)[..16])
}

fn sanitize(value: &str) -> String {
    let value = value.trim().replace(':', "_");
    if value.is_empty() {
        "unknown".to_string()
    } else {
        value
    }
}

fn preview(value: &str, max_len: usize) -> String {
    value.chars().take(max_len).collect()
}
