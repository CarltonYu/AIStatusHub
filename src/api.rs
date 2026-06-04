use std::sync::Arc;

use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        Path, Query, State,
    },
    http::{HeaderMap, StatusCode},
    response::{Html, IntoResponse, Response},
    routing::{get, post},
    Json, Router,
};
use serde::Deserialize;
use serde_json::json;

use crate::{
    config::AppConfig,
    hub,
    ingest::{activity_from_state, normalize_event, reduce_event},
    model::{ActivityItem, HubMessageType, HubPeer, HubStateEnvelope, IngestEventRequest},
    security,
    snapshot,
    state::{ServerEvent, StateFilter, StateStore},
    web,
};

#[derive(Clone)]
pub struct AppState {
    pub config: Arc<AppConfig>,
    pub store: Arc<StateStore>,
}

impl AppState {
    pub fn new(config: Arc<AppConfig>, store: Arc<StateStore>) -> Self {
        Self { config, store }
    }
}

#[derive(Debug, Deserialize)]
struct StateQuery {
    source: Option<String>,
    origin_hub_id: Option<String>,
    locality: Option<String>,
    status: Option<String>,
    limit: Option<usize>,
}

#[derive(Debug, Deserialize)]
struct ActivityQuery {
    limit: Option<usize>,
}

#[derive(Debug, Deserialize)]
struct HubSnapshotRequest {
    items: Vec<HubStateEnvelope>,
}

pub fn router(state: AppState) -> Router {
    Router::new()
        .route("/", get(index))
        .route("/health", get(health))
        .route("/api/v1/state", get(list_state))
        .route("/api/v1/state/{key}", get(get_state))
        .route("/api/v1/activity", get(list_activity))
        .route("/api/v1/hubs", get(list_hubs))
        .route("/api/v1/hubs/{hub_id}", get(get_hub))
        .route("/api/v1/events/stream", get(events_stream))
        .route("/api/v1/ingest/event", post(ingest_event))
        .route("/api/v1/hub/state", post(hub_state))
        .route("/api/v1/hub/snapshot", post(hub_snapshot))
        .with_state(state)
}

async fn index() -> Html<&'static str> {
    Html(web::INDEX_HTML)
}

async fn health(State(state): State<AppState>) -> impl IntoResponse {
    Json(json!({
        "ok": true,
        "hub": {
            "hub_id": state.config.hub.hub_id,
            "display_name": state.config.hub.display_name
        }
    }))
}

async fn list_state(
    State(state): State<AppState>,
    Query(query): Query<StateQuery>,
) -> impl IntoResponse {
    let data = state
        .store
        .list_states(StateFilter {
            source: query.source,
            origin_hub_id: query.origin_hub_id,
            locality: query.locality,
            status: query.status,
            limit: query.limit,
        })
        .await;

    Json(json!({ "data": data }))
}

async fn get_state(State(state): State<AppState>, Path(key): Path<String>) -> Response {
    match state.store.get_state(&key).await {
        Some(data) => Json(json!({ "data": data })).into_response(),
        None => json_error(StatusCode::NOT_FOUND, "state not found"),
    }
}

async fn list_activity(
    State(state): State<AppState>,
    Query(query): Query<ActivityQuery>,
) -> impl IntoResponse {
    Json(json!({
        "data": state.store.list_activity(query.limit.unwrap_or(100)).await
    }))
}

async fn list_hubs(State(state): State<AppState>) -> impl IntoResponse {
    Json(json!({ "data": state.store.list_hubs().await }))
}

async fn get_hub(State(state): State<AppState>, Path(hub_id): Path<String>) -> Response {
    match state.store.get_hub(&hub_id).await {
        Some(data) => Json(json!({ "data": data })).into_response(),
        None => json_error(StatusCode::NOT_FOUND, "hub not found"),
    }
}

async fn events_stream(ws: WebSocketUpgrade, State(state): State<AppState>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| websocket_loop(socket, state))
}

