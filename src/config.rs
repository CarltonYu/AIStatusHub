use std::{
    env, fs,
    path::{Path, PathBuf},
};

use anyhow::{Context, Result};
use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppConfig {
    #[serde(default)]
    pub server: ServerConfig,
    #[serde(default)]
    pub hub: HubConfig,
    #[serde(default)]
    pub privacy: PrivacyConfig,
    #[serde(default)]
    pub state: StateConfig,
    #[serde(default)]
    pub proxy: ProxyConfig,
    #[serde(default)]
    pub monitors: MonitorsConfig,
    #[serde(default)]
    pub trusted_hubs: Vec<TrustedHubConfig>,
    #[serde(default)]
    pub outputs: Vec<OutputConfig>,
    #[serde(skip)]
    pub config_path: PathBuf,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ServerConfig {
    #[serde(default = "default_host")]
    pub host: String,
    #[serde(default = "default_port")]
    pub port: u16,
    pub auth_token: Option<String>,
}

impl Default for ServerConfig {
    fn default() -> Self {
        Self {
            host: default_host(),
            port: default_port(),
            auth_token: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HubConfig {
    #[serde(default = "default_generated_hub_id")]
    pub hub_id: String,
    #[serde(default = "default_display_name")]
    pub display_name: String,
    #[serde(default)]
    pub accept_remote_hubs: bool,
    #[serde(default = "default_max_hops")]
    pub max_hops: usize,
}

impl Default for HubConfig {
    fn default() -> Self {
        Self {
            hub_id: default_generated_hub_id(),
            display_name: default_display_name(),
            accept_remote_hubs: false,
            max_hops: default_max_hops(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PrivacyConfig {
    #[serde(default)]
    pub store_raw_events: bool,
    #[serde(default)]
    pub store_prompt_text: bool,
    #[serde(default = "default_true")]
    pub hash_workspace: bool,
    #[serde(default = "default_max_preview_length")]
    pub max_preview_length: usize,
}

impl Default for PrivacyConfig {
    fn default() -> Self {
        Self {
            store_raw_events: false,
            store_prompt_text: false,
            hash_workspace: true,
            max_preview_length: default_max_preview_length(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StateConfig {
    #[serde(default = "default_activity_ring_size")]
    pub activity_ring_size: usize,
    #[serde(default)]
    pub snapshot_enabled: bool,
    #[serde(default = "default_snapshot_path")]
    pub snapshot_path: PathBuf,
    #[serde(default = "default_snapshot_debounce_ms")]
    pub snapshot_debounce_ms: u64,
    #[serde(default = "default_remote_state_ttl_ms")]
    pub remote_state_ttl_ms: u64,
}

impl Default for StateConfig {
    fn default() -> Self {
        Self {
            activity_ring_size: default_activity_ring_size(),
            snapshot_enabled: false,
            snapshot_path: default_snapshot_path(),
            snapshot_debounce_ms: default_snapshot_debounce_ms(),
            remote_state_ttl_ms: default_remote_state_ttl_ms(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrustedHubConfig {
    pub hub_id: String,
    pub display_name: Option<String>,
    pub token: Option<String>,
    #[serde(default)]
    pub allow_relay: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OutputConfig {
    pub id: String,
    #[serde(rename = "type")]
    pub output_type: String,
    #[serde(default)]
    pub enabled: bool,
    pub url: Option<String>,
    pub busy_expression: Option<String>,
    pub idle_expression: Option<String>,
    #[serde(default = "default_output_busy_state_ttl_ms")]
    pub busy_state_ttl_ms: u64,
    #[serde(default = "default_output_refresh_interval_ms")]
    pub refresh_interval_ms: u64,
    pub target_hub_id: Option<String>,
    pub token: Option<String>,
    pub format: Option<String>,
    #[serde(default)]
    pub send_snapshot_on_start: bool,
    #[serde(default)]
    pub include_remote_states: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProxyConfig {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default = "default_proxy_source")]
    pub source: String,
    #[serde(default = "default_proxy_anthropic_source")]
    pub anthropic_source: String,
    #[serde(default = "default_proxy_kimi_source")]
    pub kimi_source: String,
    #[serde(default = "default_proxy_mimo_source")]
    pub mimo_source: String,
    #[serde(default = "default_proxy_source_header")]
    pub source_header: String,
    #[serde(default = "default_proxy_upstream_base_url_header")]
    pub upstream_base_url_header: String,
    pub upstream_base_url: Option<String>,
    pub anthropic_upstream_base_url: Option<String>,
    pub kimi_upstream_base_url: Option<String>,
    pub mimo_upstream_base_url: Option<String>,
    pub api_key: Option<String>,
    pub mimo_api_key: Option<String>,
    pub mimo_upstream_model: Option<String>,
    #[serde(default = "default_proxy_timeout_secs")]
    pub timeout_secs: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct MonitorsConfig {
    #[serde(default)]
    pub codex_local: CodexLocalMonitorConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CodexLocalMonitorConfig {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default = "default_codex_monitor_source")]
    pub source: String,
    pub sessions_dir: Option<PathBuf>,
    #[serde(default = "default_codex_monitor_poll_interval_ms")]
    pub poll_interval_ms: u64,
    #[serde(default = "default_codex_monitor_idle_after_ms")]
    pub idle_after_ms: u64,
    #[serde(default = "default_codex_monitor_max_scan_files")]
    pub max_scan_files: usize,
}

impl Default for CodexLocalMonitorConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            source: default_codex_monitor_source(),
            sessions_dir: None,
            poll_interval_ms: default_codex_monitor_poll_interval_ms(),
            idle_after_ms: default_codex_monitor_idle_after_ms(),
            max_scan_files: default_codex_monitor_max_scan_files(),
        }
    }
}

impl Default for ProxyConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            source: default_proxy_source(),
            anthropic_source: default_proxy_anthropic_source(),
            kimi_source: default_proxy_kimi_source(),
            mimo_source: default_proxy_mimo_source(),
            source_header: default_proxy_source_header(),
            upstream_base_url_header: default_proxy_upstream_base_url_header(),
            upstream_base_url: None,
            anthropic_upstream_base_url: None,
            kimi_upstream_base_url: None,
            mimo_upstream_base_url: None,
            api_key: None,
            mimo_api_key: None,
            mimo_upstream_model: None,
            timeout_secs: default_proxy_timeout_secs(),
        }
    }
}

pub fn config_path_from_args() -> PathBuf {
    let mut args = env::args().skip(1);

    while let Some(arg) = args.next() {
        if arg == "--config" || arg == "-c" {
            if let Some(path) = args.next() {
                return PathBuf::from(path);
            }
        }
    }

    PathBuf::from("config.toml")
}

pub fn load_or_create_config(path: &Path) -> Result<AppConfig> {
    if !path.exists() {
        let mut config = AppConfig::default();
        config.config_path = path.to_path_buf();
        ensure_hub_id(&mut config, true)?;
        write_config(&config)?;
        return Ok(config);
    }

    let text = fs::read_to_string(path)
        .with_context(|| format!("unable to read config {}", path.display()))?;
    let mut config: AppConfig =
        toml::from_str(&text).with_context(|| format!("invalid config {}", path.display()))?;
    config.config_path = path.to_path_buf();
    ensure_hub_id(&mut config, false)?;

    Ok(config)
}

fn ensure_hub_id(config: &mut AppConfig, is_new: bool) -> Result<()> {
    if config.hub.hub_id.trim().is_empty() || config.hub.hub_id == "hub_auto_generated" {
        config.hub.hub_id = format!("hub_{}", Uuid::new_v4());
        write_config(config)?;
    } else if is_new {
        write_config(config)?;
    }

    Ok(())
}

fn write_config(config: &AppConfig) -> Result<()> {
    if let Some(parent) = config.config_path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)?;
        }
    }

    let text = toml::to_string_pretty(config)?;
    fs::write(&config.config_path, text)?;
    Ok(())
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            server: ServerConfig::default(),
            hub: HubConfig::default(),
            privacy: PrivacyConfig::default(),
            state: StateConfig::default(),
            proxy: ProxyConfig::default(),
            monitors: MonitorsConfig::default(),
            trusted_hubs: Vec::new(),
            outputs: Vec::new(),
            config_path: PathBuf::from("config.toml"),
        }
    }
}

fn default_host() -> String {
    "127.0.0.1".to_string()
}

fn default_port() -> u16 {
    17888
}

fn default_generated_hub_id() -> String {
    "hub_auto_generated".to_string()
}

fn default_display_name() -> String {
    env::var("COMPUTERNAME")
        .or_else(|_| env::var("HOSTNAME"))
        .unwrap_or_else(|_| "AIStatusHub".to_string())
}

fn default_max_hops() -> usize {
    5
}

fn default_true() -> bool {
    true
}

fn default_max_preview_length() -> usize {
    180
}

fn default_activity_ring_size() -> usize {
    300
}

fn default_snapshot_path() -> PathBuf {
    if let Some(project_dirs) = ProjectDirs::from("", "", "AIStatusHub") {
        return project_dirs.data_local_dir().join("state.snapshot.json");
    }

    PathBuf::from("state.snapshot.json")
}

fn default_snapshot_debounce_ms() -> u64 {
    1000
}

fn default_remote_state_ttl_ms() -> u64 {
    120_000
}

fn default_output_busy_state_ttl_ms() -> u64 {
    0
}

fn default_output_refresh_interval_ms() -> u64 {
    30_000
}

fn default_proxy_source() -> String {
    "hermes-agent".to_string()
}

fn default_proxy_anthropic_source() -> String {
    "vscode-claude-code".to_string()
}

fn default_proxy_kimi_source() -> String {
    "vscode-kimi-code".to_string()
}

fn default_proxy_mimo_source() -> String {
    "mimo-code".to_string()
}

fn default_proxy_source_header() -> String {
    "x-aistatushub-source".to_string()
}

fn default_proxy_upstream_base_url_header() -> String {
    "x-aistatushub-upstream-base-url".to_string()
}

fn default_proxy_timeout_secs() -> u64 {
    120
}

fn default_codex_monitor_source() -> String {
    "codex-local".to_string()
}

fn default_codex_monitor_poll_interval_ms() -> u64 {
    1500
}

fn default_codex_monitor_idle_after_ms() -> u64 {
    30_000
}

fn default_codex_monitor_max_scan_files() -> usize {
    5000
}
