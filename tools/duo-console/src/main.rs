use clap::Parser;
use std::io::{self, BufRead, Write};
use std::net::UdpSocket;
use std::time::Duration;

const DEFAULT_HOST: &str = "192.168.42.1";
const DEFAULT_PORT: u16 = 25251;

#[derive(Parser, Debug)]
#[command(name = "duo-console")]
#[command(version = "0.1.0")]
#[command(about = "Send text/keys to a Milk-V Duo local console over UDP")]
struct Args {
    /// Duo IP address.
    #[arg(short, long, default_value = DEFAULT_HOST)]
    host: String,

    /// UDP port of the console input daemon.
    #[arg(short, long, default_value_t = DEFAULT_PORT)]
    port: u16,

    /// Command to send. If omitted, read commands interactively from stdin.
    #[arg(trailing_var_arg = true, allow_hyphen_values = true)]
    command: Vec<String>,
}

fn send(socket: &UdpSocket, addr: &str, data: &[u8]) -> io::Result<()> {
    socket.send_to(data, addr)?;
    Ok(())
}

fn main() -> io::Result<()> {
    let args = Args::parse();
    let addr = format!("{}:{}", args.host, args.port);

    // Bind to any local port; UdpSocket::bind("0.0.0.0:0") works on macOS/Linux/Windows.
    let socket = UdpSocket::bind("0.0.0.0:0")?;
    socket.set_write_timeout(Some(Duration::from_secs(3)))?;

    if !args.command.is_empty() {
        // Send the joined command-line arguments as a single packet.
        let line = args.command.join(" ");
        send(&socket, &addr, line.as_bytes())?;
        return Ok(());
    }

    // Interactive / piped mode: read lines and send each as a packet.
    let stdin = io::stdin();
    let mut stdout = io::stdout();
    let mut locked = stdin.lock();

    if atty::is(atty::Stream::Stdin) {
        println!("duo-console: sending to {}. Type commands and press Enter.", addr);
        println!("Examples: echo hello   /   cmd enter   /   combo ctrl+c");
        print!("> ");
        stdout.flush()?;
    }

    let mut line = String::new();
    while locked.read_line(&mut line)? > 0 {
        // Strip trailing newline; the daemon already handles that, but keep the packet clean.
        let trimmed = line.trim_end_matches('\n').trim_end_matches('\r');
        if !trimmed.is_empty() {
            send(&socket, &addr, trimmed.as_bytes())?;
        }

        if atty::is(atty::Stream::Stdin) {
            print!("> ");
            stdout.flush()?;
        }
        line.clear();
    }

    Ok(())
}
