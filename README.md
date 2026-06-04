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
