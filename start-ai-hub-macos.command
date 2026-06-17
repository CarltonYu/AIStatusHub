#!/usr/bin/env bash
set -e

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" && pwd -P)"
exec "$script_dir/scripts/start-ai-hub.sh" "$@"
