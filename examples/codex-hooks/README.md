# Codex Hook Test

AIStatusHub can receive Codex hook events through the single binary:

```powershell
D:\code\github\AIStatusHub\target\debug\aistatushub.exe report --codex-hook --quiet --token dev-local-token
```

Codex sends hook JSON to stdin. `--codex-hook` maps common Codex hook event names to AIStatusHub event types without storing prompts or raw payloads.

Example `.codex/config.toml` snippet:

```toml
[[hooks.UserPromptSubmit]]
matcher = "*"

[[hooks.UserPromptSubmit.hooks]]
type = "command"
command = 'D:\code\github\AIStatusHub\target\debug\aistatushub.exe report --codex-hook --quiet --token dev-local-token'
timeout = 5

[[hooks.PreToolUse]]
matcher = "*"

[[hooks.PreToolUse.hooks]]
type = "command"
command = 'D:\code\github\AIStatusHub\target\debug\aistatushub.exe report --codex-hook --quiet --token dev-local-token'
timeout = 5

[[hooks.PostToolUse]]
matcher = "*"

[[hooks.PostToolUse.hooks]]
type = "command"
command = 'D:\code\github\AIStatusHub\target\debug\aistatushub.exe report --codex-hook --quiet --token dev-local-token'
timeout = 5

[[hooks.PermissionRequest]]
matcher = "*"

[[hooks.PermissionRequest.hooks]]
type = "command"
command = 'D:\code\github\AIStatusHub\target\debug\aistatushub.exe report --codex-hook --quiet --token dev-local-token'
timeout = 5

[[hooks.Stop]]
matcher = "*"

[[hooks.Stop.hooks]]
type = "command"
command = 'D:\code\github\AIStatusHub\target\debug\aistatushub.exe report --codex-hook --quiet --token dev-local-token'
timeout = 5
```

Manual smoke test:

```powershell
'{"hook_event_name":"PreToolUse","cwd":"D:/code/github/AIStatusHub","session_id":"codex_test","tool_name":"Bash","tool_input":{"command":"cargo check"}}' |
  target\debug\aistatushub.exe report --codex-hook --token dev-local-token
```
