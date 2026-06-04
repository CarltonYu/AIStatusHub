use crate::{
    config::{AppConfig, TrustedHubConfig},
    ingest::now_iso,
    model::{HubHop, HubPeer, HubProvenance, HubStateEnvelope, Locality, TaskState},
};

#[derive(Debug)]
pub enum HubMergeError {
    RemoteDisabled,
    UntrustedHub,
    TokenMismatch,
    TargetMismatch,
    RelayNotAllowed,
    LoopDetected,
    EmptyPath,
    HopCountMismatch,
    MaxHopsExceeded,
    OriginMismatch,
    MissingState,
    OlderState,
}

impl HubMergeError {
    pub fn message(&self) -> &'static str {
        match self {
            Self::RemoteDisabled => "remote hub ingest is disabled",
            Self::UntrustedHub => "sender hub is not trusted",
            Self::TokenMismatch => "trusted hub token mismatch",
            Self::TargetMismatch => "target_hub_id does not match current hub",
            Self::RelayNotAllowed => "trusted hub is not allowed to relay remote states",
            Self::LoopDetected => "hub_path already contains current hub_id",
            Self::EmptyPath => "hub_path is empty",
            Self::HopCountMismatch => "hop_count does not match hub_path length",
            Self::MaxHopsExceeded => "hub_path exceeds max_hops",
            Self::OriginMismatch => "hub_path origin does not match origin_hub_id",
            Self::MissingState => "envelope has no state",
            Self::OlderState => "incoming state is older than current state",
        }
    }
}

pub fn trusted_hub(
    config: &AppConfig,
    sender_hub_id: &str,
    token: Option<&str>,
) -> Result<Option<TrustedHubConfig>, HubMergeError> {
    if !config.hub.accept_remote_hubs {
        return Err(HubMergeError::RemoteDisabled);
    }

    let trusted = config
        .trusted_hubs
        .iter()
        .find(|hub| hub.hub_id == sender_hub_id);

    if !config.trusted_hubs.is_empty() && trusted.is_none() {
        return Err(HubMergeError::UntrustedHub);
    }

    if let Some(trusted) = trusted {
        if let Some(expected) = trusted.token.as_deref() {
            if Some(expected) != token {
                return Err(HubMergeError::TokenMismatch);
            }
        }
    } else if let Some(expected) = config.server.auth_token.as_deref() {
        if Some(expected) != token {
            return Err(HubMergeError::TokenMismatch);
        }
    }

    Ok(trusted.cloned())
}

pub fn merge_remote_state(
    config: &AppConfig,
    trusted: Option<&TrustedHubConfig>,
    previous: Option<&TaskState>,
    envelope: &HubStateEnvelope,
) -> Result<TaskState, HubMergeError> {
    if let Some(target) = envelope.target_hub_id.as_deref() {
        if target != config.hub.hub_id {
            return Err(HubMergeError::TargetMismatch);
        }
    }

    validate_path(&envelope.provenance, config)?;

    if trusted.map(|hub| !hub.allow_relay).unwrap_or(false)
        && envelope.provenance.origin_hub_id != envelope.sender_hub.hub_id
    {
        return Err(HubMergeError::RelayNotAllowed);
    }

    let incoming = envelope.state.as_ref().ok_or(HubMergeError::MissingState)?;

    if let Some(previous) = previous {
        if previous.updated_at > incoming.updated_at {
            return Err(HubMergeError::OlderState);
        }
    }

    let now = now_iso();
    let mut state = incoming.clone();
    let provenance = append_current_hub(config, &envelope.provenance, &now);
    let relayed = envelope.sender_hub.hub_id != envelope.provenance.origin_hub_id;

    state.locality = if relayed {
        Locality::Relayed
    } else {
        Locality::Remote
    };
    state.origin_hub_id = provenance.origin_hub_id;
    state.origin_hub_name = provenance.origin_hub_name;
    state.received_from_hub_id = Some(envelope.sender_hub.hub_id.clone());
    state.hub_path = provenance.hub_path;
    state.last_seen_at = now.clone();
    state.expires_at = Some(expires_at(&now, config.state.remote_state_ttl_ms));

    Ok(state)
}

pub fn peer_from_envelope(
    envelope: &HubStateEnvelope,
    trusted: Option<&TrustedHubConfig>,
) -> HubPeer {
    let now = now_iso();

    HubPeer {
        hub_id: envelope.sender_hub.hub_id.clone(),
        display_name: envelope.sender_hub.display_name.clone(),
        direction: "inbound".to_string(),
        trusted: trusted.is_some(),
        allow_relay: trusted.map(|hub| hub.allow_relay).unwrap_or(false),
        last_seen_at: Some(now.clone()),
        last_error: None,
        updated_at: now,
    }
}

fn validate_path(provenance: &HubProvenance, config: &AppConfig) -> Result<(), HubMergeError> {
    if provenance.hub_path.is_empty() {
        return Err(HubMergeError::EmptyPath);
    }

    if provenance.hub_path.iter().any(|hop| hop.hub_id == config.hub.hub_id) {
        return Err(HubMergeError::LoopDetected);
    }

    if provenance.hub_path.len() != provenance.hop_count {
        return Err(HubMergeError::HopCountMismatch);
    }

    if provenance.hub_path.len() > config.hub.max_hops {
        return Err(HubMergeError::MaxHopsExceeded);
    }

    if provenance
        .hub_path
        .first()
        .map(|hop| hop.hub_id.as_str())
        != Some(provenance.origin_hub_id.as_str())
    {
        return Err(HubMergeError::OriginMismatch);
    }

    Ok(())
}

fn append_current_hub(config: &AppConfig, provenance: &HubProvenance, now: &str) -> HubProvenance {
    let mut hub_path = provenance.hub_path.clone();
    hub_path.push(HubHop {
        hub_id: config.hub.hub_id.clone(),
        display_name: Some(config.hub.display_name.clone()),
        received_at: Some(now.to_string()),
        forwarded_at: None,
    });

    HubProvenance {
        origin_hub_id: provenance.origin_hub_id.clone(),
        origin_hub_name: provenance.origin_hub_name.clone(),
        received_from_hub_id: provenance.hub_path.last().map(|hop| hop.hub_id.clone()),
        hop_count: hub_path.len(),
        hub_path,
    }
}

fn expires_at(now: &str, ttl_ms: u64) -> String {
    let now = time::OffsetDateTime::parse(now, &time::format_description::well_known::Rfc3339)
        .unwrap_or_else(|_| time::OffsetDateTime::now_utc());
    (now + time::Duration::milliseconds(ttl_ms as i64))
        .format(&time::format_description::well_known::Rfc3339)
        .unwrap_or_else(|_| now.to_string())
}
