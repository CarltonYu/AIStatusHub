# Mimo Code through AIStatusHub

This setup lets Mimo Code requests pass through AIStatusHub so the hub records
Mimo activity and drives the Duo face output.

## One-click start on Windows

```powershell
.\start-mimo-with-hub.bat
```

The launcher starts AIStatusHub if needed, then runs:

```powershell
C:\ai\mimo.exe <project> -m aistatushub-mimo/mimo-auto
```

The launcher also installs a global Mimo provider into
`%USERPROFILE%\.config\mimocode\mimocode.json`. The project config at
`.mimocode/mimocode.json` defines the same
`aistatushub-mimo/mimo-auto` model with this API base:

```text
http://127.0.0.1:17888/mimo/v1
```

## Global `mimo` command

Install the wrapper once:

```powershell
.\install-mimo-command.bat
```

It writes `mimo.cmd` into `%USERPROFILE%\.local\bin`. After opening a new
terminal, run `mimo` from any project directory. The wrapper passes the current
directory as the Mimo project, starts AIStatusHub if needed, and injects
`-m aistatushub-mimo/mimo-auto`.

If you need the original binary without AIStatusHub, use:

```powershell
mimo-direct
```

## Hub config

`config.toml` needs proxy and Duo output enabled:

```toml
[proxy]
enabled = true
mimo_source = "mimo-code"
mimo_upstream_base_url = "https://api.xiaomimimo.com/api/free-ai/openai"
mimo_api_key = ""
mimo_upstream_model = "mimo-v2.5-pro"

[[outputs]]
id = "duo-face"
type = "irisoled-udp"
enabled = true
url = "udp://192.168.42.1:25250"
busy_expression = "angry"
idle_expression = "normal"
busy_state_ttl_ms = 0
refresh_interval_ms = 30000
```

Starting the Mimo wrapper only starts AIStatusHub and selects the hub-backed
model. It does not emit a process heartbeat or change the Duo face by itself.
When Mimo calls `/mimo/v1/chat/completions`, AIStatusHub emits a busy state at
request start and a completed/error state at the end. The Duo face output sends
`default angry` while busy and `normal` when idle.
Keep `busy_state_ttl_ms = 0` when long-running proxied Mimo requests should stay
busy for the whole request.
`refresh_interval_ms` periodically resends the current expression to the Duo
screen.

For `https://api.xiaomimimo.com/api/free-ai/openai`, AIStatusHub uses Mimo's
local free-client fingerprint to bootstrap the short-lived upstream token. By
default it reads `%USERPROFILE%\.local\share\mimocode\mimo-free-client`; set
`MIMO_FREE_CLIENT_PATH` or `MIMO_FREE_CLIENT` if needed.

For Xiaomi token/code plan, use:

```toml
mimo_upstream_base_url = "https://token-plan-cn.xiaomimimo.com/v1"
mimo_api_key = "YOUR_XIAOMI_API_KEY"
mimo_upstream_model = "mimo-v2.5-pro"
```

You can also keep the key out of `config.toml` by setting `XIAOMI_API_KEY`.
