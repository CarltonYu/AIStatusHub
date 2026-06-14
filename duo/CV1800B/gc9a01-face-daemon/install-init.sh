#!/bin/sh
set -eu

if [ -x /etc/init.d/S99gc9a01-face ]; then
  /etc/init.d/S99gc9a01-face stop 2>/dev/null || true
fi
if [ -x /etc/init.d/S99zgc9a01-face ]; then
  /etc/init.d/S99zgc9a01-face stop 2>/dev/null || true
fi
killall gc9a01-face-daemon 2>/dev/null || true

install -m 0755 /root/gc9a01-face-daemon /usr/bin/gc9a01-face-daemon

# Remove any old init script name.
rm -f /etc/init.d/S99gc9a01-face

cat >/etc/init.d/S99zgc9a01-face <<'EOF'
#!/bin/sh
#
# GC9A01 expression screen daemon
# Run late (after S99user) so GP22 pinmux/devmem is ready.
# A watchdog loop restarts the daemon if it crashes.
#

PIDFILE=/var/run/gc9a01-face-daemon.pid
NO_RESPAWN_FLAG=/var/run/gc9a01-face.no-respawn
DAEMON=/usr/bin/gc9a01-face-daemon
LOG=/tmp/gc9a01-face.log

# Configure pinmux for GC9A01 (DC/RST/SCK/MOSI/CS).
# GP22 (Pin29) is SPI2 CS1 in the device tree; make sure it is in GPIO mode
duo-pinmux -w GP3/GP3 2>/dev/null || true
duo-pinmux -w GP6/SPI2_SCK 2>/dev/null || true
duo-pinmux -w GP7/SPI2_SDO 2>/dev/null || true
duo-pinmux -w GP8/GP8 2>/dev/null || true

# Configure GP22 (Pin29, SPI CS1) as GPIO, matching /mnt/system/duo-init.sh.
# Without this the SPI core cannot assert CS1 and the screen stays black.
devmem 0x0502707c 32 0x111 2>/dev/null || true
devmem 0x03001068 32 0x3 2>/dev/null || true

# Wait for the spidev node in case this script runs very early.
for i in $(seq 1 30); do
  [ -c /dev/spidev0.1 ] && break
  sleep 0.5
done

start_daemon() {
    "${DAEMON}" daemon \
      --spi /dev/spidev0.1 \
      --udp-port 25250 \
      --fps 20 \
      --default normal \
      --color 00d7ff \
      >>"${LOG}" 2>&1
}

case "$1" in
  start)
    echo "Starting gc9a01-face-daemon..."
    rm -f "${NO_RESPAWN_FLAG}"
    if [ -f "${PIDFILE}" ]; then
        kill "$(cat "${PIDFILE}")" 2>/dev/null || true
        rm -f "${PIDFILE}"
    fi
    killall gc9a01-face-daemon 2>/dev/null || true
    sleep 1
    (
        while [ ! -f "${NO_RESPAWN_FLAG}" ]; do
            start_daemon
            if [ -f "${NO_RESPAWN_FLAG}" ]; then
                break
            fi
            echo "$(date '+%Y-%m-%d %H:%M:%S') gc9a01-face-daemon exited, restarting in 3s..." >> "${LOG}"
            sleep 3
        done
    ) &
    echo $! > "${PIDFILE}"
    ;;
  stop)
    echo "Stopping gc9a01-face-daemon..."
    touch "${NO_RESPAWN_FLAG}"
    "${DAEMON}" stop-daemon 2>/dev/null || true
    sleep 1
    killall gc9a01-face-daemon 2>/dev/null || true
    if [ -f "${PIDFILE}" ]; then
        kill "$(cat "${PIDFILE}")" 2>/dev/null || true
        rm -f "${PIDFILE}"
    fi
    rm -f "${NO_RESPAWN_FLAG}"
    ;;
  restart)
    "$0" stop
    sleep 1
    "$0" start
    ;;
  *)
    echo "usage: $0 {start|stop|restart}"
    exit 1
    ;;
esac
EOF

chmod +x /etc/init.d/S99zgc9a01-face
/etc/init.d/S99zgc9a01-face restart
echo "installed /usr/bin/gc9a01-face-daemon and enabled /etc/init.d/S99zgc9a01-face"
