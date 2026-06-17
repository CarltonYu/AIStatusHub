use std::{
    collections::HashMap,
    sync::Mutex,
    time::{Duration, Instant},
};

use serde_json::json;
use time::{format_description::well_known::Rfc3339, OffsetDateTime};
use tokio::net::UdpSocket;

use crate::{
    config::{AppConfig, OutputConfig},
    model::{Locality, Status, TaskState},
};

#[derive(Debug, Default)]
pub struct OutputRuntime {
    device_modes: Mutex<HashMap<String, DeviceModeState>>,
}

#[derive(Debug, Clone)]
struct DeviceModeState {
    mode: String,
    sent_at: Instant,
}

impl OutputRuntime {
    pub fn new() -> Self {
        Self::default()
    }
}

pub fn emit_state_upsert(config: &AppConfig, state: &TaskState) {
    for output in config.outputs.iter().filter(|output| output.enabled) {
        match output.output_type.as_str() {
            "stdout" | "print" => print_state(output.id.as_str(), "state.upsert", state),
            _ => {}
        }
    }
}

pub fn emit_state_delete(config: &AppConfig, key: &str) {
    for output in config.outputs.iter().filter(|output| output.enabled) {
        match output.output_type.as_str() {
            "stdout" | "print" => {
                println!(
                    "{}",
                    json!({
                        "output": output.id,
                        "type": "state.delete",
                        "key": key
                    })
                );
            }
            _ => {}
        }
    }
}

pub async fn reconcile_device_outputs(
    config: &AppConfig,
    states: &[TaskState],
    runtime: &OutputRuntime,
) {
    for output in config.outputs.iter().filter(|output| output.enabled) {
        if !is_irisoled_udp_output(&output.output_type) {
            continue;
        }

        let now = OffsetDateTime::now_utc();
        let busy = states
            .iter()
            .filter(|state| output.include_remote_states || state.locality == Locality::Local)
            .any(|state| is_active_busy_state(state, output.busy_state_ttl_ms, now));
        let desired_mode = if busy { "busy" } else { "idle" };
        let refresh_interval = Duration::from_millis(output.refresh_interval_ms);
        let send_started_at = Instant::now();

        {
            let modes = runtime
                .device_modes
                .lock()
                .unwrap_or_else(|err| err.into_inner());
            if modes.get(&output.id).is_some_and(|previous| {
                previous.mode == desired_mode && previous.sent_at.elapsed() < refresh_interval
            }) {
                continue;
            }
        }

        match send_irisoled_udp(output, desired_mode).await {
            Ok(()) => {
                runtime
                    .device_modes
                    .lock()
                    .unwrap_or_else(|err| err.into_inner())
                    .insert(
                        output.id.clone(),
                        DeviceModeState {
                            mode: desired_mode.to_string(),
                            sent_at: send_started_at,
                        },
                    );
            }
            Err(error) => {
                tracing::warn!(
                    output_id = %output.id,
                    mode = desired_mode,
                    %error,
                    "failed to send IrisOLED UDP output"
                );
            }
        }
    }
}

fn print_state(output_id: &str, kind: &str, state: &TaskState) {
    println!(
        "{}",
        json!({
            "output": output_id,
            "type": kind,
            "key": state.key,
            "locality": state.locality,
            "source": state.source,
            "origin_hub_id": state.origin_hub_id,
            "origin_hub_name": state.origin_hub_name,
            "status": state.status,
            "active_tool": state.active_tool,
            "last_message": state.last_message,
            "workspace_hash": state.workspace_hash,
            "session_id": state.session_id,
            "updated_at": state.updated_at,
            "hub_path": state.hub_path,
        })
    );
}

fn is_irisoled_udp_output(output_type: &str) -> bool {
    matches!(
        output_type,
        "irisoled-udp" | "iris-oled-udp" | "duo-irisoled" | "duo-face"
    )
}

fn is_busy_status(status: &Status) -> bool {
    matches!(
        status,
        Status::Starting
            | Status::Thinking
            | Status::Streaming
            | Status::ToolRunning
            | Status::WaitingApproval
            | Status::Blocked
            | Status::Compacting
    )
}

fn is_active_busy_state(state: &TaskState, busy_state_ttl_ms: u64, now: OffsetDateTime) -> bool {
    if !is_busy_status(&state.status) {
        return false;
    }
    if busy_state_ttl_ms == 0 {
        return true;
    }

    let Ok(updated_at) = OffsetDateTime::parse(&state.updated_at, &Rfc3339) else {
        return true;
    };
    if updated_at > now {
        return true;
    }

    let ttl_ms = busy_state_ttl_ms.min(i64::MAX as u64) as i64;
    now - updated_at <= time::Duration::milliseconds(ttl_ms)
}

async fn send_irisoled_udp(output: &OutputConfig, mode: &str) -> std::io::Result<()> {
    let Some(target) = irisoled_target(output) else {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "missing irisoled UDP url",
        ));
    };
    let command = irisoled_command(output, mode);
    let socket = UdpSocket::bind("0.0.0.0:0").await?;
    socket.send_to(command.as_bytes(), target.as_str()).await?;

    tracing::info!(
        output_id = %output.id,
        target = %target,
        mode,
        command = %command,
        "sent IrisOLED UDP output"
    );

    Ok(())
}

fn irisoled_target(output: &OutputConfig) -> Option<String> {
    output
        .url
        .as_deref()
        .map(str::trim)
        .filter(|url| !url.is_empty())
        .map(|url| url.strip_prefix("udp://").unwrap_or(url))
        .map(|target| target.trim_end_matches('/').to_string())
}

fn irisoled_command(output: &OutputConfig, mode: &str) -> String {
    match mode {
        "busy" => {
            let expression = expression_name(output.busy_expression.as_deref(), "angry");
            format!("default {expression}")
        }
        _ => {
            let expression = expression_name(output.idle_expression.as_deref(), "normal");
            if expression == "normal" {
                "normal".to_string()
            } else {
                format!("default {expression}")
            }
        }
    }
}

fn expression_name(value: Option<&str>, default: &str) -> String {
    let value = value.map(str::trim).filter(|value| !value.is_empty());
    let value = value.unwrap_or(default);

    if value
        .bytes()
        .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'-')
    {
        value.to_string()
    } else {
        default.to_string()
    }
}
