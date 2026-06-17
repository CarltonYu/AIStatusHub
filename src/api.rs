use std::{env, fs, path::PathBuf, sync::Arc};

use axum::{
    body::{Body, Bytes},
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        Path, Query, State,
    },
    http::{header, HeaderMap, HeaderValue, Method, StatusCode, Uri},
    response::{Html, IntoResponse, Response},
    routing::{any, get, post},
    Json, Router,
};
use futures_util::StreamExt;
use serde::Deserialize;
use serde_json::json;
use std::time::{Duration, Instant};

use crate::{
    config::AppConfig,
    hub,
    ingest::{activity_from_state, normalize_event, reduce_event},
    model::{
        ActivityItem, EventType, HubMessageType, HubStateEnvelope, IngestEventRequest, Status,
    },
    outputs, security, snapshot,
    state::{ServerEvent, StateFilter, StateStore},
    web,
};

#[derive(Clone)]
pub struct AppState {
    pub config: Arc<AppConfig>,
    pub store: Arc<StateStore>,
    pub outputs: Arc<outputs::OutputRuntime>,
}

impl AppState {
    pub fn new(config: Arc<AppConfig>, store: Arc<StateStore>) -> Self {
        Self {
            config,
            store,
            outputs: Arc::new(outputs::OutputRuntime::new()),
        }
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

#[derive(Debug, Deserialize)]
struct MimoFreeBootstrapResponse {
    jwt: String,
}

#[derive(Clone, Copy)]
enum ProxyAuthMode {
    Standard,
    Mimo,
}

const OPENAI_PROXY_SURFACE: &str = "openai-compatible-proxy";
const ANTHROPIC_PROXY_SURFACE: &str = "anthropic-compatible-proxy";
const KIMI_PROXY_SURFACE: &str = "kimi-openai-compatible-proxy";
const MIMO_PROXY_SURFACE: &str = "mimo-openai-compatible-proxy";

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
        .route("/anthropic/v1/messages", post(proxy_anthropic_messages))
        .route("/anthropic/*proxy_path", any(proxy_anthropic_path))
        .route(
            "/kimi/v1/chat/completions",
            post(proxy_kimi_chat_completions),
        )
        .route("/kimi/v1/models", get(proxy_kimi_models))
        .route("/kimi/v1/*proxy_path", any(proxy_kimi_v1_path))
        .route(
            "/mimo/v1/chat/completions",
            post(proxy_mimo_chat_completions),
        )
        .route("/mimo/v1/models", get(proxy_mimo_models))
        .route("/mimo/v1/*proxy_path", any(proxy_mimo_v1_path))
        .route("/v1/chat/completions", post(proxy_chat_completions))
        .route("/v1/models", get(proxy_models))
        .route("/v1/*proxy_path", any(proxy_v1_path))
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
    if let Err(response) =
        security::require_token(&headers, state.config.server.auth_token.as_deref())
    {
        return response;
    }

    let event = normalize_event(payload, &state.config);
    let previous_key = crate::ingest::task_key(
        &event.provenance.origin_hub_id,
        &event.source,
        event.workspace_hash.as_deref(),
        event
            .session_id
            .as_deref()
            .or(event.turn_id.as_deref())
            .or(Some(&event.id)),
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
        emit_state_changed(&state, &task_state).await;
        save_snapshot_best_effort(&state).await;
    }

    Json(json!({
        "event_id": event.id,
        "task_key": task_state.key,
        "state_changed": changed
    }))
    .into_response()
}

async fn proxy_chat_completions(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(payload): Json<serde_json::Value>,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "OpenAI-compatible proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_upstream_base_url(&state, &headers) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy upstream_base_url is not configured",
        );
    };

    let model = payload
        .get("model")
        .and_then(|value| value.as_str())
        .map(ToOwned::to_owned);
    let stream = payload
        .get("stream")
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    let source = proxy_source(&state, &headers);
    let session_id = format!("proxy_{}", uuid::Uuid::new_v4());
    let started = Instant::now();

    let _ = apply_internal_event(
        &state,
        IngestEventRequest {
            source: source.clone(),
            surface: Some(OPENAI_PROXY_SURFACE.to_string()),
            workspace: None,
            workspace_hash: Some("proxy".to_string()),
            session_id: Some(session_id.clone()),
            turn_id: None,
            event_type: EventType::PromptSubmitted,
            status: Some(Status::Thinking),
            model: model.clone(),
            tool: None,
            message: Some(format!(
                "Proxy request started{}",
                model
                    .as_ref()
                    .map(|model| format!(" for {model}"))
                    .unwrap_or_default()
            )),
            metrics: Default::default(),
            severity: Default::default(),
            created_at: None,
        },
    )
    .await;

    if stream {
        let _ = apply_internal_event(
            &state,
            IngestEventRequest {
                source: source.clone(),
                surface: Some(OPENAI_PROXY_SURFACE.to_string()),
                workspace: None,
                workspace_hash: Some("proxy".to_string()),
                session_id: Some(session_id.clone()),
                turn_id: None,
                event_type: EventType::MessageDelta,
                status: Some(Status::Streaming),
                model: model.clone(),
                tool: None,
                message: Some("Proxy streaming response".to_string()),
                metrics: Default::default(),
                severity: Default::default(),
                created_at: None,
            },
        )
        .await;
    }

    let upstream_url = format!(
        "{}/chat/completions",
        upstream_base_url.trim_end_matches('/')
    );
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(state.config.proxy.timeout_secs))
        .build()
    {
        Ok(client) => client,
        Err(error) => return json_error(StatusCode::INTERNAL_SERVER_ERROR, &error.to_string()),
    };
    let mut request = client.post(upstream_url).json(&payload);
    request = apply_openai_proxy_headers(request, &headers, &state, &upstream_base_url);

    let upstream = match request.send().await {
        Ok(response) => response,
        Err(error) => {
            let _ = proxy_finish_event(
                &state,
                OPENAI_PROXY_SURFACE,
                &source,
                &session_id,
                model,
                Status::Error,
                format!("Proxy request failed: {error}"),
                started.elapsed().as_millis() as u64,
            )
            .await;
            return json_error(StatusCode::BAD_GATEWAY, &error.to_string());
        }
    };

    let status =
        StatusCode::from_u16(upstream.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
    let content_type = upstream
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .map(ToOwned::to_owned);
    if stream && status.is_success() {
        let stream_state = state.clone();
        let stream_source = source.clone();
        let stream_session_id = session_id.clone();
        let stream_model = model.clone();
        let stream_started = started;
        let body_stream = async_stream::stream! {
            let mut chunks = upstream.bytes_stream();
            while let Some(chunk) = chunks.next().await {
                match chunk {
                    Ok(bytes) => yield Ok::<Bytes, reqwest::Error>(bytes),
                    Err(error) => {
                        let _ = proxy_finish_event(
                            &stream_state,
                            OPENAI_PROXY_SURFACE,
                            &stream_source,
                            &stream_session_id,
                            stream_model.clone(),
                            Status::Error,
                            format!("Proxy stream failed: {error}"),
                            stream_started.elapsed().as_millis() as u64,
                        )
                        .await;
                        yield Err::<Bytes, reqwest::Error>(error);
                        return;
                    }
                }
            }

            let _ = proxy_finish_event(
                &stream_state,
                OPENAI_PROXY_SURFACE,
                &stream_source,
                &stream_session_id,
                stream_model,
                Status::Completed,
                format!("Proxy stream completed with HTTP {status}"),
                stream_started.elapsed().as_millis() as u64,
            )
            .await;
        };

        let mut response = Response::builder().status(status);
        if let Some(content_type) = content_type {
            if let Ok(value) = HeaderValue::from_str(&content_type) {
                response = response.header(header::CONTENT_TYPE, value);
            }
        }

        return response
            .body(Body::from_stream(body_stream))
            .unwrap_or_else(|_| {
                json_error(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "failed to build proxy response",
                )
            });
    }

    let bytes = match upstream.bytes().await {
        Ok(bytes) => bytes,
        Err(error) => {
            let _ = proxy_finish_event(
                &state,
                OPENAI_PROXY_SURFACE,
                &source,
                &session_id,
                model,
                Status::Error,
                format!("Proxy response read failed: {error}"),
                started.elapsed().as_millis() as u64,
            )
            .await;
            return json_error(StatusCode::BAD_GATEWAY, &error.to_string());
        }
    };
    let final_status = if status.is_success() {
        Status::Completed
    } else {
        Status::Error
    };
    let _ = proxy_finish_event(
        &state,
        OPENAI_PROXY_SURFACE,
        &source,
        &session_id,
        model,
        final_status,
        format!("Proxy response completed with HTTP {status}"),
        started.elapsed().as_millis() as u64,
    )
    .await;

    let mut response = Response::builder().status(status);
    if let Some(content_type) = content_type {
        if let Ok(value) = HeaderValue::from_str(&content_type) {
            response = response.header(header::CONTENT_TYPE, value);
        }
    }

    response.body(Body::from(bytes)).unwrap_or_else(|_| {
        json_error(
            StatusCode::INTERNAL_SERVER_ERROR,
            "failed to build proxy response",
        )
    })
}

