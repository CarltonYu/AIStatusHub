# Hermes Agent / Mimo Proxy Test

Enable the OpenAI-compatible proxy in `config.toml`:

```toml
[server]
host = "0.0.0.0"

[proxy]
enabled = true
source = "hermes-agent"
upstream_base_url = "https://token-plan-cn.xiaomimimo.com/v1"
api_key = ""
timeout_secs = 1800
```

Then start AIStatusHub:

```powershell
target\debug\aistatushub.exe --config config.toml
```

Install the Windows global wrapper once:

```powershell
.\install-hermes-command.bat
```

It writes `hermes.cmd` and `hermes-tui.cmd` to `%USERPROFILE%\.local\bin`.
Both wrappers use the console's current directory as the Hermes WSL working
directory.

Point Hermes Agent at AIStatusHub:

```bash
hermes config set model.provider custom
hermes config set model.base_url http://172.28.96.1:17888/v1
hermes config set model.default mimo-v2.5-pro
```

If the WSL gateway changes, check it with
`wsl -d HermesUbuntu -- ip route show default` and use the `default via` IP.

When `proxy.api_key` is empty, AIStatusHub forwards Hermes' incoming `Authorization` header to the upstream provider. For Hermes custom endpoints, set `OPENAI_API_KEY` in `~/.hermes/.env`; it can use the same Mimo key as `XIAOMI_API_KEY`.

When Hermes calls `/v1/chat/completions`, AIStatusHub emits:

- `prompt.submitted`
- `message.delta` when `stream = true`
- `turn.completed` or `turn.failed`

AIStatusHub also pass-through proxies other OpenAI-compatible `/v1/*` paths such as `/v1/models`.

Manual proxy smoke test:

```powershell
$body = @{
  model = "your-mimo-model"
  messages = @(@{ role = "user"; content = "Say hello" })
  stream = $false
} | ConvertTo-Json -Depth 8

Invoke-RestMethod `
  -Uri http://127.0.0.1:17888/v1/chat/completions `
  -Method Post `
  -Headers @{ Authorization = "Bearer test" } `
  -ContentType "application/json" `
  -Body $body
```
