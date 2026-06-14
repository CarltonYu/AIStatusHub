#!/usr/bin/env python3
"""
Minimal password-based SSH helper for the Duo (192.168.42.1 / milkv).
Usage:
    ./scripts/ssh_duo.py "command to run on Duo"
"""
import os, sys, pty, select, time, termios, tty

HOST = "192.168.42.1"
USER = "root"
PASSWORD = "milkv"


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "echo hello"
    ssh_cmd = ["ssh", "-o", "StrictHostKeyChecking=no",
               "-o", "UserKnownHostsFile=/dev/null",
               "-o", "LogLevel=ERROR",
               f"{USER}@{HOST}", cmd]

    master_fd, slave_fd = pty.openpty()
    pid = os.fork()
    if pid == 0:
        os.close(master_fd)
        os.setsid()
        os.dup2(slave_fd, 0)
        os.dup2(slave_fd, 1)
        os.dup2(slave_fd, 2)
        os.close(slave_fd)
        os.execvp(ssh_cmd[0], ssh_cmd)
        os._exit(1)

    os.close(slave_fd)

    old_tty = None
    try:
        old_tty = termios.tcgetattr(sys.stdin.fileno())
        tty.setraw(sys.stdin.fileno())
    except Exception:
        pass

    password_sent = False
    output = b""
    try:
        while True:
            r, _, _ = select.select([master_fd, sys.stdin.fileno()], [], [], 0.1)
            if master_fd in r:
                try:
                    data = os.read(master_fd, 4096)
                except OSError:
                    break
                if not data:
                    break
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
                output += data
                sys.stderr.buffer.write(b"[RECV] " + data + b"\n")
                sys.stderr.flush()
                if not password_sent and (b"password:" in output.lower() or b"password for" in output.lower()):
                    time.sleep(0.2)
                    os.write(master_fd, (PASSWORD + "\n").encode())
                    password_sent = True
                    output = b""
            if sys.stdin.fileno() in r:
                try:
                    data = os.read(sys.stdin.fileno(), 4096)
                except OSError:
                    break
                os.write(master_fd, data)
    finally:
        if old_tty is not None:
            try:
                termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old_tty)
            except Exception:
                pass

    _, status = os.waitpid(pid, 0)
    sys.exit(os.waitstatus_to_exitcode(status))


if __name__ == "__main__":
    main()
