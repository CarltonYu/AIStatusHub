# duo-console

Cross-platform CLI tool for sending text/keystrokes to a Milk-V Duo local
console over UDP. It talks to `udp-console-input-daemon` (port `25251` by
default).

## Install on Windows

From the repository root:

```powershell
.\install-duo-console.bat
```

The installer builds `tools\duo-console` in release mode, copies
`duo-console.exe` to `%USERPROFILE%\.local\bin`, and adds that directory to the
current user's `Path` environment variable. Open a new terminal after install,
then try:

```powershell
duo-console "echo hello"
duo-console "cmd enter"
duo-console "combo ctrl+c"
```

Advanced options:

```powershell
.\scripts\install-duo-console-command.ps1 -InstallDir C:\tools\bin
.\scripts\install-duo-console-command.ps1 -NoBuild
.\scripts\install-duo-console-command.ps1 -NoPathUpdate
```

## Install on macOS / Linux

Build the binary, copy it to a directory on your `PATH`, then open a new shell:

```bash
cd tools/duo-console
cargo build --release
mkdir -p ~/.local/bin
cp target/release/duo-console ~/.local/bin/
export PATH=$HOME/.local/bin:$PATH
```

Open a new terminal tab/window and try:

```bash
duo-console "echo hello"
duo-console "cmd enter"
duo-console "combo ctrl+c"
```

## Usage

### One-shot mode

Pass the command as arguments:

```bash
duo-console string hello world
duo-console cmd enter
duo-console combo ctrl+c
```

### Interactive mode

Run without arguments and type commands followed by Enter:

```bash
$ duo-console
duo-console: sending to 192.168.42.1:25251. Type commands and press Enter.
Examples: echo hello   /   cmd enter   /   combo ctrl+c
> echo hello
> cmd enter
> combo ctrl+c
> ^D
```

Press `Ctrl+D` (Unix) or `Ctrl+Z` then Enter (Windows) to exit.

### Piped mode

```bash
echo "string hello" | duo-console
printf "cmd enter\ncmd backspace\n" | duo-console
```

### Custom host/port

```bash
duo-console -h 192.168.42.1 -p 25251 "string hi"
```

## Supported commands

These are the same commands accepted by `udp-console-input-daemon`:

| Command | Example | Meaning |
|---|---|---|
| `echo <text>` | `echo hello` | Type text into Duo console |
| `cmd <key>` | `cmd enter` | Press a special key |
| `combo <mod>+<key>` | `combo ctrl+c` | Press a modifier combo |

### Special keys

`enter`, `return`, `tab`, `backspace`, `delete`, `escape`/`esc`, `space`,
`up`, `down`, `left`, `right`, `home`, `end`, `pgup`/`pageup`,
`pgdown`/`pagedown`, `insert`.

### Modifiers

`ctrl`, `alt`, `shift`. Multiple modifiers can be chained, e.g.
`combo ctrl+alt+c`.

## Building for other platforms

The tool is written in Rust and uses only the standard library + `clap` + `atty`,
so it builds on macOS, Linux, and Windows.

### macOS / Linux native

```bash
cd tools/duo-console
cargo build --release
# Binary: target/release/duo-console
```

### Windows native

Install [Rust for Windows](https://rustup.rs/), then:

```powershell
cd tools\duo-console
cargo build --release
# Binary: target\release\duo-console.exe
```

### Cross-compile from macOS

```bash
# Linux x86_64 (static musl)
rustup target add x86_64-unknown-linux-musl
cargo build --release --target x86_64-unknown-linux-musl

# Windows x86_64 (GNU toolchain; requires mingw-w64 installed)
rustup target add x86_64-pc-windows-gnu
cargo build --release --target x86_64-pc-windows-gnu
```

For other targets, run `rustup target list` and add the desired triple.

## Adding to PATH Manually

Copy the built binary to a directory on your PATH, for example:

```bash
mkdir -p ~/.local/bin
cp target/release/duo-console ~/.local/bin/
```

Then ensure `~/.local/bin` is in your shell profile:

```bash
# ~/.zshrc or ~/.bashrc
export PATH=$HOME/.local/bin:$PATH
```

On Windows, add the folder containing `duo-console.exe` to the system
`Path` environment variable.