async fn proxy_anthropic_messages(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(payload): Json<serde_json::Value>,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Anthropic proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_anthropic_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy anthropic_upstream_base_url is not configured",
        );
    };

    let model = payload
        .get("model")
        .and_then(|value| value.as_str())
        .map(ToOwned::to_owned);
    let stream = payload
        .get("stream")
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    let source = proxy_anthropic_source(&state);
    let session_id = format!("proxy_{}", uuid::Uuid::new_v4());
    let started = Instant::now();

    let _ = apply_internal_event(
        &state,
        IngestEventRequest {
            source: source.clone(),
            surface: Some(ANTHROPIC_PROXY_SURFACE.to_string()),
            workspace: None,
            workspace_hash: Some("proxy".to_string()),
            session_id: Some(session_id.clone()),
            turn_id: None,
            event_type: EventType::PromptSubmitted,
            status: Some(Status::Thinking),
            model: model.clone(),
            tool: None,
            message: Some(format!(
                "Anthropic proxy request started{}",
                model
                    .as_ref()
                    .map(|model| format!(" for {model}"))
                    .unwrap_or_default()
            )),
            metrics: Default::default(),
            severity: Default::default(),
            created_at: None,
        },
    )
    .await;

    if stream {
        let _ = apply_internal_event(
            &state,
            IngestEventRequest {
                source: source.clone(),
                surface: Some(ANTHROPIC_PROXY_SURFACE.to_string()),
                workspace: None,
                workspace_hash: Some("proxy".to_string()),
                session_id: Some(session_id.clone()),
                turn_id: None,
                event_type: EventType::MessageDelta,
                status: Some(Status::Streaming),
                model: model.clone(),
                tool: None,
                message: Some("Anthropic proxy streaming response".to_string()),
                metrics: Default::default(),
                severity: Default::default(),
                created_at: None,
            },
        )
        .await;
    }

    let upstream_url = anthropic_upstream_url(&upstream_base_url, "v1/messages", "");
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(state.config.proxy.timeout_secs))
        .build()
    {
        Ok(client) => client,
        Err(error) => return json_error(StatusCode::INTERNAL_SERVER_ERROR, &error.to_string()),
    };
    let mut request = client.post(upstream_url).json(&payload);
    request = apply_proxy_headers(request, &headers, &state);

    let upstream = match request.send().await {
        Ok(response) => response,
        Err(error) => {
            let _ = proxy_finish_event(
                &state,
                ANTHROPIC_PROXY_SURFACE,
                &source,
                &session_id,
                model,
                Status::Error,
                format!("Anthropic proxy request failed: {error}"),
                started.elapsed().as_millis() as u64,
            )
            .await;
            return json_error(StatusCode::BAD_GATEWAY, &error.to_string());
        }
    };

    let status =
        StatusCode::from_u16(upstream.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
    let content_type = upstream
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .map(ToOwned::to_owned);
    let content_type_is_stream = content_type
        .as_deref()
        .map(|value| value.to_ascii_lowercase().contains("text/event-stream"))
        .unwrap_or(false);

    if status.is_success() && (stream || content_type_is_stream) {
        let stream_state = state.clone();
        let stream_source = source.clone();
        let stream_session_id = session_id.clone();
        let stream_model = model.clone();
        let stream_started = started;
        let body_stream = async_stream::stream! {
            let mut chunks = upstream.bytes_stream();
            while let Some(chunk) = chunks.next().await {
                match chunk {
                    Ok(bytes) => yield Ok::<Bytes, reqwest::Error>(bytes),
                    Err(error) => {
                        let _ = proxy_finish_event(
                            &stream_state,
                            ANTHROPIC_PROXY_SURFACE,
                            &stream_source,
                            &stream_session_id,
                            stream_model.clone(),
                            Status::Error,
                            format!("Anthropic proxy stream failed: {error}"),
                            stream_started.elapsed().as_millis() as u64,
                        )
                        .await;
                        yield Err::<Bytes, reqwest::Error>(error);
                        return;
                    }
                }
            }

            let _ = proxy_finish_event(
                &stream_state,
                ANTHROPIC_PROXY_SURFACE,
                &stream_source,
                &stream_session_id,
                stream_model,
                Status::Completed,
                format!("Anthropic proxy stream completed with HTTP {status}"),
                stream_started.elapsed().as_millis() as u64,
            )
            .await;
        };

        let mut response = Response::builder().status(status);
        if let Some(content_type) = content_type {
            if let Ok(value) = HeaderValue::from_str(&content_type) {
                response = response.header(header::CONTENT_TYPE, value);
            }
        }

        return response
            .body(Body::from_stream(body_stream))
            .unwrap_or_else(|_| {
                json_error(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "failed to build proxy response",
                )
            });
    }

    let bytes = match upstream.bytes().await {
        Ok(bytes) => bytes,
        Err(error) => {
            let _ = proxy_finish_event(
                &state,
                ANTHROPIC_PROXY_SURFACE,
                &source,
                &session_id,
                model,
                Status::Error,
                format!("Anthropic proxy response read failed: {error}"),
                started.elapsed().as_millis() as u64,
            )
            .await;
            return json_error(StatusCode::BAD_GATEWAY, &error.to_string());
        }
    };
    let final_status = if status.is_success() {
        Status::Completed
    } else {
        Status::Error
    };
    let _ = proxy_finish_event(
        &state,
        ANTHROPIC_PROXY_SURFACE,
        &source,
        &session_id,
        model,
        final_status,
        format!("Anthropic proxy response completed with HTTP {status}"),
        started.elapsed().as_millis() as u64,
    )
    .await;

    let mut response = Response::builder().status(status);
    if let Some(content_type) = content_type {
        if let Ok(value) = HeaderValue::from_str(&content_type) {
            response = response.header(header::CONTENT_TYPE, value);
        }
    }

    response.body(Body::from(bytes)).unwrap_or_else(|_| {
        json_error(
            StatusCode::INTERNAL_SERVER_ERROR,
            "failed to build proxy response",
        )
    })
}

