#!/bin/sh
set -eu

if [ -x /etc/init.d/S99irisoled-face ]; then
  /etc/init.d/S99irisoled-face stop 2>/dev/null || true
fi
killall irisoled-face 2>/dev/null || true

install -m 0755 /root/irisoled-face /usr/bin/irisoled-face

cat >/etc/init.d/S99irisoled-face <<'EOF'
#!/bin/sh

case "$1" in
  start)
    killall irisoled-face 2>/dev/null || true
    /usr/bin/irisoled-face daemon --fb /dev/fb0 --udp-port 25250 --fps 20 --width 224 --height 112 --color 00d7ff --default normal >/tmp/irisoled-face.log 2>&1 &
    ;;
  stop)
    /usr/bin/irisoled-face stop-daemon 2>/dev/null || true
    sleep 1
    killall irisoled-face 2>/dev/null || true
    ;;
  restart)
    "$0" stop
    "$0" start
    ;;
  *)
    echo "usage: $0 {start|stop|restart}"
    exit 1
    ;;
esac
EOF

chmod +x /etc/init.d/S99irisoled-face
/etc/init.d/S99irisoled-face restart
echo "installed /usr/bin/irisoled-face and enabled /etc/init.d/S99irisoled-face"
