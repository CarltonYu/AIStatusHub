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

If startup fails with `another instance may already be running`, AIStatusHub is
already bound to the configured port. Check or stop it on Windows:

```powershell
Get-Process aistatushub
Get-Process aistatushub | Stop-Process
.\target\debug\aistatushub.exe --config config.toml
```

## Current MVP Support

- Generic HTTP ingest for local AI tools and hooks.
- Current-state aggregation in memory.
- Minimal built-in web page.
- WebSocket state stream.
- JSON snapshot, when enabled in config.
- `stdout` output for test-stage printing.
- IrisOLED UDP output for a Milk-V Duo + GC9A01 face display.
- `aistatushub report` for Codex hooks, VSCode tasks, and manual test events.
- OpenAI-compatible `/v1/chat/completions`, `/v1/models`, and `/v1/*` proxy for Hermes Agent / Mimo-style clients.
- Anthropic-compatible `/anthropic/v1/messages` proxy for VSCode ClaudeCode.
- Kimi `/kimi/v1/chat/completions` proxy for VSCode Kimi Code.
- Hub-to-Hub receive endpoints.

Codex/Claude/Kimi can be connected through hooks or local proxy routes. Current
VSCode test setup uses:

- ClaudeCode extension: `ANTHROPIC_BASE_URL=http://127.0.0.1:17888/anthropic`
- Codex extension: user-level `~/.codex/config.toml` hooks, or the built-in local session monitor
- Kimi Code extension: `KIMI_BASE_URL=http://127.0.0.1:17888/kimi/v1`

Codex hooks are enabled by default, but Codex requires non-managed command hooks
to be reviewed once before they run. In the VSCode Codex panel, type `/hooks`,
review the AIStatusHub hook entries, then trust them. If no Codex callbacks
arrive, first check `/hooks` and `/status` to confirm the active config and
project trust state.

Some Codex desktop/VSCode builds do not expose `/hooks` in their chat UI. For
that case, enable the local monitor instead:

```toml
[monitors.codex_local]
enabled = true
source = "codex-local"
sessions_dir = ""
poll_interval_ms = 1500
idle_after_ms = 30000
max_scan_files = 5000
```

The monitor reads Codex's local `sessions` directory and treats recent session
file updates as AI activity. It only reports busy/idle transitions, so it stays
small and does not store conversation content.

Hermes Agent can be tested by enabling `[proxy]` in AIStatusHub config. When
Hermes runs inside the `HermesUbuntu` WSL distro and AIStatusHub runs on
Windows, set `server.host = "0.0.0.0"` and point Hermes at the Windows WSL
gateway address, for example `http://172.28.96.1:17888/v1`:

```powershell
.\install-hermes-command.bat
```

The installer creates global `hermes` and `hermes-tui` commands in
`%USERPROFILE%\.local\bin`. They launch the Hermes WSL distro from the current
Windows console directory, so `hermes` opened in a project folder uses that
folder as its working directory.

```bash
hermes config set model.provider custom
hermes config set model.base_url http://172.28.96.1:17888/v1
hermes config set model.default mimo-v2.5-pro
```

If the WSL gateway changes, check it with
`wsl -d HermesUbuntu -- ip route show default` and use the `default via` IP.

If AIStatusHub `proxy.api_key` is empty, Hermes must send a Bearer token, for example through `OPENAI_API_KEY` in `~/.hermes/.env`.

## Duo Face Output

AIStatusHub can drive the face daemon running on the Duo board over UDP. When
any local AI task is active, it sends `default angry`; when all local AI tasks
finish, it sends `normal`.

For the dual-screen setup (ST7789V Linux console + GC9A01 expression screen),
the image runs `gc9a01-face-daemon`, which listens on UDP port 25250 and drives
the GC9A01 round LCD via `/dev/spidev0.1`.

```toml
[[outputs]]
id = "duo-face"
type = "irisoled-udp"
enabled = true
url = "udp://192.168.42.1:25250"
busy_expression = "angry"
idle_expression = "normal"
include_remote_states = false
```

For single-screen setups, the older `irisoled-face` framebuffer daemon is still
available under `duo/CV1800B/lvgl-framebuffer-gc9a01/irisoled-face/`.

## Duo Console Input Output

For setups where the Duo has its own display (e.g. ST7789V framebuffer console),
the image runs `udp-console-input-daemon`. It listens on UDP port **25251** and
injects received text/keystrokes into the local Linux TTY (`/dev/tty1`) using
`TIOCSTI`, so commands appear directly on the Duo screen's command line.

```bash
# Type text (use -w 0 so nc exits immediately on macOS)
echo "string hello world" | nc -u -w 0 192.168.42.1 25251

# Special keys
echo "cmd enter"  | nc -u -w 0 192.168.42.1 25251
echo "cmd left"   | nc -u -w 0 192.168.42.1 25251

# Modifier combinations
echo "combo ctrl+c"  | nc -u -w 0 192.168.42.1 25251
echo "combo alt+tab" | nc -u -w 0 192.168.42.1 25251
```

The console font is set to `fbcon=font:6x8` for the 240×320 ST7789V screen.

A cross-host helper `duo-console` is provided in `tools/duo-console/`. After
installation you can type commands directly instead of piping through `nc`:

```powershell
# Windows
.\install-duo-console.bat
```

```bash
# One-shot
duo-console "echo hello"
duo-console "cmd enter"
duo-console "combo ctrl+c"

# Interactive REPL
duo-console
> string hello
> cmd enter
```

See `duo/CV1800B/udp-console-input-daemon/README.md` for the full command list
and `tools/duo-console/README.md` for install/build instructions.

The repository also includes `.vscode/tasks.json` for manual source events.
