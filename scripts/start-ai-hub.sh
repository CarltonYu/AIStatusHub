#!/usr/bin/env bash
set -u

script_path="${BASH_SOURCE[0]:-$0}"
script_dir="$(cd -- "$(dirname -- "$script_path")" && pwd -P)"
if [ -f "$script_dir/Cargo.toml" ]; then
  repo_root="$script_dir"
else
  repo_root="$(cd -- "$script_dir/.." && pwd -P)"
fi

cd "$repo_root" || exit 1

config_path="${AI_STATUS_HUB_CONFIG:-$repo_root/config.toml}"
if [ ! -f "$config_path" ]; then
  if [ -f "$repo_root/config.example.toml" ]; then
    config_path="$repo_root/config.example.toml"
  else
    echo "AIStatusHub: config.toml was not found." >&2
    exit 1
  fi
fi

toml_value() {
  local section="$1"
  local key="$2"
  awk -v section="$section" -v key="$key" '
    BEGIN { in_section = 0 }
    /^[[:space:]]*\[/ {
      in_section = ($0 ~ "^[[:space:]]*\\[" section "\\][[:space:]]*$")
    }
    in_section && $0 ~ "^[[:space:]]*" key "[[:space:]]*=" {
      sub(/^[^=]*=/, "", $0)
      sub(/[[:space:]]*#.*/, "", $0)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
      gsub(/^"|"$/, "", $0)
      print
      exit
    }
  ' "$config_path"
}

server_host="${AI_STATUS_HUB_HOST:-$(toml_value server host)}"
server_port="${AI_STATUS_HUB_PORT:-$(toml_value server port)}"
server_host="${server_host:-127.0.0.1}"
server_port="${server_port:-17888}"
browser_host="$server_host"
case "$browser_host" in
  ""|"0.0.0.0"|"::") browser_host="127.0.0.1" ;;
esac
hub_url="http://$browser_host:$server_port"

duo_host="${AI_STATUS_HUB_DUO_HOST:-192.168.42.1}"
duo_port="${AI_STATUS_HUB_DUO_PORT:-25250}"
duo_probe="${AI_STATUS_HUB_DUO_PROBE:-0}"
skip_duo_probe="${AI_STATUS_HUB_SKIP_DUO_PROBE:-0}"
open_browser="${AI_STATUS_HUB_OPEN_BROWSER:-1}"
release_build="${AI_STATUS_HUB_RELEASE:-0}"

open_hub_browser() {
  [ "$open_browser" = "0" ] && return 0

  if command -v open >/dev/null 2>&1; then
    open "$hub_url" >/dev/null 2>&1 || true
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$hub_url" >/dev/null 2>&1 || true
  fi
}

health_ok() {
  if command -v curl >/dev/null 2>&1; then
    curl -fsS --max-time 1 "$hub_url/health" >/dev/null 2>&1
  elif command -v wget >/dev/null 2>&1; then
    wget -q -T 1 -O - "$hub_url/health" >/dev/null 2>&1
  else
    return 1
  fi
}

send_duo_probe() {
  [ "$duo_probe" = "1" ] || return 0
  [ "$skip_duo_probe" = "1" ] && return 0

  local command_text="normal"
  if command -v nc >/dev/null 2>&1; then
    printf '%s\n' "$command_text" | nc -u -w1 "$duo_host" "$duo_port" >/dev/null 2>&1 || return 1
    return 0
  fi

  if [ -n "${BASH_VERSION:-}" ]; then
    exec 3<>"/dev/udp/$duo_host/$duo_port" 2>/dev/null || return 1
    printf '%s\n' "$command_text" >&3
    exec 3<&-
    exec 3>&-
    return 0
  fi

  return 1
}

wait_for_hub() {
  local pid="$1"
  local attempt=0

  while [ "$attempt" -lt 90 ]; do
    if health_ok; then
      echo "AIStatusHub is ready: $hub_url"
      open_hub_browser
      return 0
    fi

    if ! kill -0 "$pid" >/dev/null 2>&1; then
      return 1
    fi

    attempt=$((attempt + 1))
    sleep 1
  done

  echo "AIStatusHub started, but /health did not respond yet: $hub_url"
  return 0
}

if health_ok; then
  echo "AIStatusHub is already running: $hub_url"
  open_hub_browser
  exit 0
fi

echo "AIStatusHub root: $repo_root"
echo "Config: $config_path"
echo "Web UI: $hub_url"

if [ "$duo_probe" = "1" ] && send_duo_probe; then
  echo "Duo face probe sent to udp://$duo_host:$duo_port"
else
  echo "Duo face probe skipped or did not get a local send path; continuing."
fi

hub_pid=""
cleanup() {
  if [ -n "$hub_pid" ] && kill -0 "$hub_pid" >/dev/null 2>&1; then
    kill "$hub_pid" >/dev/null 2>&1 || true
  fi
}
trap cleanup INT TERM

if command -v cargo >/dev/null 2>&1; then
  if [ "$release_build" = "1" ]; then
    cargo run --release -- --config "$config_path" &
  else
    cargo run -- --config "$config_path" &
  fi
  hub_pid="$!"
else
  bin_path="$repo_root/target/debug/aistatushub"
  if [ ! -x "$bin_path" ]; then
    echo "AIStatusHub: cargo is not installed and $bin_path is missing." >&2
    exit 1
  fi
  "$bin_path" --config "$config_path" &
  hub_pid="$!"
fi

wait_for_hub "$hub_pid" || true
wait "$hub_pid"
exit $?
