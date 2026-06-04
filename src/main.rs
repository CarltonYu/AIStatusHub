mod api;
mod cli;
mod config;
mod hub;
mod ingest;
mod model;
mod outputs;
mod security;
mod snapshot;
mod state;
mod web;

use std::{net::SocketAddr, sync::Arc};

use anyhow::Context;
use tokio::net::TcpListener;
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    if cli::is_report_command() {
        cli::run_report_command().await?;
        return Ok(());
    }

    tracing_subscriber::registry()
        .with(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "aistatushub=info,tower_http=warn,axum=warn".into()),
        )
        .with(tracing_subscriber::fmt::layer())
        .init();

    let config_path = config::config_path_from_args();
    let config = config::load_or_create_config(&config_path)
        .with_context(|| format!("failed to load config {}", config_path.display()))?;
    let store = Arc::new(state::StateStore::new(config.state.activity_ring_size));

    if let Err(error) = snapshot::restore_snapshot(&config, &store).await {
        tracing::warn!(%error, "failed to restore state snapshot");
    }

    let app_state = api::AppState::new(Arc::new(config), store);
    let app = api::router(app_state.clone());
    let addr: SocketAddr = format!(
        "{}:{}",
        app_state.config.server.host, app_state.config.server.port
    )
    .parse()
    .context("invalid server host/port")?;
    let listener = TcpListener::bind(addr).await?;

    tracing::info!(
        hub_id = %app_state.config.hub.hub_id,
        address = %addr,
        "AIStatusHub listening"
    );

    axum::serve(listener, app).await?;

    Ok(())
}