async fn websocket_loop(mut socket: WebSocket, state: AppState) {
    let states = state
        .store
        .list_states(StateFilter {
            limit: Some(500),
            ..StateFilter::default()
        })
        .await;
    let _ = socket
        .send(Message::Text(
            serde_json::to_string(&json!({ "type": "state.snapshot", "data": states }))
                .unwrap_or_else(|_| "{}".to_string()),
        ))
        .await;

    let mut rx = state.store.subscribe();

    while let Ok(event) = rx.recv().await {
        let payload = match event {
            ServerEvent::StateUpdated(data) => json!({ "type": "state.updated", "data": data }),
            ServerEvent::StateDeleted { key } => json!({ "type": "state.deleted", "key": key }),
            ServerEvent::Activity(data) => json!({ "type": "activity", "data": data }),
            ServerEvent::HubUpdated(data) => json!({ "type": "hub.updated", "data": data }),
            ServerEvent::Heartbeat => json!({ "type": "heartbeat" }),
        };

        if socket
            .send(Message::Text(
                serde_json::to_string(&payload).unwrap_or_else(|_| "{}".to_string()),
            ))
            .await
            .is_err()
        {
            break;
        }
    }
}

async fn ingest_event(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(payload): Json<IngestEventRequest>,
) -> Response {
    if let Err(response) = security::require_token(&headers, state.config.server.auth_token.as_deref()) {
        return response;
    }

    let event = normalize_event(payload, &state.config);
    let previous_key = crate::ingest::task_key(
        &event.provenance.origin_hub_id,
        &event.source,
        event.workspace_hash.as_deref(),
        event.session_id.as_deref().or(event.turn_id.as_deref()).or(Some(&event.id)),
    );
    let previous = state.store.get_state(&previous_key).await;
    let task_state = reduce_event(previous, &event);
    let changed = state.store.upsert_state(task_state.clone()).await;
    let activity = activity_from_state(
        event_type_name(&event.event_type),
        &task_state,
        event.message.clone(),
    );
    state.store.add_activity(activity).await;

    if changed {
        save_snapshot_best_effort(&state).await;
    }

    Json(json!({
        "event_id": event.id,
        "task_key": task_state.key,
        "state_changed": changed
    }))
    .into_response()
}

async fn hub_state(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(envelope): Json<HubStateEnvelope>,
) -> Response {
    handle_hub_envelope(state, headers, envelope).await
}

async fn hub_snapshot(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(payload): Json<HubSnapshotRequest>,
) -> Response {
    let mut results = Vec::with_capacity(payload.items.len());

    for envelope in payload.items {
        let response = handle_hub_envelope(state.clone(), headers.clone(), envelope).await;
        if !response.status().is_success() {
            return response;
        }
        results.push(json!({ "ok": true }));
    }

    Json(json!({ "results": results })).into_response()
}

