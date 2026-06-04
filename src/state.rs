use std::{
    collections::{HashMap, VecDeque},
    sync::Arc,
};

use tokio::sync::{broadcast, RwLock};

use crate::model::{ActivityItem, HubPeer, TaskState};

#[derive(Debug, Clone)]
pub struct StateStore {
    inner: Arc<RwLock<StateInner>>,
    events: broadcast::Sender<ServerEvent>,
    activity_capacity: usize,
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(tag = "type", content = "data")]
pub enum ServerEvent {
    StateUpdated(TaskState),
    StateDeleted { key: String },
    Activity(ActivityItem),
    HubUpdated(HubPeer),
}

#[derive(Debug, Default)]
struct StateInner {
    states: HashMap<String, TaskState>,
    activity: VecDeque<ActivityItem>,
    hubs: HashMap<String, HubPeer>,
}

#[derive(Debug, Clone, Default)]
pub struct StateFilter {
    pub source: Option<String>,
    pub origin_hub_id: Option<String>,
    pub locality: Option<String>,
    pub status: Option<String>,
    pub limit: Option<usize>,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct SnapshotData {
    pub states: Vec<TaskState>,
    pub hubs: Vec<HubPeer>,
}

impl StateStore {
    pub fn new(activity_capacity: usize) -> Self {
        let (events, _) = broadcast::channel(256);

        Self {
            inner: Arc::new(RwLock::new(StateInner::default())),
            events,
            activity_capacity: activity_capacity.max(1),
        }
    }

    pub fn subscribe(&self) -> broadcast::Receiver<ServerEvent> {
        self.events.subscribe()
    }

    pub async fn get_state(&self, key: &str) -> Option<TaskState> {
        self.inner.read().await.states.get(key).cloned()
    }

    pub async fn upsert_state(&self, state: TaskState) -> bool {
        let changed = {
            let mut inner = self.inner.write().await;
            let changed = inner.states.get(&state.key) != Some(&state);
            if changed {
                inner.states.insert(state.key.clone(), state.clone());
            }
            changed
        };

        if changed {
            let _ = self.events.send(ServerEvent::StateUpdated(state));
        }

        changed
    }

    pub async fn delete_state(&self, key: &str) -> bool {
        let changed = self.inner.write().await.states.remove(key).is_some();

        if changed {
            let _ = self.events.send(ServerEvent::StateDeleted {
                key: key.to_string(),
            });
        }

        changed
    }

    pub async fn list_states(&self, filter: StateFilter) -> Vec<TaskState> {
        let mut states: Vec<_> = self.inner.read().await.states.values().cloned().collect();

        if let Some(source) = filter.source {
            states.retain(|state| state.source == source);
        }

        if let Some(origin_hub_id) = filter.origin_hub_id {
            states.retain(|state| state.origin_hub_id == origin_hub_id);
        }

        if let Some(locality) = filter.locality {
            states.retain(|state| format!("{:?}", state.locality).eq_ignore_ascii_case(&locality));
        }

        if let Some(status) = filter.status {
            states.retain(|state| format!("{:?}", state.status).eq_ignore_ascii_case(&status));
        }

        states.sort_by(|a, b| b.updated_at.cmp(&a.updated_at));
        states.truncate(filter.limit.unwrap_or(100));
        states
    }

    pub async fn add_activity(&self, item: ActivityItem) {
        {
            let mut inner = self.inner.write().await;
            inner.activity.push_back(item.clone());
            while inner.activity.len() > self.activity_capacity {
                inner.activity.pop_front();
            }
        }

        let _ = self.events.send(ServerEvent::Activity(item));
    }

    pub async fn list_activity(&self, limit: usize) -> Vec<ActivityItem> {
        let mut items: Vec<_> = self.inner.read().await.activity.iter().cloned().collect();
        items.reverse();
        items.truncate(limit);
        items
    }

    pub async fn upsert_hub(&self, hub: HubPeer) {
        self.inner
            .write()
            .await
            .hubs
            .insert(hub.hub_id.clone(), hub.clone());
        let _ = self.events.send(ServerEvent::HubUpdated(hub));
    }

    pub async fn list_hubs(&self) -> Vec<HubPeer> {
        let mut hubs: Vec<_> = self.inner.read().await.hubs.values().cloned().collect();
        hubs.sort_by(|a, b| b.updated_at.cmp(&a.updated_at));
        hubs
    }

    pub async fn get_hub(&self, hub_id: &str) -> Option<HubPeer> {
        self.inner.read().await.hubs.get(hub_id).cloned()
    }

    pub async fn to_snapshot(&self) -> SnapshotData {
        let inner = self.inner.read().await;

        SnapshotData {
            states: inner.states.values().cloned().collect(),
            hubs: inner.hubs.values().cloned().collect(),
        }
    }

    pub async fn restore_snapshot(&self, snapshot: SnapshotData) {
        let mut inner = self.inner.write().await;
        inner.states = snapshot
            .states
            .into_iter()
            .map(|state| (state.key.clone(), state))
            .collect();
        inner.hubs = snapshot
            .hubs
            .into_iter()
            .map(|hub| (hub.hub_id.clone(), hub))
            .collect();
    }
}
