use std::fs;

use anyhow::{Context, Result};

use crate::{config::AppConfig, state::{SnapshotData, StateStore}};

pub async fn restore_snapshot(config: &AppConfig, store: &StateStore) -> Result<()> {
    if !config.state.snapshot_enabled || !config.state.snapshot_path.exists() {
        return Ok(());
    }

    let text = fs::read_to_string(&config.state.snapshot_path)
        .with_context(|| format!("read snapshot {}", config.state.snapshot_path.display()))?;
    let snapshot: SnapshotData = serde_json::from_str(&text)?;
    store.restore_snapshot(snapshot).await;
    Ok(())
}

pub async fn save_snapshot(config: &AppConfig, store: &StateStore) -> Result<()> {
    if !config.state.snapshot_enabled {
        return Ok(());
    }

    if let Some(parent) = config.state.snapshot_path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)?;
        }
    }

    let snapshot = store.to_snapshot().await;
    let text = serde_json::to_string_pretty(&snapshot)?;
    fs::write(&config.state.snapshot_path, text)
        .with_context(|| format!("write snapshot {}", config.state.snapshot_path.display()))?;

    Ok(())
}
