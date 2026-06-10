use std::{
    env, fs,
    path::{Path, PathBuf},
    time::{Duration, SystemTime},
};

use crate::{
    api,
    config::CodexLocalMonitorConfig,
    model::{EventType, IngestEventRequest, Status},
};

pub fn spawn(app: api::AppState) {
    if app.config.monitors.codex_local.enabled {
        tokio::spawn(run_codex_local_monitor(app));
    }
}

async fn run_codex_local_monitor(app: api::AppState) {
    let config = app.config.monitors.codex_local.clone();
    let Some(sessions_dir) = codex_sessions_dir(&config) else {
        tracing::warn!("Codex local monitor enabled but no sessions directory could be resolved");
        return;
    };

    let poll_interval = Duration::from_millis(config.poll_interval_ms.max(500));
    let idle_after = Duration::from_millis(config.idle_after_ms.max(5_000));
    let mut ticker = tokio::time::interval(poll_interval);
    let mut active = false;
    let mut last_turn_id: Option<String> = None;

    tracing::info!(
        sessions_dir = %sessions_dir.display(),
        source = %config.source,
        "Codex local monitor started"
    );

    loop {
        ticker.tick().await;

        let sessions_dir = sessions_dir.clone();
        let max_scan_files = config.max_scan_files.max(1);
        let scan = tokio::task::spawn_blocking(move || {
            latest_session_file(&sessions_dir, max_scan_files)
        })
        .await;

        let latest = match scan {
            Ok(Ok(latest)) => latest,
            Ok(Err(error)) => {
                tracing::warn!(%error, "Codex local monitor scan failed");
                continue;
            }
            Err(error) => {
                tracing::warn!(%error, "Codex local monitor task failed");
                continue;
            }
        };

        let Some(latest) = latest else {
            if active {
                active = false;
                report_codex_monitor(
                    &app,
                    &config,
                    EventType::TurnCompleted,
                    Status::Completed,
                    None,
                    "Codex local activity idle",
                )
                .await;
            }
            continue;
        };

        let age = SystemTime::now()
            .duration_since(latest.modified_at)
            .unwrap_or(Duration::ZERO);
        let is_busy = age <= idle_after;

        if is_busy {
            if !active {
                active = true;
                last_turn_id = Some(latest.session_id.clone());
                report_codex_monitor(
                    &app,
                    &config,
                    EventType::PromptSubmitted,
                    Status::Thinking,
                    Some(latest.session_id),
                    "Codex local activity detected",
                )
                .await;
            } else {
                last_turn_id = Some(latest.session_id);
            }
        } else if active {
            active = false;
            report_codex_monitor(
                &app,
                &config,
                EventType::TurnCompleted,
                Status::Completed,
                last_turn_id.take(),
                "Codex local activity idle",
            )
            .await;
        }
    }
}

async fn report_codex_monitor(
    app: &api::AppState,
    config: &CodexLocalMonitorConfig,
    event_type: EventType,
    status: Status,
    turn_id: Option<String>,
    message: &str,
) {
    let source = if config.source.trim().is_empty() {
        "codex-local"
    } else {
        config.source.trim()
    };

    let _ = api::apply_internal_event(
        app,
        IngestEventRequest {
            source: source.to_string(),
            surface: Some("codex-local-monitor".to_string()),
            workspace: None,
            workspace_hash: Some("codex-local".to_string()),
            session_id: Some("codex-local-monitor".to_string()),
            turn_id,
            event_type,
            status: Some(status),
            model: None,
            tool: None,
            message: Some(message.to_string()),
            metrics: Default::default(),
            severity: Default::default(),
            created_at: None,
        },
    )
    .await;
}

#[derive(Debug, Clone)]
struct SessionFile {
    session_id: String,
    modified_at: SystemTime,
}

fn latest_session_file(dir: &Path, max_scan_files: usize) -> anyhow::Result<Option<SessionFile>> {
    if !dir.exists() {
        return Ok(None);
    }

    let mut latest: Option<SessionFile> = None;
    let mut scanned_files = 0usize;
    visit_session_dir(dir, max_scan_files, &mut scanned_files, &mut latest)?;
    Ok(latest)
}

fn visit_session_dir(
    dir: &Path,
    max_scan_files: usize,
    scanned_files: &mut usize,
    latest: &mut Option<SessionFile>,
) -> anyhow::Result<()> {
    if *scanned_files >= max_scan_files {
        return Ok(());
    }

    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        let file_type = entry.file_type()?;

        if file_type.is_dir() {
            visit_session_dir(&path, max_scan_files, scanned_files, latest)?;
            if *scanned_files >= max_scan_files {
                return Ok(());
            }
            continue;
        }

        if !is_codex_rollout_file(&path) {
            continue;
        }

        *scanned_files += 1;
        let metadata = entry.metadata()?;
        let modified_at = metadata.modified()?;
        let is_newer = latest
            .as_ref()
            .map(|item| modified_at > item.modified_at)
            .unwrap_or(true);

        if is_newer {
            *latest = Some(SessionFile {
                session_id: session_id_from_path(&path).unwrap_or_else(|| "unknown".to_string()),
                modified_at,
            });
        }

        if *scanned_files >= max_scan_files {
            return Ok(());
        }
    }

    Ok(())
}

fn is_codex_rollout_file(path: &Path) -> bool {
    path.extension().and_then(|value| value.to_str()) == Some("jsonl")
        && path
            .file_name()
            .and_then(|value| value.to_str())
            .map(|name| name.starts_with("rollout-"))
            .unwrap_or(false)
}

fn session_id_from_path(path: &Path) -> Option<String> {
    let stem = path.file_stem()?.to_str()?;
    if stem.len() >= 36 {
        Some(stem[stem.len() - 36..].to_string())
    } else {
        None
    }
}

fn codex_sessions_dir(config: &CodexLocalMonitorConfig) -> Option<PathBuf> {
    if let Some(path) = config
        .sessions_dir
        .as_ref()
        .filter(|path| !path.as_os_str().is_empty())
    {
        return Some(path.clone());
    }

    if let Ok(codex_home) = env::var("CODEX_HOME") {
        let path = PathBuf::from(codex_home);
        if !path.as_os_str().is_empty() {
            return Some(path.join("sessions"));
        }
    }

    if cfg!(windows) {
        if let Ok(user_profile) = env::var("USERPROFILE") {
            let path = PathBuf::from(user_profile);
            if !path.as_os_str().is_empty() {
                return Some(path.join(".codex").join("sessions"));
            }
        }
    }

    env::var("HOME")
        .ok()
        .map(PathBuf::from)
        .filter(|path| !path.as_os_str().is_empty())
        .map(|path| path.join(".codex").join("sessions"))
}
