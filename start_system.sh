#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="${PROJECT_DIR:-$HOME/Desktop/ICVision/UPWORK_PROJECTS/queue-monitoring}"
LOG_DIR="$PROJECT_DIR/logs"
RUN_DIR="$PROJECT_DIR/.run"

mkdir -p "$LOG_DIR" "$RUN_DIR"

DEEPSTREAM_CONFIG="$PROJECT_DIR/configs/deepstream_queue.txt"
DEEPSTREAM_PID="$RUN_DIR/deepstream.pid"
TRANSCODER_PID="$RUN_DIR/transcoder.pid"
FASTAPI_PID="$RUN_DIR/fastapi.pid"

log() {
  printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

pid_alive() {
  local pidfile="$1"
  [[ -f "$pidfile" ]] || return 1
  local pid
  pid="$(cat "$pidfile" 2>/dev/null || true)"
  [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

wait_rtsp() {
  local url="$1"
  local attempts="${2:-30}"

  for ((i=1; i<=attempts; i++)); do
    if ffprobe -v error -rtsp_transport tcp \
      -select_streams v:0 \
      -show_entries stream=codec_name \
      -of default=noprint_wrappers=1:nokey=1 \
      "$url" 2>/dev/null | grep -q h264; then
      return 0
    fi
    sleep 1
  done

  return 1
}

log "1/5 - Mosquitto"
sudo systemctl start mosquitto
if ! systemctl is-active --quiet mosquitto; then
  echo "ERROR: Mosquitto n'est pas actif."
  exit 1
fi

log "2/5 - MediaMTX"
if docker ps -a --format '{{.Names}}' | grep -qx mediamtx; then
  docker start mediamtx >/dev/null 2>&1 || true
else
  echo "ERROR: le conteneur 'mediamtx' n'existe pas."
  echo "Crée-le d'abord avec la configuration publisher validée."
  exit 1
fi

log "3/5 - DeepStream"
if pid_alive "$DEEPSTREAM_PID"; then
  log "DeepStream est déjà lancé (PID $(cat "$DEEPSTREAM_PID"))."
else
  cd "$PROJECT_DIR"
  nohup deepstream-app -c "$DEEPSTREAM_CONFIG" \
    >"$LOG_DIR/deepstream.log" 2>&1 &
  echo $! > "$DEEPSTREAM_PID"
fi

log "Attente du RTSP DeepStream :8554..."
if ! wait_rtsp "rtsp://127.0.0.1:8554/ds-test" 40; then
  echo "ERROR: le flux RTSP DeepStream n'est pas disponible."
  echo "Voir: $LOG_DIR/deepstream.log"
  exit 1
fi

log "4/5 - Transcodage WebRTC-compatible"
if pid_alive "$TRANSCODER_PID"; then
  log "Transcodeur déjà lancé (PID $(cat "$TRANSCODER_PID"))."
else
  nohup gst-launch-1.0 -e \
    rtspsrc \
      location=rtsp://127.0.0.1:8554/ds-test \
      protocols=tcp \
      latency=30 \
    ! rtph264depay \
    ! h264parse \
    ! nvv4l2decoder \
    ! nvvideoconvert \
    ! 'video/x-raw,format=I420' \
    ! x264enc \
        tune=zerolatency \
        speed-preset=ultrafast \
        bitrate=4000 \
        key-int-max=15 \
        bframes=0 \
        byte-stream=true \
        cabac=false \
    ! 'video/x-h264,profile=baseline' \
    ! h264parse config-interval=-1 \
    ! rtspclientsink \
        location=rtsp://127.0.0.1:8555/queue \
        protocols=tcp \
    >"$LOG_DIR/transcoder.log" 2>&1 &
  echo $! > "$TRANSCODER_PID"
fi

log "Attente du flux MediaMTX :8555/queue..."
if ! wait_rtsp "rtsp://127.0.0.1:8555/queue" 40; then
  echo "ERROR: MediaMTX ne reçoit pas le flux transcodé."
  echo "Voir: $LOG_DIR/transcoder.log"
  echo "Et: docker logs mediamtx"
  exit 1
fi

log "5/5 - FastAPI"
if pid_alive "$FASTAPI_PID"; then
  log "FastAPI est déjà lancé (PID $(cat "$FASTAPI_PID"))."
else
  cd "$PROJECT_DIR/backend"
  nohup python3 -m uvicorn app.main:app \
    --host 0.0.0.0 \
    --port 8000 \
    >"$LOG_DIR/fastapi.log" 2>&1 &
  echo $! > "$FASTAPI_PID"
fi

sleep 2

echo
echo "============================================================"
echo " Queue Monitoring démarré"
echo "============================================================"
echo " Dashboard : http://127.0.0.1:8000"
echo " WebRTC    : http://127.0.0.1:8889/queue"
echo " API       : http://127.0.0.1:8000/api/queue/status"
echo " RTSP DS   : rtsp://127.0.0.1:8554/ds-test"
echo " RTSP MTX  : rtsp://127.0.0.1:8555/queue"
echo
echo " Logs:"
echo "   $LOG_DIR/deepstream.log"
echo "   $LOG_DIR/transcoder.log"
echo "   $LOG_DIR/fastapi.log"
echo "============================================================"
