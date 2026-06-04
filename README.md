# AIStatusHub

AIStatusHub is a lightweight local status hub for AI coding tools.

It is designed as a small Rust executable that reads one `config.toml`, keeps only current task state by default, and can forward compact state to another AIStatusHub instance.

## Development

```bash
cargo run -- --config config.example.toml
```

Default address:

```text
http://127.0.0.1:17888
```

## Current MVP Support

- Generic HTTP ingest for local AI tools and hooks.
- Current-state aggregation in memory.
- Minimal built-in web page.
- WebSocket state stream.
- JSON snapshot, when enabled in config.
- `stdout` output for test-stage printing.
- `aistatushub report` for Codex hooks, VSCode tasks, and manual test events.
- OpenAI-compatible `/v1/chat/completions`, `/v1/models`, and `/v1/*` proxy for Hermes Agent / Mimo-style clients.
- Hub-to-Hub receive endpoints.

Codex/Claude/Kimi can be connected through hooks that POST to `/api/v1/ingest/event`.

Hermes Agent can be tested by enabling `[proxy]` in AIStatusHub config, then pointing Hermes at `http://127.0.0.1:17888/v1`:

```bash
hermes config set model.provider custom
hermes config set model.base_url http://127.0.0.1:17888/v1
hermes config set model.default mimo-v2.5-pro
```

If AIStatusHub `proxy.api_key` is empty, Hermes must send a Bearer token, for example through `OPENAI_API_KEY` in `~/.hermes/.env`.

VSCode can be tested through Continue by setting model `apiBase` to `http://127.0.0.1:17888/v1` and adding:

```yaml
requestOptions:
  headers:
    x-aistatushub-source: vscode
    x-aistatushub-upstream-base-url: https://YOUR_EXISTING_OPENAI_COMPATIBLE_BASE/v1
```

The repository also includes `.vscode/tasks.json` for manual `vscode` source events.