async fn proxy_kimi_chat_completions(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(payload): Json<serde_json::Value>,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Kimi proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_kimi_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy kimi_upstream_base_url is not configured",
        );
    };

    let source = proxy_kimi_source(&state);
    proxy_openai_chat_request(
        state,
        headers,
        payload,
        upstream_base_url,
        source,
        KIMI_PROXY_SURFACE,
        "Kimi proxy",
        "chat/completions",
        ProxyAuthMode::Standard,
    )
    .await
}

async fn proxy_mimo_chat_completions(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(payload): Json<serde_json::Value>,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Mimo proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_mimo_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy mimo_upstream_base_url is not configured",
        );
    };

    let payload = proxy_mimo_chat_payload(&state, &upstream_base_url, payload);
    let source = proxy_mimo_source(&state);
    let chat_path = mimo_openai_chat_path(&upstream_base_url);
    proxy_openai_chat_request(
        state,
        headers,
        payload,
        upstream_base_url,
        source,
        MIMO_PROXY_SURFACE,
        "Mimo proxy",
        chat_path,
        ProxyAuthMode::Mimo,
    )
    .await
}

async fn proxy_openai_chat_request(
    state: AppState,
    headers: HeaderMap,
    payload: serde_json::Value,
    upstream_base_url: String,
    source: String,
    surface: &'static str,
    label: &'static str,
    chat_path: &'static str,
    auth_mode: ProxyAuthMode,
) -> Response {
    let model = payload
        .get("model")
        .and_then(|value| value.as_str())
        .map(ToOwned::to_owned);
    let stream = payload
        .get("stream")
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    let session_id = format!("proxy_{}", uuid::Uuid::new_v4());
    let started = Instant::now();

    let _ = apply_internal_event(
        &state,
        IngestEventRequest {
            source: source.clone(),
            surface: Some(surface.to_string()),
            workspace: None,
            workspace_hash: Some("proxy".to_string()),
            session_id: Some(session_id.clone()),
            turn_id: None,
            event_type: EventType::PromptSubmitted,
            status: Some(Status::Thinking),
            model: model.clone(),
            tool: None,
            message: Some(format!(
                "{label} request started{}",
                model
                    .as_ref()
                    .map(|model| format!(" for {model}"))
                    .unwrap_or_default()
            )),
            metrics: Default::default(),
            severity: Default::default(),
            created_at: None,
        },
    )
    .await;

    if stream {
        let _ = apply_internal_event(
            &state,
            IngestEventRequest {
                source: source.clone(),
                surface: Some(surface.to_string()),
                workspace: None,
                workspace_hash: Some("proxy".to_string()),
                session_id: Some(session_id.clone()),
                turn_id: None,
                event_type: EventType::MessageDelta,
                status: Some(Status::Streaming),
                model: model.clone(),
                tool: None,
                message: Some(format!("{label} streaming response")),
                metrics: Default::default(),
                severity: Default::default(),
                created_at: None,
            },
        )
        .await;
    }

    let upstream_url = format!("{}/{}", upstream_base_url.trim_end_matches('/'), chat_path);
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(state.config.proxy.timeout_secs))
        .build()
    {
        Ok(client) => client,
        Err(error) => return json_error(StatusCode::INTERNAL_SERVER_ERROR, &error.to_string()),
    };
    let mut request = client.post(upstream_url).json(&payload);
    request = match apply_proxy_headers_with_auth(
        request,
        &headers,
        &state,
        &client,
        &upstream_base_url,
        auth_mode,
    )
    .await
    {
        Ok(request) => request,
        Err(error) => {
            let _ = proxy_finish_event(
                &state,
                surface,
                &source,
                &session_id,
                model,
                Status::Error,
                format!("{label} request failed: {error}"),
                started.elapsed().as_millis() as u64,
            )
            .await;
            return json_error(StatusCode::BAD_GATEWAY, &error);
        }
    };

    let upstream = match request.send().await {
        Ok(response) => response,
        Err(error) => {
            let _ = proxy_finish_event(
                &state,
                surface,
                &source,
                &session_id,
                model,
                Status::Error,
                format!("{label} request failed: {error}"),
                started.elapsed().as_millis() as u64,
            )
            .await;
            return json_error(StatusCode::BAD_GATEWAY, &error.to_string());
        }
    };

    let status =
        StatusCode::from_u16(upstream.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
    let content_type = upstream
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .map(ToOwned::to_owned);
    if stream && status.is_success() {
        let stream_state = state.clone();
        let stream_source = source.clone();
        let stream_session_id = session_id.clone();
        let stream_model = model.clone();
        let stream_started = started;
        let body_stream = async_stream::stream! {
            let mut chunks = upstream.bytes_stream();
            while let Some(chunk) = chunks.next().await {
                match chunk {
                    Ok(bytes) => yield Ok::<Bytes, reqwest::Error>(bytes),
                    Err(error) => {
                        let _ = proxy_finish_event(
                            &stream_state,
                            surface,
                            &stream_source,
                            &stream_session_id,
                            stream_model.clone(),
                            Status::Error,
                            format!("{label} stream failed: {error}"),
                            stream_started.elapsed().as_millis() as u64,
                        )
                        .await;
                        yield Err::<Bytes, reqwest::Error>(error);
                        return;
                    }
                }
            }

            let _ = proxy_finish_event(
                &stream_state,
                surface,
                &stream_source,
                &stream_session_id,
                stream_model,
                Status::Completed,
                format!("{label} stream completed with HTTP {status}"),
                stream_started.elapsed().as_millis() as u64,
            )
            .await;
        };

        let mut response = Response::builder().status(status);
        if let Some(content_type) = content_type {
            if let Ok(value) = HeaderValue::from_str(&content_type) {
                response = response.header(header::CONTENT_TYPE, value);
            }
        }

        return response
            .body(Body::from_stream(body_stream))
            .unwrap_or_else(|_| {
                json_error(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "failed to build proxy response",
                )
            });
    }

    let bytes = match upstream.bytes().await {
        Ok(bytes) => bytes,
        Err(error) => {
            let _ = proxy_finish_event(
                &state,
                surface,
                &source,
                &session_id,
                model,
                Status::Error,
                format!("{label} response read failed: {error}"),
                started.elapsed().as_millis() as u64,
            )
            .await;
            return json_error(StatusCode::BAD_GATEWAY, &error.to_string());
        }
    };
    let final_status = if status.is_success() {
        Status::Completed
    } else {
        Status::Error
    };
    let _ = proxy_finish_event(
        &state,
        surface,
        &source,
        &session_id,
        model,
        final_status,
        format!("{label} response completed with HTTP {status}"),
        started.elapsed().as_millis() as u64,
    )
    .await;

    let mut response = Response::builder().status(status);
    if let Some(content_type) = content_type {
        if let Ok(value) = HeaderValue::from_str(&content_type) {
            response = response.header(header::CONTENT_TYPE, value);
        }
    }

    response.body(Body::from(bytes)).unwrap_or_else(|_| {
        json_error(
            StatusCode::INTERNAL_SERVER_ERROR,
            "failed to build proxy response",
        )
    })
}

