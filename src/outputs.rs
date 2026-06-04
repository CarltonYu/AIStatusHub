use serde_json::json;

use crate::{config::AppConfig, model::TaskState};

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
