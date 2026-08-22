#!/usr/bin/env bash
set -u

PROJECT_DIR="${PROJECT_DIR:-$HOME/Desktop/ICVision/UPWORK_PROJECTS/queue-monitoring}"
RUN_DIR="$PROJECT_DIR/.run"

show_pid() {
  local name="$1"
  local pidfile="$2"

  if [[ -f "$pidfile" ]]; then
    local pid
    pid="$(cat "$pidfile" 2>/dev/null || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      printf "%-14s RUNNING  PID=%s\n" "$name" "$pid"
      return
    fi
  fi

  printf "%-14s STOPPED\n" "$name"
}

echo "=== SERVICES ==="
if systemctl is-active --quiet mosquitto; then
  echo "Mosquitto      RUNNING"
else
  echo "Mosquitto      STOPPED"
fi

if docker ps --format '{{.Names}}' | grep -qx mediamtx; then
  echo "MediaMTX       RUNNING"
else
  echo "MediaMTX       STOPPED"
fi

show_pid "DeepStream" "$RUN_DIR/deepstream.pid"
show_pid "Transcoder" "$RUN_DIR/transcoder.pid"
show_pid "FastAPI" "$RUN_DIR/fastapi.pid"

echo
echo "=== PORTS ==="
ss -ltnup 2>/dev/null | grep -E ':(1883|8000|8554|8555|8889|8189)\b' || true

echo
echo "=== URLS ==="
echo "Dashboard : http://127.0.0.1:8000"
echo "WebRTC    : http://127.0.0.1:8889/queue"
echo "API       : http://127.0.0.1:8000/api/queue/status"