async fn proxy_models(State(state): State<AppState>, headers: HeaderMap) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "OpenAI-compatible proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_upstream_base_url(&state, &headers) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy upstream_base_url is not configured",
        );
    };
    let client = reqwest::Client::new();
    let mut request = client.get(format!(
        "{}/models",
        upstream_base_url.trim_end_matches('/')
    ));
    request = apply_openai_proxy_headers(request, &headers, &state, &upstream_base_url);

    match request.send().await {
        Ok(response) => {
            let status =
                StatusCode::from_u16(response.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
            let content_type = response
                .headers()
                .get(reqwest::header::CONTENT_TYPE)
                .and_then(|value| value.to_str().ok())
                .map(ToOwned::to_owned);
            let bytes = match response.bytes().await {
                Ok(bytes) => bytes,
                Err(error) => return json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
            };
            let mut builder = Response::builder().status(status);
            if let Some(content_type) = content_type {
                if let Ok(value) = HeaderValue::from_str(&content_type) {
                    builder = builder.header(header::CONTENT_TYPE, value);
                }
            }
            builder.body(Body::from(bytes)).unwrap_or_else(|_| {
                json_error(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "failed to build proxy response",
                )
            })
        }
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
}

async fn proxy_kimi_models(State(state): State<AppState>, headers: HeaderMap) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Kimi proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_kimi_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy kimi_upstream_base_url is not configured",
        );
    };
    let client = reqwest::Client::new();
    let mut request = client.get(format!(
        "{}/models",
        upstream_base_url.trim_end_matches('/')
    ));
    request = apply_openai_proxy_headers(request, &headers, &state, &upstream_base_url);

    match request.send().await {
        Ok(response) => {
            let status =
                StatusCode::from_u16(response.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
            let content_type = response
                .headers()
                .get(reqwest::header::CONTENT_TYPE)
                .and_then(|value| value.to_str().ok())
                .map(ToOwned::to_owned);
            let bytes = match response.bytes().await {
                Ok(bytes) => bytes,
                Err(error) => return json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
            };
            let mut builder = Response::builder().status(status);
            if let Some(content_type) = content_type {
                if let Ok(value) = HeaderValue::from_str(&content_type) {
                    builder = builder.header(header::CONTENT_TYPE, value);
                }
            }
            builder.body(Body::from(bytes)).unwrap_or_else(|_| {
                json_error(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "failed to build proxy response",
                )
            })
        }
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
}

async fn proxy_mimo_models(State(state): State<AppState>, headers: HeaderMap) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Mimo proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_mimo_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy mimo_upstream_base_url is not configured",
        );
    };
    if is_mimo_free_upstream(&upstream_base_url) {
        return Json(json!({
            "object": "list",
            "data": [
                {
                    "id": "mimo-auto",
                    "object": "model",
                    "created": 0,
                    "owned_by": "mimo"
                }
            ]
        }))
        .into_response();
    }

    let client = reqwest::Client::new();
    let mut request = client.get(format!(
        "{}/models",
        upstream_base_url.trim_end_matches('/')
    ));
    request = match apply_proxy_headers_with_auth(
        request,
        &headers,
        &state,
        &client,
        &upstream_base_url,
        ProxyAuthMode::Mimo,
    )
    .await
    {
        Ok(request) => request,
        Err(error) => return json_error(StatusCode::BAD_GATEWAY, &error),
    };

    match request.send().await {
        Ok(response) => {
            let status =
                StatusCode::from_u16(response.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
            let content_type = response
                .headers()
                .get(reqwest::header::CONTENT_TYPE)
                .and_then(|value| value.to_str().ok())
                .map(ToOwned::to_owned);
            let bytes = match response.bytes().await {
                Ok(bytes) => bytes,
                Err(error) => return json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
            };
            let mut builder = Response::builder().status(status);
            if let Some(content_type) = content_type {
                if let Ok(value) = HeaderValue::from_str(&content_type) {
                    builder = builder.header(header::CONTENT_TYPE, value);
                }
            }
            builder.body(Body::from(bytes)).unwrap_or_else(|_| {
                json_error(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "failed to build proxy response",
                )
            })
        }
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
}

async fn proxy_v1_path(
    State(state): State<AppState>,
    headers: HeaderMap,
    method: Method,
    uri: Uri,
    Path(proxy_path): Path<String>,
    body: Bytes,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "OpenAI-compatible proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_upstream_base_url(&state, &headers) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy upstream_base_url is not configured",
        );
    };

    let clean_path = proxy_path.trim_start_matches('/');
    if clean_path.is_empty() || clean_path.contains("..") {
        return json_error(StatusCode::BAD_REQUEST, "invalid proxy path");
    }

    let query = uri
        .query()
        .map(|value| format!("?{value}"))
        .unwrap_or_default();
    let upstream_url = format!(
        "{}/{}{}",
        upstream_base_url.trim_end_matches('/'),
        clean_path,
        query
    );
    let reqwest_method = match reqwest::Method::from_bytes(method.as_str().as_bytes()) {
        Ok(method) => method,
        Err(error) => return json_error(StatusCode::BAD_REQUEST, &error.to_string()),
    };
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(state.config.proxy.timeout_secs))
        .build()
    {
        Ok(client) => client,
        Err(error) => return json_error(StatusCode::INTERNAL_SERVER_ERROR, &error.to_string()),
    };

    let mut request = client.request(reqwest_method, upstream_url);
    request = apply_openai_proxy_headers(request, &headers, &state, &upstream_base_url);
    if !body.is_empty() {
        request = request.body(body);
    }

    match request.send().await {
        Ok(response) => proxy_response(response, false).await,
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
}

