use std::{
    collections::HashMap,
    env,
    io::{self, Read},
    path::PathBuf,
};

use anyhow::{bail, Context};
use serde_json::json;

pub fn is_report_command() -> bool {
    env::args().nth(1).as_deref() == Some("report")
}

pub async fn run_report_command() -> anyhow::Result<()> {
    let args = parse_args(env::args().skip(2).collect());
    let quiet = args.contains_key("quiet");
    let is_codex_hook = args.contains_key("codex-hook");
    let url = args
        .get("url")
        .cloned()
        .unwrap_or_else(|| "http://127.0.0.1:17888/api/v1/ingest/event".to_string());
    let token = args
        .get("token")
        .cloned()
        .or_else(|| env::var("AI_STATUS_HUB_TOKEN").ok());
    let mut source = args
        .get("source")
        .cloned()
        .unwrap_or_else(|| "manual".to_string());
    let mut event_type = args
        .get("event-type")
        .or_else(|| args.get("event_type"))
        .cloned()
        .unwrap_or_else(|| "notification".to_string());
    let mut workspace = args
        .get("workspace")
        .cloned()
        .or_else(|| env::current_dir().ok().map(path_to_string));
    let mut message = args.get("message").cloned();
    let mut session_id = args
        .get("session-id")
        .or_else(|| args.get("session_id"))
        .cloned();
    let mut turn_id = args.get("turn-id").or_else(|| args.get("turn_id")).cloned();
    let mut model = args.get("model").cloned();
    let mut status = args.get("status").cloned();
    let mut tool_name = args.get("tool").or_else(|| args.get("tool-name")).cloned();
    let mut input_preview = args
        .get("input-preview")
        .or_else(|| args.get("input_preview"))
        .cloned();

    if is_codex_hook {
        let hook = read_stdin_json()?;
        source = "codex".to_string();
        workspace = hook
            .get("cwd")
            .and_then(|value| value.as_str())
            .map(ToOwned::to_owned)
            .or(workspace);
        session_id = hook
            .get("session_id")
            .and_then(|value| value.as_str())
            .map(ToOwned::to_owned)
            .or(session_id);
        turn_id = hook
            .get("turn_id")
            .and_then(|value| value.as_str())
            .map(ToOwned::to_owned)
            .or(turn_id);
        model = hook
            .get("model")
            .and_then(|value| value.as_str())
            .map(ToOwned::to_owned)
            .or(model);
        tool_name = hook
            .get("tool_name")
            .and_then(|value| value.as_str())
            .map(ToOwned::to_owned)
            .or(tool_name);
        input_preview = hook
            .pointer("/tool_input/command")
            .and_then(|value| value.as_str())
            .map(ToOwned::to_owned)
            .or(input_preview);

        let hook_event = hook
            .get("hook_event_name")
            .and_then(|value| value.as_str())
            .unwrap_or("Notification");
        let mapped = map_codex_hook_event(hook_event);
        event_type = mapped.event_type.to_string();
        status = mapped.status.map(ToOwned::to_owned).or(status);
        message = Some(mapped.message.to_string());
    }

    if source.trim().is_empty() {
        bail!("--source cannot be empty");
    }

    let mut body = json!({
        "source": source,
        "event_type": event_type,
    });

    insert_optional(&mut body, "workspace", workspace);
    insert_optional(&mut body, "message", message);
    insert_optional(&mut body, "session_id", session_id);
    insert_optional(&mut body, "turn_id", turn_id);
    insert_optional(&mut body, "model", model);
    insert_optional(&mut body, "status", status);

    if tool_name.is_some() || input_preview.is_some() {
        body["tool"] = json!({
            "name": tool_name.unwrap_or_else(|| "unknown".to_string()),
            "input_preview": input_preview
        });
    }

    let client = reqwest::Client::new();
    let mut request = client.post(url).json(&body);

    if let Some(token) = token {
        request = request.bearer_auth(token);
    }

    let response = request.send().await.context("failed to send report")?;
    let status = response.status();
    let text = response.text().await.unwrap_or_default();

    if !status.is_success() {
        bail!("report failed with status {status}: {text}");
    }

    if !quiet {
        println!("{text}");
    }
    Ok(())
}

fn parse_args(args: Vec<String>) -> HashMap<String, String> {
    let mut map = HashMap::new();
    let mut index = 0usize;

    while index < args.len() {
        let arg = &args[index];

        if let Some(key) = arg.strip_prefix("--") {
            if let Some((key, value)) = key.split_once('=') {
                map.insert(key.to_string(), value.to_string());
            } else if key == "quiet" || key == "codex-hook" {
                map.insert(key.to_string(), "true".to_string());
            } else if let Some(value) = args.get(index + 1) {
                map.insert(key.to_string(), value.clone());
                index += 1;
            }
        }

        index += 1;
    }

    map
}

fn insert_optional(body: &mut serde_json::Value, key: &str, value: Option<String>) {
    if let Some(value) = value {
        body[key] = serde_json::Value::String(value);
    }
}

fn path_to_string(path: PathBuf) -> String {
    path.to_string_lossy().replace('\\', "/")
}

fn read_stdin_json() -> anyhow::Result<serde_json::Value> {
    let mut text = String::new();
    io::stdin().read_to_string(&mut text)?;

    if text.trim().is_empty() {
        return Ok(serde_json::Value::Object(Default::default()));
    }

    Ok(serde_json::from_str(&text)?)
}

struct MappedHookEvent {
    event_type: &'static str,
    status: Option<&'static str>,
    message: &'static str,
}

fn map_codex_hook_event(name: &str) -> MappedHookEvent {
    match name {
        "SessionStart" => MappedHookEvent {
            event_type: "session.started",
            status: Some("starting"),
            message: "Codex session started",
        },
        "UserPromptSubmit" => MappedHookEvent {
            event_type: "prompt.submitted",
            status: Some("thinking"),
            message: "Codex prompt submitted",
        },
        "PreToolUse" => MappedHookEvent {
            event_type: "tool.started",
            status: Some("tool_running"),
            message: "Codex tool started",
        },
        "PermissionRequest" => MappedHookEvent {
            event_type: "approval.requested",
            status: Some("waiting_approval"),
            message: "Codex approval requested",
        },
        "PostToolUse" => MappedHookEvent {
            event_type: "tool.completed",
            status: Some("thinking"),
            message: "Codex tool completed",
        },
        "Stop" | "SubagentStop" => MappedHookEvent {
            event_type: "turn.completed",
            status: Some("completed"),
            message: "Codex turn completed",
        },
        "PreCompact" => MappedHookEvent {
            event_type: "notification",
            status: Some("compacting"),
            message: "Codex compacting",
        },
        "PostCompact" => MappedHookEvent {
            event_type: "notification",
            status: Some("thinking"),
            message: "Codex compacted",
        },
        _ => MappedHookEvent {
            event_type: "notification",
            status: None,
            message: "Codex hook event",
        },
    }
}
