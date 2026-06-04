# VSCode Test

AIStatusHub supports VSCode testing in two ways:

- Automatic proxy path for Continue: point Continue's OpenAI-compatible `apiBase` at AIStatusHub and send source/upstream headers.
- Manual task path: use the `report` subcommand from a VSCode task or terminal.

Continue model example:

```yaml
requestOptions:
  headers:
    x-aistatushub-source: vscode
    x-aistatushub-upstream-base-url: https://YOUR_EXISTING_OPENAI_COMPATIBLE_BASE/v1
models:
  - name: Your Model
    provider: openai
    model: your-model
    apiBase: http://127.0.0.1:17888/v1
    apiKey: YOUR_EXISTING_API_KEY
```

AIStatusHub uses `x-aistatushub-source` for source attribution and forwards requests to `x-aistatushub-upstream-base-url`. It does not persist prompts or responses.

Manual command:

```powershell
target\debug\aistatushub.exe report `
  --source vscode `
  --event-type prompt.submitted `
  --status thinking `
  --workspace "$PWD" `
  --session-id vscode_manual `
  --message "VSCode AI request started" `
  --token dev-local-token
```

Example `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "AIStatusHub: VSCode AI started",
      "type": "shell",
      "command": "${workspaceFolder}\\target\\debug\\aistatushub.exe",
      "args": [
        "report",
        "--source", "vscode",
        "--event-type", "prompt.submitted",
        "--status", "thinking",
        "--workspace", "${workspaceFolder}",
        "--session-id", "vscode_task",
        "--message", "VSCode AI request started",
        "--token", "dev-local-token"
      ],
      "problemMatcher": []
    }
  ]
}
```