async fn proxy_kimi_v1_path(
    State(state): State<AppState>,
    headers: HeaderMap,
    method: Method,
    uri: Uri,
    Path(proxy_path): Path<String>,
    body: Bytes,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Kimi proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_kimi_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy kimi_upstream_base_url is not configured",
        );
    };

    let clean_path = proxy_path.trim_start_matches('/');
    if clean_path.is_empty() || clean_path.contains("..") {
        return json_error(StatusCode::BAD_REQUEST, "invalid proxy path");
    }

    let query = uri
        .query()
        .map(|value| format!("?{value}"))
        .unwrap_or_default();
    let upstream_url = format!(
        "{}/{}{}",
        upstream_base_url.trim_end_matches('/'),
        clean_path,
        query
    );
    let reqwest_method = match reqwest::Method::from_bytes(method.as_str().as_bytes()) {
        Ok(method) => method,
        Err(error) => return json_error(StatusCode::BAD_REQUEST, &error.to_string()),
    };
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(state.config.proxy.timeout_secs))
        .build()
    {
        Ok(client) => client,
        Err(error) => return json_error(StatusCode::INTERNAL_SERVER_ERROR, &error.to_string()),
    };

    let mut request = client.request(reqwest_method, upstream_url);
    request = apply_proxy_headers(request, &headers, &state);
    if !body.is_empty() {
        request = request.body(body);
    }

    match request.send().await {
        Ok(response) => proxy_response(response, false).await,
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
}

async fn proxy_mimo_v1_path(
    State(state): State<AppState>,
    headers: HeaderMap,
    method: Method,
    uri: Uri,
    Path(proxy_path): Path<String>,
    body: Bytes,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Mimo proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_mimo_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy mimo_upstream_base_url is not configured",
        );
    };

    let clean_path = proxy_path.trim_start_matches('/');
    if clean_path.is_empty() || clean_path.contains("..") {
        return json_error(StatusCode::BAD_REQUEST, "invalid proxy path");
    }
    let clean_path = if is_mimo_free_upstream(&upstream_base_url)
        && clean_path.eq_ignore_ascii_case("chat/completions")
    {
        "chat"
    } else {
        clean_path
    };

    let query = uri
        .query()
        .map(|value| format!("?{value}"))
        .unwrap_or_default();
    let upstream_url = format!(
        "{}/{}{}",
        upstream_base_url.trim_end_matches('/'),
        clean_path,
        query
    );
    let reqwest_method = match reqwest::Method::from_bytes(method.as_str().as_bytes()) {
        Ok(method) => method,
        Err(error) => return json_error(StatusCode::BAD_REQUEST, &error.to_string()),
    };
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(state.config.proxy.timeout_secs))
        .build()
    {
        Ok(client) => client,
        Err(error) => return json_error(StatusCode::INTERNAL_SERVER_ERROR, &error.to_string()),
    };

    let mut request = client.request(reqwest_method, upstream_url);
    request = match apply_proxy_headers_with_auth(
        request,
        &headers,
        &state,
        &client,
        &upstream_base_url,
        ProxyAuthMode::Mimo,
    )
    .await
    {
        Ok(request) => request,
        Err(error) => return json_error(StatusCode::BAD_GATEWAY, &error),
    };
    if !body.is_empty() {
        request = request.body(body);
    }

    match request.send().await {
        Ok(response) => proxy_response(response, false).await,
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
}