async fn handle_hub_envelope(
    state: AppState,
    headers: HeaderMap,
    envelope: HubStateEnvelope,
) -> Response {
    let token = security::bearer_token(&headers);
    let trusted = match hub::trusted_hub(&state.config, &envelope.sender_hub.hub_id, token) {
        Ok(value) => value,
        Err(error) => return json_error(StatusCode::FORBIDDEN, error.message()),
    };

    let peer = hub::peer_from_envelope(&envelope, trusted.as_ref());
    state.store.upsert_hub(peer).await;

    match envelope.message_type {
        HubMessageType::Heartbeat => {
            state.store.add_activity(ActivityItem {
                id: format!("act_{}", uuid::Uuid::new_v4()),
                kind: "heartbeat".to_string(),
                message: Some(format!("Heartbeat from {}", envelope.sender_hub.hub_id)),
                source: None,
                origin_hub_id: Some(envelope.sender_hub.hub_id),
                state_key: None,
                created_at: crate::ingest::now_iso(),
            }).await;
            Json(json!({ "changed": false })).into_response()
        }
        HubMessageType::ActivityCompact => {
            if let Some(activity) = envelope.activity {
                state.store.add_activity(activity).await;
            }
            Json(json!({ "changed": false })).into_response()
        }
        HubMessageType::StateDelete => {
            let Some(incoming) = envelope.state.as_ref() else {
                return json_error(StatusCode::BAD_REQUEST, "state.delete requires state");
            };
            let changed = state.store.delete_state(&incoming.key).await;
            if changed {
                save_snapshot_best_effort(&state).await;
            }
            Json(json!({ "changed": changed, "task_key": incoming.key })).into_response()
        }
        HubMessageType::StateSnapshot => {
            let mut changed_count = 0usize;
            for incoming in envelope.states.clone() {
                let mut item = envelope.clone();
                item.message_type = HubMessageType::StateUpsert;
                item.state = Some(incoming);
                item.states.clear();
                match merge_one_remote_state(&state, trusted.as_ref(), item).await {
                    Ok(true) => changed_count += 1,
                    Ok(false) => {}
                    Err(response) => return response,
                }
            }
            if changed_count > 0 {
                save_snapshot_best_effort(&state).await;
            }
            Json(json!({ "changed": changed_count > 0, "changed_count": changed_count })).into_response()
        }
        HubMessageType::StateUpsert => match merge_one_remote_state(&state, trusted.as_ref(), envelope).await {
            Ok(changed) => {
                if changed {
                    save_snapshot_best_effort(&state).await;
                }
                Json(json!({ "changed": changed })).into_response()
            }
            Err(response) => response,
        },
    }
}

async fn merge_one_remote_state(
    app: &AppState,
    trusted: Option<&crate::config::TrustedHubConfig>,
    envelope: HubStateEnvelope,
) -> Result<bool, Response> {
    let key = envelope
        .state
        .as_ref()
        .map(|state| state.key.clone())
        .unwrap_or_default();
    let previous = if key.is_empty() {
        None
    } else {
        app.store.get_state(&key).await
    };
    let merged = match hub::merge_remote_state(&app.config, trusted, previous.as_ref(), &envelope) {
        Ok(state) => state,
        Err(error) => return Err(json_error(StatusCode::BAD_REQUEST, error.message())),
    };
    let changed = app.store.upsert_state(merged.clone()).await;

    if changed {
        app.store
            .add_activity(activity_from_state(
                "remote.state.updated",
                &merged,
                merged.last_message.clone(),
            ))
            .await;
    }

    Ok(changed)
}

async fn save_snapshot_best_effort(state: &AppState) {
    if let Err(error) = snapshot::save_snapshot(&state.config, &state.store).await {
        tracing::warn!(%error, "failed to save snapshot");
    }
}

fn json_error(status: StatusCode, message: &str) -> Response {
    (status, Json(json!({ "error": message }))).into_response()
}

fn event_type_name(event_type: &crate::model::EventType) -> &'static str {
    match event_type {
        crate::model::EventType::SessionStarted => "session.started",
        crate::model::EventType::SessionResumed => "session.resumed",
        crate::model::EventType::PromptSubmitted => "prompt.submitted",
        crate::model::EventType::TurnStarted => "turn.started",
        crate::model::EventType::MessageDelta => "message.delta",
        crate::model::EventType::MessageCompleted => "message.completed",
        crate::model::EventType::ToolStarted => "tool.started",
        crate::model::EventType::ToolCompleted => "tool.completed",
        crate::model::EventType::ApprovalRequested => "approval.requested",
        crate::model::EventType::ApprovalApproved => "approval.approved",
        crate::model::EventType::ApprovalRejected => "approval.rejected",
        crate::model::EventType::TurnCompleted => "turn.completed",
        crate::model::EventType::TurnFailed => "turn.failed",
        crate::model::EventType::TurnCancelled => "turn.cancelled",
        crate::model::EventType::Notification => "notification",
        crate::model::EventType::MetricsUpdated => "metrics.updated",
    }
}
