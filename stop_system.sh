#!/usr/bin/env bash
set -u

PROJECT_DIR="${PROJECT_DIR:-$HOME/Desktop/ICVision/UPWORK_PROJECTS/queue-monitoring}"
RUN_DIR="$PROJECT_DIR/.run"

stop_pidfile() {
  local name="$1"
  local pidfile="$2"

  if [[ ! -f "$pidfile" ]]; then
    echo "$name: aucun PID enregistré."
    return
  fi

  local pid
  pid="$(cat "$pidfile" 2>/dev/null || true)"

  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    echo "Arrêt $name (PID $pid)..."
    kill "$pid" 2>/dev/null || true

    for _ in {1..10}; do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.5
    done

    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  fi

  rm -f "$pidfile"
}

stop_pidfile "FastAPI" "$RUN_DIR/fastapi.pid"
stop_pidfile "transcodeur" "$RUN_DIR/transcoder.pid"
stop_pidfile "DeepStream" "$RUN_DIR/deepstream.pid"

if docker ps --format '{{.Names}}' | grep -qx mediamtx; then
  echo "Arrêt MediaMTX..."
  docker stop mediamtx >/dev/null
fi

echo
echo "Queue Monitoring arrêté."
echo "Mosquitto reste actif en tant que service système."