async fn proxy_anthropic_path(
    State(state): State<AppState>,
    headers: HeaderMap,
    method: Method,
    uri: Uri,
    Path(proxy_path): Path<String>,
    body: Bytes,
) -> Response {
    if !state.config.proxy.enabled {
        return json_error(StatusCode::NOT_FOUND, "Anthropic proxy is disabled");
    }

    let Some(upstream_base_url) = proxy_anthropic_upstream_base_url(&state) else {
        return json_error(
            StatusCode::BAD_REQUEST,
            "proxy anthropic_upstream_base_url is not configured",
        );
    };

    let clean_path = proxy_path.trim_start_matches('/');
    if clean_path.is_empty() || clean_path.contains("..") {
        return json_error(StatusCode::BAD_REQUEST, "invalid proxy path");
    }

    let query = uri
        .query()
        .map(|value| format!("?{value}"))
        .unwrap_or_default();
    let upstream_url = anthropic_upstream_url(&upstream_base_url, clean_path, &query);
    let reqwest_method = match reqwest::Method::from_bytes(method.as_str().as_bytes()) {
        Ok(method) => method,
        Err(error) => return json_error(StatusCode::BAD_REQUEST, &error.to_string()),
    };
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(state.config.proxy.timeout_secs))
        .build()
    {
        Ok(client) => client,
        Err(error) => return json_error(StatusCode::INTERNAL_SERVER_ERROR, &error.to_string()),
    };

    let mut request = client.request(reqwest_method, upstream_url);
    request = apply_proxy_headers(request, &headers, &state);
    if !body.is_empty() {
        request = request.body(body);
    }

    match request.send().await {
        Ok(response) => proxy_response(response, false).await,
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
}

async fn proxy_response(upstream: reqwest::Response, prefer_stream: bool) -> Response {
    let status =
        StatusCode::from_u16(upstream.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
    let content_type = upstream
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .map(ToOwned::to_owned);
    let should_stream = prefer_stream
        || content_type
            .as_deref()
            .map(|value| value.to_ascii_lowercase().contains("text/event-stream"))
            .unwrap_or(false);

    let mut builder = Response::builder().status(status);
    if let Some(content_type) = content_type {
        if let Ok(value) = HeaderValue::from_str(&content_type) {
            builder = builder.header(header::CONTENT_TYPE, value);
        }
    }

    if should_stream {
        return builder
            .body(Body::from_stream(upstream.bytes_stream()))
            .unwrap_or_else(|_| {
                json_error(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "failed to build proxy response",
                )
            });
    }

    match upstream.bytes().await {
        Ok(bytes) => builder.body(Body::from(bytes)).unwrap_or_else(|_| {
            json_error(
                StatusCode::INTERNAL_SERVER_ERROR,
                "failed to build proxy response",
            )
        }),
        Err(error) => json_error(StatusCode::BAD_GATEWAY, &error.to_string()),
    }
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
            state
                .store
                .add_activity(ActivityItem {
                    id: format!("act_{}", uuid::Uuid::new_v4()),
                    kind: "heartbeat".to_string(),
                    message: Some(format!("Heartbeat from {}", envelope.sender_hub.hub_id)),
                    source: None,
                    origin_hub_id: Some(envelope.sender_hub.hub_id),
                    state_key: None,
                    created_at: crate::ingest::now_iso(),
                })
                .await;
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
                outputs::emit_state_delete(&state.config, &incoming.key);
                reconcile_device_outputs(&state).await;
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
            Json(json!({ "changed": changed_count > 0, "changed_count": changed_count }))
                .into_response()
        }
        HubMessageType::StateUpsert => {
            match merge_one_remote_state(&state, trusted.as_ref(), envelope).await {
                Ok(changed) => {
                    if changed {
                        save_snapshot_best_effort(&state).await;
                    }
                    Json(json!({ "changed": changed })).into_response()
                }
                Err(response) => response,
            }
        }
    }
}

pub(crate) async fn apply_internal_event(
    state: &AppState,
    payload: IngestEventRequest,
) -> (String, bool) {
    let event = normalize_event(payload, &state.config);
    let previous_key = crate::ingest::task_key(
        &event.provenance.origin_hub_id,
        &event.source,
        event.workspace_hash.as_deref(),
        event
            .session_id
            .as_deref()
            .or(event.turn_id.as_deref())
            .or(Some(&event.id)),
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
        emit_state_changed(state, &task_state).await;
        save_snapshot_best_effort(state).await;
    }

    (task_state.key, changed)
}

async fn proxy_finish_event(
    state: &AppState,
    surface: &str,
    source: &str,
    session_id: &str,
    model: Option<String>,
    status: Status,
    message: String,
    latency_ms: u64,
) -> (String, bool) {
    apply_internal_event(
        state,
        IngestEventRequest {
            source: source.to_string(),
            surface: Some(surface.to_string()),
            workspace: None,
            workspace_hash: Some("proxy".to_string()),
            session_id: Some(session_id.to_string()),
            turn_id: None,
            event_type: if status == Status::Error {
                EventType::TurnFailed
            } else {
                EventType::TurnCompleted
            },
            status: Some(status),
            model,
            tool: None,
            message: Some(message),
            metrics: crate::model::Metrics {
                latency_ms: Some(latency_ms),
                ..Default::default()
            },
            severity: Default::default(),
            created_at: None,
        },
    )
    .await
}

fn apply_proxy_headers(
    mut request: reqwest::RequestBuilder,
    headers: &HeaderMap,
    state: &AppState,
) -> reqwest::RequestBuilder {
    request = apply_forwarded_proxy_headers(request, headers, state);
    apply_standard_proxy_auth(request, headers, state)
}

