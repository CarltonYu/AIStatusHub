pub const INDEX_HTML: &str = r#"<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>AIStatusHub</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 24px; background: #0f172a; color: #e2e8f0; }
    h1 { font-size: 22px; margin: 0 0 16px; }
    table { width: 100%; border-collapse: collapse; background: #111827; }
    th, td { padding: 10px; border-bottom: 1px solid #334155; text-align: left; font-size: 14px; }
    th { color: #93c5fd; }
    .status { font-weight: 700; }
    .muted { color: #94a3b8; }
  </style>
</head>
<body>
  <h1>AIStatusHub</h1>
  <table>
    <thead>
      <tr><th>Status</th><th>Source</th><th>Hub</th><th>Workspace</th><th>Message</th><th>Updated</th></tr>
    </thead>
    <tbody id="rows"><tr><td colspan="6" class="muted">Loading...</td></tr></tbody>
  </table>
  <script>
    async function load() {
      const res = await fetch('/api/v1/state');
      const json = await res.json();
      render(json.data || []);
    }
    function render(states) {
      const rows = document.getElementById('rows');
      rows.innerHTML = '';
      if (!states.length) {
        rows.innerHTML = '<tr><td colspan="6" class="muted">No active AI tasks</td></tr>';
        return;
      }
      for (const state of states) {
        const tr = document.createElement('tr');
        tr.innerHTML = `<td class="status">${state.status}</td><td>${state.source}</td><td>${state.origin_hub_name || state.origin_hub_id}</td><td>${state.workspace_hash || ''}</td><td>${state.last_message || ''}</td><td>${state.updated_at}</td>`;
        rows.appendChild(tr);
      }
    }
    load();
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/v1/events/stream`);
    ws.onmessage = load;
  </script>
</body>
</html>"#;
