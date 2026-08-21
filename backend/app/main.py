import asyncio
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from app.queue.manager import QueueManager
from app.mqtt.subscriber import MQTTSubscriber


BASE_DIR = Path(__file__).resolve().parent.parent
STATIC_DIR = BASE_DIR / "static"


queue_manager = QueueManager(
    leave_timeout=3.0
)

mqtt_subscriber = MQTTSubscriber(
    queue_manager=queue_manager
)


@asynccontextmanager
async def lifespan(app: FastAPI):

    mqtt_subscriber.start()

    print("Queue Monitoring API started")

    yield

    mqtt_subscriber.stop()


app = FastAPI(
    title="AI Queue Monitoring API",
    version="0.2.0",
    lifespan=lifespan,
)


app.mount(
    "/static",
    StaticFiles(directory=STATIC_DIR),
    name="static",
)


@app.get("/")
def dashboard():

    return FileResponse(
        STATIC_DIR / "index.html"
    )


@app.get("/api/queue/status")
def queue_status():

    return queue_manager.get_stats()


@app.websocket("/ws/queue")
async def queue_websocket(
    websocket: WebSocket
):

    await websocket.accept()

    try:

        while True:

            stats = queue_manager.get_stats()

            await websocket.send_json(
                stats
            )

            await asyncio.sleep(1)

    except WebSocketDisconnect:

        print("Dashboard disconnected")