fn apply_openai_proxy_headers(
    mut request: reqwest::RequestBuilder,
    headers: &HeaderMap,
    state: &AppState,
    upstream_base_url: &str,
) -> reqwest::RequestBuilder {
    request = apply_forwarded_proxy_headers(request, headers, state);
    if is_mimo_upstream(upstream_base_url) {
        if let Some(api_key) = configured_proxy_mimo_api_key(state) {
            return request.bearer_auth(api_key).header("X-Mimo-Source", "hermes-agent");
        }
    }

    apply_standard_proxy_auth(request, headers, state)
}

async fn apply_proxy_headers_with_auth(
    request: reqwest::RequestBuilder,
    headers: &HeaderMap,
    state: &AppState,
    client: &reqwest::Client,
    upstream_base_url: &str,
    auth_mode: ProxyAuthMode,
) -> Result<reqwest::RequestBuilder, String> {
    let request = apply_forwarded_proxy_headers(request, headers, state);
    match auth_mode {
        ProxyAuthMode::Standard => Ok(apply_standard_proxy_auth(request, headers, state)),
        ProxyAuthMode::Mimo if is_mimo_free_upstream(upstream_base_url) => {
            let jwt = mimo_free_jwt(client, upstream_base_url).await?;
            Ok(request
                .bearer_auth(jwt)
                .header("X-Mimo-Source", "mimocode-cli-free"))
        }
        ProxyAuthMode::Mimo => Ok(apply_mimo_proxy_auth(request, headers, state)),
    }
}

fn apply_forwarded_proxy_headers(
    mut request: reqwest::RequestBuilder,
    headers: &HeaderMap,
    state: &AppState,
) -> reqwest::RequestBuilder {
    for (name, value) in headers.iter() {
        if !should_forward_proxy_header(name.as_str(), state) {
            continue;
        }

        let Ok(header_name) = reqwest::header::HeaderName::from_bytes(name.as_str().as_bytes())
        else {
            continue;
        };
        let Ok(header_value) = reqwest::header::HeaderValue::from_bytes(value.as_bytes()) else {
            continue;
        };
        request = request.header(header_name, header_value);
    }

    request
}

fn apply_standard_proxy_auth(
    mut request: reqwest::RequestBuilder,
    headers: &HeaderMap,
    state: &AppState,
) -> reqwest::RequestBuilder {
    if let Some(api_key) = configured_proxy_api_key(state) {
        request = request.bearer_auth(api_key);
    } else if let Some(auth) = headers
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
    {
        request = request.header(reqwest::header::AUTHORIZATION, auth);
    }

    request
}

fn apply_mimo_proxy_auth(
    request: reqwest::RequestBuilder,
    headers: &HeaderMap,
    state: &AppState,
) -> reqwest::RequestBuilder {
    if let Some(api_key) = configured_proxy_mimo_api_key(state) {
        request
            .bearer_auth(api_key)
            .header("X-Mimo-Source", "mimocode-cli")
    } else {
        apply_standard_proxy_auth(request, headers, state)
    }
}

fn proxy_mimo_chat_payload(
    state: &AppState,
    upstream_base_url: &str,
    mut payload: serde_json::Value,
) -> serde_json::Value {
    if is_mimo_free_upstream(upstream_base_url) {
        return payload;
    }

    let Some(model) = payload.get("model").and_then(|value| value.as_str()) else {
        return payload;
    };
    if model != "mimo-auto" {
        return payload;
    }
    let Some(upstream_model) = configured_proxy_mimo_upstream_model(state) else {
        return payload;
    };
    if let Some(object) = payload.as_object_mut() {
        object.insert("model".to_string(), json!(upstream_model));
    }

    payload
}

async fn mimo_free_jwt(
    client: &reqwest::Client,
    upstream_base_url: &str,
) -> Result<String, String> {
    let client_id = mimo_free_client_id()?;
    let bootstrap_url = mimo_free_bootstrap_url(upstream_base_url)
        .ok_or_else(|| "Mimo free bootstrap URL is not available".to_string())?;
    let response = client
        .post(bootstrap_url)
        .json(&json!({ "client": client_id }))
        .send()
        .await
        .map_err(|error| format!("Mimo free bootstrap failed: {error}"))?;
    let status = response.status();
    if !status.is_success() {
        let body = response.text().await.unwrap_or_default();
        let body = body.chars().take(200).collect::<String>();
        return Err(format!(
            "Mimo free bootstrap returned HTTP {status}: {body}"
        ));
    }

    let payload = response
        .json::<MimoFreeBootstrapResponse>()
        .await
        .map_err(|error| format!("Mimo free bootstrap response parse failed: {error}"))?;
    let jwt = payload.jwt.trim();
    if jwt.is_empty() {
        return Err("Mimo free bootstrap response missing jwt".to_string());
    }

    Ok(jwt.to_string())
}

fn mimo_free_client_id() -> Result<String, String> {
    if let Ok(value) = env::var("MIMO_FREE_CLIENT") {
        let value = value.trim();
        if !value.is_empty() {
            return Ok(value.to_string());
        }
    }

    let mut paths = Vec::new();
    if let Ok(path) = env::var("MIMO_FREE_CLIENT_PATH") {
        paths.push(PathBuf::from(path));
    }
    if let Ok(profile) = env::var("USERPROFILE") {
        paths.push(
            PathBuf::from(profile)
                .join(".local")
                .join("share")
                .join("mimocode")
                .join("mimo-free-client"),
        );
    }
    if let Ok(home) = env::var("HOME") {
        paths.push(
            PathBuf::from(home)
                .join(".local")
                .join("share")
                .join("mimocode")
                .join("mimo-free-client"),
        );
    }

    for path in paths {
        let Ok(value) = fs::read_to_string(&path) else {
            continue;
        };
        let value = value.trim();
        if !value.is_empty() {
            return Ok(value.to_string());
        }
    }

    Err(
        "Mimo free client fingerprint not found; run mimo once or set MIMO_FREE_CLIENT_PATH"
            .to_string(),
    )
}

fn mimo_free_bootstrap_url(upstream_base_url: &str) -> Option<String> {
    let base = upstream_base_url.trim().trim_end_matches('/');
    base.strip_suffix("/api/free-ai/openai")
        .map(|root| format!("{root}/api/free-ai/bootstrap"))
}

fn is_mimo_free_upstream(upstream_base_url: &str) -> bool {
    mimo_free_bootstrap_url(upstream_base_url).is_some()
}

fn is_mimo_upstream(upstream_base_url: &str) -> bool {
    reqwest::Url::parse(upstream_base_url)
        .ok()
        .and_then(|url| url.host_str().map(|host| host.to_ascii_lowercase()))
        .is_some_and(|host| host.ends_with("xiaomimimo.com"))
}

fn mimo_openai_chat_path(upstream_base_url: &str) -> &'static str {
    if is_mimo_free_upstream(upstream_base_url) {
        "chat"
    } else {
        "chat/completions"
    }
}

fn should_forward_proxy_header(name: &str, state: &AppState) -> bool {
    if name.eq_ignore_ascii_case(state.config.proxy.source_header.trim())
        || name.eq_ignore_ascii_case(state.config.proxy.upstream_base_url_header.trim())
    {
        return false;
    }

    !matches!(
        name.to_ascii_lowercase().as_str(),
        "authorization"
            | "connection"
            | "content-length"
            | "host"
            | "keep-alive"
            | "proxy-authenticate"
            | "proxy-authorization"
            | "te"
            | "trailer"
            | "transfer-encoding"
            | "upgrade"
    )
}

fn proxy_upstream_base_url(state: &AppState, headers: &HeaderMap) -> Option<String> {
    let header_name = state.config.proxy.upstream_base_url_header.trim();
    if !header_name.is_empty() {
        if let Some(value) = headers
            .get(header_name)
            .and_then(|value| value.to_str().ok())
        {
            let value = value.trim().trim_end_matches('/');
            if is_valid_upstream_base_url(value) {
                return Some(value.to_string());
            }
        }
    }

    state
        .config
        .proxy
        .upstream_base_url
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(|value| value.trim_end_matches('/').to_string())
}

fn proxy_anthropic_upstream_base_url(state: &AppState) -> Option<String> {
    state
        .config
        .proxy
        .anthropic_upstream_base_url
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(|value| value.trim_end_matches('/'))
        .filter(|value| is_valid_upstream_base_url(value))
        .map(ToOwned::to_owned)
}

fn proxy_kimi_upstream_base_url(state: &AppState) -> Option<String> {
    state
        .config
        .proxy
        .kimi_upstream_base_url
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(|value| value.trim_end_matches('/'))
        .filter(|value| is_valid_upstream_base_url(value))
        .map(ToOwned::to_owned)
}

fn proxy_mimo_upstream_base_url(state: &AppState) -> Option<String> {
    state
        .config
        .proxy
        .mimo_upstream_base_url
        .as_deref()
        .or(state.config.proxy.upstream_base_url.as_deref())
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(|value| value.trim_end_matches('/'))
        .filter(|value| is_valid_upstream_base_url(value))
        .map(ToOwned::to_owned)
}

fn anthropic_upstream_url(base_url: &str, path: &str, query: &str) -> String {
    let base = base_url.trim_end_matches('/');
    let clean_path = path.trim_start_matches('/');
    let clean_path = if base.ends_with("/v1") {
        clean_path.strip_prefix("v1/").unwrap_or(clean_path)
    } else {
        clean_path
    };
    format!("{base}/{clean_path}{query}")
}

fn is_valid_upstream_base_url(value: &str) -> bool {
    reqwest::Url::parse(value)
        .map(|url| matches!(url.scheme(), "http" | "https") && url.host_str().is_some())
        .unwrap_or(false)
}

fn proxy_source(state: &AppState, headers: &HeaderMap) -> String {
    let header_name = state.config.proxy.source_header.trim();
    if !header_name.is_empty() {
        if let Some(value) = headers
            .get(header_name)
            .and_then(|value| value.to_str().ok())
        {
            let value = value.trim();
            if is_valid_source_label(value) {
                return value.to_string();
            }
        }
    }

    state.config.proxy.source.clone()
}

fn proxy_anthropic_source(state: &AppState) -> String {
    let value = state.config.proxy.anthropic_source.trim();
    if is_valid_source_label(value) {
        value.to_string()
    } else {
        "vscode-claude-code".to_string()
    }
}

fn proxy_kimi_source(state: &AppState) -> String {
    let value = state.config.proxy.kimi_source.trim();
    if is_valid_source_label(value) {
        value.to_string()
    } else {
        "vscode-kimi-code".to_string()
    }
}

fn proxy_mimo_source(state: &AppState) -> String {
    let value = state.config.proxy.mimo_source.trim();
    if is_valid_source_label(value) {
        value.to_string()
    } else {
        "mimo-code".to_string()
    }
}

fn is_valid_source_label(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
}

fn configured_proxy_api_key(state: &AppState) -> Option<&str> {
    state
        .config
        .proxy
        .api_key
        .as_deref()
        .filter(|value| !value.trim().is_empty())
}

fn configured_proxy_mimo_api_key(state: &AppState) -> Option<String> {
    env::var("XIAOMI_API_KEY")
        .ok()
        .or_else(|| env::var("MIMO_API_KEY").ok())
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
        .or_else(|| {
            state
                .config
                .proxy
                .mimo_api_key
                .as_deref()
                .map(str::trim)
                .filter(|value| !value.is_empty())
                .map(ToOwned::to_owned)
        })
}

fn configured_proxy_mimo_upstream_model(state: &AppState) -> Option<String> {
    state
        .config
        .proxy
        .mimo_upstream_model
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(ToOwned::to_owned)
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
        emit_state_changed(app, &merged).await;
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

async fn emit_state_changed(app: &AppState, state: &crate::model::TaskState) {
    outputs::emit_state_upsert(&app.config, state);
    reconcile_device_outputs(app).await;
}

async fn reconcile_device_outputs(app: &AppState) {
    let states = app.store.all_states().await;
    outputs::reconcile_device_outputs(&app.config, &states, &app.outputs).await;
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
